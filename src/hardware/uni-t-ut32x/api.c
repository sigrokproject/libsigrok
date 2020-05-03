/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_THERMOMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

/*
 * BEWARE! "T1-T2" looks like a range, and is probably not a good
 * channel name. Using it in sigrok-cli -C specs is troublesome. Use
 * "delta" instead? -- But OTOH channels are not selected by the
 * software. Instead received packets just reflect the one channel
 * that manually was selected by the user via the device's buttons.
 * So the name is not a blocker, and it matches the labels on the
 * device and in the manual. So we can get away with it.
 */
static const char *channel_names[] = {
	"T1", "T2", "T1-T2",
};

static const char *data_sources[] = {
	"Live", "Memory",
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn, *serialcomm;
	struct sr_config *src;
	GSList *l, *devices;
	struct sr_serial_dev_inst *serial;
	int rc;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t i;

	/*
	 * Implementor's note: Do _not_ add a default conn value here,
	 * always expect users to specify the connection. Otherwise the
	 * UT32x driver's scan routine results in false positives, will
	 * match _any_ UT-D04 cable which uses the same USB HID chip.
	 */
	conn = NULL;
	serialcomm = "2400/8n1";
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
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	rc = serial_open(serial, SERIAL_RDWR);
	/* Cannot query/identify the device. Successful open shall suffice. */
	serial_close(serial);
	if (rc != SR_OK) {
		sr_serial_dev_inst_free(serial);
		return devices;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("UNI-T");
	sdi->model = g_strdup("UT32x");
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	sr_sw_limits_init(&devc->limits);
	devc->data_source = DEFAULT_DATA_SOURCE;
	for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
		sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
				channel_names[i]);
	}
	devices = g_slist_append(devices, sdi);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_string(data_sources[devc->data_source]);
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
	int idx;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(data_sources))) < 0)
			return SR_ERR_ARG;
		devc->data_source = idx;
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
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
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
	uint8_t cmd;

	devc = sdi->priv;
	serial = sdi->conn;

	sr_sw_limits_acquisition_start(&devc->limits);
	devc->packet_len = 0;
	std_session_send_df_header(sdi);

	if (devc->data_source == DATA_SOURCE_LIVE)
		cmd = CMD_GET_LIVE;
	else
		cmd = CMD_GET_STORED;
	serial_write_blocking(serial, &cmd, sizeof(cmd), 0);

	serial_source_add(sdi->session, serial, G_IO_IN, 10,
			ut32x_handle_events, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* Have the reception routine stop the acquisition. */
	sdi->status = SR_ST_STOPPING;

	return SR_OK;
}

static struct sr_dev_driver uni_t_ut32x_driver_info = {
	.name = "uni-t-ut32x",
	.longname = "UNI-T UT32x",
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
SR_REGISTER_DEV_DRIVER(uni_t_ut32x_driver_info);
