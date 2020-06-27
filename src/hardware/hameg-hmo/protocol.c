/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
 * Copyright (C) 2018 Guido Trentalancia <guido@trentalancia.com>
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
#include <stdlib.h>
#include "scpi.h"
#include "protocol.h"

SR_PRIV void hmo_queue_logic_data(struct dev_context *devc,
				  size_t group, GByteArray *pod_data);
SR_PRIV void hmo_send_logic_packet(struct sr_dev_inst *sdi,
				   struct dev_context *devc);
SR_PRIV void hmo_cleanup_logic_data(struct dev_context *devc);

static const char *hameg_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:POD%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":POD%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":POD%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:A:TYPE EDGE;:TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:PEAK?",
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:PEAK %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":POD%d:THR?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":POD%d:THR %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":POD%d:THR:UDL%d?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":POD%d:THR:UDL%d %s",
};

static const char *rohde_schwarz_log_not_pod_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:LOG%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",
	[SCPI_CMD_GET_HORIZONTAL_DIV]	      = ":TIM:DIV?",
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:A:TYPE EDGE;:TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:PEAK?",
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:PEAK %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:POS?",	/* Might not be supported on RTB200x... */
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	      = ":PROB%d:SET:ATT:UNIT?",
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":DIG%d:TECH?",
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":DIG%d:TECH %s",
	[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD] = ":DIG%d:THR?",
	[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD] = ":DIG%d:THR %s",
};

static const uint32_t devopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_digital[] = {
	SR_CONF_LOGIC_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LOGIC_THRESHOLD_CUSTOM | SR_CONF_GET | SR_CONF_SET,
};

static const char *coupling_options[] = {
	"AC",  // AC with 50 Ohm termination (152x, 202x, 30xx, 1202)
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rtb200x[] = {
	"ACL", // AC with 1 MOhm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *coupling_options_rtm300x[] = {
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
};

static const char *scope_trigger_slopes[] = {
	"POS",
	"NEG",
	"EITH",
};

/* Predefined logic thresholds. */
static const char *logic_threshold[] = {
	"TTL",
	"ECL",
	"CMOS",
	"USER1",
	"USER2", // overwritten by logic_threshold_custom, use USER1 for permanent setting
};

static const char *logic_threshold_rtb200x_rtm300x[] = {
	"TTL",
	"ECL",
	"CMOS",
	"MAN", // overwritten by logic_threshold_custom
};

/* This might need updates whenever logic_threshold* above change. */
#define MAX_NUM_LOGIC_THRESHOLD_ENTRIES ARRAY_SIZE(logic_threshold)

/* RTC1002, HMO Compact2 and HMO1002/HMO1202 */
static const char *an2_dig8_trigger_sources[] = {
	"CH1", "CH2",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
};

/* HMO3xx2 */
static const char *an2_dig16_trigger_sources[] = {
	"CH1", "CH2",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* RTB2002 and RTM3002 */
static const char *an2_dig16_sbus_trigger_sources[] = {
	"CH1", "CH2",
	"LINE", "EXT", "PATT", "SBUS1", "SBUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* HMO Compact4 */
static const char *an4_dig8_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
};

/* HMO3xx4 and HMO2524 */
static const char *an4_dig16_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"LINE", "EXT", "PATT", "BUS1", "BUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

/* RTB2004, RTM3004 and RTA4004 */
static const char *an4_dig16_sbus_trigger_sources[] = {
	"CH1", "CH2", "CH3", "CH4",
	"LINE", "EXT", "PATT", "SBUS1", "SBUS2",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
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
};

/* HMO Compact series (HMO722/724/1022/1024/1522/1524/2022/2024) do
 * not support 1 ns timebase setting.
 */
static const uint64_t timebases_hmo_compact[][2] = {
	/* nanoseconds */
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
};

static const uint64_t vdivs[][2] = {
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
};

static const char *scope_analog_channel_names[] = {
	"CH1", "CH2", "CH3", "CH4",
};

static const char *scope_digital_channel_names[] = {
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
};

static struct scope_config scope_models[] = {
	{
		/* HMO Compact2: HMO722/1022/1522/2022 support only 8 digital channels. */
		.name = {"HMO722", "HMO1022", "HMO1522", "HMO2022", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases_hmo_compact,
		.num_timebases = ARRAY_SIZE(timebases_hmo_compact),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* RTC1002 and HMO1002/HMO1202 support only 8 digital channels. */
		.name = {"RTC1002", "HMO1002", "HMO1202", NULL},
		.analog_channels = 2,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* HMO3032/3042/3052/3522 support 16 digital channels. */
		.name = {"HMO3032", "HMO3042", "HMO3052", "HMO3522", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an2_dig16_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* HMO Compact4: HMO724/1024/1524/2024 support only 8 digital channels. */
		.name = {"HMO724", "HMO1024", "HMO1524", "HMO2024", NULL},
		.analog_channels = 4,
		.digital_channels = 8,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an4_dig8_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig8_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases_hmo_compact,
		.num_timebases = ARRAY_SIZE(timebases_hmo_compact),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		.name = {"HMO2524", "HMO3034", "HMO3044", "HMO3054", "HMO3524", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &an4_dig16_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		.name = {"RTB2002", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options_rtb200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtb200x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an2_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTB2004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options_rtb200x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtb200x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTM3002", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an2_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an2_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTM3004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
	{
		.name = {"RTA4004", NULL},
		.analog_channels = 4,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.coupling_options = &coupling_options_rtm300x,
		.num_coupling_options = ARRAY_SIZE(coupling_options_rtm300x),

		.logic_threshold = &logic_threshold_rtb200x_rtm300x,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold_rtb200x_rtm300x),
		.logic_threshold_for_pod = FALSE,

		.trigger_sources = &an4_dig16_sbus_trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(an4_dig16_sbus_trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_ydivs = 8,

		.scpi_dialect = &rohde_schwarz_log_not_pod_scpi_dialect,
	},
};

static void scope_state_dump(const struct scope_config *config,
			     struct scope_state *state)
{
	unsigned int i;
	char *tmp;

	for (i = 0; i < config->analog_channels; i++) {
		tmp = sr_voltage_string((*config->vdivs)[state->analog_channels[i].vdiv][0],
					     (*config->vdivs)[state->analog_channels[i].vdiv][1]);
		sr_info("State of analog channel %d -> %s : %s (coupling) %s (vdiv) %2.2e (offset)",
			i + 1, state->analog_channels[i].state ? "On" : "Off",
			(*config->coupling_options)[state->analog_channels[i].coupling],
			tmp, state->analog_channels[i].vertical_offset);
	}

	for (i = 0; i < config->digital_channels; i++) {
		sr_info("State of digital channel %d -> %s", i,
			state->digital_channels[i] ? "On" : "Off");
	}

	for (i = 0; i < config->digital_pods; i++) {
		if (!strncmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold], 4) ||
		    !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			sr_info("State of digital POD %d -> %s : %E (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				state->digital_pods[i].user_threshold);
		else
			sr_info("State of digital POD %d -> %s : %s (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				(*config->logic_threshold)[state->digital_pods[i].threshold]);
	}

	tmp = sr_period_string((*config->timebases)[state->timebase][0],
			       (*config->timebases)[state->timebase][1]);
	sr_info("Current timebase: %s", tmp);
	g_free(tmp);

	tmp = sr_samplerate_string(state->sample_rate);
	sr_info("Current samplerate: %s", tmp);
	g_free(tmp);

	if (!strcmp("PATT", (*config->trigger_sources)[state->trigger_source]))
		sr_info("Current trigger: %s (pattern), %.2f (offset)",
			state->trigger_pattern,
			state->horiz_triggerpos);
	else // Edge (slope) trigger
		sr_info("Current trigger: %s (source), %s (slope) %.2f (offset)",
			(*config->trigger_sources)[state->trigger_source],
			(*config->trigger_slopes)[state->trigger_slope],
			state->horiz_triggerpos);
}

static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi,
		const char *command, const char *(*array)[], unsigned int n, int *result)
{
	char *tmp;
	int idx;

	if (sr_scpi_get_string(scpi, command, &tmp) != SR_OK)
		return SR_ERR;

	if ((idx = std_str_idx_s(tmp, *array, n)) < 0) {
		g_free(tmp);
		return SR_ERR_ARG;
	}

	*result = idx;

	g_free(tmp);

	return SR_OK;
}

/**
 * This function takes a value of the form "2.000E-03" and returns the index
 * of an array where a matching pair was found.
 *
 * @param value The string to be parsed.
 * @param array The array of s/f pairs.
 * @param array_len The number of pairs in the array.
 * @param result The index at which a matching pair was found.
 *
 * @return SR_ERR on any parsing error, SR_OK otherwise.
 */
static int array_float_get(gchar *value, const uint64_t array[][2],
		int array_len, unsigned int *result)
{
	struct sr_rational rval;
	struct sr_rational aval;

	if (sr_parse_rational(value, &rval) != SR_OK)
		return SR_ERR;

	for (int i = 0; i < array_len; i++) {
		sr_rational_set(&aval, array[i][0], array[i][1]);
		if (sr_rational_eq(&rval, &aval)) {
			*result = i;
			return SR_OK;
		}
	}

	return SR_ERR;
}

static struct sr_channel *get_channel_by_index_and_type(GSList *channel_lhead,
							int index, int type)
{
	while (channel_lhead) {
		struct sr_channel *ch = channel_lhead->data;
		if (ch->index == index && ch->type == type)
			return ch;

		channel_lhead = channel_lhead->next;
	}

	return 0;
}

static int analog_channel_state_get(struct sr_dev_inst *sdi,
				    const struct scope_config *config,
				    struct scope_state *state)
{
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	char *tmp_str;
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	for (i = 0; i < config->analog_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_CHAN_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->analog_channels[i].state) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_ANALOG);
		if (ch)
			ch->enabled = state->analog_channels[i].state;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_SCALE],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (array_float_get(tmp_str, ARRAY_AND_SIZE(vdivs), &j) != SR_OK) {
			g_free(tmp_str);
			sr_err("Could not determine array index for vertical div scale.");
			return SR_ERR;
		}

		g_free(tmp_str);
		state->analog_channels[i].vdiv = j;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_OFFSET],
			   i + 1);

		if (sr_scpi_get_float(scpi, command,
				     &state->analog_channels[i].vertical_offset) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_COUPLING],
			   i + 1);

		if (scope_state_get_array_option(scpi, command, config->coupling_options,
					 config->num_coupling_options,
					 &state->analog_channels[i].coupling) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_PROBE_UNIT],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (tmp_str[0] == 'A')
			state->analog_channels[i].probe_unit = 'A';
		else
			state->analog_channels[i].probe_unit = 'V';
		g_free(tmp_str);
	}

	return SR_OK;
}

static int digital_channel_state_get(struct sr_dev_inst *sdi,
				     const struct scope_config *config,
				     struct scope_state *state)
{
	unsigned int i, idx;
	int result = SR_ERR;
	char *logic_threshold_short[MAX_NUM_LOGIC_THRESHOLD_ENTRIES];
	char command[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	for (i = 0; i < config->digital_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
			   i);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_channels[i]) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_LOGIC);
		if (ch)
			ch->enabled = state->digital_channels[i];
	}

	/* According to the SCPI standard, on models that support multiple
	 * user-defined logic threshold settings the response to the command
	 * SCPI_CMD_GET_DIG_POD_THRESHOLD might return "USER" instead of
	 * "USER1".
	 *
	 * This makes more difficult to validate the response when the logic
	 * threshold is set to "USER1" and therefore we need to prevent device
	 * opening failures in such configuration case...
	 */
	for (i = 0; i < config->num_logic_threshold; i++) {
		logic_threshold_short[i] = g_strdup((*config->logic_threshold)[i]);
		if (!strcmp("USER1", (*config->logic_threshold)[i]))
			g_strlcpy(logic_threshold_short[i],
				  (*config->logic_threshold)[i], strlen((*config->logic_threshold)[i]));
	}

	for (i = 0; i < config->digital_pods; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_pods[i].state) != SR_OK)
			goto exit;

		/* Check if the threshold command is based on the POD or digital channel index. */
		if (config->logic_threshold_for_pod)
			idx = i + 1;
		else
			idx = i * DIGITAL_CHANNELS_PER_POD;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_THRESHOLD],
			   idx);

		/* Check for both standard and shortened responses. */
		if (scope_state_get_array_option(scpi, command, config->logic_threshold,
						 config->num_logic_threshold,
						 &state->digital_pods[i].threshold) != SR_OK)
			if (scope_state_get_array_option(scpi, command, (const char * (*)[]) &logic_threshold_short,
							 config->num_logic_threshold,
							 &state->digital_pods[i].threshold) != SR_OK)
				goto exit;

		/* If used-defined or custom threshold is active, get the level. */
		if (!strcmp("USER1", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
				   idx, 1); /* USER1 logic threshold setting. */
		else if (!strcmp("USER2", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
				   idx, 2); /* USER2 for custom logic_threshold setting. */
		else if (!strcmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			 !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
				   idx); /* USER or MAN for custom logic_threshold setting. */
		if (!strcmp("USER1", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
		    !strcmp("USER2", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
		    !strcmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
		    !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			if (sr_scpi_get_float(scpi, command,
			    &state->digital_pods[i].user_threshold) != SR_OK)
				goto exit;
	}

	result = SR_OK;

exit:
	for (i = 0; i < config->num_logic_threshold; i++)
		g_free(logic_threshold_short[i]);

	return result;
}

SR_PRIV int hmo_update_sample_rate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	if (sr_scpi_get_float(sdi->conn,
			      (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE],
			      &tmp_float) != SR_OK)
		return SR_ERR;

	state->sample_rate = tmp_float;

	return SR_OK;
}

SR_PRIV int hmo_scope_state_get(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;
	unsigned int i;
	char *tmp_str;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	sr_info("Fetching scope state");

	if (analog_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (digital_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TIMEBASE],
			&tmp_str) != SR_OK)
		return SR_ERR;

	if (array_float_get(tmp_str, ARRAY_AND_SIZE(timebases), &i) != SR_OK) {
		g_free(tmp_str);
		sr_err("Could not determine array index for time base.");
		return SR_ERR;
	}
	g_free(tmp_str);

	state->timebase = i;

	/* Determine the number of horizontal (x) divisions. */
	if (sr_scpi_get_int(sdi->conn,
	    (*config->scpi_dialect)[SCPI_CMD_GET_HORIZONTAL_DIV],
	    (int *)&config->num_xdivs) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_float(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_HORIZ_TRIGGERPOS],
			&tmp_float) != SR_OK)
		return SR_ERR;
	state->horiz_triggerpos = tmp_float /
		(((double) (*config->timebases)[state->timebase][0] /
		  (*config->timebases)[state->timebase][1]) * config->num_xdivs);
	state->horiz_triggerpos -= 0.5;
	state->horiz_triggerpos *= -1;

	if (scope_state_get_array_option(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SOURCE],
			config->trigger_sources, config->num_trigger_sources,
			&state->trigger_source) != SR_OK)
		return SR_ERR;

	if (scope_state_get_array_option(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SLOPE],
			config->trigger_slopes, config->num_trigger_slopes,
			&state->trigger_slope) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(sdi->conn,
			       (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_PATTERN],
			       &tmp_str) != SR_OK)
		return SR_ERR;
	strncpy(state->trigger_pattern,
		sr_scpi_unquote_string(tmp_str),
		MAX_ANALOG_CHANNEL_COUNT + MAX_DIGITAL_CHANNEL_COUNT);
	g_free(tmp_str);

	if (sr_scpi_get_string(sdi->conn,
			     (*config->scpi_dialect)[SCPI_CMD_GET_HIGH_RESOLUTION],
			     &tmp_str) != SR_OK)
		return SR_ERR;
	if (!strcmp("OFF", tmp_str))
		state->high_resolution = FALSE;
	else
		state->high_resolution = TRUE;
	g_free(tmp_str);

	if (sr_scpi_get_string(sdi->conn,
			     (*config->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION],
			     &tmp_str) != SR_OK)
		return SR_ERR;
	if (!strcmp("OFF", tmp_str))
		state->peak_detection = FALSE;
	else
		state->peak_detection = TRUE;
	g_free(tmp_str);

	if (hmo_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	sr_info("Fetching finished.");

	scope_state_dump(config, state);

	return SR_OK;
}

static struct scope_state *scope_state_new(const struct scope_config *config)
{
	struct scope_state *state;

	state = g_malloc0(sizeof(struct scope_state));
	state->analog_channels = g_malloc0_n(config->analog_channels,
			sizeof(struct analog_channel_state));
	state->digital_channels = g_malloc0_n(
			config->digital_channels, sizeof(gboolean));
	state->digital_pods = g_malloc0_n(config->digital_pods,
			sizeof(struct digital_pod_state));

	return state;
}

SR_PRIV void hmo_scope_state_free(struct scope_state *state)
{
	g_free(state->analog_channels);
	g_free(state->digital_channels);
	g_free(state->digital_pods);
	g_free(state);
}

SR_PRIV int hmo_init_device(struct sr_dev_inst *sdi)
{
	int model_index;
	unsigned int i, j, group;
	struct sr_channel *ch;
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	model_index = -1;

	/* Find the exact model. */
	for (i = 0; i < ARRAY_SIZE(scope_models); i++) {
		for (j = 0; scope_models[i].name[j]; j++) {
			if (!strcmp(sdi->model, scope_models[i].name[j])) {
				model_index = i;
				break;
			}
		}
		if (model_index != -1)
			break;
	}

	if (model_index == -1) {
		sr_dbg("Unsupported device.");
		return SR_ERR_NA;
	}

	/* Configure the number of PODs given the number of digital channels. */
	scope_models[model_index].digital_pods = scope_models[model_index].digital_channels / DIGITAL_CHANNELS_PER_POD;

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					scope_models[model_index].analog_channels);
	devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					 scope_models[model_index].digital_pods);
	if (!devc->analog_groups || !devc->digital_groups) {
		g_free(devc->analog_groups);
		g_free(devc->digital_groups);
		return SR_ERR_MALLOC;
	}

	/* Add analog channels. */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
			   (*scope_models[model_index].analog_names)[i]);

		devc->analog_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->analog_groups[i]->name = g_strdup(
			(char *)(*scope_models[model_index].analog_names)[i]);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups,
						   devc->analog_groups[i]);
	}

	/* Add digital channel groups. */
	ret = SR_OK;
	for (i = 0; i < scope_models[model_index].digital_pods; i++) {
		devc->digital_groups[i] = g_malloc0(sizeof(struct sr_channel_group));
		if (!devc->digital_groups[i]) {
			ret = SR_ERR_MALLOC;
			break;
		}
		devc->digital_groups[i]->name = g_strdup_printf("POD%d", i + 1);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				   devc->digital_groups[i]);
	}
	if (ret != SR_OK)
		return ret;

	/* Add digital channels. */
	for (i = 0; i < scope_models[model_index].digital_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
			   (*scope_models[model_index].digital_names)[i]);

		group = i / DIGITAL_CHANNELS_PER_POD;
		devc->digital_groups[group]->channels = g_slist_append(
			devc->digital_groups[group]->channels, ch);
	}

	devc->model_config = &scope_models[model_index];
	devc->samples_limit = 0;
	devc->frame_limit = 0;

	if (!(devc->model_state = scope_state_new(devc->model_config)))
		return SR_ERR_MALLOC;

	return SR_OK;
}

/* Queue data of one channel group, for later submission. */
SR_PRIV void hmo_queue_logic_data(struct dev_context *devc,
				  size_t group, GByteArray *pod_data)
{
	size_t size;
	GByteArray *store;
	uint8_t *logic_data;
	size_t idx, logic_step;

	/*
	 * Upon first invocation, allocate the array which can hold the
	 * combined logic data for all channels. Assume that each channel
	 * will yield an identical number of samples per receive call.
	 *
	 * As a poor man's safety measure: (Silently) skip processing
	 * for unexpected sample counts, and ignore samples for
	 * unexpected channel groups. Don't bother with complicated
	 * resize logic, considering that many models only support one
	 * pod, and the most capable supported models have two pods of
	 * identical size. We haven't yet seen any "odd" configuration.
	 */
	if (!devc->logic_data) {
		size = pod_data->len * devc->pod_count;
		store = g_byte_array_sized_new(size);
		memset(store->data, 0, size);
		store = g_byte_array_set_size(store, size);
		devc->logic_data = store;
	} else {
		store = devc->logic_data;
		size = store->len / devc->pod_count;
		if (group >= devc->pod_count)
			return;
	}

	/*
	 * Fold the data of the most recently received channel group into
	 * the storage, where data resides for all channels combined.
	 */
	logic_data = store->data;
	logic_data += group;
	logic_step = devc->pod_count;
	for (idx = 0; idx < pod_data->len; idx++) {
		*logic_data = pod_data->data[idx];
		logic_data += logic_step;
	}

	/* Truncate acquisition if a smaller number of samples has been requested. */
	if (devc->samples_limit > 0 && devc->logic_data->len > devc->samples_limit * devc->pod_count)
		devc->logic_data->len = devc->samples_limit * devc->pod_count;
}

/* Submit data for all channels, after the individual groups got collected. */
SR_PRIV void hmo_send_logic_packet(struct sr_dev_inst *sdi,
				   struct dev_context *devc)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (!devc->logic_data)
		return;

	logic.data = devc->logic_data->data;
	logic.length = devc->logic_data->len;
	logic.unitsize = devc->pod_count;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;

	sr_session_send(sdi, &packet);
}

/* Undo previous resource allocation. */
SR_PRIV void hmo_cleanup_logic_data(struct dev_context *devc)
{

	if (devc->logic_data) {
		g_byte_array_free(devc->logic_data, TRUE);
		devc->logic_data = NULL;
	}
	/*
	 * Keep 'pod_count'! It's required when more frames will be
	 * received, and does not harm when kept after acquisition.
	 */
}

SR_PRIV int hmo_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_channel *ch;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_datafeed_packet packet;
	GByteArray *data;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_logic logic;
	size_t group;

	(void)fd;
	(void)revents;

	data = NULL;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	/* Although this is correct in general, the USBTMC libusb implementation
	 * currently does not generate an event prior to the first read. Often
	 * it is ok to start reading just after the 50ms timeout. See bug #785.
	if (revents != G_IO_IN)
		return TRUE;
	*/

	ch = devc->current_channel->data;
	state = devc->model_state;

	/*
	 * Send "frame begin" packet upon reception of data for the
	 * first enabled channel.
	 */
	if (devc->current_channel == devc->enabled_channels)
		std_session_send_df_frame_begin(sdi);

	/*
	 * Pass on the received data of the channel(s).
	 */
	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);
			return TRUE;
		}

		packet.type = SR_DF_ANALOG;

		analog.data = data->data;
		analog.num_samples = data->len / sizeof(float);
		/* Truncate acquisition if a smaller number of samples has been requested. */
		if (devc->samples_limit > 0 && analog.num_samples > devc->samples_limit)
			analog.num_samples = devc->samples_limit;
		/* TODO: Use proper 'digits' value for this device (and its modes). */
		sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
		encoding.is_signed = TRUE;
		if (state->analog_channels[ch->index].probe_unit == 'V') {
			meaning.mq = SR_MQ_VOLTAGE;
			meaning.unit = SR_UNIT_VOLT;
		} else {
			meaning.mq = SR_MQ_CURRENT;
			meaning.unit = SR_UNIT_AMPERE;
		}
		meaning.channels = g_slist_append(NULL, ch);
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		devc->num_samples = data->len / sizeof(float);
		g_slist_free(meaning.channels);
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	case SR_CHANNEL_LOGIC:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);
			return TRUE;
		}

		/*
		 * If only data from the first pod is involved in the
		 * acquisition, then the raw input bytes can get passed
		 * forward for performance reasons. When the second pod
		 * is involved (either alone, or in combination with the
		 * first pod), then the received bytes need to be put
		 * into memory in such a layout that all channel groups
		 * get combined, and a unitsize larger than a single byte
		 * applies. The "queue" logic transparently copes with
		 * any such configuration. This works around the lack
		 * of support for "meaning" to logic data, which is used
		 * above for analog data.
		 */
		if (devc->pod_count == 1) {
			packet.type = SR_DF_LOGIC;
			logic.data = data->data;
			logic.length = data->len;
			/* Truncate acquisition if a smaller number of samples has been requested. */
			if (devc->samples_limit > 0 && logic.length > devc->samples_limit)
				logic.length = devc->samples_limit;
			logic.unitsize = 1;
			packet.payload = &logic;
			sr_session_send(sdi, &packet);
		} else {
			group = ch->index / DIGITAL_CHANNELS_PER_POD;
			hmo_queue_logic_data(devc, group, data);
		}

		devc->num_samples = data->len / devc->pod_count;
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	default:
		sr_err("Invalid channel type.");
		break;
	}

	/*
	 * Advance to the next enabled channel. When data for all enabled
	 * channels was received, then flush potentially queued logic data,
	 * and send the "frame end" packet.
	 */
	if (devc->current_channel->next) {
		devc->current_channel = devc->current_channel->next;
		hmo_request_data(sdi);
		return TRUE;
	}
	hmo_send_logic_packet(sdi, devc);

	/*
	 * Release the logic data storage after each frame. This copes
	 * with sample counts that differ in length per frame. -- Is
	 * this a real constraint when acquiring multiple frames with
	 * identical device settings?
	 */
	hmo_cleanup_logic_data(devc);

	std_session_send_df_frame_end(sdi);

	/*
	 * End of frame was reached. Stop acquisition after the specified
	 * number of frames or after the specified number of samples, or
	 * continue reception by starting over at the first enabled channel.
	 */
	if (++devc->num_frames >= devc->frame_limit || devc->num_samples >= devc->samples_limit) {
		sr_dev_acquisition_stop(sdi);
		hmo_cleanup_logic_data(devc);
	} else {
		devc->current_channel = devc->enabled_channels;
		hmo_request_data(sdi);
	}

	return TRUE;
}
