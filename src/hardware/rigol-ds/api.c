/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 Mathias Grimmberger <mgri@zaphod.sax.de>
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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM
};

static const uint32_t devopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_SET,
	SR_CONF_NUM_TIMEBASE | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
};

static const uint32_t analog_devopts[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
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
	{ 200, 1 },
	{ 500, 1 },
	{ 1000, 1 },
};

static const uint64_t vdivs[][2] = {
	/* microvolts */
	{ 500, 1000000 },
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

#define NUM_TIMEBASE  ARRAY_SIZE(timebases)
#define NUM_VDIV      ARRAY_SIZE(vdivs)

static const char *trigger_sources[] = {
	"CH1",
	"CH2",
	"CH3",
	"CH4",
	"EXT",
	"AC Line",
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	"D8",
	"D9",
	"D10",
	"D11",
	"D12",
	"D13",
	"D14",
	"D15",
};

static const char *trigger_slopes[] = {
	"r",
	"f",
};

static const char *coupling[] = {
	"AC",
	"DC",
	"GND",
};

/* Do not change the order of entries */
static const char *data_sources[] = {
	"Live",
	"Memory",
	"Segmented",
};

enum vendor {
	RIGOL,
	AGILENT,
};

enum series {
	VS5000,
	DS1000,
	DS2000,
	DS2000A,
	DSO1000,
};

/* short name, full name */
static const struct rigol_ds_vendor supported_vendors[] = {
	[RIGOL] = {"Rigol", "Rigol Technologies"},
	[AGILENT] = {"Agilent", "Agilent Technologies"},
};

#define VENDOR(x) &supported_vendors[x]
/* vendor, series, protocol, max timebase, min vdiv, number of horizontal divs,
 * live waveform samples, memory buffer samples */
static const struct rigol_ds_series supported_series[] = {
	[VS5000] = {VENDOR(RIGOL), "VS5000", PROTOCOL_V1, FORMAT_RAW,
		{50, 1}, {2, 1000}, 14, 2048, 0},
	[DS1000] = {VENDOR(RIGOL), "DS1000", PROTOCOL_V2, FORMAT_IEEE488_2,
		{50, 1}, {2, 1000}, 12, 600, 1048576},
	[DS2000] = {VENDOR(RIGOL), "DS2000", PROTOCOL_V3, FORMAT_IEEE488_2,
		{500, 1}, {2, 1000}, 14, 1400, 14000},
	[DS2000A] = {VENDOR(RIGOL), "DS2000A", PROTOCOL_V3, FORMAT_IEEE488_2,
		{1000, 1}, {500, 1000000}, 14, 1400, 14000},
	[DSO1000] = {VENDOR(AGILENT), "DSO1000", PROTOCOL_V3, FORMAT_IEEE488_2,
		{50, 1}, {2, 1000}, 12, 600, 20480},
};

#define SERIES(x) &supported_series[x]
/* series, model, min timebase, analog channels, digital */
static const struct rigol_ds_model supported_models[] = {
	{SERIES(VS5000), "VS5022", {20, 1000000000}, 2, false},
	{SERIES(VS5000), "VS5042", {10, 1000000000}, 2, false},
	{SERIES(VS5000), "VS5062", {5, 1000000000}, 2, false},
	{SERIES(VS5000), "VS5102", {2, 1000000000}, 2, false},
	{SERIES(VS5000), "VS5202", {2, 1000000000}, 2, false},
	{SERIES(VS5000), "VS5022D", {20, 1000000000}, 2, true},
	{SERIES(VS5000), "VS5042D", {10, 1000000000}, 2, true},
	{SERIES(VS5000), "VS5062D", {5, 1000000000}, 2, true},
	{SERIES(VS5000), "VS5102D", {2, 1000000000}, 2, true},
	{SERIES(VS5000), "VS5202D", {2, 1000000000}, 2, true},
	{SERIES(DS1000), "DS1052E", {5, 1000000000}, 2, false},
	{SERIES(DS1000), "DS1102E", {2, 1000000000}, 2, false},
	{SERIES(DS1000), "DS1152E", {2, 1000000000}, 2, false},
	{SERIES(DS1000), "DS1052D", {5, 1000000000}, 2, true},
	{SERIES(DS1000), "DS1102D", {2, 1000000000}, 2, true},
	{SERIES(DS1000), "DS1152D", {2, 1000000000}, 2, true},
	{SERIES(DS2000), "DS2072", {5, 1000000000}, 2, false},
	{SERIES(DS2000), "DS2102", {5, 1000000000}, 2, false},
	{SERIES(DS2000), "DS2202", {2, 1000000000}, 2, false},
	{SERIES(DS2000), "DS2302", {1, 1000000000}, 2, false},
	{SERIES(DS2000A), "DS2072A", {5, 1000000000}, 2, false},
	{SERIES(DS2000A), "DS2102A", {5, 1000000000}, 2, false},
	{SERIES(DS2000A), "DS2202A", {2, 1000000000}, 2, false},
	{SERIES(DS2000A), "DS2302A", {1, 1000000000}, 2, false},
	{SERIES(DSO1000), "DSO1002A", {5, 1000000000}, 2, false},
	{SERIES(DSO1000), "DSO1004A", {5, 1000000000}, 4, false},
	{SERIES(DSO1000), "DSO1012A", {2, 1000000000}, 2, false},
	{SERIES(DSO1000), "DSO1014A", {2, 1000000000}, 4, false},
	{SERIES(DSO1000), "DSO1022A", {2, 1000000000}, 2, false},
	{SERIES(DSO1000), "DSO1024A", {2, 1000000000}, 4, false},
};

SR_PRIV struct sr_dev_driver rigol_ds_driver_info;
static struct sr_dev_driver *di = &rigol_ds_driver_info;

static void clear_helper(void *priv)
{
	struct dev_context *devc;
	unsigned int i;

	devc = priv;
	g_free(devc->data);
	g_free(devc->buffer);
	for (i = 0; i < ARRAY_SIZE(devc->coupling); i++)
		g_free(devc->coupling[i]);
	g_free(devc->trigger_source);
	g_free(devc->trigger_slope);
	g_free(devc->analog_groups);
	g_free(devc->digital_group);
	g_free(devc);
}

static int dev_clear(void)
{
	return std_dev_clear(di, clear_helper);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	struct sr_channel *ch;
	long n[3];
	unsigned int i;
	const struct rigol_ds_model *model = NULL;
	gchar *channel_name, **version;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response, retrying.");
		sr_scpi_close(scpi);
		sr_scpi_open(scpi);
		if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
			sr_info("Couldn't get IDN response.");
			return NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (!strcasecmp(hw_info->manufacturer,
					supported_models[i].series->vendor->full_name) &&
				!strcmp(hw_info->model, supported_models[i].name)) {
			model = &supported_models[i];
			break;
		}
	}

	if (!model) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_ACTIVE;
	sdi->vendor = g_strdup(model->series->vendor->name);
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = di;
	sdi->inst_type = SR_INST_SCPI;
	sdi->serial_num = g_strdup(hw_info->serial_number);
	devc = g_malloc0(sizeof(struct dev_context));
	devc->limit_frames = 0;
	devc->model = model;
	devc->format = model->series->format;

	/* DS1000 models with firmware before 0.2.4 used the old data format. */
	if (model->series == SERIES(DS1000)) {
		version = g_strsplit(hw_info->firmware_version, ".", 0);
		do {
			if (!version[0] || !version[1] || !version[2])
				break;
			if (version[0][0] == 0 || version[1][0] == 0 || version[2][0] == 0)
				break;
			for (i = 0; i < 3; i++) {
				if (sr_atol(version[i], &n[i]) != SR_OK)
					break;
			}
			if (i != 3)
				break;
			if (n[0] != 0 || n[1] > 2)
				break;
			if (n[1] == 2 && n[2] > 3)
				break;
			sr_dbg("Found DS1000 firmware < 0.2.4, using raw data format.");
			devc->format = FORMAT_RAW;
		} while(0);
		g_strfreev(version);
	}

	sr_scpi_hw_info_free(hw_info);

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					model->analog_channels);

	for (i = 0; i < model->analog_channels; i++) {
		if (!(channel_name = g_strdup_printf("CH%d", i + 1)))
			return NULL;
		ch = sr_channel_new(i, SR_CHANNEL_ANALOG, TRUE, channel_name);
		sdi->channels = g_slist_append(sdi->channels, ch);

		devc->analog_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->analog_groups[i]->name = channel_name;
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				devc->analog_groups[i]);
	}

	if (devc->model->has_digital) {
		devc->digital_group = g_malloc0(sizeof(struct sr_channel_group*));

		for (i = 0; i < ARRAY_SIZE(devc->digital_channels); i++) {
			if (!(channel_name = g_strdup_printf("D%d", i)))
				return NULL;
			ch = sr_channel_new(i, SR_CHANNEL_LOGIC, TRUE, channel_name);
			g_free(channel_name);
			sdi->channels = g_slist_append(sdi->channels, ch);
			devc->digital_group->channels = g_slist_append(
					devc->digital_group->channels, ch);
		}
		devc->digital_group->name = g_strdup("LA");
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				devc->digital_group);
	}

	for (i = 0; i < NUM_TIMEBASE; i++) {
		if (!memcmp(&devc->model->min_timebase, &timebases[i], sizeof(uint64_t[2])))
			devc->timebases = &timebases[i];
		if (!memcmp(&devc->model->series->max_timebase, &timebases[i], sizeof(uint64_t[2])))
			devc->num_timebases = &timebases[i] - devc->timebases + 1;
	}

	for (i = 0; i < NUM_VDIV; i++)
		if (!memcmp(&devc->model->series->min_vdiv, &vdivs[i], sizeof(uint64_t[2])))
			devc->vdivs = &vdivs[i];

	if (!(devc->buffer = g_try_malloc(ACQ_BUFFER_SIZE)))
		return NULL;
	if (!(devc->data = g_try_malloc(ACQ_BUFFER_SIZE * sizeof(float))))
		return NULL;

	devc->data_source = DATA_SOURCE_LIVE;

	sdi->priv = devc;

	return sdi;
}

static GSList *scan(GSList *options)
{
	return sr_scpi_scan(di->priv, options, probe_device);
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if ((ret = sr_scpi_open(scpi)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	if ((ret = rigol_ds_get_dev_cfg(sdi)) < 0) {
		sr_err("Failed to get device config: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;
	devc = sdi->priv;

	if (devc->model->series->protocol == PROTOCOL_V2)
		rigol_ds_config_set(sdi, ":KEY:LOCK DISABLE");

	if (scpi) {
		if (sr_scpi_close(scpi) < 0)
			return SR_ERR;
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int cleanup(void)
{
	return dev_clear();
}

static int analog_frame_size(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_channel *ch;
	int analog_channels = 0;
	GSList *l;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_ANALOG && ch->enabled)
			analog_channels++;
	}

	if (analog_channels == 0)
		return 0;

	switch (devc->data_source) {
	case DATA_SOURCE_LIVE:
		return devc->model->series->live_samples;
	case DATA_SOURCE_MEMORY:
		return devc->model->series->buffer_samples / analog_channels;
	default:
		return 0;
	}
}

static int digital_frame_size(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	switch (devc->data_source) {
	case DATA_SOURCE_LIVE:
		return devc->model->series->live_samples * 2;
	case DATA_SOURCE_MEMORY:
		return devc->model->series->buffer_samples * 2;
	default:
		return 0;
	}
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const char *tmp_str;
	uint64_t samplerate;
	int analog_channel = -1;
	float smallest_diff = 0.0000000001;
	int idx = -1;
	unsigned i;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

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
	case SR_CONF_NUM_TIMEBASE:
		*data = g_variant_new_int32(devc->model->series->num_horizontal_divs);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(NUM_VDIV);
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else if (devc->data_source == DATA_SOURCE_MEMORY)
			*data = g_variant_new_string("Memory");
		else
			*data = g_variant_new_string("Segmented");
		break;
	case SR_CONF_SAMPLERATE:
		if (devc->data_source == DATA_SOURCE_LIVE) {
			samplerate = analog_frame_size(sdi) /
				(devc->timebase * devc->model->series->num_horizontal_divs);
			*data = g_variant_new_uint64(samplerate);
		} else {
			sr_dbg("Unknown data source: %d.", devc->data_source);
			return SR_ERR_NA;
		}
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!strcmp(devc->trigger_source, "ACL"))
			tmp_str = "AC Line";
		else if (!strcmp(devc->trigger_source, "CHAN1"))
			tmp_str = "CH1";
		else if (!strcmp(devc->trigger_source, "CHAN2"))
			tmp_str = "CH2";
		else if (!strcmp(devc->trigger_source, "CHAN3"))
			tmp_str = "CH3";
		else if (!strcmp(devc->trigger_source, "CHAN4"))
			tmp_str = "CH4";
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
	case SR_CONF_TIMEBASE:
		for (i = 0; i < devc->num_timebases; i++) {
			float tb = (float)devc->timebases[i][0] / devc->timebases[i][1];
			float diff = fabs(devc->timebase - tb);
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
			float diff = fabs(devc->vdiv[analog_channel] - vdiv);
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
	default:
		sr_dbg("Tried to get unknown config key: %d.", key);
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t p, q;
	double t_dbl;
	unsigned int i, j;
	int ret;
	const char *tmp_str;
	char buffer[16];

	if (!(devc = sdi->priv))
		return SR_ERR_ARG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

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
		tmp_str = g_variant_get_string(data, NULL);

		if (!tmp_str || !(tmp_str[0] == 'f' || tmp_str[0] == 'r')) {
			sr_err("Unknown trigger slope: '%s'.",
			       (tmp_str) ? tmp_str : "NULL");
			return SR_ERR_ARG;
		}

		g_free(devc->trigger_slope);
		devc->trigger_slope = g_strdup((tmp_str[0] == 'r') ? "POS" : "NEG");
		ret = rigol_ds_config_set(sdi, ":TRIG:EDGE:SLOP %s", devc->trigger_slope);
		break;
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
		ret = rigol_ds_config_set(sdi, ":TIM:OFFS %s", buffer);
		break;
	case SR_CONF_TIMEBASE:
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < devc->num_timebases; i++) {
			if (devc->timebases[i][0] == p && devc->timebases[i][1] == q) {
				devc->timebase = (float)p / q;
				g_ascii_formatd(buffer, sizeof(buffer), "%.9f",
				                devc->timebase);
				ret = rigol_ds_config_set(sdi, ":TIM:SCAL %s", buffer);
				break;
			}
		}
		if (i == devc->num_timebases) {
			sr_err("Invalid timebase index: %d.", i);
			ret = SR_ERR_ARG;
		}
		break;
	case SR_CONF_TRIGGER_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(trigger_sources); i++) {
			if (!strcmp(trigger_sources[i], tmp_str)) {
				g_free(devc->trigger_source);
				devc->trigger_source = g_strdup(trigger_sources[i]);
				if (!strcmp(devc->trigger_source, "AC Line"))
					tmp_str = "ACL";
				else if (!strcmp(devc->trigger_source, "CH1"))
					tmp_str = "CHAN1";
				else if (!strcmp(devc->trigger_source, "CH2"))
					tmp_str = "CHAN2";
				else if (!strcmp(devc->trigger_source, "CH3"))
					tmp_str = "CHAN3";
				else if (!strcmp(devc->trigger_source, "CH4"))
					tmp_str = "CHAN4";
				else
					tmp_str = (char *)devc->trigger_source;
				ret = rigol_ds_config_set(sdi, ":TRIG:EDGE:SOUR %s", tmp_str);
				break;
			}
		}
		if (i == ARRAY_SIZE(trigger_sources)) {
			sr_err("Invalid trigger source index: %d.", i);
			ret = SR_ERR_ARG;
		}
		break;
	case SR_CONF_VDIV:
		if (!cg) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < devc->model->analog_channels; i++) {
			if (cg == devc->analog_groups[i]) {
				for (j = 0; j < ARRAY_SIZE(vdivs); j++) {
					if (vdivs[j][0] != p || vdivs[j][1] != q)
						continue;
					devc->vdiv[i] = (float)p / q;
					g_ascii_formatd(buffer, sizeof(buffer), "%.3f",
					                devc->vdiv[i]);
					return rigol_ds_config_set(sdi, ":CHAN%d:SCAL %s", i + 1,
							buffer);
				}
				sr_err("Invalid vdiv index: %d.", j);
				return SR_ERR_ARG;
			}
		}
		sr_dbg("Didn't set vdiv, unknown channel(group).");
		return SR_ERR_NA;
	case SR_CONF_COUPLING:
		if (!cg) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}
		tmp_str = g_variant_get_string(data, NULL);
		for (i = 0; i < devc->model->analog_channels; i++) {
			if (cg == devc->analog_groups[i]) {
				for (j = 0; j < ARRAY_SIZE(coupling); j++) {
					if (!strcmp(tmp_str, coupling[j])) {
						g_free(devc->coupling[i]);
						devc->coupling[i] = g_strdup(coupling[j]);
						return rigol_ds_config_set(sdi, ":CHAN%d:COUP %s", i + 1,
								devc->coupling[i]);
					}
				}
				sr_err("Invalid coupling index: %d.", j);
				return SR_ERR_ARG;
			}
		}
		sr_dbg("Didn't set coupling, unknown channel(group).");
		return SR_ERR_NA;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "Live"))
			devc->data_source = DATA_SOURCE_LIVE;
		else if (devc->model->series->protocol >= PROTOCOL_V2
			&& !strcmp(tmp_str, "Memory"))
			devc->data_source = DATA_SOURCE_MEMORY;
		else if (devc->model->series->protocol >= PROTOCOL_V3
			 && !strcmp(tmp_str, "Segmented"))
			devc->data_source = DATA_SOURCE_SEGMENTED;
		else {
			sr_err("Unknown data source: '%s'.", tmp_str);
			return SR_ERR;
		}
		break;
	default:
		sr_dbg("Tried to set unknown config key: %d.", key);
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	GVariant *tuple, *rational[2];
	GVariantBuilder gvb;
	unsigned int i;
	struct dev_context *devc = NULL;

	if (sdi)
		devc = sdi->priv;

	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	} else if (key == SR_CONF_DEVICE_OPTIONS && cg == NULL) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		return SR_OK;
	}

	/* Every other option requires a valid device instance. */
	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	/* If a channel group is specified, it must be a valid one. */
	if (cg) {
		for (i = 0; i < devc->model->analog_channels; i++)
			if (cg == devc->analog_groups[i])
				break;
		if (i >= devc->model->analog_channels) {
			sr_err("Invalid channel group specified.");
			return SR_ERR;
		}
	}

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}
		if (cg == devc->digital_group) {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				NULL, 0, sizeof(uint32_t));
			return SR_OK;
		} else {
			for (i = 0; i < devc->model->analog_channels; i++) {
				if (cg == devc->analog_groups[i]) {
					*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
						analog_devopts, ARRAY_SIZE(analog_devopts), sizeof(uint32_t));
					return SR_OK;
				}
			}
			return SR_ERR_NA;
		}
		break;
	case SR_CONF_COUPLING:
		if (!cg) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}
		*data = g_variant_new_strv(coupling, ARRAY_SIZE(coupling));
		break;
	case SR_CONF_VDIV:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (!cg) {
			sr_err("No channel group specified.");
			return SR_ERR_CHANNEL_GROUP;
		}
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < NUM_VDIV; i++) {
			rational[0] = g_variant_new_uint64(devc->vdivs[i][0]);
			rational[1] = g_variant_new_uint64(devc->vdivs[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TIMEBASE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (devc->num_timebases <= 0)
			return SR_ERR_NA;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < devc->num_timebases; i++) {
			rational[0] = g_variant_new_uint64(devc->timebases[i][0]);
			rational[1] = g_variant_new_uint64(devc->timebases[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		*data = g_variant_new_strv(trigger_sources,
				devc->model->has_digital ? ARRAY_SIZE(trigger_sources) : 4);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_strv(trigger_slopes, ARRAY_SIZE(trigger_slopes));
		break;
	case SR_CONF_DATA_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		switch (devc->model->series->protocol) {
		case PROTOCOL_V1:
			*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources) - 2);
			break;
		case PROTOCOL_V2:
			*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources) - 1);
			break;
		default:
			*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources));
			break;
		}
		break;
	default:
		sr_dbg("Tried to list unknown config key: %d.", key);
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_datafeed_packet packet;
	GSList *l;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;
	devc = sdi->priv;

	devc->num_frames = 0;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		sr_dbg("handling channel %s", ch->name);
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (ch->enabled)
				devc->enabled_analog_channels = g_slist_append(
						devc->enabled_analog_channels, ch);
			if (ch->enabled != devc->analog_channels[ch->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (rigol_ds_config_set(sdi, ":CHAN%d:DISP %s", ch->index + 1,
						ch->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->analog_channels[ch->index] = ch->enabled;
			}
		} else if (ch->type == SR_CHANNEL_LOGIC) {
			if (ch->enabled) {
				devc->enabled_digital_channels = g_slist_append(
						devc->enabled_digital_channels, ch);
				/* Turn on LA module if currently off. */
				if (!devc->la_enabled) {
					if (rigol_ds_config_set(sdi, ":LA:DISP ON") != SR_OK)
						return SR_ERR;
					devc->la_enabled = TRUE;
				}
			}
			if (ch->enabled != devc->digital_channels[ch->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (rigol_ds_config_set(sdi, ":DIG%d:TURN %s", ch->index,
						ch->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->digital_channels[ch->index] = ch->enabled;
			}
		}
	}

	if (!devc->enabled_analog_channels && !devc->enabled_digital_channels)
		return SR_ERR;

	/* Turn off LA module if on and no digital channels selected. */
	if (devc->la_enabled && !devc->enabled_digital_channels)
		if (rigol_ds_config_set(sdi, ":LA:DISP OFF") != SR_OK)
			return SR_ERR;

	/* Set memory mode. */
	if (devc->data_source == DATA_SOURCE_SEGMENTED) {
		sr_err("Data source 'Segmented' not yet supported");
		return SR_ERR;
	}

	devc->analog_frame_size = analog_frame_size(sdi);
	devc->digital_frame_size = digital_frame_size(sdi);

	switch (devc->model->series->protocol) {
	case PROTOCOL_V2:
		if (rigol_ds_config_set(sdi, ":ACQ:MEMD LONG") != SR_OK)
			return SR_ERR;
		break;
	case PROTOCOL_V3:
		/* Apparently for the DS2000 the memory
		 * depth can only be set in Running state -
		 * this matches the behaviour of the UI. */
		if (rigol_ds_config_set(sdi, ":RUN") != SR_OK)
			return SR_ERR;
		if (rigol_ds_config_set(sdi, ":ACQ:MDEP %d",
					devc->analog_frame_size) != SR_OK)
			return SR_ERR;
		if (rigol_ds_config_set(sdi, ":STOP") != SR_OK)
			return SR_ERR;
		break;
	default:
		break;
	}

	if (devc->data_source == DATA_SOURCE_LIVE)
		if (rigol_ds_config_set(sdi, ":RUN") != SR_OK)
			return SR_ERR;

	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			rigol_ds_receive, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	if (devc->enabled_analog_channels)
		devc->channel_entry = devc->enabled_analog_channels;
	else
		devc->channel_entry = devc->enabled_digital_channels;

	if (rigol_ds_capture_start(sdi) != SR_OK)
		return SR_ERR;

	/* Start of first frame. */
	packet.type = SR_DF_FRAME_BEGIN;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_datafeed_packet packet;

	(void)cb_data;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	/* End of last frame. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	g_slist_free(devc->enabled_analog_channels);
	g_slist_free(devc->enabled_digital_channels);
	devc->enabled_analog_channels = NULL;
	devc->enabled_digital_channels = NULL;
	scpi = sdi->conn;
	sr_scpi_source_remove(sdi->session, scpi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver rigol_ds_driver_info = {
	.name = "rigol-ds",
	.longname = "Rigol DS",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
