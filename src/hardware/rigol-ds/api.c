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

#include <config.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
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

static const char *trigger_sources_2_chans[] = {
	"CH1", "CH2",
	"EXT", "AC Line",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static const char *trigger_sources_4_chans[] = {
	"CH1", "CH2", "CH3", "CH4",
	"EXT", "AC Line",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static const char *trigger_slopes[] = {
	"r", "f",
};

static const char *coupling[] = {
	"AC", "DC", "GND",
};

static const uint64_t probe_factor[] = {
	1, 2, 5, 10, 20, 50, 100, 200, 500, 1000,
};

/* Do not change the order of entries */
static const char *data_sources[] = {
	"Live",
	"Memory",
	"Segmented",
};

static const struct rigol_ds_command std_cmd[] = {
	{ CMD_GET_HORIZ_TRIGGERPOS, ":TIM:OFFS?" },
	{ CMD_SET_HORIZ_TRIGGERPOS, ":TIM:OFFS %s" },
};

static const struct rigol_ds_command mso7000a_cmd[] = {
	{ CMD_GET_HORIZ_TRIGGERPOS, ":TIM:POS?" },
	{ CMD_SET_HORIZ_TRIGGERPOS, ":TIM:POS %s" },
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
	DSO1000B,
	DS1000Z,
	DS4000,
	MSO5000,
	MSO7000A,
};

/* short name, full name */
static const struct rigol_ds_vendor supported_vendors[] = {
	[RIGOL] = {"Rigol", "Rigol Technologies"},
	[AGILENT] = {"Agilent", "Agilent Technologies"},
};

#define VENDOR(x) &supported_vendors[x]
/* vendor, series/name, protocol, data format, max timebase, min vdiv,
 * number of horizontal divs, live waveform samples, memory buffer samples */
static const struct rigol_ds_series supported_series[] = {
	[VS5000] = {VENDOR(RIGOL), "VS5000", PROTOCOL_V1, FORMAT_RAW,
		{50, 1}, {2, 1000}, 14, 2048, 0},
	[DS1000] = {VENDOR(RIGOL), "DS1000", PROTOCOL_V2, FORMAT_IEEE488_2,
		{50, 1}, {2, 1000}, 12, 600, 1048576},
	[DS2000] = {VENDOR(RIGOL), "DS2000", PROTOCOL_V3, FORMAT_IEEE488_2,
		{500, 1}, {500, 1000000}, 14, 1400, 14000},
	[DS2000A] = {VENDOR(RIGOL), "DS2000A", PROTOCOL_V3, FORMAT_IEEE488_2,
		{1000, 1}, {500, 1000000}, 14, 1400, 14000},
	[DSO1000] = {VENDOR(AGILENT), "DSO1000", PROTOCOL_V3, FORMAT_IEEE488_2,
		{50, 1}, {2, 1000}, 12, 600, 20480},
	[DSO1000B] = {VENDOR(AGILENT), "DSO1000", PROTOCOL_V3, FORMAT_IEEE488_2,
		{50, 1}, {2, 1000}, 12, 600, 20480},
	[DS1000Z] = {VENDOR(RIGOL), "DS1000Z", PROTOCOL_V4, FORMAT_IEEE488_2,
		{50, 1}, {1, 1000}, 12, 1200, 12000000},
	[DS4000] = {VENDOR(RIGOL), "DS4000", PROTOCOL_V4, FORMAT_IEEE488_2,
		{1000, 1}, {1, 1000}, 14, 1400, 0},
	[MSO5000] = {VENDOR(RIGOL), "MSO5000", PROTOCOL_V5, FORMAT_IEEE488_2,
		{1000, 1}, {500, 1000000}, 10, 1000, 0},
	[MSO7000A] = {VENDOR(AGILENT), "MSO7000A", PROTOCOL_V4, FORMAT_IEEE488_2,
		{50, 1}, {2, 1000}, 10, 1000, 8000000},
};

#define SERIES(x) &supported_series[x]
/*
 * Use a macro to select the correct list of trigger sources and its length
 * based on the number of analog channels and presence of digital channels.
 */
#define CH_INFO(num, digital) \
	num, digital, trigger_sources_##num##_chans, \
	digital ? ARRAY_SIZE(trigger_sources_##num##_chans) : (num + 2)
/* series, model, min timebase, analog channels, digital */
static const struct rigol_ds_model supported_models[] = {
	{SERIES(VS5000), "VS5022", {20, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(VS5000), "VS5042", {10, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(VS5000), "VS5062", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(VS5000), "VS5102", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(VS5000), "VS5202", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(VS5000), "VS5022D", {20, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(VS5000), "VS5042D", {10, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(VS5000), "VS5062D", {5, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(VS5000), "VS5102D", {2, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(VS5000), "VS5202D", {2, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS1000), "DS1052E", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000), "DS1102E", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000), "DS1152E", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000), "DS1152E-EDU", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000), "DS1052D", {5, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS1000), "DS1102D", {2, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS1000), "DS1152D", {2, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS2000), "DS2072", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000), "DS2102", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000), "DS2202", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000), "DS2302", {1, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000A), "DS2072A", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000A), "DS2102A", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000A), "DS2202A", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000A), "DS2302A", {1, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS2000A), "MSO2072A", {5, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS2000A), "MSO2102A", {5, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS2000A), "MSO2202A", {2, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DS2000A), "MSO2302A", {1, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(DSO1000), "DSO1002A", {5, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DSO1000), "DSO1004A", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DSO1000), "DSO1012A", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DSO1000), "DSO1014A", {2, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DSO1000), "DSO1022A", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DSO1000), "DSO1024A", {2, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DSO1000B), "DSO1052B", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DSO1000B), "DSO1072B", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DSO1000B), "DSO1102B", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DSO1000B), "DSO1152B", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000Z), "DS1054Z", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1074Z", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1104Z", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1074Z-S", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1104Z-S", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1074Z Plus", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1104Z Plus", {5, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(DS1000Z), "DS1102Z-E", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000Z), "DS1202Z-E", {2, 1000000000}, CH_INFO(2, false), std_cmd},
	{SERIES(DS1000Z), "MSO1074Z", {5, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(DS1000Z), "MSO1104Z", {5, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(DS1000Z), "MSO1074Z-S", {5, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(DS1000Z), "MSO1104Z-S", {5, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(DS4000), "DS4024", {1, 1000000000}, CH_INFO(4, false), std_cmd},
	{SERIES(MSO5000), "MSO5072", {1, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(MSO5000), "MSO5074", {1, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(MSO5000), "MSO5102", {1, 1000000000}, CH_INFO(2, true), std_cmd},
	{SERIES(MSO5000), "MSO5104", {1, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(MSO5000), "MSO5204", {1, 1000000000}, CH_INFO(4, true), std_cmd},
	{SERIES(MSO5000), "MSO5354", {1, 1000000000}, CH_INFO(4, true), std_cmd},
	/* TODO: Digital channels are not yet supported on MSO7000A. */
	{SERIES(MSO7000A), "MSO7034A", {2, 1000000000}, CH_INFO(4, false), mso7000a_cmd},
};

static struct sr_dev_driver rigol_ds_driver_info;

static int analog_frame_size(const struct sr_dev_inst *);

static void clear_helper(struct dev_context *devc)
{
	unsigned int i;

	g_free(devc->data);
	g_free(devc->buffer);
	for (i = 0; i < ARRAY_SIZE(devc->coupling); i++)
		g_free(devc->coupling[i]);
	g_free(devc->trigger_source);
	g_free(devc->trigger_slope);
	g_free(devc->analog_groups);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
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
		if (!g_ascii_strcasecmp(hw_info->manufacturer,
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
	sdi->vendor = g_strdup(model->series->vendor->name);
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = &rigol_ds_driver_info;
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
			scpi->firmware_version = n[0] * 100 + n[1] * 10 + n[2];
			if (scpi->firmware_version < 24) {
				sr_dbg("Found DS1000 firmware < 0.2.4, using raw data format.");
				devc->format = FORMAT_RAW;
			}
			break;
		} while (0);
		g_strfreev(version);
	}

	sr_scpi_hw_info_free(hw_info);

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
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
		if (!memcmp(&devc->model->series->min_vdiv,
					&vdivs[i], sizeof(uint64_t[2]))) {
			devc->vdivs = &vdivs[i];
			devc->num_vdivs = ARRAY_SIZE(vdivs) - i;
		}
	}

	devc->buffer = g_malloc(ACQ_BUFFER_SIZE);
	devc->data = g_malloc(ACQ_BUFFER_SIZE * sizeof(float));

	devc->data_source = DATA_SOURCE_LIVE;

	sdi->priv = devc;

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
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

	if ((ret = rigol_ds_get_dev_cfg(sdi)) < 0) {
		sr_err("Failed to get device config: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	scpi = sdi->conn;
	devc = sdi->priv;

	if (!scpi)
		return SR_ERR_BUG;

	if (devc->model->series->protocol == PROTOCOL_V2)
		rigol_ds_config_set(sdi, ":KEY:LOCK DISABLE");

	return sr_scpi_close(scpi);
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
	case DATA_SOURCE_SEGMENTED:
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
	case DATA_SOURCE_SEGMENTED:
		return devc->model->series->buffer_samples * 2;
	default:
		return 0;
	}
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
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else if (devc->data_source == DATA_SOURCE_MEMORY)
			*data = g_variant_new_string("Memory");
		else
			*data = g_variant_new_string("Segmented");
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limit_frames);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
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
	case SR_CONF_TRIGGER_LEVEL:
		*data = g_variant_new_double(devc->trigger_level);
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
	int ret, idx, i;
	const char *tmp_str;
	char buffer[16];

	devc = sdi->priv;

	/* If a channel group is specified, it must be a valid one. */
	if (cg && !g_slist_find(sdi->channel_groups, cg)) {
		sr_err("Invalid channel group specified.");
		return SR_ERR;
	}

	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = g_variant_get_uint64(data);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes))) < 0)
			return SR_ERR_ARG;
		g_free(devc->trigger_slope);
		devc->trigger_slope = g_strdup((trigger_slopes[idx][0] == 'r') ? "POS" : "NEG");
		return rigol_ds_config_set(sdi, ":TRIG:EDGE:SLOP %s", devc->trigger_slope);
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
		return rigol_ds_config_set(sdi,
			devc->model->cmds[CMD_SET_HORIZ_TRIGGERPOS].str, buffer);
	case SR_CONF_TRIGGER_LEVEL:
		t_dbl = g_variant_get_double(data);
		g_ascii_formatd(buffer, sizeof(buffer), "%.3f", t_dbl);
		ret = rigol_ds_config_set(sdi, ":TRIG:EDGE:LEV %s", buffer);
		if (ret == SR_OK)
			devc->trigger_level = t_dbl;
		return ret;
	case SR_CONF_TIMEBASE:
		if ((idx = std_u64_tuple_idx(data, devc->timebases, devc->num_timebases)) < 0)
			return SR_ERR_ARG;
		devc->timebase = (float)devc->timebases[idx][0] / devc->timebases[idx][1];
		g_ascii_formatd(buffer, sizeof(buffer), "%.9f",
		                devc->timebase);
		return rigol_ds_config_set(sdi, ":TIM:SCAL %s", buffer);
	case SR_CONF_TRIGGER_SOURCE:
		if ((idx = std_str_idx(data, devc->model->trigger_sources, devc->model->num_trigger_sources)) < 0)
			return SR_ERR_ARG;
		g_free(devc->trigger_source);
		devc->trigger_source = g_strdup(devc->model->trigger_sources[idx]);
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
		return rigol_ds_config_set(sdi, ":TRIG:EDGE:SOUR %s", tmp_str);
	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(vdivs))) < 0)
			return SR_ERR_ARG;
		devc->vdiv[i] = (float)vdivs[idx][0] / vdivs[idx][1];
		g_ascii_formatd(buffer, sizeof(buffer), "%.3f", devc->vdiv[i]);
		return rigol_ds_config_set(sdi, ":CHAN%d:SCAL %s", i + 1, buffer);
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(coupling))) < 0)
			return SR_ERR_ARG;
		g_free(devc->coupling[i]);
		devc->coupling[i] = g_strdup(coupling[idx]);
		return rigol_ds_config_set(sdi, ":CHAN%d:COUP %s", i + 1, devc->coupling[i]);
	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->model->analog_channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_u64_idx(data, ARRAY_AND_SIZE(probe_factor))) < 0)
			return SR_ERR_ARG;
		p = g_variant_get_uint64(data);
		devc->attenuation[i] = probe_factor[idx];
		ret = rigol_ds_config_set(sdi, ":CHAN%d:PROB %"PRIu64, i + 1, p);
		if (ret == SR_OK)
			rigol_ds_get_dev_cfg_vertical(sdi);
		return ret;
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
		return SR_ERR_NA;
	}

	return SR_OK;
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
		*data = g_variant_new_strv(devc->model->trigger_sources, devc->model->num_trigger_sources);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_slopes));
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
			*data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
			break;
		}
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
	gboolean some_digital;
	GSList *l;
	char *cmd;
	int protocol;

	scpi = sdi->conn;
	devc = sdi->priv;
	protocol = devc->model->series->protocol;

	devc->num_frames = 0;
	devc->num_frames_segmented = 0;

	some_digital = FALSE;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		sr_dbg("handling channel %s", ch->name);
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (ch->enabled)
				devc->enabled_channels = g_slist_append(
						devc->enabled_channels, ch);
			if (ch->enabled != devc->analog_channels[ch->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (rigol_ds_config_set(sdi, ":CHAN%d:DISP %s", ch->index + 1,
						ch->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->analog_channels[ch->index] = ch->enabled;
			}
		} else if (ch->type == SR_CHANNEL_LOGIC) {
			/* Only one list entry for older protocols. All channels are
			 * retrieved together when this entry is processed. */
			if (ch->enabled && (
						protocol > PROTOCOL_V3 ||
						!some_digital))
				devc->enabled_channels = g_slist_append(
						devc->enabled_channels, ch);
			if (ch->enabled) {
				some_digital = TRUE;
				/* Turn on LA module if currently off. */
				if (!devc->la_enabled) {
					if (rigol_ds_config_set(sdi, protocol >= PROTOCOL_V3 ?
								":LA:STAT ON" : ":LA:DISP ON") != SR_OK)
						return SR_ERR;
					devc->la_enabled = TRUE;
				}
			}
			if (ch->enabled != devc->digital_channels[ch->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (protocol >= PROTOCOL_V5)
					cmd = ":LA:DISP D%d,%s";
				else if (protocol >= PROTOCOL_V3)
					cmd = ":LA:DIG%d:DISP %s";
				else
					cmd = ":DIG%d:TURN %s";

				if (rigol_ds_config_set(sdi, cmd, ch->index,
						ch->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->digital_channels[ch->index] = ch->enabled;
			}
		}
	}

	if (!devc->enabled_channels)
		return SR_ERR;

	/* Turn off LA module if on and no digital channels selected. */
	if (devc->la_enabled && !some_digital)
		if (rigol_ds_config_set(sdi,
				devc->model->series->protocol >= PROTOCOL_V3 ?
					":LA:STAT OFF" : ":LA:DISP OFF") != SR_OK)
			return SR_ERR;

	/* Set memory mode. */
	if (devc->data_source == DATA_SOURCE_SEGMENTED) {
		switch (protocol) {
		case PROTOCOL_V1:
		case PROTOCOL_V2:
			/* V1 and V2 do not have segmented data */
			sr_err("Data source 'Segmented' not supported on this model");
			break;
		case PROTOCOL_V3:
		case PROTOCOL_V4:
		{
			int frames = 0;
			if (sr_scpi_get_int(sdi->conn,
						protocol == PROTOCOL_V4 ? "FUNC:WREP:FEND?" :
						"FUNC:WREP:FMAX?", &frames) != SR_OK)
				return SR_ERR;
			if (frames <= 0) {
				sr_err("No segmented data available");
				return SR_ERR;
			}
			devc->num_frames_segmented = frames;
			break;
		}
		case PROTOCOL_V5:
			/* The frame limit has to be read on the fly, just set up
			 * reading of the first frame */
			if (rigol_ds_config_set(sdi, "REC:CURR 1") != SR_OK)
				return SR_ERR;
			break;
		default:
			sr_err("Data source 'Segmented' not yet supported");
			return SR_ERR;
		}
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

	std_session_send_df_header(sdi);

	devc->channel_entry = devc->enabled_channels;

	if (devc->data_source == DATA_SOURCE_LIVE)
		devc->sample_rate = analog_frame_size(sdi) / 
			(devc->timebase * devc->model->series->num_horizontal_divs);
	else {
		float xinc;
		if (devc->model->series->protocol >= PROTOCOL_V3 && 
				sr_scpi_get_float(sdi->conn, "WAV:XINC?", &xinc) != SR_OK) {
			sr_err("Couldn't get sampling rate");
			return SR_ERR;
		}
		devc->sample_rate = 1. / xinc;
	}


	if (rigol_ds_capture_start(sdi) != SR_OK)
		return SR_ERR;

	/* Start of first frame. */
	std_session_send_df_frame_begin(sdi);

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

static struct sr_dev_driver rigol_ds_driver_info = {
	.name = "rigol-ds",
	.longname = "Rigol DS",
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
SR_REGISTER_DEV_DRIVER(rigol_ds_driver_info);
