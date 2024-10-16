/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static const char *scan_conn[] = {
	/* 287/289 */
	"115200/8n1",
	/* 87/89/187/189 */
	"9600/8n1",
	/* Scopemeter 190 series */
	"1200/8n1",
	NULL
};

static const struct flukedmm_profile supported_flukedmm[] = {
	{ FLUKE_87, "87", NULL, "QM\r", fluke_handle_qm_18x, 100, 1000 },
	{ FLUKE_89, "89", NULL, "QM\r", fluke_handle_qm_18x, 100, 1000 },
	{ FLUKE_187, "187", NULL, "QM\r", fluke_handle_qm_18x, 100, 1000 },
	{ FLUKE_189, "189", NULL, "QM\r", fluke_handle_qm_18x, 100, 1000 },
	{ FLUKE_190, "199B", NULL, "QM\r", fluke_handle_qm_190, 1000, 3500 },
	{ FLUKE_287, "287", fluke_init_channels_28x, "QDDA\r", fluke_handle_qdda_28x, 100, 1000 },
	{ FLUKE_289, "289", fluke_init_channels_28x, "QDDA\r", fluke_handle_qdda_28x, 100, 1000 },
	{ },
};

static const struct flukedmm_profile *find_profile(const char *model)
{
	int i;

	if (strncmp("FLUKE", model, 5))
		return NULL;

	for (i = 0; supported_flukedmm[i].model; i++) {
		if (!strcmp(supported_flukedmm[i].modelname, model + 6))
			return &supported_flukedmm[i];
	}

	return NULL;
}

static GSList *fluke_scan(struct sr_dev_driver *di, const char *conn,
		const char *serialcomm)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	int retry, len, s;
	char buf[128], *b, **tokens;
	const struct flukedmm_profile *profile;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	b = buf;
	retry = 0;
	devices = NULL;
	/* We'll try the discovery sequence three times in case the device
	 * is not in an idle state when we send ID. */
	while (!devices && retry < 3) {
		retry++;
		serial_flush(serial);
		if (serial_write_blocking(serial, "ID\r", 3, SERIAL_WRITE_TIMEOUT_MS) < 0) {
			sr_err("Unable to send ID string");
			continue;
		}

		/* Response is first a CMD_ACK byte (ASCII '0' for OK,
		 * or '1' to signify an error. */
		len = sizeof(buf);
		serial_readline(serial, &b, &len, 150);
		if (len != 1)
			continue;
		if (buf[0] != '0')
			continue;

		/* If CMD_ACK was OK, ID string follows. */
		len = sizeof(buf);
		serial_readline(serial, &b, &len, 850);
		if (len < 10)
			continue;
		if (strcspn(buf, ",") < 15)
			/* Looks like it's comma-separated. */
			tokens = g_strsplit(buf, ",", 3);
		else
			/* Fluke 199B, at least, uses semicolon. */
			tokens = g_strsplit(buf, ";", 3);

		profile = find_profile(tokens[0]);
		if (profile && tokens[1] && tokens[2]) {
			/* Skip leading spaces in version number. */
			for (s = 0; tokens[1][s] == ' '; s++);
			sdi = g_malloc0(sizeof(struct sr_dev_inst));
			sdi->status = SR_ST_INACTIVE;
			sdi->vendor = g_strdup("Fluke");
			sdi->model = g_strdup(tokens[0] + 6);
			sdi->version = g_strdup(tokens[1] + s);
			devc = g_malloc0(sizeof(struct dev_context));
			sr_sw_limits_init(&devc->limits);
			devc->profile = profile;
			sdi->inst_type = SR_INST_SERIAL;
			sdi->conn = serial;
			sdi->priv = devc;
			if (profile->init_channels) {
				profile->init_channels(sdi);
			} else {
				sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
			}
			devices = g_slist_append(devices, sdi);
		}
		g_strfreev(tokens);
		if (devices)
			/* Found one. */
			break;
	}
	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	GSList *l, *devices;
	int i;
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

	devices = NULL;
	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = fluke_scan(di, conn, serialcomm);
	} else {
		for (i = 0; scan_conn[i]; i++) {
			if ((devices = fluke_scan(di, conn, scan_conn[i])))
				break;
			/* The Scopemeter 199B, at least, requires this
			 * after all the 115k/9.6k confusion. */
			g_usleep(5 * 1000);
		}
	}

	return devices;
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
	const char *poll_cmd;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			fluke_receive_data, (void *)sdi);

	poll_cmd = devc->profile->poll_cmd;
	if (serial_write_blocking(serial, poll_cmd, strlen(poll_cmd), SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send poll command.");
		return SR_ERR;
	}
	devc->cmd_sent_at = g_get_monotonic_time() / 1000;
	devc->expect_response = TRUE;

	return SR_OK;
}

static struct sr_dev_driver flukedmm_driver_info = {
	.name = "fluke-dmm",
	.longname = "Fluke 8x/18x/28x series DMMs",
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
SR_REGISTER_DEV_DRIVER(flukedmm_driver_info);
