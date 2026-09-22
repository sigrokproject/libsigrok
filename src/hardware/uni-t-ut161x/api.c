/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Jochen Rueter <jo.rueter@gmail.com>
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
#include <string.h>
#include "protocol.h"

/* The UT-D09A cable with its WCH CH9329 chip, see README.devices. */
#define DEFAULT_CONN		"hid/ch9329"
#define DEFAULT_SERIALCOMM	"9600/8n1"

#define SCAN_TIMEOUT_MS		1000
#define SCAN_ATTEMPTS		2

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

/*
 * Ask the meter for its name ("UT161E" etc). The first response to the
 * request is an acknowledge, the name follows in a separate frame.
 */
static char *get_model_name(struct sr_serial_dev_inst *serial,
	struct dev_context *devc)
{
	uint8_t payload[FRAME_MAX_LEN];
	size_t len, i;
	int attempt;
	int64_t deadline;
	gboolean valid;

	for (attempt = 0; attempt < SCAN_ATTEMPTS; attempt++) {
		devc->buf_len = 0;
		if (ut161x_send_cmd(serial, CMD_GET_NAME) != SR_OK)
			return NULL;
		deadline = g_get_monotonic_time() + SCAN_TIMEOUT_MS * 1000;
		while (g_get_monotonic_time() < deadline) {
			if (ut161x_read_input(serial, devc) < 0)
				return NULL;
			if (!ut161x_get_frame(devc, payload, &len)) {
				g_usleep(5 * 1000);
				continue;
			}
			if (len < 2 || payload[0] != 'U' || payload[1] != 'T')
				continue;
			valid = TRUE;
			for (i = 0; i < len; i++) {
				if (!g_ascii_isprint(payload[i]))
					valid = FALSE;
			}
			if (valid)
				return g_strndup((const char *)payload, len);
		}
	}

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn, *serialcomm;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	char *model;

	conn = DEFAULT_CONN;
	serialcomm = DEFAULT_SERIALCOMM;
	(void)sr_serial_extract_options(options, &conn, &serialcomm);

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
		sr_serial_dev_inst_free(serial);
		return NULL;
	}
	(void)serial_flush(serial);

	devc = g_malloc0(sizeof(*devc));
	model = get_model_name(serial, devc);
	serial_close(serial);
	if (!model) {
		sr_dbg("No UNI-T UT161x meter found on %s.", conn);
		g_free(devc);
		sr_serial_dev_inst_free(serial);
		return NULL;
	}
	sr_info("Found UNI-T %s on %s.", model, conn);
	devc->buf_len = 0;
	sr_sw_limits_init(&devc->limits);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("UNI-T");
	sdi->model = model;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->connection_id = g_strdup(conn);
	sdi->priv = devc;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	default:
		return SR_ERR_NA;
	}
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

	devc->buf_len = 0;
	devc->req_pending = FALSE;
	(void)serial_flush(serial);

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	/* The receive routine sends the first measurement request. */
	return serial_source_add(sdi->session, serial, G_IO_IN,
		POLL_INTERVAL_MS, uni_t_ut161x_receive_data, (void *)sdi);
}

static struct sr_dev_driver uni_t_ut161x_driver_info = {
	.name = "uni-t-ut161x",
	.longname = "UNI-T UT161B/D/E (UT-D09A cable)",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(uni_t_ut161x_driver_info);
