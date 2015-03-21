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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_THERMOMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static const char *channel_names[] = {
	"T1", "T2", "T3", "T4",
	NULL,
};

SR_PRIV struct sr_dev_driver center_309_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_k204_driver_info;

SR_PRIV const struct center_dev_info center_devs[] = {
	{
		"Center", "309", "9600/8n1", 4, 32000, 45,
		center_3xx_packet_valid,
		&center_309_driver_info, receive_data_CENTER_309,
	},
	{
		"Voltcraft", "K204", "9600/8n1", 4, 32000, 45,
		center_3xx_packet_valid,
		&voltcraft_k204_driver_info, receive_data_VOLTCRAFT_K204,
	},
};

static int dev_clear(int idx)
{
	return std_dev_clear(center_devs[idx].di, NULL);
}

static int init(struct sr_context *sr_ctx, int idx)
{
	return std_init(sr_ctx, center_devs[idx].di, LOG_PREFIX);
}

static GSList *center_scan(const char *conn, const char *serialcomm, int idx)
{
	int i;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	GSList *devices;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	drvc = center_devs[idx].di->priv;
	devices = NULL;
	serial_flush(serial);

	sr_info("Found device on port %s.", conn);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(center_devs[idx].vendor);
	sdi->model = g_strdup(center_devs[idx].device);
	devc = g_malloc0(sizeof(struct dev_context));
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	sdi->driver = center_devs[idx].di;

	for (i = 0; i <  center_devs[idx].num_channels; i++) {
		sr_channel_new(sdi, i, SR_CHANNEL_ANALOG,
				    TRUE, channel_names[i]);
	}

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
		devices = center_scan(conn, serialcomm, idx);
	} else {
		/* Try the default. */
		devices = center_scan(conn, center_devs[idx].conn, idx);
	}

	return devices;
}

static GSList *dev_list(int idx)
{
	return ((struct drv_context *)(center_devs[idx].di->priv))->instances;
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
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi)
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		else
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
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

	/* Poll every 500ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 500,
		      center_devs[idx].receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data,
			std_serial_dev_close, sdi->conn, LOG_PREFIX);
}

/* Driver-specific API function wrappers */
#define HW_INIT(X) \
static int init_##X(struct sr_dev_driver *d, \
	struct sr_context *sr_ctx) { (void)d; return init(sr_ctx, X); }
#define HW_CLEANUP(X) \
static int cleanup_##X(const struct sr_dev_driver *d) { \
	(void)d; return cleanup(X); }
#define HW_SCAN(X) \
static GSList *scan_##X(struct sr_dev_driver *d, GSList *options) { \
	(void)d; return scan(options, X); }
#define HW_DEV_LIST(X) \
static GSList *dev_list_##X(const struct sr_dev_driver *d) { \
	(void)d; return dev_list(X); }
#define HW_DEV_CLEAR(X) \
static int dev_clear_##X(const struct sr_dev_driver *d) { \
	(void)d; return dev_clear(X); }
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

DRV(center_309, CENTER_309, "center-309", "Center 309")
DRV(voltcraft_k204, VOLTCRAFT_K204, "voltcraft-k204", "Voltcraft K204")
