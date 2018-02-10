/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
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

#define SERIALCOMM "115200/8n1/flow=1"

static struct sr_dev_driver hameg_hmo_driver_info;

static const char *manufacturers[] = {
	"HAMEG",
	"Rohde&Schwarz",
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

enum {
	CG_INVALID = -1,
	CG_NONE,
	CG_ANALOG,
	CG_DIGITAL,
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
	sdi->driver = &hameg_hmo_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));

	sdi->priv = devc;

	if (hmo_init_device(sdi) != SR_OK)
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
	hmo_scope_state_free(devc->model_state);
	g_free(devc->analog_groups);
	g_free(devc->digital_groups);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	if (sr_scpi_open(sdi->conn) != SR_OK)
		return SR_ERR;

	if (hmo_scope_state_get(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

static int check_channel_group(struct dev_context *devc,
			     const struct sr_channel_group *cg)
{
	const struct scope_config *model;

	model = devc->model_config;

	if (!cg)
		return CG_NONE;

	if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) >= 0)
		return CG_ANALOG;

	if (std_cg_idx(cg, devc->digital_groups, model->digital_pods) >= 0)
		return CG_DIGITAL;

	sr_err("Invalid channel group specified.");

	return CG_INVALID;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int cg_type, idx;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	state = devc->model_state;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(model->num_xdivs);
		break;
	case SR_CONF_TIMEBASE:
		*data = g_variant_new("(tt)", (*model->timebases)[state->timebase][0],
				      (*model->timebases)[state->timebase][1]);
		break;
	case SR_CONF_NUM_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_int32(model->num_ydivs);
		break;
	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
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
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_ANALOG)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new_string((*model->coupling_options)[state->analog_channels[idx].coupling]);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(state->sample_rate);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret, cg_type, idx, j;
	char command[MAX_COMMAND_SIZE], float_str[30];
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	double tmp_d;
	gboolean update_sample_rate;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	state = devc->model_state;
	update_sample_rate = FALSE;

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
			   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_SOURCE],
			   (*model->trigger_sources)[idx]);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((idx = std_u64_tuple_idx(data, *model->vdivs, model->num_vdivs)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		state->analog_channels[j].vdiv = idx;
		g_ascii_formatd(float_str, sizeof(float_str), "%E",
			(float) (*model->vdivs)[idx][0] / (*model->vdivs)[idx][1]);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_VERTICAL_DIV],
			   j + 1, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		ret = SR_OK;
		break;
	case SR_CONF_TIMEBASE:
		if ((idx = std_u64_tuple_idx(data, *model->timebases, model->num_timebases)) < 0)
			return SR_ERR_ARG;
		state->timebase = idx;
		g_ascii_formatd(float_str, sizeof(float_str), "%E",
			(float) (*model->timebases)[idx][0] / (*model->timebases)[idx][1]);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TIMEBASE],
			   float_str);
		ret = sr_scpi_send(sdi->conn, command);
		update_sample_rate = TRUE;
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_d = g_variant_get_double(data);
		if (tmp_d < 0.0 || tmp_d > 1.0)
			return SR_ERR;
		state->horiz_triggerpos = tmp_d;
		tmp_d = -(tmp_d - 0.5) *
			((double) (*model->timebases)[state->timebase][0] /
			(*model->timebases)[state->timebase][1])
			 * model->num_xdivs;
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_HORIZ_TRIGGERPOS],
			   float_str);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, *model->trigger_slopes, model->num_trigger_slopes)) < 0)
			return SR_ERR_ARG;
		state->trigger_slope = idx;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_SLOPE],
			   (*model->trigger_slopes)[idx]);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((idx = std_str_idx(data, *model->coupling_options, model->num_coupling_options)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		state->analog_channels[j].coupling = idx;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_COUPLING],
			   j + 1, (*model->coupling_options)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
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
		ret = hmo_update_sample_rate(sdi);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int cg_type = CG_NONE;
	struct dev_context *devc = NULL;
	const struct scope_config *model = NULL;

	if (sdi) {
		devc = sdi->priv;
		if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
			return SR_ERR;

		model = devc->model_config;
	}

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(scanopts));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg) {
			if (model)
				*data = std_gvar_array_u32((const uint32_t *)model->devopts, model->num_devopts);
			else
				*data = std_gvar_array_u32(ARRAY_AND_SIZE(drvopts));
		} else if (cg_type == CG_ANALOG) {
			*data = std_gvar_array_u32((const uint32_t *)model->devopts_cg_analog, model->num_devopts_cg_analog);
		} else {
			*data = std_gvar_array_u32(NULL, 0);
		}
		break;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (!model)
			return SR_ERR_ARG;
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
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (!model)
			return SR_ERR_ARG;
		*data = std_gvar_tuple_array(*model->vdivs, model->num_vdivs);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV int hmo_request_data(const struct sr_dev_inst *sdi)
{
	char command[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	struct dev_context *devc;
	const struct scope_config *model;

	devc = sdi->priv;
	model = devc->model_config;

	ch = devc->current_channel->data;

	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_GET_ANALOG_DATA],
#ifdef WORDS_BIGENDIAN
			   "MSBF",
#else
			   "LSBF",
#endif
			   ch->index + 1);
		break;
	case SR_CHANNEL_LOGIC:
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_GET_DIG_DATA],
			   ch->index < 8 ? 1 : 2);
		break;
	default:
		sr_err("Invalid channel type.");
		break;
	}

	return sr_scpi_send(sdi->conn, command);
}

static int hmo_check_channels(GSList *channels)
{
	GSList *l;
	struct sr_channel *ch;
	gboolean enabled_chan[MAX_ANALOG_CHANNEL_COUNT];
	gboolean enabled_pod[MAX_DIGITAL_GROUP_COUNT];
	size_t idx;

	/* Preset "not enabled" for all channels / pods. */
	for (idx = 0; idx < ARRAY_SIZE(enabled_chan); idx++)
		enabled_chan[idx] = FALSE;
	for (idx = 0; idx < ARRAY_SIZE(enabled_pod); idx++)
		enabled_pod[idx] = FALSE;

	/*
	 * Determine which channels / pods are required for the caller's
	 * specified configuration.
	 */
	for (l = channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			idx = ch->index;
			if (idx < ARRAY_SIZE(enabled_chan))
				enabled_chan[idx] = TRUE;
			break;
		case SR_CHANNEL_LOGIC:
			idx = ch->index / 8;
			if (idx < ARRAY_SIZE(enabled_pod))
				enabled_pod[idx] = TRUE;
			break;
		default:
			return SR_ERR;
		}
	}

	/*
	 * Check for resource conflicts. Some channels can be either
	 * analog or digital, but never both at the same time.
	 *
	 * Note that the constraints might depend on the specific model.
	 * These tests might need some adjustment when support for more
	 * models gets added to the driver.
	 */
	if (enabled_pod[0] && enabled_chan[2])
		return SR_ERR;
	if (enabled_pod[1] && enabled_chan[3])
		return SR_ERR;
	return SR_OK;
}

static int hmo_setup_channels(const struct sr_dev_inst *sdi)
{
	GSList *l;
	unsigned int i;
	gboolean *pod_enabled, setup_changed;
	char command[MAX_COMMAND_SIZE];
	struct scope_state *state;
	const struct scope_config *model;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	int ret;

	devc = sdi->priv;
	scpi = sdi->conn;
	state = devc->model_state;
	model = devc->model_config;
	setup_changed = FALSE;

	pod_enabled = g_try_malloc0(sizeof(gboolean) * model->digital_pods);

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			if (ch->enabled == state->analog_channels[ch->index].state)
				break;
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_ANALOG_CHAN_STATE],
				   ch->index + 1, ch->enabled);

			if (sr_scpi_send(scpi, command) != SR_OK)
				return SR_ERR;
			state->analog_channels[ch->index].state = ch->enabled;
			setup_changed = TRUE;
			break;
		case SR_CHANNEL_LOGIC:
			/*
			 * A digital POD needs to be enabled for every group of
			 * 8 channels.
			 */
			if (ch->enabled)
				pod_enabled[ch->index < 8 ? 0 : 1] = TRUE;

			if (ch->enabled == state->digital_channels[ch->index])
				break;
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_CHAN_STATE],
				   ch->index, ch->enabled);

			if (sr_scpi_send(scpi, command) != SR_OK)
				return SR_ERR;

			state->digital_channels[ch->index] = ch->enabled;
			setup_changed = TRUE;
			break;
		default:
			g_free(pod_enabled);
			return SR_ERR;
		}
	}

	ret = SR_OK;
	for (i = 0; i < model->digital_pods; i++) {
		if (state->digital_pods[i] == pod_enabled[i])
			continue;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_STATE],
			   i + 1, pod_enabled[i]);
		if (sr_scpi_send(scpi, command) != SR_OK) {
			ret = SR_ERR;
			break;
		}
		state->digital_pods[i] = pod_enabled[i];
		setup_changed = TRUE;
	}
	g_free(pod_enabled);
	if (ret != SR_OK)
		return ret;

	if (setup_changed && hmo_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	GSList *l;
	gboolean digital_added[MAX_DIGITAL_GROUP_COUNT];
	size_t group, pod_count;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	int ret;

	scpi = sdi->conn;
	devc = sdi->priv;

	/* Preset empty results. */
	for (group = 0; group < ARRAY_SIZE(digital_added); group++)
		digital_added[group] = FALSE;
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	/*
	 * Contruct the list of enabled channels. Determine the highest
	 * number of digital pods involved in the acquisition.
	 */
	pod_count = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		/* Only add a single digital channel per group (pod). */
		group = ch->index / 8;
		if (ch->type != SR_CHANNEL_LOGIC || !digital_added[group]) {
			devc->enabled_channels = g_slist_append(
					devc->enabled_channels, ch);
			if (ch->type == SR_CHANNEL_LOGIC) {
				digital_added[group] = TRUE;
				if (pod_count < group + 1)
					pod_count = group + 1;
			}
		}
	}
	if (!devc->enabled_channels)
		return SR_ERR;
	devc->pod_count = pod_count;
	devc->logic_data = NULL;

	/*
	 * Check constraints. Some channels can be either analog or
	 * digital, but not both at the same time.
	 */
	if (hmo_check_channels(devc->enabled_channels) != SR_OK) {
		sr_err("Invalid channel configuration specified!");
		ret = SR_ERR_NA;
		goto free_enabled;
	}

	/*
	 * Configure the analog and digital channels and the
	 * corresponding digital pods.
	 */
	if (hmo_setup_channels(sdi) != SR_OK) {
		sr_err("Failed to setup channel configuration!");
		ret = SR_ERR;
		goto free_enabled;
	}

	/*
	 * Start acquisition on the first enabled channel. The
	 * receive routine will continue driving the acquisition.
	 */
	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			hmo_receive_data, (void *)sdi);

	std_session_send_df_header(sdi);

	devc->current_channel = devc->enabled_channels;

	return hmo_request_data(sdi);

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

static struct sr_dev_driver hameg_hmo_driver_info = {
	.name = "hameg-hmo",
	.longname = "Hameg HMO",
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
SR_REGISTER_DEV_DRIVER(hameg_hmo_driver_info);
