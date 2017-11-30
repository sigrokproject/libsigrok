/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
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
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct korad_kaxxxxp_model models[] = {
	/* Device enum, vendor, model, ID reply, channels, voltage, current */
	{VELLEMAN_PS3005D, "Velleman", "PS3005D",
		"VELLEMANPS3005DV2.0", 1, {0, 31, 0.01}, {0, 5, 0.001}},
	{VELLEMAN_LABPS3005D, "Velleman", "LABPS3005D",
		"VELLEMANLABPS3005DV2.0", 1, {0, 31, 0.01}, {0, 5, 0.001}},
	{KORAD_KA3005P, "Korad", "KA3005P",
		"KORADKA3005PV2.0", 1, {0, 31, 0.01}, {0, 5, 0.001}},
	/* Sometimes the KA3005P has an extra 0x01 after the ID. */
	{KORAD_KA3005P_0X01, "Korad", "KA3005P",
		"KORADKA3005PV2.0\x01", 1, {0, 31, 0.01}, {0, 5, 0.001}},
	/* KORAD_KD3005P */
	{KORAD_KD3005P, "Korad", "KD3005P",
		"KORAD KD3005P V2.0", 1, {0, 31, 0.01}, {0, 5, 0.001}},
	ALL_ZERO
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	GSList *l;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	const char *conn, *serialcomm;
	struct sr_serial_dev_inst *serial;
	char reply[50];
	int i, model_id;
	unsigned int len;

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
		default:
			sr_err("Unknown option %d, skipping.", src->key);
			break;
		}
	}

	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = "9600/8n1";

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);

	/* Get the device model. */
	len = 0;
	for (i = 0; models[i].id; i++) {
		if (strlen(models[i].id) > len)
			len = strlen(models[i].id);
	}
	memset(&reply, 0, sizeof(reply));
	sr_dbg("Want max %d bytes.", len);
	if ((korad_kaxxxxp_send_cmd(serial, "*IDN?") < 0))
		return NULL;

	/* i is used here for debug purposes only. */
	if ((i = korad_kaxxxxp_read_chars(serial, len, reply)) < 0)
		return NULL;
	sr_dbg("Received: %d, %s", i, reply);
	model_id = -1;
	for (i = 0; models[i].id; i++) {
		if (!strcmp(models[i].id, reply))
			model_id = i;
	}
	if (model_id < 0) {
		sr_err("Unknown model ID '%s' detected, aborting.", reply);
		return NULL;
	}
	sr_dbg("Found: %s %s (idx %d, ID '%s').", models[model_id].vendor,
		models[model_id].name, model_id, models[model_id].id);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(models[model_id].vendor);
	sdi->model = g_strdup(models[model_id].name);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	devc->model = &models[model_id];
	devc->reply[5] = 0;
	devc->req_sent_at = 0;
	sdi->priv = devc;

	/* Get current status of device. */
	if (korad_kaxxxxp_get_all_values(serial, devc) < 0)
		goto exit_err;

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

exit_err:
	sr_dev_inst_free(sdi);
	g_free(devc);
	sr_dbg("Scan failed.");

	return NULL;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_VOLTAGE:
		*data = g_variant_new_double(devc->voltage);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		*data = g_variant_new_double(devc->voltage_max);
		break;
	case SR_CONF_CURRENT:
		*data = g_variant_new_double(devc->current);
		break;
	case SR_CONF_CURRENT_LIMIT:
		*data = g_variant_new_double(devc->current_max);
		break;
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(devc->output_enabled);
		break;
	case SR_CONF_REGULATION:
		/* Dual channel not supported. */
		*data = g_variant_new_string((devc->cc_mode[0]) ? "CC" : "CV");
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(devc->ocp_enabled);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(devc->ovp_enabled);
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
	double dval;
	gboolean bval;

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
		devc->voltage_max = dval;
		devc->target = KAXXXXP_VOLTAGE_MAX;
		if (korad_kaxxxxp_set_value(sdi->conn, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_CURRENT_LIMIT:
		dval = g_variant_get_double(data);
		if (dval < devc->model->current[0] || dval > devc->model->current[1])
			return SR_ERR_ARG;
		devc->current_max = dval;
		devc->target = KAXXXXP_CURRENT_MAX;
		if (korad_kaxxxxp_set_value(sdi->conn, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_ENABLED:
		bval = g_variant_get_boolean(data);
		/* Set always so it is possible turn off with sigrok-cli. */
		devc->output_enabled = bval;
		devc->target = KAXXXXP_OUTPUT;
		if (korad_kaxxxxp_set_value(sdi->conn, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		bval = g_variant_get_boolean(data);
		devc->ocp_enabled = bval;
		devc->target = KAXXXXP_OCP;
		if (korad_kaxxxxp_set_value(sdi->conn, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		bval = g_variant_get_boolean(data);
		devc->ovp_enabled = bval;
		devc->target = KAXXXXP_OVP;
		if (korad_kaxxxxp_set_value(sdi->conn, devc) < 0)
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
		*data = std_gvar_min_max_step_array(devc->model->voltage);
		break;
	case SR_CONF_CURRENT_LIMIT:
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

	devc->reply_pending = FALSE;
	devc->req_sent_at = 0;
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN,
			KAXXXXP_POLL_INTERVAL_MS,
			korad_kaxxxxp_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver korad_kaxxxxp_driver_info = {
	.name = "korad-kaxxxxp",
	.longname = "Korad KAxxxxP",
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
SR_REGISTER_DEV_DRIVER(korad_kaxxxxp_driver_info);
