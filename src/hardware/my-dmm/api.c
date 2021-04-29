/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 AlexEagleC <orlovaleksandr7922@gmail.com>
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

#define SERIALCOMM "9600/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER
};

static const uint32_t devopts[] = {
	SR_CONF_MEASURED_QUANTITY | SR_CONF_SET,
	SR_CONF_RANGE | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_SET,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static struct sr_dev_driver example_driver_info;

#define RESPONSE_DELAY_US (20 * 1000)

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_HZ(20),
	SR_HZ(1),
};


/*static struct char *voltage_ranges[] = {
	
};

static struct char *current_ranges[] = {
	
};

static struct char *resistance_ranges[] = {
	
};*/

static const char *quantities[] = {
	"Voltage",
	"Current",
	"Resistance"
};

static const char *quantity_flags[] = {
	"AC",
	"DC",
	"Diode"
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	GSList *l, *devices;
	int num_read;
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

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	cg->channels = g_slist_append(cg->channels, ch);

	ch = sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");
	cg->channels = g_slist_append(cg->channels, ch);


		devc = g_malloc0(sizeof(struct dev_context));
		sr_sw_limits_init(&devc->limits);
		//devc->cur_mq[0] = SR_MQ_VOLTAGE;
		//devc->cur_mq[1] = SR_MQ_CURRENT;
		devc->quantity = SR_MQ_VOLTAGE;
		devc->quantity_flag = SR_MQFLAG_DC;
		devc->cur_samplerate = 1;
		sdi->inst_type = SR_INST_SERIAL;
		sdi->conn = serial;
		sdi->priv = devc;

		devices = g_slist_append(devices, sdi);
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and open it. */

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and close it. */

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *mq_arr[2];

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_MEASURED_QUANTITY:
		mq_arr[0] = g_variant_new_uint32(quantities[devc->quantity]);
		mq_arr[1] = g_variant_new_uint64(quantity_flags[devc->quantity_flag]);
		*data = g_variant_new_tuple(mq_arr, 2);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;
	(void)sdi;
	(void)data;
	(void)cg;
	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
		case SR_CONF_LIMIT_MSEC:
			return sr_sw_limits_config_set(&devc->limits, key, data);
	default:
		ret = SR_ERR_NA;
	}
	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;
	ret = SR_OK;
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

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;

	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial;
	char *buf = g_malloc0(sizeof(uint8_t));
	*buf = CMD_START;
	serial = sdi->conn;
	if (serial_write_blocking(serial, buf, 1, 1) != 1) {
		sr_err("Unable to send identification string.");
		
		return NULL;
	}
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	memset(devc->buf, 0, BUFSIZE);
	devc->buflen = 0;
	
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			my_dmm_receive_data, (struct sr_dev_inst *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */
	struct sr_serial_dev_inst *serial;
	//sr_session_source_remove(sdi->session, -1);
	serial = sdi->conn;
	serial_source_remove(sdi->session, serial);
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	ret = std_serial_dev_acquisition_stop(sdi);

	return ret;
}

static struct sr_dev_driver example_driver_info = {
	.name = "Example",
	.longname = "Example for sigrok",
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
SR_REGISTER_DEV_DRIVER(example_driver_info);
