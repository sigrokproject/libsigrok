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
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

SR_PRIV struct sr_dev_driver mic_98581_driver_info;
SR_PRIV struct sr_dev_driver mic_98583_driver_info;

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

static int dev_clear(int idx)
{
	return std_dev_clear(mic_devs[idx].di, NULL);
}

static int init(struct sr_context *sr_ctx, int idx)
{
	return std_init(sr_ctx, mic_devs[idx].di, LOG_PREFIX);
}

static GSList *mic_scan(const char *conn, const char *serialcomm, int idx)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	GSList *devices;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	drvc = mic_devs[idx].di->context;
	devices = NULL;
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
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	sdi->driver = mic_devs[idx].di;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "Temperature");

	if (mic_devs[idx].has_humidity)
		sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "Humidity");

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	serial_close(serial);

	return devices;
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

static GSList *dev_list(int idx)
{
	return ((struct drv_context *)(mic_devs[idx].di->context))->instances;
}

static int cleanup(int idx)
{
	return dev_clear(idx);
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
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
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	std_session_send_df_header(sdi, LOG_PREFIX);

	/* Poll every 100ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
		      mic_devs[idx].receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	return std_serial_dev_acquisition_stop(sdi, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

/* Driver-specific API function wrappers */
#define HW_INIT(X) \
static int init_##X(struct sr_dev_driver *di, struct sr_context *sr_ctx) { \
	(void)di; return init(sr_ctx, X); }
#define HW_CLEANUP(X) \
static int cleanup_##X(const struct sr_dev_driver *di) { \
	(void)di; return cleanup(X); }
#define HW_SCAN(X) \
static GSList *scan_##X(struct sr_dev_driver *di, GSList *options) { \
	(void)di; return scan(options, X); }
#define HW_DEV_LIST(X) \
static GSList *dev_list_##X(const struct sr_dev_driver *di) { \
	(void)di; return dev_list(X); }
#define HW_DEV_CLEAR(X) \
static int dev_clear_##X(const struct sr_dev_driver *di) { \
	(void)di; return dev_clear(X); }
#define HW_CONFIG_LIST(X) \
static int config_list_##X(uint32_t key, GVariant **data, \
const struct sr_dev_inst *sdi, const struct sr_channel_group *cg) { \
return config_list(key, data, sdi, cg, X); }
#define HW_DEV_ACQUISITION_START(X) \
static int dev_acquisition_start_##X(const struct sr_dev_inst *sdi \
) { return dev_acquisition_start(sdi, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_INIT(ID_UPPER) \
HW_CLEANUP(ID_UPPER) \
HW_SCAN(ID_UPPER) \
HW_DEV_LIST(ID_UPPER) \
HW_DEV_CLEAR(ID_UPPER) \
HW_CONFIG_LIST(ID_UPPER) \
HW_DEV_ACQUISITION_START(ID_UPPER) \
SR_PRIV struct sr_dev_driver ID##_driver_info = { \
	.name = NAME, \
	.longname = LONGNAME, \
	.api_version = 1, \
	.init = init_##ID_UPPER, \
	.cleanup = cleanup_##ID_UPPER, \
	.scan = scan_##ID_UPPER, \
	.dev_list = dev_list_##ID_UPPER, \
	.dev_clear = dev_clear_##ID_UPPER, \
	.config_get = NULL, \
	.config_set = config_set, \
	.config_list = config_list_##ID_UPPER, \
	.dev_open = std_serial_dev_open, \
	.dev_close = std_serial_dev_close, \
	.dev_acquisition_start = dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = dev_acquisition_stop, \
	.context = NULL, \
};

DRV(mic_98581, MIC_98581, "mic-98581", "MIC 98581")
DRV(mic_98583, MIC_98583, "mic-98583", "MIC 98583")
