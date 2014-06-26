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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/** @file
  *  <em>Manson HCS-3xxx Series</em> power supply driver
  *  @internal
  */

#include "protocol.h"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t devopts[] = {
	/* Device class */
	SR_CONF_POWER_SUPPLY,
	/* Aquisition modes. */
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
	/* Device configuration */
	SR_CONF_OUTPUT_CURRENT,
	SR_CONF_OUTPUT_CURRENT_MAX,
	SR_CONF_OUTPUT_ENABLED,
	SR_CONF_OUTPUT_VOLTAGE,
	SR_CONF_OUTPUT_VOLTAGE_MAX,
};

/* Note: All models have one power supply output only. */
static struct hcs_model models[] = {
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
	{ 0, NULL, NULL, { 0, 0, 0 }, { 0, 0, 0 }, },
};

SR_PRIV struct sr_dev_driver manson_hcs_3xxx_driver_info;
static struct sr_dev_driver *di = &manson_hcs_3xxx_driver_info;

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	int i, model_id;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	struct sr_channel *ch;
	GSList *devices, *l;
	const char *conn, *serialcomm;
	struct sr_serial_dev_inst *serial;
	char reply[50], **tokens;

	drvc = di->priv;
	drvc->instances = NULL;
	devices = NULL;
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

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

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
	g_strfreev(tokens);

	if (model_id < 0) {
		sr_err("Unknown model id '%s' detected, aborting.", tokens[0]);
		return NULL;
	}

	/* Init device instance, etc. */
	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "Manson",
				    models[model_id].name, NULL))) {
		sr_err("Failed to create device instance.");
		return NULL;
	}

	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->driver = di;

	if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, "CH1"))) {
		sr_err("Failed to create channel.");
		goto exit_err;
	}
	sdi->channels = g_slist_append(sdi->channels, ch);

	devc = g_malloc0(sizeof(struct dev_context));
	devc->model = &models[model_id];

	sdi->priv = devc;

	/* Get current device status. */
	if ((hcs_send_cmd(serial, "GETD\r") < 0) ||
	    (hcs_read_reply(serial, 2, reply, sizeof(reply)) < 0))
		return NULL;
	tokens = g_strsplit((const gchar *)&reply, "\r", 2);
	if (hcs_parse_volt_curr_mode(sdi, tokens) < 0)
		goto exit_err;

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;

exit_err:
	sr_dev_inst_free(sdi);
	if (devc)
		g_free(devc);
	return NULL;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int cleanup(void)
{
	return dev_clear();
}

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_OUTPUT_CURRENT:
		*data = g_variant_new_double(devc->current);
		break;
	case SR_CONF_OUTPUT_CURRENT_MAX:
		*data = g_variant_new_double(devc->current_max);
		break;
	case SR_CONF_OUTPUT_ENABLED:
		*data = g_variant_new_boolean(devc->output_enabled);
		break;
	case SR_CONF_OUTPUT_VOLTAGE:
		*data = g_variant_new_double(devc->voltage);
		break;
	case SR_CONF_OUTPUT_VOLTAGE_MAX:
		*data = g_variant_new_double(devc->voltage_max);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
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
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_OUTPUT_CURRENT_MAX:
		dval = g_variant_get_double(data);
		if (dval < devc->model->current[0] || dval > devc->model->current[1])
			return SR_ERR_ARG;

		if ((hcs_send_cmd(sdi->conn, "CURR%03.0f\r",
			(dval / devc->model->current[2])) < 0) ||
		    (hcs_read_reply(sdi->conn, 1, devc->buf, sizeof(devc->buf)) < 0))
			return SR_ERR;
		devc->current_max = dval;
		break;
	case SR_CONF_OUTPUT_ENABLED:
		bval = g_variant_get_boolean(data);
		if (bval == devc->output_enabled) /* Nothing to do. */
			break;
		if ((hcs_send_cmd(sdi->conn, "SOUT%1d\r", !bval) < 0) ||
		    (hcs_read_reply(sdi->conn, 1, devc->buf, sizeof(devc->buf)) < 0))
			return SR_ERR;
		devc->output_enabled = bval;
		break;
	case SR_CONF_OUTPUT_VOLTAGE_MAX:
		dval = g_variant_get_double(data);
		if (dval < devc->model->voltage[0] || dval > devc->model->voltage[1])
			return SR_ERR_ARG;

		if ((hcs_send_cmd(sdi->conn, "VOLT%03.0f\r",
			(dval / devc->model->voltage[2])) < 0) ||
		    (hcs_read_reply(sdi->conn, 1, devc->buf, sizeof(devc->buf)) < 0))
			return SR_ERR;
		devc->voltage_max = dval;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *gvar;
	GVariantBuilder gvb;
	int idx;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
			hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
			devopts, ARRAY_SIZE(devopts), sizeof(int32_t));
		break;
	case SR_CONF_OUTPUT_CURRENT_MAX:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		/* Min, max, step. */
		for (idx = 0; idx < 3; idx++) {
			gvar = g_variant_new_double(devc->model->current[idx]);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_OUTPUT_VOLTAGE_MAX:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		/* Min, max, step. */
		for (idx = 0; idx < 3; idx++) {
			gvar = g_variant_new_double(devc->model->voltage[idx]);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	devc->starttime = g_get_monotonic_time();
	devc->num_samples = 0;
	devc->reply_pending = FALSE;
	devc->req_sent_at = 0;

	/* Poll every 10ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(serial, G_IO_IN, 10, hcs_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data,
			std_serial_dev_close, sdi->conn, LOG_PREFIX);
}

SR_PRIV struct sr_dev_driver manson_hcs_3xxx_driver_info = {
	.name = "manson-hcs-3xxx",
	.longname = "Manson HCS-3xxx",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
