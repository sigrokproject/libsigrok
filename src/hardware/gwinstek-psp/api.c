/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 ettom <36895504+ettom@users.noreply.github.com>
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

#include <math.h>
#include <config.h>
#include "protocol.h"

static struct sr_dev_driver gwinstek_psp_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	SR_CONF_FORCE_DETECT,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

/* Voltage and current ranges. Values are: Min, max, step. */
static const double volts_20[] = { 0, 20, 0.01 };
static const double volts_40[] = { 0, 40, 0.01 };
static const double volts_60[] = { 0, 60, 0.01 };
static const double amps_3_5[] = { 0, 3.5, 0.01 };
static const double amps_5[] = { 0, 5, 0.01 };
static const double amps_10[] = { 0, 10, 0.01 };

static const struct gwinstek_psp_model models[] = {
	{ "GW Instek", "PSP-603", volts_60, amps_3_5 },
	{ "GW Instek", "PSP-405", volts_40, amps_5 },
	{ "GW Instek", "PSP-2010", volts_20, amps_10 }
};

static const struct gwinstek_psp_model *model_lookup(const char *id_text)
{
	size_t idx;
	const struct gwinstek_psp_model *check;

	if (!id_text || !*id_text)
		return NULL;

	for (idx = 0; idx < ARRAY_SIZE(models); idx++) {
		check = &models[idx];
		if (!check->name || !check->name[0])
			continue;
		if (g_ascii_strncasecmp(id_text, check->name, strlen(check->name)) != 0)
			continue;

		return check;
	}
	sr_dbg("Could not find a matching model for: [%s].", id_text);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	const char *conn, *serialcomm;
	const char *force_detect;
	const struct gwinstek_psp_model *model;
	GSList *l;

	conn = NULL;
	serialcomm = NULL;
	force_detect = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_FORCE_DETECT:
			force_detect = g_variant_get_string(src->data, NULL);
			break;
		default:
			sr_err("Unknown option %d, skipping.", src->key);
			break;
		}
	}

	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = "2400/8n1";
	if (!force_detect) {
		sr_err("The gwinstek-psp driver requires the force_detect parameter");
		return NULL;
	}

	model = model_lookup(force_detect);

	if (!model) {
		sr_err("Unsupported model ID '%s', aborting.", force_detect);
		return NULL;
	}

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(model->vendor);
	sdi->model = g_strdup(model->name);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->connection_id = g_strdup(conn);

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");

	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);
	g_mutex_init(&devc->rw_mutex);
	devc->model = model;
	devc->msg_terminator_len = 2;
	sdi->priv = devc;

	/* Get current status of device. */
	if (gwinstek_psp_get_all_values(serial, devc) < 0)
		goto exit_err;

	if (gwinstek_psp_check_terminator(serial, devc) < 0)
		goto exit_err;

	if (gwinstek_psp_get_initial_voltage_target(devc) < 0)
		goto exit_err;

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

exit_err:
	sr_dev_inst_free(sdi);
	g_free(devc);
	sr_dbg("Scan failed.");

	return NULL;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;
	if (devc) {
		serial_flush(sdi->conn);
		g_mutex_clear(&devc->rw_mutex);
	}

	return std_serial_dev_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (key != SR_CONF_CONN && gwinstek_psp_get_all_values(sdi->conn, devc) < 0)
		return SR_ERR;

	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_VOLTAGE:
		*data = g_variant_new_double(devc->voltage_or_0);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		*data = g_variant_new_double(devc->voltage_target);
		break;
	case SR_CONF_CURRENT:
		*data = g_variant_new_double(devc->current);
		break;
	case SR_CONF_CURRENT_LIMIT:
		*data = g_variant_new_double(devc->current_limit);
		break;
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(devc->output_enabled);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		*data = g_variant_new_boolean(devc->otp_active);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int voltage_limit;
	double dval;
	gboolean bval;
	char msg[20];

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_VOLTAGE_TARGET:
		dval = g_variant_get_double(data);
		if (dval < devc->model->voltage[0] || dval > devc->model->voltage[1])
			return SR_ERR_ARG;

		/* Set voltage output limit to the next positive integer. No need
		 * to check against limits, device handles overflow correctly. */
		voltage_limit = (int) ceil(dval);
		if (devc->voltage_limit == voltage_limit) {
			sr_dbg("Correct limit (%dV) already set", voltage_limit);
		} else {
			sr_snprintf_ascii(msg, sizeof(msg), "SU %d\r\n", voltage_limit);
			if (gwinstek_psp_send_cmd(sdi->conn, devc, msg, TRUE) < 0)
				return SR_ERR;
		}

		/* Set voltage output level */
		sr_snprintf_ascii(msg, sizeof(msg), "SV %05.2f\r\n", dval);
		if (gwinstek_psp_send_cmd(sdi->conn, devc, msg, TRUE) < 0)
			return SR_ERR;
		devc->set_voltage_target = dval;
		devc->set_voltage_target_updated = g_get_monotonic_time();
		break;
	case SR_CONF_CURRENT_LIMIT:
		dval = g_variant_get_double(data);
		if (dval < devc->model->current[0] || dval > devc->model->current[1])
			return SR_ERR_ARG;

		sr_snprintf_ascii(msg, sizeof(msg), "SI %04.2f\r\n", dval);
		if (gwinstek_psp_send_cmd(sdi->conn, devc, msg, TRUE) < 0)
			return SR_ERR;
		break;
	case SR_CONF_ENABLED:
		bval = g_variant_get_boolean(data);
		/* Set always so it is possible turn off with sigrok-cli. */
		if (gwinstek_psp_send_cmd(sdi->conn, devc,
					  bval ? "KOE\r\n" : "KOD\r\n", TRUE) < 0)
			return SR_ERR;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_VOLTAGE_TARGET:
		if (!devc || !devc->model)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step_array(devc->model->voltage);
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (!devc || !devc->model)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step_array(devc->model->current);
		break;
	default:
		return SR_ERR_NA;
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

	devc->next_req_time = 0;
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN,
			GWINSTEK_PSP_PROCESSING_TIME_MS,
			gwinstek_psp_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver gwinstek_psp_driver_info = {
	.name = "gwinstek-psp",
	.longname = "GW Instek PSP series",
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
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(gwinstek_psp_driver_info);
