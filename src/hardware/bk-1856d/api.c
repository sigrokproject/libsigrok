/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 LUMERIIX/danselmi <.>
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

#define SERIALCOMM "9600/8n1/dtr=1/rts=0"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	/** using TIMEBASE to select the gate time */
	SR_CONF_TIMEBASE      | SR_CONF_SET |SR_CONF_GET| SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_DATA_SOURCE   | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static struct sr_dev_driver bk_1856d_driver_info;

const uint64_t timebases[][2] = {
	/*miliseconds*/
	{ 10, 1000 },
	{ 100, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 10, 1 },
};

static const char *data_sources[] = {
	"Input A", "Input C",
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	GSList *opt;
	const char *conn;

	conn = NULL;
	for (opt = options; opt; opt = opt->next) {
		struct sr_config *src = opt->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("BK Precision");
	sdi->model = g_strdup("bk-1856d");
	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&(devc->sw_limits));
	sdi->conn = sr_serial_dev_inst_new(conn, SERIALCOMM);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->priv = devc;
	g_mutex_init(&devc->rw_mutex);
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
	devc->sel_input = InputC;
	devc->curr_sel_input = InputC;
	devc->gate_time = 0;
	devc->hold = Off;

	g_usleep(150 * 1000); /* Wait a little to allow serial port to settle. */

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!(devc = sdi->priv))
		return SR_ERR;

	switch (key) {
	case SR_CONF_TIMEBASE:
		*data = g_variant_new("(tt)",
				timebases[devc->gate_time][0],
				timebases[devc->gate_time][1]);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		sr_sw_limits_config_get(&(devc->sw_limits), key, data);
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->sel_input == InputA)
			*data = g_variant_new_string(data_sources[0]);
		else
			*data = g_variant_new_string(data_sources[1]);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int idx;
	struct dev_context *devc;

	(void)cg;

	if (!(devc = sdi->priv))
		return SR_ERR;

	switch (key) {
	case SR_CONF_TIMEBASE:
		{
			uint64_t p, q;
			g_variant_get(data, "(tt)", &p, &q);
			if	    (p ==  10 && q == 1000) bk_1856d_set_gate_time(devc, 0);
			else if (p == 100 && q == 1000) bk_1856d_set_gate_time(devc, 1);
			else if (p ==   1 && q ==    1) bk_1856d_set_gate_time(devc, 2);
			else if (p ==  10 && q ==    1) bk_1856d_set_gate_time(devc, 3);
			else
				return SR_ERR_NA;
		}
		break;
	case SR_CONF_LIMIT_SAMPLES:
		sr_sw_limits_config_set(&(devc->sw_limits),key, data);
		break;
	case SR_CONF_DATA_SOURCE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(data_sources))) < 0)
			return SR_ERR_ARG;
		bk_1856d_select_input(devc, idx);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static GVariant *build_tuples(const uint64_t (*array)[][2], unsigned int n)
{
	unsigned int i;
	GVariant *rational[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < n; i++) {
		rational[0] = g_variant_new_uint64((*array)[i][0]);
		rational[1] = g_variant_new_uint64((*array)[i][1]);
		g_variant_builder_add_value(&gvb, g_variant_new_tuple(rational, 2));
	}

	return g_variant_builder_end(&gvb);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_TIMEBASE:
		*data = build_tuples(&timebases, ARRAY_SIZE(timebases));
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;
	if (devc)
		g_mutex_clear(&devc->rw_mutex);

	return std_serial_dev_close(sdi);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			bk_1856d_receive_data, (void *)sdi);

	bk_1856d_init(sdi);

	return SR_OK;
}

static struct sr_dev_driver bk_1856d_driver_info = {
	.name = "bk-1856d",
	.longname = "BK Precision 1856D",
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
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(bk_1856d_driver_info);

