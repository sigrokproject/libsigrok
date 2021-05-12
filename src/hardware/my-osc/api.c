/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Aleksandr Orlov <orlovaleksandr7922@gmail.com>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "protocol.h"

#define SERIALCOMM "115200/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET
};

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_KHZ(100),
	SR_HZ(1),
};

static struct sr_dev_driver my_osc_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	GSList *l, *devices;
	int len;
	const char *conn, *serialcomm;
	char *buf, **tokens;

	devices = NULL;
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
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	buf = g_malloc0(sizeof(uint8_t));
	*buf = CMD_SCAN;

	if (serial_write_blocking(serial, buf, 1, 100) != 1) {
		sr_err("Unable to send identification string.");
		
		return NULL;
	}
	
	len = 64;
	buf = g_malloc(len);
	serial_readline(serial, &buf, &len, 100);

	tokens = g_strsplit(buf, ",", 3);

	if (tokens[0] && tokens[1] && tokens[2]) {
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(tokens[0]);
		sdi->model = g_strdup(tokens[1]);
		sdi->version = g_strdup(tokens[2]);

		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("1");
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

		ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");
		cg->channels = g_slist_append(cg->channels, ch);

		ch = sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "CH2");
		cg->channels = g_slist_append(cg->channels, ch);

		devc = g_malloc0(sizeof(struct dev_context));
		sr_sw_limits_init(&devc->limits);
		devc->cur_samplerate = SR_HZ(10);
		devc->limits.limit_frames = MIN_NUM_FRAMES;
		devc->channel_entry = cg->channels;
		sdi->inst_type = SR_INST_SERIAL;
		sdi->conn = serial;
		sdi->priv = devc;

		devices = g_slist_append(devices, sdi);
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limits.limit_frames);
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_string("Live");
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint64_t tmp_u64;
	devc = sdi->priv;
	serial = sdi->conn;
	if (!sdi)
		return SR_ERR_ARG;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		tmp_u64 = g_variant_get_uint64(data);
		my_osc_osc_set_samplerate(serial, tmp_u64);
		if (tmp_u64 < samplerates[0] || tmp_u64 > samplerates[1])
			return SR_ERR_SAMPLERATE;
		devc->cur_samplerate = tmp_u64;
		break;
	case SR_CONF_LIMIT_FRAMES:
		tmp_u64 = g_variant_get_uint64(data);
		my_osc_set_limit_frames(serial, tmp_u64);
		if (tmp_u64 < MIN_NUM_FRAMES)
			return SR_ERR;
		devc->limits.limit_frames = tmp_u64;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial;
	char *buf = g_malloc0(sizeof(uint8_t));
	*buf = CMD_START;
	serial = sdi->conn;
	int send_code = 0;
	send_code = serial_write_blocking(serial, buf, 1, serial_timeout(serial, 1));
	if (send_code != 1) {
		sr_err("Unable to send identification string. Code:%d", send_code);
		
		return NULL;
	}
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	memset(devc->buf, 0, BUFSIZE);
	devc->buflen = 0;
	
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			my_osc_receive_data, (struct sr_dev_inst *)sdi);

	std_session_send_df_frame_begin(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	serial = sdi->conn;
	serial_source_remove(sdi->session, serial);
	int ret;

	ret = std_serial_dev_acquisition_stop(sdi);

	return ret;
}

static struct sr_dev_driver my_osc_driver_info = {
	.name = "my-osc",
	.longname = "my-osc",
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
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(my_osc_driver_info);
