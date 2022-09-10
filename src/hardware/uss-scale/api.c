/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_SCALE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static const char *serial_modes[] = {
	"9600/8n1", /* The factory default. */
	"19200/8n1",
	"4800/8n1",
	"2400/8n1",
};

static struct sr_serial_dev_inst *probe(struct scale_info *scale, const char *conn, const char *mode)
{
	struct sr_serial_dev_inst *serial;
	uint8_t buf[128];
	size_t len;
	enum sr_error_code err;

	sr_info("Probing serial port %s with %s.", conn, mode);

	serial = sr_serial_dev_inst_new(conn, mode);
	err = serial_open(serial, SERIAL_RDWR);
	if (err)
		goto probe_failed;

	/* Let's get a bit of data and see if we can find a packet. */
	len = sizeof(buf);
	err = serial_stream_detect(serial, buf, &len, scale->packet_size,
		scale->packet_valid, NULL, NULL, 3000);
	if (!err) {
		sr_info("Found device on port %s.", conn);
		return serial;
	}

probe_failed:
	serial_close(serial);
	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct scale_info *scale;
	struct sr_config *src;
	GSList *l;
	const char *conn, *serialcomm;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	size_t n;

	scale = (struct scale_info *)di;

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
		serial = probe(scale, conn, serialcomm);
	} else {
		for (n = 0; n < ARRAY_SIZE(serial_modes); n++) {
			serial = probe(scale, conn, serial_modes[n]);
			if (serial)
				break;
		}
	}

	if (!serial)
		return NULL;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(scale->vendor);
	sdi->model = g_strdup(scale->device);
	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "Mass");
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
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	serial = sdi->conn;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 50,
		      uss_scale_receive_data, (void *)sdi);

	return SR_OK;
}

#define SCALE(ID, CHIPSET, VENDOR, MODEL, PACKETSIZE, \
			VALID, PARSE) \
	&((struct scale_info) { \
		{ \
			.name = ID, \
			.longname = VENDOR " " MODEL, \
			.api_version = 1, \
			.init = std_init, \
			.cleanup = std_cleanup, \
			.scan = scan, \
			.dev_list = std_dev_list, \
			.dev_clear = std_dev_clear, \
			.config_get = NULL, \
			.config_set = config_set, \
			.config_list = config_list, \
			.dev_open = std_serial_dev_open, \
			.dev_close = std_serial_dev_close, \
			.dev_acquisition_start = dev_acquisition_start, \
			.dev_acquisition_stop = std_serial_dev_acquisition_stop, \
			.context = NULL, \
		}, \
		VENDOR, MODEL, PACKETSIZE, \
		VALID, PARSE \
	}).di

SR_REGISTER_DEV_DRIVER_LIST(uss_scale_drivers,
	SCALE(
		"uss-dbs28", uss_dbs,
		"U.S. Solid", "DBS28",
		17, sr_uss_dbs_packet_valid, sr_uss_dbs_parse
	)
);
