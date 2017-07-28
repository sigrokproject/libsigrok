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

#define SERIALCOMM "9600/8n1"

/* 23ms is the longest interval between tokens. */
#define MAX_SCAN_TIME_US (25 * 1000)

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_SOUNDLEVELMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SPL_WEIGHT_FREQ | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SPL_WEIGHT_TIME | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SPL_MEASUREMENT_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATALOG | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HOLD_MAX | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HOLD_MIN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_POWER_OFF | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *weight_freq[] = {
	"A",
	"C",
};

static const char *weight_time[] = {
	"F",
	"S",
};

static const uint64_t meas_ranges[][2] = {
	{ 30, 130 },
	{ 30, 80 },
	{ 50, 100 },
	{ 80, 130 },
};

static const char *data_sources[] = {
	"Live",
	"Memory",
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	GSList *l, *devices;
	gint64 start;
	const char *conn;
	unsigned char c;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN)
			conn = g_variant_get_string(src->data, NULL);
	}
	if (!conn)
		return NULL;

	serial = sr_serial_dev_inst_new(conn, SERIALCOMM);

	if (serial_open(serial, SERIAL_RDONLY) != SR_OK)
		return NULL;

	devices = NULL;
	start = g_get_monotonic_time();
	while (g_get_monotonic_time() - start < MAX_SCAN_TIME_US) {
		if (serial_read_nonblocking(serial, &c, 1) == 1 && c == 0xa5) {
			/* Found one. */
			sdi = g_malloc0(sizeof(struct sr_dev_inst));
			sdi->status = SR_ST_INACTIVE;
			sdi->vendor = g_strdup("CEM");
			sdi->model = g_strdup("DT-885x");
			devc = g_malloc0(sizeof(struct dev_context));
			devc->cur_mqflags = 0;
			devc->recording = -1;
			devc->cur_meas_range = 0;
			devc->cur_data_source = DATA_SOURCE_LIVE;
			devc->enable_data_source_memory = FALSE;
			sdi->conn = sr_serial_dev_inst_new(conn, SERIALCOMM);
			sdi->inst_type = SR_INST_SERIAL;
			sdi->priv = devc;
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "SPL");
			devices = g_slist_append(devices, sdi);
			break;
		}
		/* It takes about 1ms for a byte to come in. */
		g_usleep(1000);
	}

	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t low, high;
	int tmp, ret;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_DATALOG:
		if ((ret = cem_dt_885x_recording_get(sdi, &tmp)) == SR_OK)
			*data = g_variant_new_boolean(tmp);
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		tmp = cem_dt_885x_weight_freq_get(sdi);
		if (tmp == SR_MQFLAG_SPL_FREQ_WEIGHT_A)
			*data = g_variant_new_string("A");
		else if (tmp == SR_MQFLAG_SPL_FREQ_WEIGHT_C)
			*data = g_variant_new_string("C");
		else
			return SR_ERR;
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		tmp = cem_dt_885x_weight_time_get(sdi);
		if (tmp == SR_MQFLAG_SPL_TIME_WEIGHT_F)
			*data = g_variant_new_string("F");
		else if (tmp == SR_MQFLAG_SPL_TIME_WEIGHT_S)
			*data = g_variant_new_string("S");
		else
			return SR_ERR;
		break;
	case SR_CONF_HOLD_MAX:
		if ((ret = cem_dt_885x_holdmode_get(sdi, &tmp)) == SR_OK)
			*data = g_variant_new_boolean(tmp == SR_MQFLAG_MAX);
		break;
	case SR_CONF_HOLD_MIN:
		if ((ret = cem_dt_885x_holdmode_get(sdi, &tmp)) == SR_OK)
			*data = g_variant_new_boolean(tmp == SR_MQFLAG_MIN);
		break;
	case SR_CONF_SPL_MEASUREMENT_RANGE:
		if ((ret = cem_dt_885x_meas_range_get(sdi, &low, &high)) == SR_OK)
			*data = std_gvar_tuple_u64(low, high);
		break;
	case SR_CONF_POWER_OFF:
		*data = g_variant_new_boolean(FALSE);
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->cur_data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else
			*data = g_variant_new_string("Memory");
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int tmp, ret, idx;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_DATALOG:
		ret = cem_dt_885x_recording_set(sdi, g_variant_get_boolean(data));
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(weight_freq))) < 0)
			return SR_ERR_ARG;
		return cem_dt_885x_weight_freq_set(sdi, (weight_freq[idx][0] == 'A') ?
			SR_MQFLAG_SPL_FREQ_WEIGHT_A : SR_MQFLAG_SPL_FREQ_WEIGHT_C);
	case SR_CONF_SPL_WEIGHT_TIME:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(weight_time))) < 0)
			return SR_ERR_ARG;
		return cem_dt_885x_weight_time_set(sdi, (weight_time[idx][0] == 'F') ?
			SR_MQFLAG_SPL_TIME_WEIGHT_F : SR_MQFLAG_SPL_TIME_WEIGHT_S);
	case SR_CONF_HOLD_MAX:
		tmp = g_variant_get_boolean(data) ? SR_MQFLAG_MAX : 0;
		ret = cem_dt_885x_holdmode_set(sdi, tmp);
		break;
	case SR_CONF_HOLD_MIN:
		tmp = g_variant_get_boolean(data) ? SR_MQFLAG_MIN : 0;
		ret = cem_dt_885x_holdmode_set(sdi, tmp);
		break;
	case SR_CONF_SPL_MEASUREMENT_RANGE:
		if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(meas_ranges))) < 0)
			return SR_ERR_ARG;
		return cem_dt_885x_meas_range_set(sdi, meas_ranges[idx][0], meas_ranges[idx][1]);
	case SR_CONF_POWER_OFF:
		if (g_variant_get_boolean(data))
			ret = cem_dt_885x_power_off(sdi);
		break;
	case SR_CONF_DATA_SOURCE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(data_sources))) < 0)
			return SR_ERR_ARG;
		devc->cur_data_source = idx;
		devc->enable_data_source_memory = (idx == DATA_SOURCE_MEMORY);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SPL_WEIGHT_FREQ:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(weight_freq));
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(weight_time));
		break;
	case SR_CONF_SPL_MEASUREMENT_RANGE:
		*data = std_gvar_tuple_array(ARRAY_AND_SIZE(meas_ranges));
		break;
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

	devc = sdi->priv;
	devc->state = ST_INIT;
	devc->num_samples = 0;
	devc->buf_len = 0;

	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 150,
			cem_dt_885x_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver cem_dt_885x_driver_info = {
	.name = "cem-dt-885x",
	.longname = "CEM DT-885x",
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
SR_REGISTER_DEV_DRIVER(cem_dt_885x_driver_info);
