/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
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
#include "m2k_wrapper.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_NUM_LOGIC_CHANNELS,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

SR_PRIV const char *adalmm2k_channel_names[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
	"13", "14", "15",
};

/* Possible sample rates : 10 Hz to 100 MHz */
static const uint64_t samplerates[] = {
	SR_HZ(10),
	SR_MHZ(100),
	SR_HZ(1),
};

/**
 * retrieve list of devices connected
 * @param[in] di
 * @param[in] options
 * @return list of devices
 */
static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct m2k_infos *info;
	GSList *infos = NULL;
	GSList *l;
	GSList *devices = NULL;
	const char *conn;
	guint i;
	char tmp[32];

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN)
			conn = g_variant_get_string(src->data, NULL);
	}

	if (!conn)
		m2k_list_all(&infos);
	else {
		sprintf(tmp, "usb:%s", conn);
		m2k_get_specific_info(tmp, &infos);
	}

	for (i = 0; i < g_slist_length(infos); i++) {
		info = (struct m2k_infos *)g_slist_nth_data(infos, i);
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = info->vendor;
		sdi->model = info->name;
		sdi->serial_num = info->serial_number;
		sdi->connection_id = info->uri;

		for (int chan = 0; chan < 16; chan++) {
			sprintf(tmp, "%d", chan);
		    sr_channel_new(sdi, chan,
				SR_CHANNEL_LOGIC, TRUE, tmp);
		}

		devices = g_slist_append(devices, sdi);
		g_free(info);
	}

	return std_scan_complete(di, devices);
}

/** open a specific device and apply default configuration
 * @param[in] sdi
 * return SR_ERR if something wrong, SR_OK otherwise
 */
static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->m2k = m2k_open(sdi->connection_id);
	if (devc->m2k == NULL) {
		sr_err("No ADALM2000 device available/connected to your PC.\n");
		return SR_ERR;
	}
	devc->sample_buf = NULL;
	devc->cur_samplerate = (uint64_t)m2k_get_rate(devc->m2k);
	devc->limit_samples = 1000000;
	/* set buffer to the maximum allowed size */
	devc->sample_buf = (uint16_t *)g_malloc(sizeof(short) * MAX_SAMPLES);
	if (devc->sample_buf == NULL) {
		m2k_close(devc->m2k);
		return SR_ERR;
	}
	if (m2k_set_rate(devc->m2k, devc->cur_samplerate) < 0) {
		sr_err("Fail to configure samplerate\n");
		return SR_ERR;
	}
	if (m2k_disable_trigg(devc->m2k) < 0) {
		sr_err("Fail to disable trigger\n");
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * close and cleanup device
 */
static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->sample_buf != NULL) {
		g_free(devc->sample_buf);
		devc->sample_buf = NULL;
	}

	if (m2k_close(devc->m2k) < 0) {
		sr_err("Fail to close\n");
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * return options value
 * @return SR_ERR_NA if option not supported
 */
static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(m2k_get_rate(devc->m2k));
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		*data = g_variant_new_uint32(g_slist_length(sdi->channels));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

/**
 * apply provided configuration value
 * return SR_ERR_NA if option not supported, SR_OK otherwise
 */
static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t tmp_u64;
	int ret;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		m2k_set_rate(devc->m2k, devc->cur_samplerate);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		tmp_u64 = g_variant_get_uint64(data);
		devc->limit_samples = tmp_u64;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts,
			drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = std_gvar_tuple_u64(MIN_SAMPLES, MAX_SAMPLES);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

/**
 * start acquisition: configure channels, pre trigger
 * @return SR_ERR if something wrong, SR_OK otherwise
 */
static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GSList *l;
	struct sr_channel *channel;

	devc = sdi->priv;

	/* Configure channels */
	devc->chan_en = 0;

	/* Basic triggers. */
	if (adalm_2000_convert_trigger(sdi) != TRUE)
		return SR_ERR;

	for (l = sdi->channels; l; l = l->next) {
		channel = l->data;
		if (channel->enabled)
			devc->chan_en |= (1 << channel->index);
	}

	if (m2k_enable_channel(devc->m2k, devc->chan_en) < 0) {
		sr_err("Fail to enable channel\n");
		return SR_ERR;
	}

	/* configure pre-trigger */
	int delay = -((devc->capture_ratio * devc->limit_samples) / 100);
	/* min delay is - 8192 */
	if (delay < -8192) {
		sr_warn("pre trigger delay outside allowed value, set to maximum value\n");
		delay = -8192;
	}
	if (m2k_pre_trigger_delay(devc->m2k, delay) != 0) {
		sr_err("Fail to configure pre-trigger\n");
		return SR_ERR;
	}

	std_session_send_df_header(sdi);

	if (m2k_start_acquisition(devc->m2k, devc->limit_samples) < 0) {
		sr_err("Fail to start acquisition\n");
		return SR_ERR;
	}

	sr_session_source_add(sdi->session, -1, G_IO_IN, 0,
			adalm_2000_receive_data, (void *)sdi);

	return SR_OK;
}

/**
 * stop acquisition
 * return SR_ERR if something wrong, SR_OK otherwise
 */
static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	sr_session_source_remove(sdi->session, -1);

	std_session_send_df_end(sdi);

	if (m2k_stop_acquisition(devc->m2k) < 0) {
		sr_err("Fail to stop acquisition\n");
		return SR_ERR;
	}

	return SR_OK;
}

static struct sr_dev_driver adalm_2000_driver_info = {
	.name = "adalm-2000",
	.longname = "ADALM 2000",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(adalm_2000_driver_info);
