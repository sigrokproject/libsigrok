/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 George Hopkins <george-hopkins@null.net>
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

#include <string.h>
#include <config.h>
#include "protocol.h"

#define SERIALCOMM "115200/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_SOUNDLEVELMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SPL_WEIGHT_FREQ | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SPL_WEIGHT_TIME | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SPL_MEASUREMENT_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_POWER_OFF | SR_CONF_GET | SR_CONF_SET,
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

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	GSList *l, *devices;
	const char *conn;

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

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("PCE");
	sdi->model = g_strdup("PCE-322A");
	devc = g_malloc0(sizeof(struct dev_context));
	devc->cur_mqflags = SR_MQFLAG_SPL_TIME_WEIGHT_F | SR_MQFLAG_SPL_FREQ_WEIGHT_A;
	sdi->conn = sr_serial_dev_inst_new(conn, SERIALCOMM);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->priv = devc;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "SPL");
	devices = g_slist_append(devices, sdi);

	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, NULL);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *range[2];
	uint64_t low, high;
	uint64_t tmp;
	int ret;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		tmp = pce_322a_weight_freq_get(sdi);
		if (tmp == SR_MQFLAG_SPL_FREQ_WEIGHT_A)
			*data = g_variant_new_string("A");
		else if (tmp == SR_MQFLAG_SPL_FREQ_WEIGHT_C)
			*data = g_variant_new_string("C");
		else
			return SR_ERR;
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		tmp = pce_322a_weight_time_get(sdi);
		if (tmp == SR_MQFLAG_SPL_TIME_WEIGHT_F)
			*data = g_variant_new_string("F");
		else if (tmp == SR_MQFLAG_SPL_TIME_WEIGHT_S)
			*data = g_variant_new_string("S");
		else
			return SR_ERR;
		break;
	case SR_CONF_SPL_MEASUREMENT_RANGE:
		if ((ret = pce_322a_meas_range_get(sdi, &low, &high)) == SR_OK) {
			range[0] = g_variant_new_uint64(low);
			range[1] = g_variant_new_uint64(high);
			*data = g_variant_new_tuple(range, 2);
		}
		break;
	case SR_CONF_POWER_OFF:
		*data = g_variant_new_boolean(FALSE);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t tmp_u64, low, high;
	unsigned int i;
	int ret;
	const char *tmp_str;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		tmp_u64 = g_variant_get_uint64(data);
		devc->limit_samples = tmp_u64;
		ret = SR_OK;
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "A"))
			ret = pce_322a_weight_freq_set(sdi,
					SR_MQFLAG_SPL_FREQ_WEIGHT_A);
		else if (!strcmp(tmp_str, "C"))
			ret = pce_322a_weight_freq_set(sdi,
					SR_MQFLAG_SPL_FREQ_WEIGHT_C);
		else
			return SR_ERR_ARG;
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "F"))
			ret = pce_322a_weight_time_set(sdi,
					SR_MQFLAG_SPL_TIME_WEIGHT_F);
		else if (!strcmp(tmp_str, "S"))
			ret = pce_322a_weight_time_set(sdi,
					SR_MQFLAG_SPL_TIME_WEIGHT_S);
		else
			return SR_ERR_ARG;
		break;
	case SR_CONF_SPL_MEASUREMENT_RANGE:
		g_variant_get(data, "(tt)", &low, &high);
		ret = SR_ERR_ARG;
		for (i = 0; i < ARRAY_SIZE(meas_ranges); i++) {
			if (meas_ranges[i][0] == low && meas_ranges[i][1] == high) {
				ret = pce_322a_meas_range_set(sdi, low, high);
				break;
			}
		}
		break;
	case SR_CONF_POWER_OFF:
		if (g_variant_get_boolean(data))
			ret = pce_322a_power_off(sdi);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	GVariant *tuple, *range[2];
	GVariantBuilder gvb;
	unsigned int i;
	int ret;

	(void)cg;

	ret = SR_OK;
	if (!sdi) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
			break;
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			break;
		case SR_CONF_SPL_WEIGHT_FREQ:
			*data = g_variant_new_strv(weight_freq, ARRAY_SIZE(weight_freq));
			break;
		case SR_CONF_SPL_WEIGHT_TIME:
			*data = g_variant_new_strv(weight_time, ARRAY_SIZE(weight_time));
			break;
		case SR_CONF_SPL_MEASUREMENT_RANGE:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			for (i = 0; i < ARRAY_SIZE(meas_ranges); i++) {
				range[0] = g_variant_new_uint64(meas_ranges[i][0]);
				range[1] = g_variant_new_uint64(meas_ranges[i][1]);
				tuple = g_variant_new_tuple(range, 2);
				g_variant_builder_add_value(&gvb, tuple);
			}
			*data = g_variant_builder_end(&gvb);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;

	ret = std_serial_dev_open(sdi);
	if (ret != SR_OK)
		return ret;

	return pce_322a_connect(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	/*
	 * Ensure device gets properly disconnected even when there was
	 * no acquisition.
	 */
	pce_322a_disconnect(sdi);

	return std_serial_dev_close(sdi);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->buffer_len = 0;

	std_session_send_df_header(sdi, LOG_PREFIX);

	/* Poll every 150ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 150,
			pce_322a_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	pce_322a_disconnect(sdi);

	return std_serial_dev_acquisition_stop(sdi, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

static struct sr_dev_driver pce_322a_driver_info = {
	.name = "pce-322a",
	.longname = "PCE PCE-322A",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(pce_322a_driver_info);
