/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 abraxa (Soeren Apel) <soeren@apelpie.net>
 * Based on the Hameg HMO driver by poljar (Damir Jelić) <poljarinho@gmail.com>
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

SR_PRIV struct sr_dev_driver yokogawa_dlm_driver_info;

static const char *MANUFACTURER_ID = "YOKOGAWA";
static const char *MANUFACTURER_NAME = "Yokogawa";

static const uint32_t dlm_scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t dlm_drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t dlm_devopts[] = {
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t dlm_analog_devopts[] = {
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_VDIV | SR_CONF_GET,
};

static const uint32_t dlm_digital_devopts[] = {
};

enum {
	CG_INVALID = -1,
	CG_NONE,
	CG_ANALOG,
	CG_DIGITAL,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct sr_dev_inst *probe_usbtmc_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;
	char *model_name;
	int model_index;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		goto fail;
	}

	if (strcmp(hw_info->manufacturer, MANUFACTURER_ID) != 0)
		goto fail;

	if (dlm_model_get(hw_info->model, &model_name, &model_index) != SR_OK)
		goto fail;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(MANUFACTURER_NAME);
	sdi->model = g_strdup(model_name);
	sdi->version = g_strdup(hw_info->firmware_version);

	sdi->serial_num = g_strdup(hw_info->serial_number);

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));

	sdi->driver = &yokogawa_dlm_driver_info;
	sdi->priv = devc;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	if (dlm_device_init(sdi, model_index) != SR_OK)
		goto fail;

	return sdi;

fail:
	if (hw_info)
		sr_scpi_hw_info_free(hw_info);
	if (sdi)
		sr_dev_inst_free(sdi);
	g_free(devc);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_usbtmc_device);
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	dlm_scope_state_destroy(devc->model_state);

	g_free(devc->analog_groups);
	g_free(devc->digital_groups);
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

	if (dlm_scope_state_query(sdi) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	if (sdi->status == SR_ST_INACTIVE)
		return SR_OK;

	sr_scpi_close(sdi->conn);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

/**
 * Check which category a given channel group belongs to.
 *
 * @param devc Our internal device context.
 * @param cg The channel group to check.
 *
 * @retval CG_NONE cg is NULL
 * @retval CG_ANALOG cg is an analog group
 * @retval CG_DIGITAL cg is a digital group
 * @retval CG_INVALID cg is something else
 */
static int check_channel_group(struct dev_context *devc,
			const struct sr_channel_group *cg)
{
	unsigned int i;
	const struct scope_config *model;

	model = devc->model_config;

	if (!cg)
		return CG_NONE;

	for (i = 0; i < model->analog_channels; i++)
		if (cg == devc->analog_groups[i])
			return CG_ANALOG;

	for (i = 0; i < model->pods; i++)
		if (cg == devc->digital_groups[i])
			return CG_DIGITAL;

	sr_err("Invalid channel group specified.");
	return CG_INVALID;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int ret, cg_type;
	unsigned int i;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	state = devc->model_state;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(model->num_xdivs);
		ret = SR_OK;
		break;
	case SR_CONF_TIMEBASE:
		*data = g_variant_new("(tt)",
				dlm_timebases[state->timebase][0],
				dlm_timebases[state->timebase][1]);
		ret = SR_OK;
		break;
	case SR_CONF_NUM_VDIV:
		if (cg_type == CG_NONE) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		} else if (cg_type == CG_ANALOG) {
				*data = g_variant_new_int32(model->num_ydivs);
				ret = SR_OK;
				break;
		} else {
			ret = SR_ERR_NA;
		}
		break;
	case SR_CONF_VDIV:
		ret = SR_ERR_NA;
		if (cg_type == CG_NONE) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		} else if (cg_type != CG_ANALOG)
			break;

		for (i = 0; i < model->analog_channels; i++) {
			if (cg != devc->analog_groups[i])
				continue;
			*data = g_variant_new("(tt)",
					dlm_vdivs[state->analog_states[i].vdiv][0],
					dlm_vdivs[state->analog_states[i].vdiv][1]);
			ret = SR_OK;
			break;
		}
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string((*model->trigger_sources)[state->trigger_source]);
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string(dlm_trigger_slopes[state->trigger_slope]);
		ret = SR_OK;
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		*data = g_variant_new_double(state->horiz_triggerpos);
		ret = SR_OK;
		break;
	case SR_CONF_COUPLING:
		ret = SR_ERR_NA;
		if (cg_type == CG_NONE) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		} else if (cg_type != CG_ANALOG)
			break;

		for (i = 0; i < model->analog_channels; i++) {
			if (cg != devc->analog_groups[i])
				continue;
			*data = g_variant_new_string((*model->coupling_options)[state->analog_states[i].coupling]);
			ret = SR_OK;
			break;
		}
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(state->sample_rate);
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static GVariant *build_tuples(const uint64_t (*array)[][2], unsigned int n)
{
	unsigned int i;
	GVariant *rational[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < n; i++) {
		rational[0] = g_variant_new_uint64((*array)[i][0]);
		rational[1] = g_variant_new_uint64((*array)[i][1]);

		/* FIXME: Valgrind reports a memory leak here. */
		g_variant_builder_add_value(&gvb, g_variant_new_tuple(rational, 2));
	}

	return g_variant_builder_end(&gvb);
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int ret, cg_type;
	unsigned int i, j;
	char float_str[30];
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	const char *tmp;
	uint64_t p, q;
	double tmp_d;
	gboolean update_sample_rate;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

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
			/* TODO: A and B trigger support possible? */
			ret = dlm_trigger_source_set(sdi->conn, (*model->trigger_sources)[i]);
			break;
		}
		break;
	case SR_CONF_VDIV:
		if (cg_type == CG_NONE) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}

		g_variant_get(data, "(tt)", &p, &q);

		for (i = 0; i < ARRAY_SIZE(dlm_vdivs); i++) {
			if (p != dlm_vdivs[i][0] ||
					q != dlm_vdivs[i][1])
				continue;
			for (j = 1; j <= model->analog_channels; j++) {
				if (cg != devc->analog_groups[j - 1])
					continue;
				state->analog_states[j - 1].vdiv = i;
				g_ascii_formatd(float_str, sizeof(float_str),
						"%E", (float) p / q);
				if (dlm_analog_chan_vdiv_set(sdi->conn, j, float_str) != SR_OK ||
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

		for (i = 0; i < ARRAY_SIZE(dlm_timebases); i++) {
			if (p != dlm_timebases[i][0] ||
					q != dlm_timebases[i][1])
				continue;
			state->timebase = i;
			g_ascii_formatd(float_str, sizeof(float_str),
					"%E", (float) p / q);
			ret = dlm_timebase_set(sdi->conn, float_str);
			update_sample_rate = TRUE;
			break;
		}
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_d = g_variant_get_double(data);

		/* TODO: Check if the calculation makes sense for the DLM. */
		if (tmp_d < 0.0 || tmp_d > 1.0)
			return SR_ERR;

		state->horiz_triggerpos = tmp_d;
		tmp_d = -(tmp_d - 0.5) *
				((double) dlm_timebases[state->timebase][0] /
				dlm_timebases[state->timebase][1])
				* model->num_xdivs;

		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d);
		ret = dlm_horiz_trigger_pos_set(sdi->conn, float_str);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		tmp = g_variant_get_string(data, NULL);

		if (!tmp || !(tmp[0] == 'f' || tmp[0] == 'r'))
			return SR_ERR_ARG;

		/* Note: See dlm_trigger_slopes[] in protocol.c. */
		state->trigger_slope = (tmp[0] == 'r') ?
				SLOPE_POSITIVE : SLOPE_NEGATIVE;

		ret = dlm_trigger_slope_set(sdi->conn, state->trigger_slope);
		break;
	case SR_CONF_COUPLING:
		if (cg_type == CG_NONE) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}

		tmp = g_variant_get_string(data, NULL);

		for (i = 0; (*model->coupling_options)[i]; i++) {
			if (strcmp(tmp, (*model->coupling_options)[i]) != 0)
				continue;
			for (j = 1; j <= model->analog_channels; j++) {
				if (cg != devc->analog_groups[j - 1])
					continue;
				state->analog_states[j-1].coupling = i;

				if (dlm_analog_chan_coupl_set(sdi->conn, j, tmp) != SR_OK ||
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
		ret = dlm_sample_rate_query(sdi);

	return ret;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
			struct sr_channel *ch, unsigned int changes)
{
	/* Currently we only handle SR_CHANNEL_SET_ENABLED. */
	if (changes != SR_CHANNEL_SET_ENABLED)
		return SR_ERR_NA;

	return dlm_channel_state_set(sdi, ch->index, ch->enabled);
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int cg_type = CG_NONE;
	struct dev_context *devc = NULL;
	const struct scope_config *model = NULL;

	/* SR_CONF_SCAN_OPTIONS is always valid, regardless of sdi or probe group. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				dlm_scanopts, ARRAY_SIZE(dlm_scanopts), sizeof(uint32_t));
		return SR_OK;
	}

	/* If sdi is NULL, nothing except SR_CONF_DEVICE_OPTIONS can be provided. */
	if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				dlm_drvopts, ARRAY_SIZE(dlm_drvopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	model = devc->model_config;

	/*
	 * If cg is NULL, only the SR_CONF_DEVICE_OPTIONS that are not
	 * specific to a probe group must be returned.
	 */
	if (!cg) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					dlm_devopts, ARRAY_SIZE(dlm_devopts), sizeof(uint32_t));
			return SR_OK;
		case SR_CONF_TIMEBASE:
			*data = build_tuples(&dlm_timebases, ARRAY_SIZE(dlm_timebases));
			return SR_OK;
		case SR_CONF_TRIGGER_SOURCE:
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(*model->trigger_sources,
					g_strv_length((char **)*model->trigger_sources));
			return SR_OK;
		case SR_CONF_TRIGGER_SLOPE:
			*data = g_variant_new_strv(dlm_trigger_slopes,
					g_strv_length((char **)dlm_trigger_slopes));
			return SR_OK;
		case SR_CONF_NUM_HDIV:
			*data = g_variant_new_uint32(model->num_xdivs);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if (cg_type == CG_ANALOG) {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				dlm_analog_devopts, ARRAY_SIZE(dlm_analog_devopts), sizeof(uint32_t));
		} else if (cg_type == CG_DIGITAL) {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				dlm_digital_devopts, ARRAY_SIZE(dlm_digital_devopts), sizeof(uint32_t));
		} else {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				NULL, 0, sizeof(uint32_t));
		}
		break;
	case SR_CONF_COUPLING:
		if (cg_type == CG_NONE)
			return SR_ERR_CHANNEL_GROUP;
		*data = g_variant_new_strv(*model->coupling_options,
				g_strv_length((char **)*model->coupling_options));
		break;
	case SR_CONF_VDIV:
		if (cg_type == CG_NONE)
			return SR_ERR_CHANNEL_GROUP;
		*data = build_tuples(&dlm_vdivs, ARRAY_SIZE(dlm_vdivs));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dlm_check_channels(GSList *channels)
{
	GSList *l;
	struct sr_channel *ch;
	gboolean enabled_pod1, enabled_chan4;

	enabled_pod1 = enabled_chan4 = FALSE;

	/* Note: On the DLM2000, CH4 and Logic are shared. */
	/* TODO Handle non-DLM2000 models. */
	for (l = channels; l; l = l->next) {
		ch = l->data;
		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			if (ch->index == 3)
				enabled_chan4 = TRUE;
			break;
		case SR_CHANNEL_LOGIC:
			enabled_pod1 = TRUE;
			break;
		default:
			return SR_ERR;
		}
	}

	if (enabled_pod1 && enabled_chan4)
		return SR_ERR;

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	GSList *l;
	gboolean digital_added;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;
	devc = sdi->priv;
	digital_added = FALSE;

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		/* Only add a single digital channel. */
		if (ch->type != SR_CHANNEL_LOGIC || !digital_added) {
			devc->enabled_channels = g_slist_append(
				devc->enabled_channels, ch);
			if (ch->type == SR_CHANNEL_LOGIC)
				digital_added = TRUE;
		}
	}

	if (!devc->enabled_channels)
		return SR_ERR;

	if (dlm_check_channels(devc->enabled_channels) != SR_OK) {
		sr_err("Invalid channel configuration specified!");
		return SR_ERR_NA;
	}

	/* Request data for the first enabled channel. */
	devc->current_channel = devc->enabled_channels;
	dlm_channel_data_request(sdi);

	/* Call our callback when data comes in or after 5ms. */
	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 5,
			dlm_data_receive, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	std_session_send_df_end(sdi, LOG_PREFIX);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	devc->num_frames = 0;
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	sr_scpi_source_remove(sdi->session, sdi->conn);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver yokogawa_dlm_driver_info = {
	.name = "yokogawa-dlm",
	.longname = "Yokogawa DL/DLM",
	.api_version = 1,
	.init = init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
