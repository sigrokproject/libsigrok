/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Aurelien Jacobs <aurel@gnuage.org>
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
#include <stdlib.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

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

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	GSList *devices = NULL, *l;
	const char *conn = NULL, *serialcomm = NULL;
	uint8_t buf[292];
	size_t len;
	struct sr_config *src;

	len = sizeof(buf);

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
		serialcomm = "1200/7e1";

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDONLY) != SR_OK)
		return NULL;

	sr_info("Probing serial port %s.", conn);

	serial_flush(serial);

	/* Let's get a bit of data and see if we can find a packet. */
	if (serial_stream_detect(serial, buf, &len, len,
	                         teleinfo_packet_valid, 3000, 1200) != SR_OK)
		goto scan_cleanup;

	sr_info("Found device on port %s.", conn);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("EDF");
	sdi->model = g_strdup("Teleinfo");
	devc = g_malloc0(sizeof(struct dev_context));
	devc->optarif = teleinfo_get_optarif(buf);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P");

	if (devc->optarif == OPTARIF_BASE) {
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "BASE");
	} else if (devc->optarif == OPTARIF_HC) {
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HP");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HC");
	} else if (devc->optarif == OPTARIF_EJP) {
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HN");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HPM");
	} else if (devc->optarif == OPTARIF_BBR) {
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HPJB");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HPJW");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HPJR");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HCJB");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HCJW");
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "HCJR");
	}

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "IINST");
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "PAPP");

	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->sw_limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial = sdi->conn;
	struct dev_context *devc;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			teleinfo_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver teleinfo_driver_info = {
	.name = "teleinfo",
	.longname = "Teleinfo",
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
SR_REGISTER_DEV_DRIVER(teleinfo_driver_info);
