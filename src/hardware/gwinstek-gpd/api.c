/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Bastian Schmitz <bastian.schmitz@udo.edu>
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
#include <string.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_modes[] = {
	"Independent",
};

static const struct gpd_model models[] = {
	{ GPD_2303S, "GPD-2303S",
		CHANMODE_INDEPENDENT,
		2,
		{
			/* Channel 1 */
			{ { 0, 30, 0.001 }, { 0, 3, 0.001 } },
			/* Channel 2 */
			{ { 0, 30, 0.001 }, { 0, 3, 0.001 } },
		},
	},
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn, *serialcomm;
	const struct gpd_model *model;
	const struct sr_config *src;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	GSList *l;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	char reply[50];
	unsigned int i;
	struct dev_context *devc;
	char channel[10];
	GRegex *regex;
	GMatchInfo *match_info;
	unsigned int cc_cv_ch1, cc_cv_ch2, track1, track2, beep, baud1, baud2;

	serial = NULL;
	match_info = NULL;
	regex = NULL;
	conn = NULL;
	serialcomm = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = "115200/8n1";
	sr_info("Probing serial port %s.", conn);
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);
	gpd_send_cmd(serial, "*IDN?\n");
	if (gpd_receive_reply(serial, reply, sizeof(reply)) != SR_OK) {
		sr_err("Device did not reply.");
		goto error;
	}
	serial_flush(serial);

	/*
	 * Returned identification string is for example:
	 * "GW INSTEK,GPD-2303S,SN:ER915277,V2.10"
	 */
	regex = g_regex_new("GW INSTEK,(.+),SN:(.+),(V.+)", 0, 0, NULL);
	if (!g_regex_match(regex, reply, 0, &match_info)) {
		sr_err("Unsupported model '%s'.", reply);
		goto error;
	}

	model = NULL;
	for (i = 0; i < ARRAY_SIZE(models); i++) {
		if (!strcmp(g_match_info_fetch(match_info, 1), models[i].name)) {
			model = &models[i];
			break;
		}
	}
	if (!model) {
		sr_err("Unsupported model '%s'.", reply);
		goto error;
	}

	sr_info("Detected model '%s'.", model->name);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("GW Instek");
	sdi->model = g_strdup(model->name);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	for (i = 0; i < model->num_channels; i++) {
		snprintf(channel, sizeof(channel), "CH%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel);
		cg = g_malloc(sizeof(struct sr_channel_group));
		cg->name = g_strdup(channel);
		cg->channels = g_slist_append(NULL, ch);
		cg->priv = NULL;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	devc->model = model;
	devc->config = g_malloc0(sizeof(struct per_channel_config)
				 * model->num_channels);
	sdi->priv = devc;

	serial_flush(serial);
	gpd_send_cmd(serial, "STATUS?\n");
	gpd_receive_reply(serial, reply, sizeof(reply));

	if (sscanf(reply, "%1u%1u%1u%1u%1u%1u%1u%1u", &cc_cv_ch1,
			&cc_cv_ch2, &track1, &track2, &beep,
			&devc->output_enabled, &baud1, &baud2) != 8) {
		sr_err("Invalid reply to STATUS: '%s'.", reply);
		goto error;
	}

	for (i = 0; i < model->num_channels; ++i) {
		gpd_send_cmd(serial, "ISET%d?\n", i + 1);
		gpd_receive_reply(serial, reply, sizeof(reply));
		if (sscanf(reply, "%f", &devc->config[i].output_current_max) != 1) {
			sr_err("Invalid reply to ISETn?: '%s'.", reply);
			goto error;
		}

		gpd_send_cmd(serial, "VSET%d?\n", i + 1);
		gpd_receive_reply(serial, reply, sizeof(reply));
		if (sscanf(reply, "%f", &devc->config[i].output_voltage_max) != 1) {
			sr_err("Invalid reply to VSETn?: '%s'.", reply);
			goto error;
		}
		gpd_send_cmd(serial, "IOUT%d?\n", i + 1);
		gpd_receive_reply(serial, reply, sizeof(reply));
		if (sscanf(reply, "%f", &devc->config[i].output_current_last) != 1) {
			sr_err("Invalid reply to IOUTn?: '%s'.", reply);
			goto error;
		}
		gpd_send_cmd(serial, "VOUT%d?\n", i + 1);
		gpd_receive_reply(serial, reply, sizeof(reply));
		if (sscanf(reply, "%f", &devc->config[i].output_voltage_last) != 1) {
			sr_err("Invalid reply to VOUTn?: '%s'.", reply);
			goto error;
		}
	}

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

error:
	if (match_info)
		g_match_info_free(match_info);
	if (regex)
		g_regex_unref(regex);
	if (serial)
		serial_close(serial);

	return NULL;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret, channel;
	const struct dev_context *devc;
	const struct sr_channel *ch;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			*data = g_variant_new_string(
				channel_modes[devc->channel_mode]);
			break;
		case SR_CONF_ENABLED:
			*data = g_variant_new_boolean(devc->output_enabled);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;
		channel = ch->index;
		ret = SR_OK;
		switch (key) {
		case SR_CONF_VOLTAGE:
			*data = g_variant_new_double(
				devc->config[channel].output_voltage_last);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = g_variant_new_double(
				devc->config[channel].output_voltage_max);
			break;
		case SR_CONF_CURRENT:
			*data = g_variant_new_double(
				devc->config[channel].output_current_last);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = g_variant_new_double(
				devc->config[channel].output_current_max);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret, channel;
	const struct sr_channel *ch;
	double dval;
	gboolean bval;
	struct dev_context *devc;

	devc = sdi->priv;

	ret = SR_OK;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		bval = g_variant_get_boolean(data);
		gpd_send_cmd(sdi->conn, "OUT%c\n", bval ? '1' : '0');
		devc->output_enabled = bval;
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ch = cg->channels->data;
		channel = ch->index;
		dval = g_variant_get_double(data);
		if (dval < devc->model->channels[channel].voltage[0]
		    || dval > devc->model->channels[channel].voltage[1])
			return SR_ERR_ARG;
		gpd_send_cmd(sdi->conn, "VSET%d:%05.3lf\n", channel + 1, dval);
		devc->config[channel].output_voltage_max = dval;
		break;
	case SR_CONF_CURRENT_LIMIT:
		ch = cg->channels->data;
		channel = ch->index;
		dval = g_variant_get_double(data);
		if (dval < devc->model->channels[channel].current[0]
		    || dval > devc->model->channels[channel].current[1])
			return SR_ERR_ARG;
		gpd_send_cmd(sdi->conn, "ISET%d:%05.3lf\n", channel + 1, dval);
		devc->config[channel].output_current_max = dval;
		break;
	default:
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	const struct dev_context *devc;
	const struct sr_channel *ch;
	int channel;

	devc = (sdi) ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts,
					       drvopts, devopts);
		case SR_CONF_CHANNEL_CONFIG:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(channel_modes));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = std_gvar_min_max_step_array(
				devc->model->channels[channel].voltage);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = std_gvar_min_max_step_array(
				devc->model->channels[channel].current);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	devc->reply_pending = FALSE;
	devc->req_sent_at = 0;
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			  gpd_receive_data, (void *)sdi);

	return SR_OK;
}

SR_PRIV const struct sr_dev_driver gwinstek_gpd_driver_info = {
	.name = "gwinstek-gpd",
	.longname = "GW Instek GPD",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(gwinstek_gpd_driver_info);
