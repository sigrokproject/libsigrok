/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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
#include <fcntl.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define SERIALCOMM "9600/8e1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_SOUNDLEVELMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	GSList *l;
	const char *conn, *serialcomm;
	struct sr_serial_dev_inst *serial;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		if (!(src = l->data)) {
			sr_err("Invalid option data, skipping.");
			continue;
		}
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
		serialcomm = SERIALCOMM;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Tondaj");
	sdi->model = g_strdup("SL-814");
	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sdi->priv = devc;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial;

	std_session_send_df_header(sdi);

	sr_sw_limits_acquisition_start(&devc->limits);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 500,
		      tondaj_sl_814_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver tondaj_sl_814_driver_info = {
	.name = "tondaj-sl-814",
	.longname = "Tondaj SL-814",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(tondaj_sl_814_driver_info);
