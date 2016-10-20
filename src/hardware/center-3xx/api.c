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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_THERMOMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static const char *channel_names[] = {
	"T1", "T2", "T3", "T4",
};

static struct sr_dev_driver center_309_driver_info;
static struct sr_dev_driver voltcraft_k204_driver_info;

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

static GSList *center_scan(const char *conn, const char *serialcomm, int idx)
{
	int i;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

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

	for (i = 0; i < center_devs[idx].num_channels; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel_names[i]);

	serial_close(serial);

	return g_slist_append(NULL, sdi);
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

	return std_scan_complete(center_devs[idx].di, devices);
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->sw_limits, key, data);
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

static int dev_acquisition_start(const struct sr_dev_inst *sdi, int idx)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	std_session_send_df_header(sdi);

	/* Poll every 500ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 500,
		      center_devs[idx].receive_data, (void *)sdi);

	return SR_OK;
}

/* Driver-specific API function wrappers */
#define HW_SCAN(X) \
static GSList *scan_##X(struct sr_dev_driver *d, GSList *options) { \
	(void)d; return scan(options, X); }
#define HW_DEV_ACQUISITION_START(X) \
static int dev_acquisition_start_##X(const struct sr_dev_inst *sdi \
) { return dev_acquisition_start(sdi, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_SCAN(ID_UPPER) \
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
	.config_list = config_list, \
	.dev_open = std_serial_dev_open, \
	.dev_close = std_serial_dev_close, \
	.dev_acquisition_start = dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = std_serial_dev_acquisition_stop, \
	.context = NULL, \
}; \
SR_REGISTER_DEV_DRIVER(ID##_driver_info);

DRV(center_309, CENTER_309, "center-309", "Center 309")
DRV(voltcraft_k204, VOLTCRAFT_K204, "voltcraft-k204", "Voltcraft K204")
