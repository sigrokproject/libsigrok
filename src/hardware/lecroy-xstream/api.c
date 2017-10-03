/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Sven Schnelle <svens@stackframe.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include "scpi.h"
#include "protocol.h"

static struct sr_dev_driver lecroy_xstream_driver_info;

static const char *manufacturers[] = {
	"LECROY",
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		goto fail;
	}

	if (std_str_idx_s(hw_info->manufacturer, ARRAY_AND_SIZE(manufacturers)) < 0)
		goto fail;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->driver = &lecroy_xstream_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));
	sdi->priv = devc;

	if (lecroy_xstream_init_device(sdi) != SR_OK)
		goto fail;

	return sdi;

fail:
	sr_scpi_hw_info_free(hw_info);
	sr_dev_inst_free(sdi);
	g_free(devc);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static void clear_helper(struct dev_context *devc)
{
	lecroy_xstream_state_free(devc->model_state);
	g_free(devc->analog_groups);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	if (sr_scpi_open(sdi->conn) != SR_OK)
		return SR_ERR;

	if (lecroy_xstream_state_get(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int idx;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	model = devc->model_config;
	state = devc->model_state;
	*data = NULL;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(model->num_xdivs);
		break;
	case SR_CONF_TIMEBASE:
		*data = g_variant_new("(tt)",
				(*model->timebases)[state->timebase][0],
				(*model->timebases)[state->timebase][1]);
		break;
	case SR_CONF_NUM_VDIV:
		if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_int32(model->num_ydivs);
		break;
	case SR_CONF_VDIV:
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new("(tt)",
				(*model->vdivs)[state->analog_channels[idx].vdiv][0],
				(*model->vdivs)[state->analog_channels[idx].vdiv][1]);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string((*model->trigger_sources)[state->trigger_source]);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string((*model->trigger_slopes)[state->trigger_slope]);
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		*data = g_variant_new_double(state->horiz_triggerpos);
		break;
	case SR_CONF_COUPLING:
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_string((*model->coupling_options)[state->analog_channels[idx].coupling]);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(state->sample_rate);
		break;
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(FALSE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret, idx, j;
	char command[MAX_COMMAND_SIZE];
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	double tmp_d;
	gboolean update_sample_rate;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	model = devc->model_config;
	state = devc->model_state;
	update_sample_rate = FALSE;

	ret = SR_ERR_NA;

	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->frame_limit = g_variant_get_uint64(data);
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if ((idx = std_str_idx(data, *model->trigger_sources, model->num_trigger_sources)) < 0)
			return SR_ERR_ARG;
		state->trigger_source = idx;
		g_snprintf(command, sizeof(command),
				"TRIG_SELECT EDGE,SR,%s", (*model->trigger_sources)[idx]);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_VDIV:
		if ((idx = std_u64_tuple_idx(data, *model->vdivs, model->num_vdivs)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		state->analog_channels[j].vdiv = idx;
		g_snprintf(command, sizeof(command),
				"C%d:VDIV %E", j + 1, (float) (*model->vdivs)[idx][0] / (*model->vdivs)[idx][1]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK || sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		ret = SR_OK;
		break;
	case SR_CONF_TIMEBASE:
		if ((idx = std_u64_tuple_idx(data, *model->timebases, model->num_timebases)) < 0)
			return SR_ERR_ARG;
		state->timebase = idx;
		g_snprintf(command, sizeof(command),
				"TIME_DIV %E", (float) (*model->timebases)[idx][0] / (*model->timebases)[idx][1]);
		ret = sr_scpi_send(sdi->conn, command);
		update_sample_rate = TRUE;
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_d = g_variant_get_double(data);

		if (tmp_d < 0.0 || tmp_d > 1.0)
			return SR_ERR;

		state->horiz_triggerpos = tmp_d;
		tmp_d = -(tmp_d - 0.5) *
				((double)(*model->timebases)[state->timebase][0] /
				(*model->timebases)[state->timebase][1])
				* model->num_xdivs;

		g_snprintf(command, sizeof(command), "TRIG POS %e S", tmp_d);

		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, *model->trigger_slopes, model->num_trigger_slopes)) < 0)
			return SR_ERR_ARG;
		state->trigger_slope = idx;
		g_snprintf(command, sizeof(command),
				"%s:TRIG_SLOPE %s", (*model->trigger_sources)[state->trigger_source],
				(*model->trigger_slopes)[idx]);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_COUPLING:
		if ((idx = std_str_idx(data, *model->coupling_options, model->num_coupling_options)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		state->analog_channels[j].coupling = idx;
		g_snprintf(command, sizeof(command), "C%d:COUPLING %s",
				j + 1, (*model->coupling_options)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK || sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
		break;
	}

	if (ret == SR_OK)
		ret = sr_scpi_get_opc(sdi->conn);

	if (ret == SR_OK && update_sample_rate)
		ret = lecroy_xstream_update_sample_rate(sdi);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct scope_config *model;

	devc = (sdi) ? sdi->priv : NULL;
	model = (devc) ? devc->model_config : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, NULL, NULL);
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg)
			return STD_CONFIG_LIST(key, data, sdi, cg, NULL, drvopts, devopts);
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog));
		break;
	case SR_CONF_COUPLING:
		*data = g_variant_new_strv(*model->coupling_options, model->num_coupling_options);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!model)
			return SR_ERR_ARG;
		*data = g_variant_new_strv(*model->trigger_sources, model->num_trigger_sources);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!model)
			return SR_ERR_ARG;
		*data = g_variant_new_strv(*model->trigger_slopes, model->num_trigger_slopes);
		break;
	case SR_CONF_TIMEBASE:
		if (!model)
			return SR_ERR_ARG;
		*data = std_gvar_tuple_array(*model->timebases, model->num_timebases);
		break;
	case SR_CONF_VDIV:
		if (!model)
			return SR_ERR_ARG;
		*data = std_gvar_tuple_array(*model->vdivs, model->num_vdivs);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV int lecroy_xstream_request_data(const struct sr_dev_inst *sdi)
{
	char command[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	struct dev_context *devc;

	devc = sdi->priv;
	ch = devc->current_channel->data;

	if (ch->type != SR_CHANNEL_ANALOG)
		return SR_ERR;

	g_snprintf(command, sizeof(command), "C%d:WAVEFORM?", ch->index + 1);
	return sr_scpi_send(sdi->conn, command);
}

static int setup_channels(const struct sr_dev_inst *sdi)
{
	GSList *l;
	gboolean setup_changed;
	char command[MAX_COMMAND_SIZE];
	struct scope_state *state;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	devc = sdi->priv;
	scpi = sdi->conn;
	state = devc->model_state;
	setup_changed = FALSE;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			if (ch->enabled == state->analog_channels[ch->index].state)
				break;
			g_snprintf(command, sizeof(command), "C%d:TRACE %s",
					ch->index + 1, ch->enabled ? "ON" : "OFF");

			if (sr_scpi_send(scpi, command) != SR_OK)
				return SR_ERR;

			state->analog_channels[ch->index].state = ch->enabled;
			setup_changed = TRUE;
			break;
		default:
			return SR_ERR;
		}
	}

	if (setup_changed && lecroy_xstream_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	GSList *l;
	struct sr_channel *ch;
	struct dev_context *devc;
	int ret;
	struct sr_scpi_dev_inst *scpi;

	devc = sdi->priv;
	scpi = sdi->conn;

	/* Preset empty results. */
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	/*
	 * Contruct the list of enabled channels. Determine the highest
	 * number of digital pods involved in the acquisition.
	 */

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		/* Only add a single digital channel per group (pod). */
		devc->enabled_channels = g_slist_append(
			devc->enabled_channels, ch);
	}

	if (!devc->enabled_channels)
		return SR_ERR;

	/*
	 * Configure the analog channels and the
	 * corresponding digital pods.
	 */
	if (setup_channels(sdi) != SR_OK) {
		sr_err("Failed to setup channel configuration!");
		ret = SR_ERR;
		goto free_enabled;
	}

	/*
	 * Start acquisition on the first enabled channel. The
	 * receive routine will continue driving the acquisition.
	 */
	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			lecroy_xstream_receive_data, (void *)sdi);

	std_session_send_df_header(sdi);

	devc->current_channel = devc->enabled_channels;

	return lecroy_xstream_request_data(sdi);

free_enabled:
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	return ret;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	std_session_send_df_end(sdi);

	devc = sdi->priv;

	devc->num_frames = 0;
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;
	scpi = sdi->conn;
	sr_scpi_source_remove(sdi->session, scpi);

	return SR_OK;
}

static struct sr_dev_driver lecroy_xstream_driver_info = {
	.name = "lecroy-xstream",
	.longname = "LeCroy X-Stream",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(lecroy_xstream_driver_info);
