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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>
#include "protocol.h"

#define SERIALCOMM "115200/8n1"

#define CMD_VERSION "version\r\n"
#define CMD_MONITOR "monitor 200\r\n"
#define CMD_MONITOR_STOP "monitor 0\r\n"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET | SR_CONF_LIST,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_UNDER_VOLTAGE_CONDITION | SR_CONF_GET,
	SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE | SR_CONF_GET,
	SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *regulation[] = {
	/* CC mode only. */
	"CC",
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	GSList *l;
	int ret, len;
	const char *conn, *serialcomm;
	char buf[100];
	char *bufptr;
	double version;

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

	/*
	 * First stop potentially running monitoring and wait for 50ms before
	 * next command can be sent.
	 */
	if (serial_write_blocking(serial, CMD_MONITOR_STOP,
			strlen(CMD_MONITOR_STOP), serial_timeout(serial,
			strlen(CMD_MONITOR_STOP))) < (int)strlen(CMD_MONITOR_STOP)) {
		sr_dbg("Unable to write while probing for hardware.");
		serial_close(serial);
		return NULL;
	}
	g_usleep(50 * 1000);

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
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Arachnid Labs");
	sdi->model = g_strdup("Re:load Pro");
	sdi->version = g_strdup(buf + 8);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("1");
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	cg->channels = g_slist_append(cg->channels, ch);

	ch = sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");
	cg->channels = g_slist_append(cg->channels, ch);

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	sdi->priv = devc;

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (!cg) {
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_REGULATION:
			*data = std_gvar_array_str(ARRAY_AND_SIZE(regulation));
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = std_gvar_min_max_step(0.0, 6.0, 0.001);
			break;
		case SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD:
			*data = std_gvar_min_max_step(0.0, 60.0, 0.001);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
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

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
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
	case SR_CONF_UNDER_VOLTAGE_CONDITION:
		if (reloadpro_get_under_voltage_threshold(sdi, &fvalue) == SR_OK)
			*data = g_variant_new_boolean(fvalue != 0.0);
		break;
	case SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE:
		*data = g_variant_new_boolean(devc->uvc_active);
		break;
	case SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD:
		if (reloadpro_get_under_voltage_threshold(sdi, &fvalue) == SR_OK)
			*data = g_variant_new_double(fvalue);
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

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		return reloadpro_set_on_off(sdi, g_variant_get_boolean(data));
	case SR_CONF_CURRENT_LIMIT:
		return reloadpro_set_current_limit(sdi, g_variant_get_double(data));
	case SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD:
		return reloadpro_set_under_voltage_threshold(sdi,
			g_variant_get_double(data));
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	if (serial_write_blocking(sdi->conn, CMD_MONITOR_STOP,
			strlen(CMD_MONITOR_STOP), serial_timeout(sdi->conn,
			strlen(CMD_MONITOR_STOP))) < (int)strlen(CMD_MONITOR_STOP)) {
		sr_dbg("Unable to stop monitoring.");
	}

	return std_serial_dev_close(sdi);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	devc->acquisition_running = TRUE;

	serial = sdi->conn;

	/* Send the 'monitor <ms>' command (doesn't have a reply). */
	if ((ret = serial_write_blocking(serial, CMD_MONITOR,
			strlen(CMD_MONITOR), serial_timeout(serial,
			strlen(CMD_MONITOR)))) < (int)strlen(CMD_MONITOR)) {
		sr_err("Unable to send 'monitor' command: %d.", ret);
		return SR_ERR;
	}

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	memset(devc->buf, 0, RELOADPRO_BUFSIZE);
	devc->buflen = 0;

	g_mutex_init(&devc->acquisition_mutex);

	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			  reloadpro_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	devc->acquisition_running = FALSE;

	ret = std_serial_dev_acquisition_stop(sdi);
	g_mutex_clear(&devc->acquisition_mutex);

	return ret;
}

static struct sr_dev_driver arachnid_labs_re_load_pro_driver_info = {
	.name = "arachnid-labs-re-load-pro",
	.longname = "Arachnid Labs Re:load Pro",
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
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(arachnid_labs_re_load_pro_driver_info);
