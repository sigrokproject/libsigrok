/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Andreas Sandberg <andreas@sandberg.pp.se>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define RDTECH_TC_SERIALCOMM "115200/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ENERGYMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static GSList *rdtech_tc_scan(struct sr_dev_driver *di, const char *conn,
			      const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;
	GSList *devices = NULL;
	struct dev_context *devc = NULL;
	struct sr_dev_inst *sdi = NULL;

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		goto err_out;

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);

	if (rdtech_tc_probe(serial, devc) != SR_OK) {
		sr_err("Failed to find a supported RDTech TC device.");
		goto err_out_serial;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("RDTech");
	sdi->model = g_strdup(devc->dev_info.model_name);
	sdi->version = g_strdup(devc->dev_info.fw_ver);
	sdi->serial_num = g_strdup_printf("%08" PRIu32, devc->dev_info.serial_num);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;

	for (int i = 0; devc->channels[i].name; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, devc->channels[i].name);

	devices = g_slist_append(devices, sdi);
	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);

err_out_serial:
	g_free(devc);
	serial_close(serial);
err_out:
	sr_serial_dev_inst_free(serial);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	const char *conn = NULL;
	const char *serialcomm = RDTECH_TC_SERIALCOMM;

	for (GSList *l = options; l; l = l->next) {
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

	return rdtech_tc_scan(di, conn, serialcomm);
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
	struct sr_serial_dev_inst *serial = sdi->conn;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			  rdtech_tc_receive_data, (void *)sdi);

	return rdtech_tc_poll(sdi);
}

static struct sr_dev_driver rdtech_tc_driver_info = {
	.name = "rdtech-tc",
	.longname = "RDTech TC66C USB power meter",
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
SR_REGISTER_DEV_DRIVER(rdtech_tc_driver_info);
