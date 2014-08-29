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

#include "protocol.h"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t hwcaps[] = {
	SR_CONF_THERMOMETER,
	SR_CONF_HYGROMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
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
	struct sr_channel *ch;
	struct sr_serial_dev_inst *serial;
	GSList *devices;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	drvc = mic_devs[idx].di->priv;
	devices = NULL;
	serial_flush(serial);

	/* TODO: Query device type. */
	// ret = mic_cmd_get_device_info(serial);

	sr_info("Found device on port %s.", conn);

	/* TODO: Fill in version from protocol response. */
	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, mic_devs[idx].vendor,
				    mic_devs[idx].device, NULL)))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sdi->priv = devc;
	sdi->driver = mic_devs[idx].di;

	if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, "Temperature")))
		goto scan_cleanup;
	sdi->channels = g_slist_append(sdi->channels, ch);

	if (mic_devs[idx].has_humidity) {
		if (!(ch = sr_channel_new(1, SR_CHANNEL_ANALOG, TRUE, "Humidity")))
			goto scan_cleanup;
		sdi->channels = g_slist_append(sdi->channels, ch);
	}

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
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
	return ((struct drv_context *)(mic_devs[idx].di->priv))->instances;
}

static int cleanup(int idx)
{
	return dev_clear(idx);
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	switch (id) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data, int idx)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->cb_data = cb_data;
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Poll every 100ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
		      mic_devs[idx].receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

/* Driver-specific API function wrappers */
#define HW_INIT(X) \
static int init_##X(struct sr_context *sr_ctx) { return init(sr_ctx, X); }
#define HW_CLEANUP(X) \
static int cleanup_##X(void) { return cleanup(X); }
#define HW_SCAN(X) \
static GSList *scan_##X(GSList *options) { return scan(options, X); }
#define HW_DEV_LIST(X) \
static GSList *dev_list_##X(void) { return dev_list(X); }
#define HW_DEV_CLEAR(X) \
static int dev_clear_##X(void) { return dev_clear(X); }
#define HW_DEV_ACQUISITION_START(X) \
static int dev_acquisition_start_##X(const struct sr_dev_inst *sdi, \
void *cb_data) { return dev_acquisition_start(sdi, cb_data, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_INIT(ID_UPPER) \
HW_CLEANUP(ID_UPPER) \
HW_SCAN(ID_UPPER) \
HW_DEV_LIST(ID_UPPER) \
HW_DEV_CLEAR(ID_UPPER) \
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
	.config_list = config_list, \
	.dev_open = std_serial_dev_open, \
	.dev_close = std_serial_dev_close, \
	.dev_acquisition_start = dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = dev_acquisition_stop, \
	.priv = NULL, \
};

DRV(mic_98581, MIC_98581, "mic-98581", "MIC 98581")
DRV(mic_98583, MIC_98583, "mic-98583", "MIC 98583")
