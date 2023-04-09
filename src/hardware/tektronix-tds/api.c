/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Patrick Plenefisch <simonpatp@gmail.com>
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
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"
#include "scpi.h"

/**
 * Documentation for the SCPI commands can be found in
 * https://download.tek.com/manual/TBS1000-B-EDU-TDS2000-B-C-TDS1000-B-C-EDU-TDS200-TPS2000-B-Programmer-077044403_RevB.pdf
 * and is referred to as "doc page $PDF_PAGE/$PRINTED_PAGE"
 */


/**
 * Missing semi-important features:
 *   bandwidth limiting ch<x>:bandwidth
 *   chanel invert ch<x>:invert
 *   volt/amp configuration ch:<x>:yunit
 *   pulse triggering
 *   ext trigger coupling
 *   peak-detect mode (data retreival)
 * 
 * Missing less important features:
 *   capture/savefiles
 *   screenshots
 *   fine adjust of vdivs
 *   video triggering
 */


static struct sr_dev_driver tektronix_tds_driver_info;

static const uint32_t scanopts[] = {SR_CONF_CONN, SR_CONF_SERIALCOMM};

static const uint32_t drvopts[] = {SR_CONF_OSCILLOSCOPE};

static const uint32_t devopts[] = {
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVERAGING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVG_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_BUFFERSIZE | SR_CONF_GET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

// TODO: Compensation adjusts the vdivs, but PulseView doesn't pick up
// any such dynamic changes, nor does it seem to support fine-adjust,
// so for now all possible compensation vdivs are present

// validated in doc page 75/2-57
static const uint64_t vdivs[][2] = {
	/* millivolts */
	{2, 1000},
	{5, 1000},
	{10, 1000},
	{20, 1000},
	{50, 1000},
	{100, 1000},
	{200, 1000},
	{500, 1000},
	/* volts */
	{1, 1},
	{2, 1},
	{5, 1},
	/* >1x compensation only */
	{10, 1},
	{20, 1},
	{50, 1},
	/* >10x compensation only */
	{100, 1},
	{200, 1},
	{500, 1},
	/* >100x compensation only */
	{1000, 1},
	{2000, 1},
	{5000, 1},
};

// everyone uses the same voltrange, only modified by compensation ranges (also the same)
#define VOLTRANGE_2m_5V 0, 0

static const uint64_t timebases[][2] = {
	/* nanoseconds */
	{25, 10000000000},
	{5, 1000000000},
	{10, 1000000000},
	{25, 1000000000},
	{50, 1000000000},
	{100, 1000000000},
	{250, 1000000000},
	{500, 1000000000},
	/* microseconds */
	{1, 1000000},
	{25, 10000000},
	{5, 1000000},
	{10, 1000000},
	{25, 1000000},
	{50, 1000000},
	{100, 1000000},
	{250, 1000000},
	{500, 1000000},
	/* milliseconds */
	{1, 1000},
	{25, 10000},
	{5, 1000},
	{10, 1000},
	{25, 1000},
	{50, 1000},
	{100, 1000},
	{250, 1000},
	{500, 1000},
	/* seconds */
	{1, 1},
	{25, 10},
	{5, 1},
	{10, 1},
	{25, 1},
	{50, 1},
};
// Timebase limits are forward index, and reverse index
#define TIMEBASE_2n5_50s 0, 0
#define TIMEBASE_5ns_50s 1, 0
#define TIMEBASE_10n_50s 2, 0
#define TIMEBASE_5ns_5s  1, 3

// validated in doc page 71/2-53
static const char *coupling[] = {
	"AC",
	"DC",
	"GND",
};

// validated in doc page 74/2-53
static const uint64_t probe_factor_new[] = {1, 10, 20, 50, 100, 500, 1000};
// Only for tds200, tds2000, tds1000
static const uint64_t probe_factor_old[] = {1, 10, 100, 1000};

static const char *trigger_slopes[] = {
	"r",
	"f",
};

// validated in doc page 60/2-42
static const uint64_t averages[] = {4, 16, 64, 128};

enum series_support_matrix
{
	T_S_Remainder,
	TDS224,
	TPS_2k,
};

// Must be in same order as the enum values DRIVER_CAPTURE_MODE
static const char *data_sources[] = {
	[CAPTURE_LIVE] = "Live",
	[CAPTURE_ONE_SHOT] = "One Shot",
	[CAPTURE_DISPLAY] = "Memory+Live",
	[CAPTURE_MEMORY] = "Memory",
};

// Note, CH3 and 4 should be last so that 4ch vs 2ch scopes
// can simply truncate this list by two
// validated in doc page 214/2-196
static const char *trigger_sources_models_T_S_Remainder[] = {
	"Ext", "Ext /5", "AC Line",
	"CH1", "CH2",
	/* 4ch only: */ "CH3", "CH4",
};
static const char *trigger_sources_models_TDS224[] = {
	"AC Line",
	"CH1", "CH2",
	/* 4ch only: */ "CH3", "CH4"
};
static const char *trigger_sources_models_TPS_2k[] = {
	"Ext", "Ext /5", "Ext /10",
	"CH1", "CH2",
	/* 4ch only: */ "CH3", "CH4",
};

#define DEVICE_SPEC(id_name, channels, sa_per_s, bw, probe_factors,            \
	time_range, volt_range, trigger_sources)                               \
	{                                                                      \
		id_name, channels, SA_##sa_per_s, BW_##bw,                     \
			ARRAY_AND_SIZE(probe_factor_##probe_factors),          \
			TIMEBASE_##time_range, VOLTRANGE_##volt_range,         \
			(trigger_sources_models_##trigger_sources),            \
			ARRAY_SIZE(trigger_sources_models_##trigger_sources) - \
			4 + channels                                           \
	}

/* This table was generated from the documentation:
 *
 * TBS1000B/EDU: https://download.tek.com/manual/TBS1000B-User-Manual-077088602-RevA.pdf
 * TBS1000: https://download.tek.com/manual/TBS1000-Oscilloscope-User-Manual_077076001.pdf
 * TDS2000C/TDS1000C-EDU: https://download.tek.com/manual/TDS2000C-and-TDS1000C-EDU-Oscilloscope-User-Manual-EN_077082600.pdf
 * TDS2000B/TDS1000B: https://download.tek.com/manual/071181702web.pdf
 * TDS2000/TDS1000: https://download.tek.com/manual/TDS2000_TDS1000_User_071106400_Revision_A.pdf
 * TDS200: https://download.tek.com/manual/071039803.pdf
 * TPS2000B:https://download.tek.com/manual/TPS2000B-Digital-Oscilloscope-User-Manual-077137901.pdf
 * TPS2000: https://download.tek.com/manual/071144105web.pdf
 *
 * All specs can be found in Appendix A's of the linked pdfs
 * EDU series are badge-only, and respond as if they are non-EDU products
 */
static const struct device_spec device_models[] = {

	// TBS original-series
	DEVICE_SPEC("TBS 1022", 2, 500M,  25MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1042", 2, 500M,  40MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1062", 2,   1G,  60MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1064", 4,   1G,  60MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1102", 2,   1G, 100MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1104", 4,   1G, 100MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1152", 2,   1G, 150MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1154", 4,   1G, 150MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),

	// TBS B-series
	DEVICE_SPEC("TBS 1032B",     2, 500M,  30MHz, new, 10n_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1052B",     2,   1G,  50MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1072B",     2,   1G,  70MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1102B",     2,   2G, 100MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1152B",     2,   2G, 150MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TBS 1202B",     2,   2G, 200MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),

	// TDS 200-series, only supports 5ns-5s/div
	DEVICE_SPEC("TDS 210", 2, 1G , 60MHz, old, 5ns_5s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 220", 2, 1G, 100MHz, old, 5ns_5s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 224", 4, 1G, 100MHz, old, 5ns_5s, 2m_5V, TDS224),

	// TDS original-series
	DEVICE_SPEC("TDS 1002", 2, 1G,  60MHz, old, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 1012", 2, 1G, 100MHz, old, 5ns_50s, 2m_5V, T_S_Remainder),

	DEVICE_SPEC("TDS 2002", 2, 1G,  60MHz, old, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2012", 2, 1G, 100MHz, old, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2014", 4, 1G, 100MHz, old, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2022", 2, 2G, 200MHz, old, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2024", 4, 2G, 200MHz, old, 2n5_50s, 2m_5V, T_S_Remainder),

	// TDS B-series
	DEVICE_SPEC("TDS 1001B", 2, 500M,  40MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 1002B", 2,   1G,  60MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 1012B", 2,   1G, 100MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),

	DEVICE_SPEC("TDS 2002B", 2, 1G,  60MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2004B", 4, 1G,  60MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2012B", 2, 1G, 100MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2014B", 4, 1G, 100MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2022B", 2, 2G, 200MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2024B", 4, 2G, 200MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),

	// TDS C-series
	DEVICE_SPEC("TDS 1001C", 2, 500M,  40MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 1002C", 2,   1G,  60MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 1012C", 2,   1G, 100MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),

	DEVICE_SPEC("TDS 2001C", 2, 500M,  50MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2002C", 2,   1G,  70MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2004C", 4,   1G,  70MHz, new, 5ns_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2012C", 2,   2G, 100MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2014C", 4,   2G, 100MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2022C", 2,   2G, 200MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),
	DEVICE_SPEC("TDS 2024C", 4,   2G, 200MHz, new, 2n5_50s, 2m_5V, T_S_Remainder),

	// TPS original-series
	DEVICE_SPEC("TPS 2012",  2, 1G, 100MHz, new, 5ns_50s, 2m_5V, TPS_2k),
	DEVICE_SPEC("TPS 2014",  4, 1G, 100MHz, new, 5ns_50s, 2m_5V, TPS_2k),
	DEVICE_SPEC("TPS 2024",  4, 2G, 200MHz, new, 2n5_50s, 2m_5V, TPS_2k),

	// TPS B-series
	DEVICE_SPEC("TPS 2012B", 2, 1G, 100MHz, new, 5ns_50s, 2m_5V, TPS_2k),
	DEVICE_SPEC("TPS 2014B", 4, 1G, 100MHz, new, 5ns_50s, 2m_5V, TPS_2k),
	DEVICE_SPEC("TPS 2024B", 4, 2G, 200MHz, new, 2n5_50s, 2m_5V, TPS_2k),
};

static const char *TEKTRONIX = "Tektronix";

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;
	const struct device_spec *device;
	struct sr_channel *ch;
	unsigned int i;
	gchar *channel_name;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response, retrying.");
		sr_scpi_close(scpi);
		sr_scpi_open(scpi);
		if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
			sr_info("Couldn't get IDN response.");
			goto error;
		}
	}

	if (g_ascii_strcasecmp(hw_info->manufacturer, TEKTRONIX) != 0)
		goto error;

	device = NULL;
	for (i = 0; i < ARRAY_SIZE(device_models); i++) {
		if (g_ascii_strcasecmp(hw_info->model, device_models[i].model) != 0)
			continue;
		device = &device_models[i];
		break;
	}

	if (!device) {
		sr_dbg("Found Tektronix device not supported by the tds/tps/tbs driver: %s",
			hw_info->model);
		goto error;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup(TEKTRONIX);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->conn = scpi;
	sdi->driver = &tektronix_tds_driver_info;
	sdi->inst_type = SR_INST_SCPI;

	devc = g_malloc0(sizeof(*devc));
	devc->model = device;
	sdi->priv = devc;

	// give us a buffer on our buffer
	devc->buffer = g_malloc(TEK_BUFFER_SIZE + 1000);
	devc->limit_frames = 1;
	devc->capture_mode = CAPTURE_MEMORY;

	sr_scpi_hw_info_free(hw_info);

	devc->analog_groups = g_malloc0(
		sizeof(struct sr_channel_group *) * device->channels);

	for (i = 0; i < (unsigned)device->channels; i++) {
		channel_name = g_strdup_printf("CH%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel_name);

		devc->analog_groups[i] = sr_channel_group_new(
			sdi, channel_name, NULL);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);
	}

	return sdi;

error:
	sr_scpi_hw_info_free(hw_info);
	g_free(devc);
	sr_dev_inst_free(sdi);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;

	if ((ret = sr_scpi_open(scpi)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	if ((ret = tektronix_tds_get_dev_cfg(sdi)) < 0) {
		sr_err("Failed to get device config: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	sr_info(
		"Opened Tektronix device '%s' with %d channels, %dMHz bandwidth, and %dMSa/s",
		devc->model->model, devc->model->channels, devc->model->bandwidth,
		devc->model->sample_rate);

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
		*data = g_variant_new_int32(TEK_NUM_HDIV);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(TEK_NUM_VDIV);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limit_frames);
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_string(data_sources[devc->capture_mode]);
		break;
	case SR_CONF_SAMPLERATE:
		tektronix_tds_get_dev_cfg_horizontal(sdi);
		*data = g_variant_new_uint64(MIN(
			TEK_BUFFER_SIZE / (devc->timebase * (float)TEK_NUM_HDIV),
			devc->model->sample_rate * 1000000.0));
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string(devc->trigger_source);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (!g_ascii_strncasecmp(devc->trigger_slope, "RISE", 4)) {
			tmp_str = "r";
		} else if (!g_ascii_strncasecmp(devc->trigger_slope, "FALL", 4)) {
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
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->horiz_triggerpos * 100);
		break;
	case SR_CONF_TIMEBASE:
		for (i = devc->model->timebase_start;
			i < ARRAY_SIZE(timebases) - devc->model->timebase_stop;
			i++) {
			float tb, diff;

			tb = (float)timebases[i][0] / timebases[i][1];
			diff = fabs(devc->timebase - tb);
			if (diff < smallest_diff) {
				smallest_diff = diff;
				idx = i;
			}
		}
		if (idx < devc->model->timebase_start) {
			sr_dbg("Negative timebase index: %d.", idx);
			return SR_ERR_NA;
		}
		*data = g_variant_new("(tt)", timebases[idx][0], timebases[idx][1]);
		break;
	case SR_CONF_VDIV:
		if (analog_channel < 0) {
			sr_dbg("Negative analog channel: %d.", analog_channel);
			return SR_ERR_NA;
		}
		for (i = devc->model->voltrange_start;
			i < ARRAY_SIZE(vdivs) - devc->model->voltrange_stop; i++) {
			float vdiv = (float)vdivs[i][0] / vdivs[i][1];
			float diff = fabsf(devc->vdiv[analog_channel] - vdiv);
			if (diff < smallest_diff) {
				smallest_diff = diff;
				idx = i;
			}
		}
		if (idx < devc->model->voltrange_start) {
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
	case SR_CONF_ENABLED:
		if (analog_channel < 0) {
			sr_dbg("Negative analog channel: %d.", analog_channel);
			return SR_ERR_NA;
		}
		*data = g_variant_new_boolean(
			devc->analog_channels[analog_channel]);
		break;
	case SR_CONF_PEAK_DETECTION:
		*data = g_variant_new_boolean(devc->peak_enabled);
		break;
	case SR_CONF_AVERAGING:
		*data = g_variant_new_boolean(devc->average_enabled);
		break;
	case SR_CONF_AVG_SAMPLES:
		*data = g_variant_new_uint64(devc->average_samples);
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
	char cmd4[4];
	gboolean b;

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
		sr_info("Getting frames limit of %li", g_variant_get_uint64(data));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes))) < 0)
			return SR_ERR_ARG;
		g_free(devc->trigger_slope);
		devc->trigger_slope = g_strdup(
			(trigger_slopes[idx][0] == 'r') ? "RISE" : "FALL");
		return tektronix_tds_config_set(
			sdi, "TRIG:MAI:EDGE:SLO %s", devc->trigger_slope);
	case SR_CONF_CAPTURE_RATIO:
		t_dbl = g_variant_get_uint64(data) / 100.0;
		if (t_dbl < 0.0 || t_dbl > 1.0) {
			sr_err("Invalid horiz. trigger position: %g.", t_dbl);
			return SR_ERR;
		}
		/* Fall through */
	case SR_CONF_HORIZ_TRIGGERPOS:
		if (key == SR_CONF_HORIZ_TRIGGERPOS)
			t_dbl = g_variant_get_double(data);
		devc->horiz_triggerpos = t_dbl;
		/* We have the trigger offset as a percentage of the frame, but
		 * need to express this in seconds. */
		t_dbl = -(devc->horiz_triggerpos - 0.5) * devc->timebase * TEK_NUM_HDIV;
		return tektronix_tds_config_set(sdi, "hor:mai:pos %.3e", t_dbl);
	case SR_CONF_TRIGGER_LEVEL:
		if (!strcmp(devc->trigger_source, "AC Line"))
			sr_err("Can't set level on AC line trigger, ignoring");
		return SR_ERR;

		t_dbl = g_variant_get_double(data);
		ret = tektronix_tds_config_set(sdi, "TRIG:MAI:LEV %.3e", t_dbl);
		if (ret == SR_OK)
			devc->trigger_level = t_dbl;
		break;
	case SR_CONF_TIMEBASE:
		if ((idx = std_u64_tuple_idx(data, timebases,
			     ARRAY_SIZE(timebases) - devc->model->timebase_stop)) < 0)
			return SR_ERR_ARG;
		if (idx < devc->model->timebase_start)
			return SR_ERR_ARG;
		devc->timebase = (float)timebases[idx][0] / timebases[idx][1];
		ret = tektronix_tds_config_set(
			sdi, "hor:sca %.1e", devc->timebase);
		if (ret == SR_OK)
			tektronix_tds_get_dev_cfg_horizontal(sdi);
		return ret;
	case SR_CONF_TRIGGER_SOURCE:
		if ((idx = std_str_idx(data, devc->model->trigger_sources,
			     devc->model->num_trigger_sources)) < 0)
			return SR_ERR_ARG;
		g_free(devc->trigger_source);
		devc->trigger_source = g_strdup(devc->model->trigger_sources[idx]);
		if (!strcmp(devc->trigger_source, "AC Line")) {
			// ONLY set edge trigger, as only edge trigger supports this
			// TODO: raise error when not edge source
			return tektronix_tds_config_set(
				sdi, "TRIG:mai:edge:sou line");
		} else if (!strcmp(devc->trigger_source, "Ext /5"))
			tmp_str = "EXT5";
		else if (!strcmp(devc->trigger_source, "Ext /10"))
			tmp_str = "EXT10";
		else
			tmp_str = (char *)devc->trigger_source;
		// Note: pulse and video triggering isn't set here
		return tektronix_tds_config_set(
			sdi, "TRIG:mai:edge:sou %s", tmp_str);
	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups,
			     devc->model->channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(vdivs))) < 0)
			return SR_ERR_ARG;
		devc->vdiv[i] = (float)vdivs[idx][0] / vdivs[idx][1];
		ret = tektronix_tds_config_set(
			sdi, "CH%d:SCA %.2e", i + 1, (double)devc->vdiv[i]);
		return ret;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups,
			     devc->model->channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(coupling))) < 0)
			return SR_ERR_ARG;
		g_free(devc->coupling[i]);
		devc->coupling[i] = g_strdup(coupling[idx]);
		strncpy(cmd4, devc->coupling[i], 3);
		cmd4[3] = 0;
		return tektronix_tds_config_set(
			sdi, "CH%d:COUP %s", i + 1, cmd4);
	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups,
			     devc->model->channels)) < 0)
			return SR_ERR_ARG;
		if ((idx = std_u64_idx(data, devc->model->probe_factors,
			     devc->model->num_probe_factors)) < 0)
			return SR_ERR_ARG;
		p = g_variant_get_uint64(data);
		devc->attenuation[i] = devc->model->probe_factors[idx];
		ret = tektronix_tds_config_set(
			sdi, "CH%d:PROBE %" PRIu64, i + 1, p);
		if (ret == SR_OK)
			tektronix_tds_get_dev_cfg_vertical(sdi);
		return ret;
	case SR_CONF_ENABLED:
		sr_dbg("configuring channel");
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((i = std_cg_idx(cg, devc->analog_groups,
			     devc->model->channels)) < 0)
			return SR_ERR_ARG;
		b = g_variant_get_boolean(data);
		devc->analog_channels[i] = b;
		ret = tektronix_tds_config_set(
			sdi, "SEL:CH%d %s", i + 1, b ? "ON" : "OFF");
		return ret;
	case SR_CONF_DATA_SOURCE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(data_sources))) < 0)
			return SR_ERR_ARG;
		devc->capture_mode = idx;
		break;
	case SR_CONF_PEAK_DETECTION:
		// TODO: you can configure peak detect mode, but the data isn't
		// parsed yet
		devc->peak_enabled = g_variant_get_boolean(data);
		if (devc->peak_enabled)
			ret = tektronix_tds_config_set(sdi, "acq:mode peak");
		else
			ret = tektronix_tds_config_set(sdi, "acq:mode sam");
		devc->average_enabled = FALSE;
		sr_dbg("%s peak detect",
			devc->peak_enabled ? "Enabling" : "Disabling");
		break;
	case SR_CONF_AVERAGING:
		devc->average_enabled = g_variant_get_boolean(data);
		if (devc->average_enabled)
			ret = tektronix_tds_config_set(sdi, "acq:mode ave");
		else
			ret = tektronix_tds_config_set(sdi, "acq:mode sam");
		devc->peak_enabled = FALSE;
		sr_dbg("%s averaging",
			devc->average_enabled ? "Enabling" : "Disabling");
		break;
	case SR_CONF_AVG_SAMPLES:
		devc->average_samples = g_variant_get_uint64(data);
		sr_dbg("Setting averaging rate to %d", devc->average_samples);
		ret = tektronix_tds_config_set(
			sdi, "acq:numav %d", devc->average_samples);
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
			return STD_CONFIG_LIST(
				key, data, sdi, cg, scanopts, drvopts, devopts);
		if (!devc)
			return SR_ERR_ARG;
		if (std_cg_idx(cg, devc->analog_groups, devc->model->channels) < 0)
			return SR_ERR_ARG;
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog));
		return SR_OK;
		break;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = g_variant_new_strv(ARRAY_AND_SIZE(coupling));
		break;
	case SR_CONF_PROBE_FACTOR:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = std_gvar_array_u64(devc->model->probe_factors,
			devc->model->num_probe_factors);
		break;
	case SR_CONF_VDIV:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		*data = std_gvar_tuple_array(ARRAY_AND_SIZE(vdivs));
		break;
	case SR_CONF_TIMEBASE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		*data = std_gvar_tuple_array(
			&timebases[devc->model->timebase_start],
			ARRAY_SIZE(timebases) - devc->model->timebase_start -
				devc->model->timebase_stop);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		*data = g_variant_new_strv(devc->model->trigger_sources,
			devc->model->num_trigger_sources);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_slopes));
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
		break;
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(TEK_NUM_HDIV);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(TEK_NUM_VDIV);
		break;
	case SR_CONF_AVG_SAMPLES:
		*data = std_gvar_array_u64(ARRAY_AND_SIZE(averages));
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
	GSList *l;
	char *response = NULL;

	scpi = sdi->conn;
	devc = sdi->priv;

	devc->num_frames = 0;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->enabled)
			devc->enabled_channels = g_slist_append(
				devc->enabled_channels, ch);
		if (ch->enabled != devc->analog_channels[ch->index]) {
			/* Enabled channel is currently disabled, or vice versa. */
			if (tektronix_tds_config_set(sdi, "SEL:CH%d %s",
				    ch->index + 1,
				    ch->enabled ? "ON" : "OFF") != SR_OK)
				return SR_ERR;
			devc->analog_channels[ch->index] = ch->enabled;
		}
	}
	if (!devc->enabled_channels)
		return SR_ERR;

	// Set view to main, but don't check the status as TBS1000B doesn't
	// support this command. This ensures the timebase is correct
	tektronix_tds_config_set(sdi, "hor:view mai");

	tektronix_tds_get_dev_cfg_horizontal(sdi);

	if (sr_scpi_get_bool(scpi, "acq:state?", &devc->prior_state_running) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(scpi, "acq:stopa?", &response) != SR_OK)
		return SR_ERR;

	devc->prior_state_single =
		(g_ascii_strncasecmp("sequence", response, 3) == 0);

	// these models are slow, and TDS2xxxB takes ~1.5 seconds to begin
	// transmitting, so poll slowly However, not too slow, as that makes
	// some weird transmission timeouts appear
	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 500,
		tektronix_tds_receive, (void *)sdi);

	std_session_send_df_header(sdi);

	devc->channel_entry = devc->enabled_channels;

	if (tektronix_tds_capture_start(sdi) != SR_OK)
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

static struct sr_dev_driver tektronix_tds_driver_info = {
	// Awkward name, but there isn't a proper name for it
	.name = "tektronix-tds",
	.longname = "Tektronix TDS/TBS/TPS",
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
SR_REGISTER_DEV_DRIVER(tektronix_tds_driver_info);
