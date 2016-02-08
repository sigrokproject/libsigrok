/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015-2016 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <config.h>
#include <string.h>
#include "protocol.h"

#define SERIALCOMM "115200/8n1"

#define CMD_VERSION "version\r\n"
#define CMD_MONITOR "monitor 200\r\n"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET,
};

SR_PRIV struct sr_dev_driver arachnid_labs_re_load_pro_driver_info;

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	GSList *l, *devices;
	int ret, len;
	const char *conn, *serialcomm;
	char buf[100];
	char *bufptr;
	double version;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	conn = serialcomm = NULL;
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
		serialcomm = SERIALCOMM;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);

	if (serial_write_blocking(serial, CMD_VERSION,
			strlen(CMD_VERSION), serial_timeout(serial,
			strlen(CMD_VERSION))) < (int)strlen(CMD_VERSION)) {
		sr_dbg("Unable to write while probing for hardware.");
		serial_close(serial);
		return NULL;
	}

	memset(buf, 0, sizeof(buf));
	bufptr = buf;
	len = sizeof(buf);
	ret = serial_readline(serial, &bufptr, &len, 3000);

	if (ret < 0 || len < 9 || strncmp((const char *)&buf, "version ", 8)) {
		sr_dbg("Unable to probe version number.");
		serial_close(serial);
		return NULL;
	}

	version = g_ascii_strtod(buf + 8, NULL);
	if (version < 1.10) {
		sr_info("Firmware >= 1.10 required (got %1.2f).", version);
		serial_close(serial);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_ACTIVE;
	sdi->vendor = g_strdup("Arachnid Labs");
	sdi->model = g_strdup("Re:load Pro");
	sdi->version = g_strdup(buf + 8);
	sdi->driver = &arachnid_labs_re_load_pro_driver_info;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("1");
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	cg->channels = g_slist_append(cg->channels, ch);

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "I");
	cg->channels = g_slist_append(cg->channels, ch);

	devc = g_malloc0(sizeof(struct dev_context));
	sdi->priv = devc;
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, NULL);
}

static int cleanup(const struct sr_dev_driver *di)
{
	return dev_clear(di);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	GVariantBuilder gvb;
	int ret;

	/* Always available. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_OK;

	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts_cg, ARRAY_SIZE(devopts_cg), sizeof(uint32_t));
			break;
		case SR_CONF_CURRENT_LIMIT:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, step. */
			g_variant_builder_add_value(&gvb, g_variant_new_double(0.0));
			g_variant_builder_add_value(&gvb, g_variant_new_double(6.0));
			g_variant_builder_add_value(&gvb, g_variant_new_double(0.001)); /* 1mA steps */
			*data = g_variant_builder_end(&gvb);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;
	float fvalue;

	(void)cg;

	devc = sdi->priv;

	/*
	 * These features/keys are not supported by the hardware:
	 *  - SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE
	 *  - SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD
	 *  - SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE
	 *  - SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD
	 *  - SR_CONF_ENABLED (state cannot be queried, only set)
	 */

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_REGULATION:
		*data = g_variant_new_string("CC"); /* Always CC mode. */
		break;
	case SR_CONF_VOLTAGE:
		if (reloadpro_get_voltage_current(sdi, &fvalue, NULL) < 0)
			return SR_ERR;
		*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_CURRENT:
		if (reloadpro_get_voltage_current(sdi, NULL, &fvalue) < 0)
			return SR_ERR;
		*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (reloadpro_get_current_limit(sdi, &fvalue) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE); /* Always on. */
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE); /* Always on. */
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION:
		*data = g_variant_new_boolean(TRUE); /* Always on. */
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		*data = g_variant_new_boolean(devc->otp_active);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	case SR_CONF_ENABLED:
		ret = reloadpro_set_on_off(sdi, g_variant_get_boolean(data));
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = reloadpro_set_current_limit(sdi,
			g_variant_get_double(data));
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	int ret;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	serial = sdi->conn;

	/* Send the 'monitor <ms>' command (doesn't have a reply). */
	if ((ret = serial_write_blocking(serial, CMD_MONITOR,
			strlen(CMD_MONITOR), serial_timeout(serial,
			strlen(CMD_MONITOR)))) < (int)strlen(CMD_MONITOR)) {
		sr_err("Unable to send 'monitor' command: %d.", ret);
		return SR_ERR;
	}

	/* Poll every 100ms, or whenever some data comes in. */
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			  reloadpro_receive_data, (void *)sdi);

	std_session_send_df_header(cb_data, LOG_PREFIX);

	memset(devc->buf, 0, RELOADPRO_BUFSIZE);
	devc->buflen = 0;
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data,
		std_serial_dev_close, sdi->conn, LOG_PREFIX);
}

SR_PRIV struct sr_dev_driver arachnid_labs_re_load_pro_driver_info = {
	.name = "arachnid-labs-re-load-pro",
	.longname = "Arachnid Labs Re:load Pro",
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
	.context = NULL,
};
