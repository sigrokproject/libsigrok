/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts_temp[] = {
	SR_CONF_THERMOMETER,
};

static const uint32_t drvopts_temp_hum[] = {
	SR_CONF_THERMOMETER,
	SR_CONF_HYGROMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static struct sr_dev_driver mic_98581_driver_info;
static struct sr_dev_driver mic_98583_driver_info;

SR_PRIV const struct mic_dev_info mic_devs[] = {
	{
		"MIC", "98581", "38400/8n2", 32000, TRUE, FALSE, 6,
		packet_valid_temp,
		&mic_98581_driver_info, receive_data_MIC_98581,
	},
	{
		"MIC", "98583", "38400/8n2", 32000, TRUE, TRUE, 10,
		packet_valid_temp_hum,
		&mic_98583_driver_info, receive_data_MIC_98583,
	},
};

static GSList *mic_scan(const char *conn, const char *serialcomm, int idx)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);

	/* TODO: Query device type. */
	// ret = mic_cmd_get_device_info(serial);

	sr_info("Found device on port %s.", conn);

	/* TODO: Fill in version from protocol response. */
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(mic_devs[idx].vendor);
	sdi->model = g_strdup(mic_devs[idx].device);
	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "Temperature");

	if (mic_devs[idx].has_humidity)
		sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "Humidity");

	serial_close(serial);

	return std_scan_complete(mic_devs[idx].di, g_slist_append(NULL, sdi));
}

static GSList *scan(GSList *options, int idx)
{
	struct sr_config *src;
	GSList *l, *devices;
	const char *conn, *serialcomm;

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

	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = mic_scan(conn, serialcomm, idx);
	} else {
		/* Try the default. */
		devices = mic_scan(conn, mic_devs[idx].conn, idx);
	}

	return devices;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg, int idx)
{
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi && !mic_devs[idx].has_humidity) {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts_temp, ARRAY_SIZE(drvopts_temp),
				sizeof(uint32_t));
		} else if (!sdi && mic_devs[idx].has_humidity) {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts_temp_hum, ARRAY_SIZE(drvopts_temp_hum),
				sizeof(uint32_t));
		} else {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, int idx)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	/* Poll every 100ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
		      mic_devs[idx].receive_data, (void *)sdi);

	return SR_OK;
}

/* Driver-specific API function wrappers */
#define HW_SCAN(X) \
static GSList *scan_##X(struct sr_dev_driver *di, GSList *options) { \
	(void)di; return scan(options, X); }
#define HW_CONFIG_LIST(X) \
static int config_list_##X(uint32_t key, GVariant **data, \
const struct sr_dev_inst *sdi, const struct sr_channel_group *cg) { \
return config_list(key, data, sdi, cg, X); }
#define HW_DEV_ACQUISITION_START(X) \
static int dev_acquisition_start_##X(const struct sr_dev_inst *sdi \
) { return dev_acquisition_start(sdi, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_SCAN(ID_UPPER) \
HW_CONFIG_LIST(ID_UPPER) \
HW_DEV_ACQUISITION_START(ID_UPPER) \
static struct sr_dev_driver ID##_driver_info = { \
	.name = NAME, \
	.longname = LONGNAME, \
	.api_version = 1, \
	.init = std_init, \
	.cleanup = std_cleanup, \
	.scan = scan_##ID_UPPER, \
	.dev_list = std_dev_list, \
	.config_get = NULL, \
	.config_set = config_set, \
	.config_list = config_list_##ID_UPPER, \
	.dev_open = std_serial_dev_open, \
	.dev_close = std_serial_dev_close, \
	.dev_acquisition_start = dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = std_serial_dev_acquisition_stop, \
	.context = NULL, \
}; \
SR_REGISTER_DEV_DRIVER(ID##_driver_info)

DRV(mic_98581, MIC_98581, "mic-98581", "MIC 98581")
DRV(mic_98583, MIC_98583, "mic-98583", "MIC 98583")
