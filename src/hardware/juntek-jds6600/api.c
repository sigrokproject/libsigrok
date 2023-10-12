/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include "config.h"

#include "protocol.h"

#define DFLT_SERIALCOMM	"115200/8n1"

#define VENDOR_TEXT	"Juntek"
#define MODEL_TEXT	"JDS6600"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_SET,
	SR_CONF_PHASE | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OFFSET | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DUTY_CYCLE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *devices;
	const char *conn, *serialcomm;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *ser;
	int ret;
	size_t ch_idx, idx, ch_nr;
	char cg_name[8];
	struct sr_channel_group *cg;
	struct sr_channel *ch;

	devices = NULL;

	conn = NULL;
	serialcomm = DFLT_SERIALCOMM;
	(void)sr_serial_extract_options(options, &conn, &serialcomm);
	if (!conn)
		return devices;

	ser = sr_serial_dev_inst_new(conn, serialcomm);
	if (!ser)
		return devices;
	ret = serial_open(ser, SERIAL_RDWR);
	if (ret != SR_OK) {
		sr_serial_dev_inst_free(ser);
		return devices;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->inst_type = SR_INST_USB;
	sdi->conn = ser;
	sdi->connection_id = g_strdup(conn);
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;

	ret = jds6600_identify(sdi);
	if (ret != SR_OK)
		goto fail;
	ret = jds6600_setup_devc(sdi);
	if (ret != SR_OK)
		goto fail;
	(void)serial_close(ser);

	sdi->vendor = g_strdup(VENDOR_TEXT);
	sdi->model = g_strdup(MODEL_TEXT);
	if (devc->device.serial_number)
		sdi->serial_num = g_strdup(devc->device.serial_number);

	ch_idx = 0;
	for (idx = 0; idx < MAX_GEN_CHANNELS; idx++) {
		ch_nr = idx + 1;
		snprintf(cg_name, sizeof(cg_name), "CH%zu", ch_nr);
		cg = sr_channel_group_new(sdi, cg_name, NULL);
		(void)cg;
		ch = sr_channel_new(sdi, ch_idx,
			SR_CHANNEL_ANALOG, FALSE, cg_name);
		cg->channels = g_slist_append(cg->channels, ch);
		ch_idx++;
	}

	devices = g_slist_append(devices, sdi);
	return std_scan_complete(di, devices);

fail:
	(void)serial_close(ser);
	sr_serial_dev_inst_free(ser);
	if (devc) {
		g_free(devc->device.serial_number);
		g_free(devc->waveforms.fw_codes);
		g_free(devc->waveforms.names);
	}
	g_free(devc);
	sr_dev_inst_free(sdi);

	return devices;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;
	size_t cg_idx;
	struct devc_wave *waves;
	struct devc_chan *chan;
	double dvalue;
	const char *s;

	devc = sdi ? sdi->priv : NULL;
	if (!cg) {
		switch (key) {
		case SR_CONF_CONN:
			if (!sdi->connection_id)
				return SR_ERR_NA;
			*data = g_variant_new_string(sdi->connection_id);
			return SR_OK;
		case SR_CONF_PHASE:
			if (!devc)
				return SR_ERR_NA;
			ret = jds6600_get_phase_chans(sdi);
			if (ret != SR_OK)
				return SR_ERR_NA;
			dvalue = devc->channels_phase;
			*data = g_variant_new_double(dvalue);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	if (!devc)
		return SR_ERR_NA;
	ret = g_slist_index(sdi->channel_groups, cg);
	if (ret < 0)
		return SR_ERR_NA;
	cg_idx = (size_t)ret;
	if (cg_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_NA;
	chan = &devc->channel_config[cg_idx];

	switch (key) {
	case SR_CONF_ENABLED:
		ret = jds6600_get_chans_enable(sdi);
		if (ret != SR_OK)
			return SR_ERR_NA;
		*data = g_variant_new_boolean(chan->enabled);
		return SR_OK;
	case SR_CONF_PATTERN_MODE:
		ret = jds6600_get_waveform(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		waves = &devc->waveforms;
		s = waves->names[chan->waveform_index];
		*data = g_variant_new_string(s);
		return SR_OK;
	case SR_CONF_OUTPUT_FREQUENCY:
		ret = jds6600_get_frequency(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		dvalue = chan->output_frequency;
		*data = g_variant_new_double(dvalue);
		return SR_OK;
	case SR_CONF_AMPLITUDE:
		ret = jds6600_get_amplitude(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		dvalue = chan->amplitude;
		*data = g_variant_new_double(dvalue);
		return SR_OK;
	case SR_CONF_OFFSET:
		ret = jds6600_get_offset(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		dvalue = chan->offset;
		*data = g_variant_new_double(dvalue);
		return SR_OK;
	case SR_CONF_DUTY_CYCLE:
		ret = jds6600_get_dutycycle(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		dvalue = chan->dutycycle;
		*data = g_variant_new_double(dvalue);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct devc_wave *waves;
	struct devc_chan *chan;
	size_t cg_idx;
	double dvalue;
	gboolean on;
	int ret, idx;

	devc = sdi ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_ENABLED:
			/* Enable/disable all channels at the same time. */
			on = g_variant_get_boolean(data);
			if (!devc)
				return SR_ERR_ARG;
			cg_idx = devc->device.channel_count_gen;
			while (cg_idx) {
				chan = &devc->channel_config[--cg_idx];
				chan->enabled = on;
			}
			ret = jds6600_set_chans_enable(sdi);
			if (ret != SR_OK)
				return SR_ERR_NA;
			return SR_OK;
		case SR_CONF_PHASE:
			if (!devc)
				return SR_ERR_ARG;
			dvalue = g_variant_get_double(data);
			devc->channels_phase = dvalue;
			ret = jds6600_set_phase_chans(sdi);
			if (ret != SR_OK)
				return SR_ERR_NA;
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	ret = g_slist_index(sdi->channel_groups, cg);
	if (ret < 0)
		return SR_ERR_NA;
	cg_idx = (size_t)ret;
	if (cg_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_NA;
	chan = &devc->channel_config[cg_idx];

	switch (key) {
	case SR_CONF_ENABLED:
		on = g_variant_get_boolean(data);
		chan->enabled = on;
		ret = jds6600_set_chans_enable(sdi);
		if (ret != SR_OK)
			return SR_ERR_NA;
		return SR_OK;
	case SR_CONF_PATTERN_MODE:
		waves = &devc->waveforms;
		idx = std_str_idx(data, waves->names, waves->names_count);
		if (idx < 0)
			return SR_ERR_NA;
		if ((size_t)idx >= waves->names_count)
			return SR_ERR_NA;
		chan->waveform_index = idx;
		chan->waveform_code = waves->fw_codes[chan->waveform_index];
		ret = jds6600_set_waveform(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		return SR_OK;
	case SR_CONF_OUTPUT_FREQUENCY:
		dvalue = g_variant_get_double(data);
		chan->output_frequency = dvalue;
		ret = jds6600_set_frequency(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		return SR_OK;
	case SR_CONF_AMPLITUDE:
		dvalue = g_variant_get_double(data);
		chan->amplitude = dvalue;
		ret = jds6600_set_amplitude(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		return SR_OK;
	case SR_CONF_OFFSET:
		dvalue = g_variant_get_double(data);
		chan->offset = dvalue;
		ret = jds6600_set_offset(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		return SR_OK;
	case SR_CONF_DUTY_CYCLE:
		dvalue = g_variant_get_double(data);
		chan->dutycycle = dvalue;
		ret = jds6600_set_dutycycle(sdi, cg_idx);
		if (ret != SR_OK)
			return SR_ERR_NA;
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct devc_wave *waves;
	double fspec[3];

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts);
		default:
			return SR_ERR_NA;
		}
	}

	if (!sdi)
		return SR_ERR_NA;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_NA;
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data =std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
		return SR_OK;
	case SR_CONF_PATTERN_MODE:
		waves = &devc->waveforms;
		*data = std_gvar_array_str(waves->names, waves->names_count);
		return SR_OK;
	case SR_CONF_OUTPUT_FREQUENCY:
		/* Announce range as tuple of min, max, step. */
		fspec[0] = 0.01;
		fspec[1] = devc->device.max_output_frequency;
		fspec[2] = 0.01;
		*data = std_gvar_min_max_step_array(fspec);
		return SR_OK;
	case SR_CONF_DUTY_CYCLE:
		/* Announce range as tuple of min, max, step. */
		fspec[0] = 0.0;
		fspec[1] = 1.0;
		fspec[2] = 0.001;
		*data = std_gvar_min_max_step_array(fspec);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static void clear_helper(struct dev_context *devc)
{
	struct devc_wave *waves;

	if (!devc)
		return;

	g_free(devc->device.serial_number);
	waves = &devc->waveforms;
	while (waves->names_count)
		g_free((char *)waves->names[--waves->names_count]);
	g_free(waves->names);
	g_free(waves->fw_codes);
	if (devc->quick_req)
		g_string_free(devc->quick_req, TRUE);
}

static int dev_clear(const struct sr_dev_driver *driver)
{
	return std_dev_clear_with_callback(driver,
		(std_dev_clear_callback)clear_helper);
}

static struct sr_dev_driver juntek_jds6600_driver_info = {
	.name = "juntek-jds6600",
	.longname = "JUNTEK JDS6600",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = std_dummy_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(juntek_jds6600_driver_info);
