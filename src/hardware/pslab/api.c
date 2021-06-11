/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Karikay Sharma <sharma.kartik2107@gmail.com>
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
#include <math.h>
#include "protocol.h"

static const uint64_t samplerates[] = {
		SR_KHZ(1),
		SR_MHZ(2),
		SR_HZ(1),
};

static const uint32_t scanopts[] = {
		SR_CONF_CONN,
		SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
		SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
		SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
		SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const struct analog_channel analog_channels[] = {
		{"CH1", 0,3,16.5, -16.5},
		{"CH2", 1,0,16.5, -16.5},
		{"CH3", 2,1,-3.3, 3.3},
		{"MIC", 3,2,-3.3,3.3},
};

static const uint64_t vdivs[][2] = {
		/* volts */
		{ 16, 1 },
		{ 8, 1 },
		{ 4, 1 },
		{ 3, 1 },
		{ 2, 1 },
		{ 1500, 1000 }, // 1.5 V
		{ 1, 1 },
		/* millivolts */
		{ 500, 1000 },
};

static struct sr_dev_driver pslab_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *l, *devices;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const char *path = NULL, *serialcomm = "1000000/8n1";
	char *device_path, *version;
	int i;

	GSList *device_paths_v5 = sr_serial_find_usb(0x04D8, 0x00DF);

	GSList *device_paths_v6 = sr_serial_find_usb(0x10C4, 0xEA60);

	GSList *device_paths = g_slist_concat(device_paths_v5, device_paths_v6);

	devices = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			path = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	for (l = device_paths; l; l = l->next) {
		device_path = l->data;
		if (path && path != device_path)
			continue;

		serial = sr_serial_dev_inst_new(device_path, serialcomm);

		if (serial_open(serial, SERIAL_RDWR) != SR_OK)
			continue;

		version = pslab_get_version(serial);
		gboolean isPSLabDevice = g_str_has_prefix(version, "PSLab") ||
			g_str_has_prefix(version, "CSpark");
		if (!isPSLabDevice) {
			g_free(version);
			serial_close(serial);
			continue;
		}
		sr_info("PSLab device found: %s on port: %s", version, device_path);

		sdi = g_new0(struct sr_dev_inst, 1);
		devc = g_new0(struct dev_context, 1);
		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_SERIAL;
		sdi->vendor = g_strdup("FOSSASIA");
		sdi->connection_id = device_path;
		sdi->conn = serial;
		sdi->version = version;

		for (i = 0; i < NUM_ANALOG_CHANNELS; i++) {
			struct sr_channel *ch = sr_channel_new(sdi, analog_channels[i].index,
				SR_CHANNEL_ANALOG, TRUE, analog_channels[i].name);
			struct channel_priv *cp = g_new0(struct channel_priv, 1);
			struct sr_channel_group *cg = g_new0(struct sr_channel_group, 1);
			struct channel_group_priv *cgp =  g_new0(struct channel_group_priv, 1);
			cp->chosa = analog_channels[i].chosa;
			cp->min_input = analog_channels[i].minInput;
			cp->max_input = analog_channels[i].maxInput;
			cp->gain = 1;
			cp->resolution = pow(2, 10) - 1;
			if (!g_strcmp0(analog_channels[i].name, "CH1")) {
				cp->programmable_gain_amplifier = 1;
				cgp->range = 0;
				devc->channel_one_map = ch;
			} else if (!g_strcmp0(analog_channels[i].name, "CH2")) {
				cgp->range = 0;
				cp->programmable_gain_amplifier = 2;
			}
			ch->priv = cp;
			cg->name = g_strdup(analog_channels[i].name);
			cg->channels = g_slist_append(cg->channels, ch);
			cg->priv = cgp;
			sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
		}
		sr_sw_limits_init(&devc->limits);
		devc->mode = SR_CONF_OSCILLOSCOPE;
		devc->samplerate = 2000;
		devc->trigger_enabled = FALSE;
		devc->trigger_voltage = 0;
		devc->trigger_channel = devc->channel_one_map;
		sdi->priv = devc;

		devices = g_slist_append(devices, sdi);
		serial_close(serial);
	}
	return std_scan_complete(di, devices);
}

static void select_range(const struct sr_channel_group *cg, uint8_t idx)
{
	uint16_t gain = GAIN_VALUES[idx];
	((struct channel_priv *)(((struct sr_channel *)(cg->channels->data))->priv))->gain = gain;
	sr_info("Set gain %d on channel %s with range %lu V",
		gain, cg->name, vdivs[idx][0]/vdivs[idx][1]);
}

static int config_get(uint32_t key, GVariant **data,
					  const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int idx;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if(!cg) {
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
			return sr_sw_limits_config_get(&devc->limits, key, data);
		case SR_CONF_SAMPLERATE:
			*data = g_variant_new_uint64(devc->samplerate);
			break;
		case SR_CONF_TRIGGER_SOURCE:
			*data = g_variant_new_string(devc->trigger_channel->name);
			break;
		case SR_CONF_TRIGGER_LEVEL:
			*data = g_variant_new_double(devc->trigger_voltage);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_VDIV:
			if ( g_strcmp0(cg->name, "CH1") && g_strcmp0(cg->name, "CH2"))
				return SR_ERR_ARG;
			idx = ((struct channel_group_priv *)(cg->priv))->range;
			*data = g_variant_new("(tt)", vdivs[idx][0], vdivs[idx][1]);
			break;
		default:
			return SR_ERR_NA;
		}
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const char *name;
	int idx;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
			return sr_sw_limits_config_set(&devc->limits, key, data);
		case SR_CONF_SAMPLERATE:
			devc->samplerate = g_variant_get_uint64(data);
			break;
		case SR_CONF_TRIGGER_SOURCE:
			devc->trigger_enabled = TRUE;
			name = g_variant_get_string(data,0);
			if (assign_channel(name, devc->trigger_channel, sdi->channels) != SR_OK)
				return SR_ERR_ARG;
			break;
		case SR_CONF_TRIGGER_LEVEL:
			devc->trigger_enabled = TRUE;
			devc->trigger_voltage = g_variant_get_double(data);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_VDIV:
			if (g_strcmp0(cg->name, "CH1") && g_strcmp0(cg->name, "CH2"))
				return SR_ERR_ARG;

			if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(vdivs))) < 0)
				return SR_ERR_ARG;

			((struct channel_group_priv *)(cg->priv))->range = idx;
			select_range(cg, (uint8_t)idx);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	GSList *l;
	GVariant **tmp;
	int i;

	devc = (sdi) ? sdi->priv : NULL;

	if(!cg) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
		case SR_CONF_SCAN_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
			break;
		case SR_CONF_TRIGGER_SOURCE:
			if (!sdi)
				return SR_ERR_ARG;

			tmp = g_malloc(NUM_ANALOG_CHANNELS * sizeof(GVariant *));
			for (l = sdi->channels, i=0; l; l = l->next, i++) {
				ch = l->data;
				tmp[i] = g_variant_new_string(ch->name);
			}
			*data = g_variant_new_array(G_VARIANT_TYPE_STRING, tmp, i);
			break;
		case SR_CONF_LIMIT_SAMPLES:
			*data = std_gvar_tuple_u64(MIN_SAMPLES, MAX_SAMPLES);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_VDIV:
			if (g_strcmp0(cg->name, "CH1") && g_strcmp0(cg->name, "CH2"))
				return SR_ERR_ARG;

			if (!devc)
				return SR_ERR_ARG;

			*data = std_gvar_tuple_array(ARRAY_AND_SIZE(vdivs));
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const GSList *l;
	struct sr_channel *ch;

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if(ch->enabled) {
			devc->enabled_channels = g_slist_append(devc->enabled_channels, ch);
			sr_info("enabled channels: {} %s", ch->name);
		}
	}
	return SR_OK;
}

static uint64_t lookup_maximum_samplerate(guint channels, gboolean trigger)
{
	static const uint64_t channels_idx[][2] = {
			{1, 0},
			{2, 1},
			{3, 2},
			{4, 2},
	};
	static const uint64_t min_samplerates[][2] = {
			{2000000, 1333333},
			{1142857, 1142857},
			{571428, 571428},
	};
	return min_samplerates[channels_idx[channels-1][1]][trigger];
}

static int check_args(guint channels,uint64_t samples ,uint64_t samplerate,
		      gboolean trigger)
{
	if (channels > 4) {
		sr_err("Number of channels to sample must be 1, 2, 3, or 4");
		return SR_ERR_ARG;
	}

	if (samples > (MAX_SAMPLES/channels)) {
		sr_err("Invalid number of samples");
		return SR_ERR_ARG;
	}

	if (samplerate > lookup_maximum_samplerate(channels, trigger)) {
		sr_err("Samplerate must be less than %lu",
		       lookup_maximum_samplerate(channels, trigger));
		return SR_ERR_SAMPLERATE;
	}

	return SR_OK;
}

static void configure_oscilloscope(const struct sr_dev_inst *sdi)
{
	GSList *l;
	struct dev_context *devc = sdi->priv;
	struct sr_channel *ch;

	for (l = devc->enabled_channels; l; l = l->next) {
		ch = l->data;
		pslab_set_gain(sdi, ch, ((struct channel_priv *) (ch->priv))->gain);
		if (g_slist_length(devc->enabled_channels) == 1)
			devc->channel_one_map = ch;
	}

	if (!devc->trigger_channel)
		devc->trigger_channel = devc->channel_one_map;
	if (devc->trigger_enabled)
		pslab_configure_trigger(sdi);

}


static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	configure_channels(sdi);

	serial = sdi->conn;
	devc = sdi->priv;

	if (!devc->enabled_channels)
		return SR_ERR;

	switch(devc->mode) {
	case SR_CONF_OSCILLOSCOPE:
		ret = check_args(g_slist_length(devc->enabled_channels),
				 devc->limits.limit_samples, devc->samplerate, devc->trigger_enabled);
		if(ret !=SR_OK)
			return ret;

		configure_oscilloscope(sdi);
		pslab_caputure_oscilloscope(sdi);
		break;
	default:
		break;
	}

	devc->channel_entry = devc->enabled_channels;
	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 10,
					  pslab_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver pslab_driver_info = {
	.name = "pslab",
	.longname = "PSLab",
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
SR_REGISTER_DEV_DRIVER(pslab_driver_info);
