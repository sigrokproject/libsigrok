/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 mhooijboer <marchelh@gmail.com>
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
#include <fcntl.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"
#include "scpi.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_HDIV | SR_CONF_GET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AVERAGING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AVG_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t timebases[][2] = {
	/* nanoseconds */
	{ 1, 1000000000 },
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
	{ 100, 1 },
};

static const uint64_t vdivs[][2] = {
	/* microvolts */
	{ 500, 100000 },
	/* millivolts */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
	{ 100, 1 },
};

static const char *trigger_sources[] = {
	"CH1", "CH2", "Ext", "Ext /5", "AC Line",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static const char *trigger_slopes[] = {
	"r", "f",
};

static const char *coupling[] = {
	"A1M AC 1 Meg",
	"A50 AC 50 Ohm",
	"D1M DC 1 Meg",
	"D50 DC 50 Ohm",
	"GND",
};

static const uint64_t probe_factor[] = {
	1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000,
};

/* Do not change the order of entries. */
static const char *data_sources[] = {
	"Display",
	"History",
};

enum vendor {
	SIGLENT,
};

enum series {
	SDS1000CML,
	SDS1000CNL,
	SDS1000DL,
	SDS1000X,
	SDS1000XP,
	SDS1000XE,
	SDS2000X,
};

/* short name, full name */
static const struct siglent_sds_vendor supported_vendors[] = {
	[SIGLENT] = {"Siglent", "Siglent Technologies"},
};

#define VENDOR(x) &supported_vendors[x]
/* vendor, series, protocol, max timebase, min vdiv, number of horizontal divs,
 * number of vertical divs, live waveform samples, memory buffer samples */
static const struct siglent_sds_series supported_series[] = {
	[SDS1000CML] = {VENDOR(SIGLENT), "SDS1000CML", NON_SPO_MODEL,
		{ 50, 1 }, { 2, 1000 }, 18, 8, 1400363},
	[SDS1000CNL] = {VENDOR(SIGLENT), "SDS1000CNL", NON_SPO_MODEL,
		{ 50, 1 }, { 2, 1000 }, 18, 8, 1400363},
	[SDS1000DL] = {VENDOR(SIGLENT), "SDS1000DL", NON_SPO_MODEL,
		{ 50, 1 }, { 2, 1000 }, 18, 8, 1400363},
	[SDS1000X] = {VENDOR(SIGLENT), "SDS1000X", SPO_MODEL,
		{ 50, 1 }, { 500, 100000 }, 14, 8, 14000363},
	[SDS1000XP] = {VENDOR(SIGLENT), "SDS1000X+", SPO_MODEL,
		{ 50, 1 }, { 500, 100000 }, 14, 8, 14000363},
	[SDS1000XE] = {VENDOR(SIGLENT), "SDS1000XE", SPO_MODEL,
		{ 50, 1 }, { 500, 100000 }, 14, 8, 14000363},
	[SDS2000X] = {VENDOR(SIGLENT), "SDS2000X", SPO_MODEL,
		{ 50, 1 }, { 500, 100000 }, 14, 8, 14000363},
};

#define SERIES(x) &supported_series[x]
/* series, model, min timebase, analog channels, digital */
static const struct siglent_sds_model supported_models[] = {
	{ SERIES(SDS1000CML), "SDS1152CML", { 20, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000CML), "SDS1102CML", { 10, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000CML), "SDS1072CML", { 5, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000CNL), "SDS1202CNL", { 20, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000CNL), "SDS1102CNL", { 10, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000CNL), "SDS1072CNL", { 5, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000DL), "SDS1202DL", { 20, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000DL), "SDS1102DL", { 10, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000DL), "SDS1022DL", { 5, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000DL), "SDS1052DL", { 5, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000DL), "SDS1052DL+", { 5, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000X), "SDS1102X", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000XP), "SDS1102X+", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000X), "SDS1202X", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000XP), "SDS1202X+", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000XE), "SDS1202X-E", { 1, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS1000XE), "SDS1104X-E", { 1, 1000000000 }, 4, true, 16 },
	{ SERIES(SDS1000XE), "SDS1204X-E", { 1, 1000000000 }, 4, true, 16 },
	{ SERIES(SDS2000X), "SDS2072X", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS2000X), "SDS2074X", { 2, 1000000000 }, 4, false, 0 },
	{ SERIES(SDS2000X), "SDS2102X", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS2000X), "SDS2104X", { 2, 1000000000 }, 4, false, 0 },
	{ SERIES(SDS2000X), "SDS2202X", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS2000X), "SDS2204X", { 2, 1000000000 }, 4, false, 0 },
	{ SERIES(SDS2000X), "SDS2302X", { 2, 1000000000 }, 2, false, 0 },
	{ SERIES(SDS2000X), "SDS2304X", { 2, 1000000000 }, 4, false, 0 },
};

SR_PRIV struct sr_dev_driver siglent_sds_driver_info;

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;
	if (!devc)
		return;
	g_free(devc->analog_groups);
	g_free(devc->enabled_channels);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, clear_helper);
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	struct sr_channel *ch;
	unsigned int i;
	const struct siglent_sds_model *model;
	gchar *channel_name;

	sr_dbg("Setting Communication Headers to off.");
	if (sr_scpi_send(scpi, "CHDR OFF") != SR_OK)
		return NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response, retrying.");
		sr_scpi_close(scpi);
		sr_scpi_open(scpi);
		if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
			sr_info("Couldn't get IDN response.");
			return NULL;
		}
	}

	model = NULL;
	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (!strcmp(hw_info->model, supported_models[i].name)) {
			model = &supported_models[i];
			break;
		}
	}

	if (!model) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(model->series->vendor->name);
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = &siglent_sds_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->serial_num = g_strdup(hw_info->serial_number);
	devc = g_malloc0(sizeof(struct dev_context));
	devc->limit_frames = 1;
	devc->model = model;

	sr_scpi_hw_info_free(hw_info);

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group *) *
		model->analog_channels);

	for (i = 0; i < model->analog_channels; i++) {
		channel_name = g_strdup_printf("CH%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel_name);

		devc->analog_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->analog_groups[i]->name = channel_name;
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
			devc->analog_groups[i]);
	}

	if (devc->model->has_digital) {
		devc->digital_group = g_malloc0(sizeof(struct sr_channel_group));

		for (i = 0; i < ARRAY_SIZE(devc->digital_channels); i++) {
			channel_name = g_strdup_printf("D%d", i);
			ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name);
			g_free(channel_name);
			devc->digital_group->channels = g_slist_append(
				devc->digital_group->channels, ch);
		}
		devc->digital_group->name = g_strdup("LA");
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
			devc->digital_group);
	}

	for (i = 0; i < ARRAY_SIZE(timebases); i++) {
		if (!memcmp(&devc->model->min_timebase, &timebases[i], sizeof(uint64_t[2])))
			devc->timebases = &timebases[i];

		if (!memcmp(&devc->model->series->max_timebase, &timebases[i], sizeof(uint64_t[2])))
			devc->num_timebases = &timebases[i] - devc->timebases + 1;
	}

	for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
		devc->vdivs = &vdivs[i];
		if (!memcmp(&devc->model->series->min_vdiv,
			&vdivs[i], sizeof(uint64_t[2]))) {
			devc->vdivs = &vdivs[i];
			devc->num_vdivs = ARRAY_SIZE(vdivs) - i;
			break;
		}
	}

	devc->buffer = g_malloc(devc->model->series->buffer_samples);
	sr_dbg("Setting device context buffer size: %i.", devc->model->series->buffer_samples);
	devc->data = g_malloc(devc->model->series->buffer_samples * sizeof(float));

	devc->data_source = DATA_SOURCE_SCREEN;

	sdi->priv = devc;

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	/* TODO: Implement RPC call for LXI device discovery. */
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if ((ret = sr_scpi_open(scpi)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	if ((ret = siglent_sds_get_dev_cfg(sdi)) < 0) {
		sr_err("Failed to get device config: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const char *tmp_str;
	int analog_channel = -1;
	float smallest_diff = INFINITY;
	int idx = -1;
	unsigned i;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	/* If a channel group is specified, it must be a valid one. */
	if (cg && !g_slist_find(sdi->channel_groups, cg)) {
		sr_err("Invalid channel group specified.");
		return SR_ERR;
	}

	if (cg) {
		ch = g_slist_nth_data(cg->channels, 0);
		if (!ch)
			return SR_ERR;
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (ch->name[2] < '1' || ch->name[2] > '4')
				return SR_ERR;
			analog_channel = ch->name[2] - '1';
		}
	}

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(devc->model->series->num_horizontal_divs);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(devc->num_vdivs);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limit_frames);
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_SCREEN)
			*data = g_variant_new_string("Screen");
		else if (devc->data_source == DATA_SOURCE_HISTORY)
			*data = g_variant_new_string("History");
		break;
	case SR_CONF_SAMPLERATE:
		siglent_sds_get_dev_cfg_horizontal(sdi);
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!strcmp(devc->trigger_source, "ACL"))
			tmp_str = "AC Line";
		else if (!strcmp(devc->trigger_source, "CHAN1"))
			tmp_str = "CH1";
		else if (!strcmp(devc->trigger_source, "CHAN2"))
			tmp_str = "CH2";
		else
			tmp_str = devc->trigger_source;
		*data = g_variant_new_string(tmp_str);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!strncmp(devc->trigger_slope, "POS", 3)) {
			tmp_str = "r";
		} else if (!strncmp(devc->trigger_slope, "NEG", 3)) {
			tmp_str = "f";
		} else {
			sr_dbg("Unknown trigger slope: '%s'.", devc->trigger_slope);
			return SR_ERR_NA;
		}
		*data = g_variant_new_string(tmp_str);
		break;
	case SR_CONF_TRIGGER_LEVEL:
		*data = g_variant_new_double(devc->trigger_level);
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		*data = g_variant_new_double(devc->horiz_triggerpos);
		break;
	case SR_CONF_TIMEBASE:
		for (i = 0; i < devc->num_timebases; i++) {
			float tb, diff;

			tb = (float)devc->timebases[i][0] / devc->timebases[i][1];
			diff = fabs(devc->timebase - tb);
			if (diff < smallest_diff) {
				smallest_diff = diff;
				idx = i;
			}
		}
		if (idx < 0) {
			sr_dbg("Negative timebase index: %d.", idx);
			return SR_ERR_NA;
		}
		*data = g_variant_new("(tt)", devc->timebases[idx][0],
			devc->timebases[idx][1]);
		break;
	case SR_CONF_VDIV:
		if (analog_channel < 0) {
			sr_dbg("Negative analog channel: %d.", analog_channel);
			return SR_ERR_NA;
		}
		for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
			float vdiv = (float)vdivs[i][0] / vdivs[i][1];
			float diff = fabsf(devc->vdiv[analog_channel] - vdiv);
			if (diff < smallest_diff) {
				smallest_diff = diff;
				idx = i;
			}
		}
		if (idx < 0) {
			sr_dbg("Negative vdiv index: %d.", idx);
			return SR_ERR_NA;
		}
		*data = g_variant_new("(tt)", vdivs[idx][0], vdivs[idx][1]);
		break;
	case SR_CONF_COUPLING:
		if (analog_channel < 0) {
			sr_dbg("Negative analog channel: %d.", analog_channel);
			return SR_ERR_NA;
		}
		*data = g_variant_new_string(devc->coupling[analog_channel]);
		break;
	case SR_CONF_PROBE_FACTOR:
		if (analog_channel < 0) {
			sr_dbg("Negative analog channel: %d.", analog_channel);
			return SR_ERR_NA;
		}
		*data = g_variant_new_uint64(devc->attenuation[analog_channel]);
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
	uint64_t p;
	double t_dbl;
	int i;
	int ret, idx;
	const char *tmp_str;
	char buffer[16];
	char *cmd = NULL;
	char cmd4[4];

	devc = sdi->priv;

	/* If a channel group is specified, it must be a valid one. */
	if (cg && !g_slist_find(sdi->channel_groups, cg)) {
		sr_err("Invalid channel group specified.");
		return SR_ERR;
	}

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = g_variant_get_uint64(data);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes))) < 0)
			return SR_ERR_ARG;
		g_free(devc->trigger_slope);
		devc->trigger_slope = g_strdup((trigger_slopes[idx][0] == 'r') ? "POS" : "NEG");
		return siglent_sds_config_set(sdi, "%s:TRSL %s",
			devc->trigger_source, devc->trigger_slope);
	case SR_CONF_HORIZ_TRIGGERPOS:
		t_dbl = g_variant_get_double(data);
		if (t_dbl < 0.0 || t_dbl > 1.0) {
			sr_err("Invalid horiz. trigger position: %g.", t_dbl);
			return SR_ERR;
		}
		devc->horiz_triggerpos = t_dbl;
		/* We have the trigger offset as a percentage of the frame, but
		 * need to express this in seconds. */
		t_dbl = -(devc->horiz_triggerpos - 0.5) * devc->timebase * devc->num_timebases;
		g_ascii_formatd(buffer, sizeof(buffer), "%.6f", t_dbl);
		return siglent_sds_config_set(sdi, ":TIM:OFFS %s", buffer);
	case SR_CONF_TRIGGER_LEVEL:
		t_dbl = g_variant_get_double(data);
		g_ascii_formatd(buffer, sizeof(buffer), "%.3f", t_dbl);
		ret = siglent_sds_config_set(sdi, ":TRIG:EDGE:LEV %s", buffer);
		if (ret == SR_OK)
			devc->trigger_level = t_dbl;
		break;
	case SR_CONF_TIMEBASE:
		if ((idx = std_u64_tuple_idx(data, devc->timebases, devc->num_timebases)) < 0)
			return SR_ERR_ARG;
		devc->timebase = (float)devc->timebases[idx][0] / devc->timebases[idx][1];
		p = devc->timebases[idx][0];
		switch (devc->timebases[idx][1]) {
		case 1:
			cmd = g_strdup_printf("%" PRIu64 "S", p);
			break;
		case 1000:
			cmd = g_strdup_printf("%" PRIu64 "MS", p);
			break;
		case 1000000:
			cmd = g_strdup_printf("%" PRIu64 "US", p);
			break;
		case 1000000000:
			cmd = g_strdup_printf("%" PRIu64 "NS", p);
			break;
		}
		ret = siglent_sds_config_set(sdi, "TDIV %s", cmd);
		g_free(cmd);
		return ret;
	case SR_CONF_TRIGGER_SOURCE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_sources))) < 0)
			return SR_ERR_ARG;
		g_free(devc->trigger_source);
		devc->trigger_source = g_strdup(trigger_sources[idx]);
		if (!strcmp(devc->trigger_source, "AC Line"))
			tmp_str = "LINE";
		else if (!strcmp(devc->trigger_source, "CH1"))
			tmp_str = "C1";
		else if (!strcmp(devc->trigger_source, "CH2"))
			tmp_str = "C2";
		else if (!strcmp(devc->trigger_source, "CH3"))
			tmp_str = "C3";
		else if (!strcmp(devc->trigger_source, "CH4"))
			tmp_str = "C4";
		else if (!strcmp(devc->trigger_source, "Ext"))
			tmp_str = "EX";
		else if (!strcmp(devc->trigger_source, "Ext /5"))
			tmp_str = "EX5";
		else
			tmp_str = (char *)devc->trigger_source;
		return siglent_sds_config_set(sdi, "TRSE EDGE,SR,%s,OFF", tmp_str);
	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(vdivs))) < 0)
			return SR_ERR_ARG;
		devc->vdiv[i] = (float)vdivs[idx][0] / vdivs[idx][1];
		p = vdivs[idx][0];
		switch (vdivs[idx][1]) {
		case 1:
			cmd = g_strdup_printf("%" PRIu64 "V", p);
			break;
		case 1000:
			cmd = g_strdup_printf("%" PRIu64 "MV", p);
			break;
		case 100000:
			cmd = g_strdup_printf("%" PRIu64 "UV", p);
			break;
		}
		ret = siglent_sds_config_set(sdi, "C%d:VDIV %s", i + 1, cmd);
		g_free(cmd);
		return ret;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(coupling))) < 0)
			return SR_ERR_ARG;
		g_free(devc->coupling[i]);
		devc->coupling[i] = g_strdup(coupling[idx]);
		strncpy(cmd4, devc->coupling[i], 3);
		cmd4[3] = 0;
		return siglent_sds_config_set(sdi, "C%d:CPL %s", i + 1, cmd4);
	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_u64_idx(data, ARRAY_AND_SIZE(probe_factor))) < 0)
			return SR_ERR_ARG;
		p = g_variant_get_uint64(data);
		devc->attenuation[i] = probe_factor[idx];
		ret = siglent_sds_config_set(sdi, "C%d:ATTN %" PRIu64, i + 1, p);
		if (ret == SR_OK)
			siglent_sds_get_dev_cfg_vertical(sdi);
		return ret;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "Display"))
			devc->data_source = DATA_SOURCE_SCREEN;
		else if (devc->model->series->protocol >= SPO_MODEL
			&& !strcmp(tmp_str, "History"))
			devc->data_source = DATA_SOURCE_HISTORY;
		else {
			sr_err("Unknown data source: '%s'.", tmp_str);
			return SR_ERR;
		}
		break;
	case SR_CONF_SAMPLERATE:
		siglent_sds_get_dev_cfg_horizontal(sdi);
		data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_AVERAGING:
		devc->average_enabled = g_variant_get_boolean(data);
		sr_dbg("%s averaging", devc->average_enabled ? "Enabling" : "Disabling");
		break;
	case SR_CONF_AVG_SAMPLES:
		devc->average_samples = g_variant_get_uint64(data);
		sr_dbg("Setting averaging rate to %" PRIu64, devc->average_samples);
		break;	
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg)
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		if (!devc)
			return SR_ERR_ARG;
		if (cg == devc->digital_group) {
			*data = std_gvar_array_u32(NULL, 0);
			return SR_OK;
		} else {
			if (std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels) < 0)
				return SR_ERR_ARG;
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog));
			return SR_OK;
		}
		break;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = g_variant_new_strv(ARRAY_AND_SIZE(coupling));
		break;
	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = std_gvar_array_u64(ARRAY_AND_SIZE(probe_factor));
		break;
	case SR_CONF_VDIV:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = std_gvar_tuple_array(devc->vdivs, devc->num_vdivs);
		break;
	case SR_CONF_TIMEBASE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (devc->num_timebases <= 0)
			return SR_ERR_NA;
		*data = std_gvar_tuple_array(devc->timebases, devc->num_timebases);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		*data = g_variant_new_strv(trigger_sources,
			devc->model->has_digital ? ARRAY_SIZE(trigger_sources) : 5);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_slopes));
		break;
	case SR_CONF_DATA_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		switch (devc->model->series->protocol) {
		/* TODO: Check what must be done here for the data source buffer sizes. */
		case NON_SPO_MODEL:
			*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources) - 1);
			break;
		case SPO_MODEL:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
			break;
		}
		break;
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(devc->model->series->num_horizontal_divs);
		break;
	case SR_CONF_AVERAGING:
		*data = g_variant_new_boolean(devc->average_enabled);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_datafeed_packet packet;
	gboolean some_digital;
	GSList *l, *d;

	scpi = sdi->conn;
	devc = sdi->priv;

	devc->num_frames = 0;
	some_digital = FALSE;

	/*
	 * Check if there are any logic channels enabled, if so then enable
	 * the MSO, otherwise skip the digital channel setup. Enable and
	 * disable channels on the device is very slow and it is faster when
	 * checked in a small loop without the actual actions.
	 */
	for (d = sdi->channels; d; d = d->next) {
		ch = d->data;
		if (ch->type == SR_CHANNEL_LOGIC && ch->enabled)
			some_digital = TRUE;
	}

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (ch->enabled)
				devc->enabled_channels = g_slist_append(
					devc->enabled_channels, ch);
			if (ch->enabled != devc->analog_channels[ch->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (siglent_sds_config_set(sdi, "C%d:TRA %s", ch->index + 1,
					ch->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->analog_channels[ch->index] = ch->enabled;
			}
		} else if (ch->type == SR_CHANNEL_LOGIC && some_digital) {
			if (ch->enabled) {
				/* Turn on LA module if currently off and digital channels are enabled. */
				if (!devc->la_enabled) {
					if (siglent_sds_config_set(sdi, "DGST ON") != SR_OK)
						return SR_ERR;
					g_usleep(630000);
					devc->la_enabled = TRUE;
				}
				devc->enabled_channels = g_slist_append(
					devc->enabled_channels, ch);
			}
			/* Enabled channel is currently disabled, or vice versa. */
			if (siglent_sds_config_set(sdi, "D%d:DGCH %s", ch->index,
				ch->enabled ? "ON" : "OFF") != SR_OK)
				return SR_ERR;
			/* Slowing the command sequence down to let the device handle it. */
			g_usleep(630000);
			devc->digital_channels[ch->index] = ch->enabled;
		}
	}
	if (!devc->enabled_channels)
		return SR_ERR;
	/* Turn off LA module if on and no digital channels selected. */
	if (devc->la_enabled && !some_digital)
		if (siglent_sds_config_set(sdi, "DGST OFF") != SR_OK) {
			devc->la_enabled = FALSE;
			g_usleep(500000);
			return SR_ERR;
		}

	// devc->analog_frame_size = devc->model->series->buffer_samples;
	// devc->digital_frame_size = devc->model->series->buffer_samples;

	switch (devc->model->series->protocol) {
	case SPO_MODEL:
		if (siglent_sds_config_set(sdi, "WFSU SP,0,TYPE,1") != SR_OK)
			return SR_ERR;
		if (devc->average_enabled) {
			if (siglent_sds_config_set(sdi, "ACQW AVERAGE,%i", devc->average_samples) != SR_OK)
				return SR_ERR;
		} else {
			if (siglent_sds_config_set(sdi, "ACQW SAMPLING") != SR_OK)
				return SR_ERR;
		}
		break;
	case NON_SPO_MODEL:
		/* TODO: Implement CML/CNL/DL models. */
		if (siglent_sds_config_set(sdi, "WFSU SP,0,TYPE,1") != SR_OK)
			return SR_ERR;
		if (siglent_sds_config_set(sdi, "ACQW SAMPLING") != SR_OK)
			return SR_ERR;
		break;
	default:
		break;
	}

	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
		siglent_sds_receive, (void *) sdi);

	std_session_send_df_header(sdi);

	devc->channel_entry = devc->enabled_channels;

	if (siglent_sds_capture_start(sdi) != SR_OK)
		return SR_ERR;

	/* Start of first frame. */
	packet.type = SR_DF_FRAME_BEGIN;
	sr_session_send(sdi, &packet);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	devc = sdi->priv;

	std_session_send_df_end(sdi);

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;
	scpi = sdi->conn;
	sr_scpi_source_remove(sdi->session, scpi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver siglent_sds_driver_info = {
	.name = "siglent-sds",
	.longname = "Siglent SDS1000/SDS2000",
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

SR_REGISTER_DEV_DRIVER(siglent_sds_driver_info);
