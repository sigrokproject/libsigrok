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

static const uint32_t analog_devopts[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static int check_manufacturer(const char *manufacturer)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(manufacturers); i++)
		if (!strcmp(manufacturer, manufacturers[i]))
			return SR_OK;

	return SR_ERR;
}

static struct sr_dev_inst *probe_serial_device(struct sr_scpi_dev_inst *scpi)
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

	if (check_manufacturer(hw_info->manufacturer) != SR_OK)
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
	return sr_scpi_scan(di->context, options, probe_serial_device);
}

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	lecroy_xstream_state_free(devc->model_state);

	g_free(devc->analog_groups);

	g_free(devc);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	if (sdi->status != SR_ST_ACTIVE && sr_scpi_open(sdi->conn) != SR_OK)
		return SR_ERR;

	if (lecroy_xstream_state_get(sdi) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	sr_scpi_close(sdi->conn);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	unsigned int i;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	ret = SR_ERR_NA;
	model = devc->model_config;
	state = devc->model_state;
	*data = NULL;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(model->num_xdivs);
		ret = SR_OK;
		break;
	case SR_CONF_TIMEBASE:
		*data = g_variant_new("(tt)",
				model->timebases[state->timebase].p,
				model->timebases[state->timebase].q);
		ret = SR_OK;
		break;
	case SR_CONF_NUM_VDIV:
		for (i = 0; i < model->analog_channels; i++) {
			if (cg != devc->analog_groups[i])
				continue;
			*data = g_variant_new_int32(model->num_ydivs);
			ret = SR_OK;
		}
		break;
	case SR_CONF_VDIV:
		for (i = 0; i < model->analog_channels; i++) {
			if (cg != devc->analog_groups[i])
				continue;
			*data = g_variant_new("(tt)",
				model->vdivs[state->analog_channels[i].vdiv].p,
				model->vdivs[state->analog_channels[i].vdiv].q);
			ret = SR_OK;
		}
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string((*model->trigger_sources)[state->trigger_source]);
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string((*model->trigger_slopes)[state->trigger_slope]);
		ret = SR_OK;
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		*data = g_variant_new_double(state->horiz_triggerpos);
		ret = SR_OK;
		break;
	case SR_CONF_COUPLING:
		for (i = 0; i < model->analog_channels; i++) {
			if (cg != devc->analog_groups[i])
				continue;
			*data = g_variant_new_string((*model->coupling_options)[state->analog_channels[i].coupling]);
			ret = SR_OK;
		}
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(state->sample_rate);
		ret = SR_OK;
		break;
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(FALSE);
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static GVariant *build_tuples(const struct sr_rational *array, unsigned int n)
{
	unsigned int i;
	GVariant *rational[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < n; i++) {
		rational[0] = g_variant_new_uint64(array[i].p);
		rational[1] = g_variant_new_uint64(array[i].q);

		/* FIXME: Valgrind reports a memory leak here. */
		g_variant_builder_add_value(&gvb, g_variant_new_tuple(rational, 2));
	}

	return g_variant_builder_end(&gvb);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	const char *tmp;
	int64_t p;
	uint64_t q;
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
		tmp = g_variant_get_string(data, NULL);
		for (i = 0; (*model->trigger_sources)[i]; i++) {
			if (g_strcmp0(tmp, (*model->trigger_sources)[i]) != 0)
				continue;
			state->trigger_source = i;
			g_snprintf(command, sizeof(command),
					"SET TRIGGER SOURCE %s",
					(*model->trigger_sources)[i]);

			ret = sr_scpi_send(sdi->conn, command);
			break;
		}
		break;
	case SR_CONF_VDIV:
		g_variant_get(data, "(tt)", &p, &q);

		for (i = 0; i < model->num_vdivs; i++) {
			if (p != model->vdivs[i].p || q != model->vdivs[i].q)
				continue;
			for (j = 1; j <= model->analog_channels; j++) {
				if (cg != devc->analog_groups[j - 1])
					continue;
				state->analog_channels[j - 1].vdiv = i;
				g_snprintf(command, sizeof(command),
						"C%d:VDIV %E", j, (float)p/q);

				if (sr_scpi_send(sdi->conn, command) != SR_OK ||
				    sr_scpi_get_opc(sdi->conn) != SR_OK)
					return SR_ERR;

				break;
			}

			ret = SR_OK;
			break;
		}
		break;
	case SR_CONF_TIMEBASE:
		g_variant_get(data, "(tt)", &p, &q);

		for (i = 0; i < model->num_timebases; i++) {
			if (p != model->timebases[i].p ||
			    q != model->timebases[i].q)
				continue;
			state->timebase = i;
			g_snprintf(command, sizeof(command),
					"TIME_DIV %E", (float)p/q);

			ret = sr_scpi_send(sdi->conn, command);
			update_sample_rate = TRUE;
			break;
		}
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_d = g_variant_get_double(data);

		if (tmp_d < 0.0 || tmp_d > 1.0)
			return SR_ERR;

		state->horiz_triggerpos = tmp_d;
		tmp_d = -(tmp_d - 0.5) *
			((double)model->timebases[state->timebase].p /
			 model->timebases[state->timebase].q)
			 * model->num_xdivs;

		g_snprintf(command, sizeof(command), "TRIG POS %e S", tmp_d);

		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		tmp = g_variant_get_string(data, NULL);
		for (i = 0; (*model->trigger_slopes)[i]; i++) {
			if (g_strcmp0(tmp, (*model->trigger_slopes)[i]) != 0)
				continue;
			state->trigger_slope = i;
			g_snprintf(command, sizeof(command),
					"SET TRIGGER SLOPE %s",
					(*model->trigger_slopes)[i]);

			ret = sr_scpi_send(sdi->conn, command);
			break;
		}
		break;
	case SR_CONF_COUPLING:
		tmp = g_variant_get_string(data, NULL);

		for (i = 0; (*model->coupling_options)[i]; i++) {
			if (strcmp(tmp, (*model->coupling_options)[i]) != 0)
				continue;
			for (j = 1; j <= model->analog_channels; j++) {
				if (cg != devc->analog_groups[j - 1])
					continue;
				state->analog_channels[j - 1].coupling = i;

				g_snprintf(command, sizeof(command),
						"C%d:COUPLING %s", j, tmp);

				if (sr_scpi_send(sdi->conn, command) != SR_OK ||
				    sr_scpi_get_opc(sdi->conn) != SR_OK)
					return SR_ERR;
				break;
			}

			ret = SR_OK;
			break;
		}
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
	struct dev_context *devc = NULL;
	const struct scope_config *model = NULL;

	(void)cg;

	/* SR_CONF_SCAN_OPTIONS is always valid, regardless of sdi or channel group. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	}

	/* If sdi is NULL, nothing except SR_CONF_DEVICE_OPTIONS can be provided. */
	if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	}

        /* Every other option requires a valid device instance. */
        if (!sdi)
                return SR_ERR_ARG;

	devc = sdi->priv;
	model = devc->model_config;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg) {
			/* If cg is NULL, only the SR_CONF_DEVICE_OPTIONS that are not
			 * specific to a channel group must be returned. */
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
                        return SR_OK;
		}
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			analog_devopts, ARRAY_SIZE(analog_devopts),
			sizeof(uint32_t));
		break;
	case SR_CONF_COUPLING:
		*data = g_variant_new_strv(*model->coupling_options,
			   g_strv_length((char **)*model->coupling_options));
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!model)
			return SR_ERR_ARG;
		*data = g_variant_new_strv(*model->trigger_sources,
			   g_strv_length((char **)*model->trigger_sources));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!model)
			return SR_ERR_ARG;
		*data = g_variant_new_strv(*model->trigger_slopes,
			   g_strv_length((char **)*model->trigger_slopes));
		break;
	case SR_CONF_TIMEBASE:
		if (!model)
			return SR_ERR_ARG;
		*data = build_tuples(model->timebases, model->num_timebases);
		break;
	case SR_CONF_VDIV:
		if (!model)
			return SR_ERR_ARG;
		*data = build_tuples(model->vdivs, model->num_vdivs);
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

	g_snprintf(command, sizeof(command),
		"COMM_FORMAT DEF9,WORD,BIN;C%d:WAVEFORM?", ch->index + 1);
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
