/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * <em>Manson HCS-3xxx series</em> power supply driver
 *
 * @internal
 */

#include <config.h>
#include "protocol.h"

static const uint32_t drvopts[] = {
	/* Device class */
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t devopts[] = {
	/* Device class */
	SR_CONF_POWER_SUPPLY,
	/* Acquisition modes. */
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	/* Device configuration */
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

/* Note: All models have one power supply output only. */
static const struct hcs_model models[] = {
	{ MANSON_HCS_3100, "HCS-3100",     "3100", { 1, 18, 0.1 }, { 0, 10,   0.10 } },
	{ MANSON_HCS_3102, "HCS-3102",     "3102", { 1, 36, 0.1 }, { 0,  5,   0.01 } },
	{ MANSON_HCS_3104, "HCS-3104",     "3104", { 1, 60, 0.1 }, { 0,  2.5, 0.01 } },
	{ MANSON_HCS_3150, "HCS-3150",     "3150", { 1, 18, 0.1 }, { 0, 15,   0.10 } },
	{ MANSON_HCS_3200, "HCS-3200",     "3200", { 1, 18, 0.1 }, { 0, 20,   0.10 } },
	{ MANSON_HCS_3202, "HCS-3202",     "3202", { 1, 36, 0.1 }, { 0, 10,   0.10 } },
	{ MANSON_HCS_3204, "HCS-3204",     "3204", { 1, 60, 0.1 }, { 0,  5,   0.01 } },
	{ MANSON_HCS_3300, "HCS-3300-USB", "3300", { 1, 16, 0.1 }, { 0, 30,   0.10 } },
	{ MANSON_HCS_3302, "HCS-3302-USB", "3302", { 1, 32, 0.1 }, { 0, 15,   0.10 } },
	{ MANSON_HCS_3304, "HCS-3304-USB", "3304", { 1, 60, 0.1 }, { 0,  8,   0.10 } },
	{ MANSON_HCS_3400, "HCS-3400-USB", "3400", { 1, 16, 0.1 }, { 0, 40,   0.10 } },
	{ MANSON_HCS_3402, "HCS-3402-USB", "3402", { 1, 32, 0.1 }, { 0, 20,   0.10 } },
	{ MANSON_HCS_3404, "HCS-3404-USB", "3404", { 1, 60, 0.1 }, { 0, 10,   0.10 } },
	{ MANSON_HCS_3600, "HCS-3600-USB", "3600", { 1, 16, 0.1 }, { 0, 60,   0.10 } },
	{ MANSON_HCS_3602, "HCS-3602-USB", "3602", { 1, 32, 0.1 }, { 0, 30,   0.10 } },
	{ MANSON_HCS_3604, "HCS-3604-USB", "3604", { 1, 60, 0.1 }, { 0, 15,   0.10 } },
	ALL_ZERO
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	int i, model_id;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	GSList *l;
	const char *conn, *serialcomm;
	struct sr_serial_dev_inst *serial;
	char reply[50], **tokens, *dummy;

	conn = NULL;
	serialcomm = NULL;
	devc = NULL;

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

	sr_info("Probing serial port %s.", conn);

	/* Get the device model. */
	memset(&reply, 0, sizeof(reply));
	if ((hcs_send_cmd(serial, "GMOD\r") < 0) ||
	    (hcs_read_reply(serial, 2, reply, sizeof(reply)) < 0))
		return NULL;
	tokens = g_strsplit((const gchar *)&reply, "\r", 2);

	model_id = -1;
	for (i = 0; models[i].id != NULL; i++) {
		if (!strcmp(models[i].id, tokens[0]))
			model_id = i;
	}
	if (model_id < 0) {
		sr_err("Unknown model ID '%s' detected, aborting.", tokens[0]);
		g_strfreev(tokens);
		return NULL;
	}
	g_strfreev(tokens);

	/* Init device instance, etc. */
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Manson");
	sdi->model = g_strdup(models[model_id].name);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	devc->model = &models[model_id];

	sdi->priv = devc;

	/* Get current voltage, current, status. */
	if ((hcs_send_cmd(serial, "GETD\r") < 0) ||
	    (hcs_read_reply(serial, 2, reply, sizeof(reply)) < 0))
		goto exit_err;
	tokens = g_strsplit((const gchar *)&reply, "\r", 2);
	if (hcs_parse_volt_curr_mode(sdi, tokens) < 0) {
		g_strfreev(tokens);
		goto exit_err;
	}
	g_strfreev(tokens);

	/* Get max. voltage and current. */
	if ((hcs_send_cmd(serial, "GMAX\r") < 0) ||
	    (hcs_read_reply(serial, 2, reply, sizeof(reply)) < 0))
		goto exit_err;
	tokens = g_strsplit((const gchar *)&reply, "\r", 2);
	devc->current_max_device = g_strtod(&tokens[0][3], &dummy) * devc->model->current[2];
	tokens[0][3] = '\0';
	devc->voltage_max_device = g_strtod(tokens[0], &dummy) * devc->model->voltage[2];
	g_strfreev(tokens);

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

exit_err:
	sr_dev_inst_free(sdi);
	g_free(devc);

	return NULL;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
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
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	gboolean bval;
	gdouble dval;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_VOLTAGE_TARGET:
		dval = g_variant_get_double(data);
		if (dval < devc->model->voltage[0] || dval > devc->voltage_max_device)
			return SR_ERR_ARG;

		if ((hcs_send_cmd(sdi->conn, "VOLT%03.0f\r",
			(dval / devc->model->voltage[2])) < 0) ||
		    (hcs_read_reply(sdi->conn, 1, devc->buf, sizeof(devc->buf)) < 0))
			return SR_ERR;
		devc->voltage_max = dval;
		break;
	case SR_CONF_CURRENT_LIMIT:
		dval = g_variant_get_double(data);
		if (dval < devc->model->current[0] || dval > devc->current_max_device)
			return SR_ERR_ARG;

		if ((hcs_send_cmd(sdi->conn, "CURR%03.0f\r",
			(dval / devc->model->current[2])) < 0) ||
		    (hcs_read_reply(sdi->conn, 1, devc->buf, sizeof(devc->buf)) < 0))
			return SR_ERR;
		devc->current_max = dval;
		break;
	case SR_CONF_ENABLED:
		bval = g_variant_get_boolean(data);
		if (bval == devc->output_enabled) /* Nothing to do. */
			break;
		if ((hcs_send_cmd(sdi->conn, "SOUT%1d\r", !bval) < 0) ||
		    (hcs_read_reply(sdi->conn, 1, devc->buf, sizeof(devc->buf)) < 0))
			return SR_ERR;
		devc->output_enabled = bval;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *gvar;
	GVariantBuilder gvb;
	double dval;
	int idx;

	(void)cg;

	/* Always available (with or without sdi). */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	}

	/* Return drvopts without sdi (and devopts with sdi, see below). */
	if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	}

	/* Every other key needs an sdi. */
	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_VOLTAGE_TARGET:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		/* Min, max, step. */
		for (idx = 0; idx < 3; idx++) {
			if (idx == 1)
				dval = devc->voltage_max_device;
			else
				dval = devc->model->voltage[idx];
			gvar = g_variant_new_double(dval);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_CURRENT_LIMIT:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		/* Min, max, step. */
		for (idx = 0; idx < 3; idx++) {
			if (idx == 1)
				dval = devc->current_max_device;
			else
				dval = devc->model->current[idx];
			gvar = g_variant_new_double(dval);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
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

	/* Poll every 10ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 10,
			hcs_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver manson_hcs_3xxx_driver_info = {
	.name = "manson-hcs-3xxx",
	.longname = "Manson HCS-3xxx",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(manson_hcs_3xxx_driver_info);
