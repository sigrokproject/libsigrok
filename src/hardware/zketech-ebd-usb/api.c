/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Sven Bursch-Osewold <sb_git@bursch.com>
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
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	GSList *l;
	struct sr_dev_inst *sdi;
	const char *conn, *serialcomm;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	uint8_t reply[MSG_LEN];

	conn = NULL;
	serialcomm = NULL;

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
		serialcomm = "9600/8e1";

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("ZKETECH");
	sdi->model = g_strdup("EBD-USB");
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");

	devc = g_malloc0(sizeof(struct dev_context));
	g_mutex_init(&devc->rw_mutex);
	devc->current_limit = 0;
	devc->running = FALSE;
	devc->load_activated = FALSE;
	sr_sw_limits_init(&devc->limits);
	sdi->priv = devc;

	/* Starting device. */
	ebd_init(serial, devc);
	int ret = ebd_read_chars(serial, MSG_LEN, reply);
	if (ret != MSG_LEN || reply[MSG_FRAME_BEGIN_POS] != MSG_FRAME_BEGIN \
			|| reply[MSG_FRAME_END_POS] != MSG_FRAME_END) {
		sr_warn("Invalid message received!");
		ret = SR_ERR;
	}
	ebd_stop(serial, devc);

	serial_close(serial);

	if (ret < 0)
		return NULL;

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;
	if (devc)
		g_mutex_clear(&devc->rw_mutex);

	return std_serial_dev_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;
	float fvalue;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_CURRENT_LIMIT:
		ret = ebd_get_current_limit(sdi, &fvalue);
		if (ret == SR_OK)
			*data = g_variant_new_double(fvalue);
		return ret;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	double value;
	struct dev_context *devc;

	(void)data;
	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_CURRENT_LIMIT:
		value = g_variant_get_double(data);
		if (value < 0.0 || value > 4.0)
			return SR_ERR_ARG;
		return ebd_set_current_limit(sdi, value);
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_CURRENT_LIMIT:
		*data = std_gvar_min_max_step(0.0, 4.0, 0.01);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	serial = sdi->conn;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	ebd_init(serial, devc);
	if (!ebd_current_is0(devc))
		ebd_loadstart(serial, devc);

	serial_source_add(sdi->session, serial, G_IO_IN, 100,
		ebd_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	ebd_loadstop(sdi->conn, sdi->priv);

	return std_serial_dev_acquisition_stop(sdi);
}

SR_PRIV struct sr_dev_driver zketech_ebd_usb_driver_info = {
	.name = "zketech-ebd-usb",
	.longname = "ZKETECH EBD-USB",
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
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(zketech_ebd_usb_driver_info);
