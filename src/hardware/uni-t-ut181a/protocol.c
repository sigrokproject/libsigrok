/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019-2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * This implementation uses protocol information which was provided by
 * the MIT licensed ut181a project. See Protocol.md for more details:
 *
 *   https://github.com/antage/ut181a/blob/master/Protocol.md
 */

#include <config.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "protocol.h"

/*
 * This driver depends on the user's enabling serial communication in
 * the multimeter's menu system: SETUP -> Communication -> ON. The BLE
 * adapter will shutdown within a short period of time when it's not
 * being communicated to, needs another power cycle to re-connect. The
 * USB cable does not suffer from such a constraint.
 *
 * Developer notes on the UT181A protocol:
 * - Serial communication over HID or BLE based "cables", UT-D09 or
 *   UT-D07A, bidirectional communication (UT-D04 won't do).
 * - UART frame format 8n1 at 9600 bps. Variable length DMM packets.
 * - DMM packet starts with a magic marker, followed by the length,
 *   followed by data bytes and terminated by the checksum field.
 *   The length includes the remainder of the frame. The checksum
 *   includes the length field as well. The checksum value is the
 *   16bit sum of all preceeding byte values.
 * - The meter has many features (live readings, saved measurements,
 *   recorded measurement series) with many additional attributes:
 *   relative, min/max/avg, peak, AC+DC, multiple temperature probes,
 *   COMP mode (PASS/FAIL). The protocol reflects this with highly
 *   variable responses, with differing response layouts including
 *   optional field presence.
 * - Frame field values are communicated in 8/16/32 bit integer as well
 *   as 32bit float formats in little endian presentation. Measurement
 *   values are represented by a combination of a float value and flags
 *   and a precision (digits count) and a text string which encodes the
 *   measured quantity including its flags and another scale factor
 *   (prefix reflecting the current range).
 * - Response frames often provide a set of values at the same time:
 *   There are multiple displays, like current and min/max/avg values,
 *   relative values including their reference and the absolute value,
 *   differences between probes, etc.
 * - The meter can hold multiple recordings with user assigned names,
 *   sample interval and duration, including interactive stop of a
 *   currently active recording. These recordings contain samples that
 *   were taken at a user specified interval.
 * - The meter can store a list of measurements, which get saved upon
 *   user requests, and can span arbitrary modes/functions/layouts per
 *   saved measurement. In contrast to recordings which keep their type
 *   of measurement across the set of samples.
 *
 * See https://github.com/antage/ut181a/blob/master/Protocol.md for a
 * detailled description of the meter's protocol. Some additional notes
 * in slightly reformatted layout for improved maintainability:
 * - "Range byte"
 *   0x00	Auto range
 *   0x01	60 mV	6 V	600 uA	60 mA	600 R	60 Hz	6 nF
 *   0x02	600 mV	60 V	6000 uA	600 mA	6 K	600 Hz	60 nF
 *   0x03		600V	(20A is: auto)	60 K	6 KHz	600 nF
 *   0x04		1000 V			600 K	60 KHz	6 uF
 *   0x05					6 M	600 KHz	60 uF
 *   0x06					60 M	6 MHz	600 uF
 *   0x07						60 MHz	6 mF
 *   0x08							60 mF
 *   ampere: 20A is auto, not user selectable
 *   continuity: 600 R, not user selectable
 *   conductivity: 60nS, not user selectable
 *   temperature: not user selectable
 *   diode: not user selectable
 *   frequency: all of the above ranges are available
 *   duty cycle, period: 60Hz to 60kHz, not user selectable beyond 60kHz
 * - DMM response packets in COMP mode (limits check, PASS/FAIL):
 *   - The device supports two limits (upper, lower) and several modes
 *     ("inner", "outer", "below", "above"). The result is boolean for
 *     PASS or FAIL.
 *   - Response packets are NORMAL MEASUREMENTs, with a MAIN value but
 *     without AUX1/AUX2/BAR. Plus some more fields after the bargraph
 *     unit field's position which are specific to COMP mode. Auto range
 *     is off (also in the display).
 *   - Example data for COMP mode responses:
 *     INNER +0mV +3.3mV PASS -- 00 00 03  33 33 53 40  00 00 00 00
 *     INNER +0mV +3.3mV FAIL -- 00 01 03  33 33 53 40  00 00 00 00
 *     INNER +1mV +3.3mV FAIL -- 00 01 03  33 33 53 40  00 00 80 3f
 *     OUTER +0mV +3.3mV PASS -- 01 00 03  33 33 53 40  00 00 00 00
 *     BELOW +30mV PASS       -- 02 00 03  00 00 f0 41
 *   - Extra fields:
 *     1 byte mode, can be 0 to 3 for INNER/OUTER/BELOW/ABOVE
 *     1 byte test result, bool failure, 0 is PASS, 1 is FAIL
 *     1 byte digits, *not* shifted as in other precision fields
 *     4 byte (always) high limit
 *     4 byte (conditional) low limit, not in all modes
 *
 * Implementation notes on this driver version:
 * - DMM channel assignment for measurement types:
 *   - normal: P1 main, P2 aux1, P3 aux2, P5 bar (as applicable)
 *   - relative: P1 relative, P2 reference, P3 absolute
 *   - min-max: P1 current, P2 maximum, P3 average, P4 minimum
 *   - peak: P2 maximum, P4 minimum
 *   - save/recording: P5 timestamp (in addition to the above)
 */

/*
 * TODO:
 * - General question: How many channels to export? An overlay with ever
 *   changing meanings? Or a multitude where values are sparse?
 * - Check how the PC side can _set_ the mode and range. Does mode
 *   selection depend on the physical knob? Would assume it does.
 *   The multitude of mode codes (some 70) and the lack of an apparent
 *   formula to them makes this enhancement tedious. Listing too many
 *   items in the "list" query could reduce usability.
 * - Add support for "COMP mode" (comparison, PASS/FAIL result).
 *   - How to express PASS/FAIL in the data feed submission? There is
 *     SR_UNIT_BOOLEAN but not a good MQ for envelope test results.
 *   - How to communicate limits to the session feed? COMP replies are
 *     normal measurements without aux1 and aux2. Is it appropriate to
 *     re-use DMM channels, or shall we add more of them?
 * - Communicate timestamps for saved measurements and recordings to the
 *   session feed.
 *   - There is SR_MQ_TIME and SR_MQFLAG_RELATIVE, and SR_UNIT_SECOND.
 *     Absolute time seems appropriate for save, relative (to the start
 *     of the recording) for recordings.
 *   - Unfortunately double data types are not fully operational, so we
 *     use float. Which is limited to 23 bits, thus can only span some
 *     100 days. But recordings can span longer periods when the sample
 *     interval is large.
 *   - Absolute times suffer from the 23bit limit (epoch time_t values
 *     require 32 bits these days). And they get presented as 1.5Gs,
 *     there seems to be no "date/time" flag or format.
 * - Dynamically allocate and re-allocate the record name table. There
 *   appears to be no limit of 20 recordings. The manual won't tell, but
 *   it's assumed that a few hundreds or thousands are supported (10K
 *   samples in total? that's a guess though).
 * - The PC side could initiate to save a live measurement. The command
 *   is there, it's just uncertain which SR_CONF_ key to use, DATALOG
 *   appears to enter/leave a period of recording, not a single shot.
 * - The PC side could start and stop recordings. But the start command
 *   requires a name, sample interval, and duration, but SR_CONF_DATALOG
 *   is just a boolean. Combining SR_CONF_LIMIT_SAMPLES, _DATALOG, et al
 *   raises the question which order applications will send configure
 *   requests for them.
 * - How to communicate the LOWPASS condition? PASS/FAIL results for
 *   COMP mode? Timestamps (absolute wall clock times)? High voltage,
 *   lead errors (probe plugs in ampere modes)?
 */

/*
 * Development HACK, to see data frame exchange at -l 2 without the
 * serial spew of -l 5. Also lets you concentrate on some of the code
 * paths which currently are most interesting during maintenance. :)
 */
#if UT181A_WITH_SER_ECHO
#  define FRAME_DUMP_LEVEL SR_LOG_WARN
#  define FRAME_DUMP_CALL sr_warn
#else
#  define FRAME_DUMP_LEVEL (SR_LOG_SPEW + 1)
#  define sr_nop(...) do { /* EMPTY */ } while (0)
#  define FRAME_DUMP_CALL sr_nop
#endif

#define FRAME_DUMP_RXDATA 0	/* UART level receive data. */
#define FRAME_DUMP_CSUM 0	/* Chunking, frame isolation. */
#define FRAME_DUMP_FRAME 0	/* DMM frames, including envelope. */
#define FRAME_DUMP_BYTES 0	/* DMM frame's payload data, "DMM packet". */
#define FRAME_DUMP_PARSE 1	/* Measurement value extraction. */
#define FRAME_DUMP_REMAIN 1	/* Unprocessed response data. */

/*
 * TODO Can we collapse several u16 modes in useful ways? Need we keep
 * them separate for "MQ+flags to mode" lookups, yet mark only some of
 * them for LIST result sets? Can't filter and need to provide them all
 * to the user? There are some 70-80 combinations. :-O
 *
 * Unfortunately there is no general pattern to these code numbers, or
 * when there is it's non-obvious. There are _some_ conventions, but also
 * exceptions, so that programmatic handling fails.
 *
 * TODO
 * - Factor out LOWPASS to a separate mode? At least derive an MQFLAG.
 */
static const struct mqopt_item ut181a_mqopts[] = {
	{
		SR_MQ_VOLTAGE, SR_MQFLAG_AC, {
			MODE_V_AC, MODE_V_AC_REL,
			MODE_mV_AC, MODE_mV_AC_REL,
			MODE_V_AC_PEAK, MODE_mV_AC_PEAK,
			MODE_V_AC_LOWPASS, MODE_V_AC_LOWPASS_REL,
			0,
		},
	},
	{
		SR_MQ_VOLTAGE, SR_MQFLAG_DC, {
			MODE_V_DC, MODE_V_DC_REL,
			MODE_mV_DC, MODE_mV_DC_REL,
			MODE_V_DC_PEAK, MODE_mV_DC_PEAK,
			0,
		},
	},
	{
		SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_AC, {
			MODE_V_DC_ACDC, MODE_V_DC_ACDC_REL,
			MODE_mV_AC_ACDC, MODE_mV_AC_ACDC_REL,
			0,
		},
	},
	{
		SR_MQ_GAIN, 0, {
			MODE_V_AC_dBV, MODE_V_AC_dBV_REL,
			MODE_V_AC_dBm, MODE_V_AC_dBm_REL,
			0,
		},
	},
	{
		SR_MQ_CURRENT, SR_MQFLAG_AC, {
			MODE_A_AC, MODE_A_AC_REL,
			MODE_A_AC_PEAK,
			MODE_mA_AC, MODE_mA_AC_REL,
			MODE_mA_AC_PEAK,
			MODE_uA_AC, MODE_uA_AC_REL,
			MODE_uA_AC_PEAK,
			0,
		},
	},
	{
		SR_MQ_CURRENT, SR_MQFLAG_DC, {
			MODE_A_DC, MODE_A_DC_REL,
			MODE_A_DC_PEAK,
			MODE_mA_DC, MODE_mA_DC_REL,
			MODE_uA_DC, MODE_uA_DC_REL,
			MODE_uA_DC_PEAK,
			0,
		},
	},
	{
		SR_MQ_CURRENT, SR_MQFLAG_DC | SR_MQFLAG_AC, {
			MODE_A_DC_ACDC, MODE_A_DC_ACDC_REL,
			MODE_mA_DC_ACDC, MODE_mA_DC_ACDC_REL,
			MODE_uA_DC_ACDC, MODE_uA_DC_ACDC_REL,
			MODE_mA_DC_ACDC_PEAK,
			0,
		},
	},
	{
		SR_MQ_RESISTANCE, 0, {
			MODE_RES, MODE_RES_REL, 0,
		},
	},
	{
		SR_MQ_CONDUCTANCE, 0, {
			MODE_COND, MODE_COND_REL, 0,
		},
	},
	{
		SR_MQ_CONTINUITY, 0, {
			MODE_CONT_SHORT, MODE_CONT_OPEN, 0,
		},
	},
	{
		SR_MQ_VOLTAGE, SR_MQFLAG_DIODE | SR_MQFLAG_DC, {
			MODE_DIODE, MODE_DIODE_ALARM, 0,
		},
	},
	{
		SR_MQ_CAPACITANCE, 0, {
			MODE_CAP, MODE_CAP_REL, 0,
		},
	},
	{
		SR_MQ_FREQUENCY, 0, {
			MODE_FREQ, MODE_FREQ_REL,
			MODE_V_AC_Hz, MODE_mV_AC_Hz,
			MODE_A_AC_Hz, MODE_mA_AC_Hz, MODE_uA_AC_Hz,
			0,
		},
	},
	{
		SR_MQ_DUTY_CYCLE, 0, {
			MODE_DUTY, MODE_DUTY_REL, 0,
		},
	},
	{
		SR_MQ_PULSE_WIDTH, 0, {
			MODE_PULSEWIDTH, MODE_PULSEWIDTH_REL, 0,
		},
	},
	{
		SR_MQ_TEMPERATURE, 0, {
			MODE_TEMP_C_T1_and_T2, MODE_TEMP_C_T1_and_T2_REL,
			MODE_TEMP_C_T1_minus_T2, MODE_TEMP_F_T1_and_T2,
			MODE_TEMP_C_T2_and_T1, MODE_TEMP_C_T2_and_T1_REL,
			MODE_TEMP_C_T2_minus_T1,
			MODE_TEMP_F_T1_and_T2_REL, MODE_TEMP_F_T1_minus_T2,
			MODE_TEMP_F_T2_and_T1, MODE_TEMP_F_T2_and_T1_REL,
			MODE_TEMP_F_T2_minus_T1,
			0,
		},
	},
};

SR_PRIV const struct mqopt_item *ut181a_get_mqitem_from_mode(uint16_t mode)
{
	size_t mq_idx, mode_idx;
	const struct mqopt_item *item;

	for (mq_idx = 0; mq_idx < ARRAY_SIZE(ut181a_mqopts); mq_idx++) {
		item = &ut181a_mqopts[mq_idx];
		for (mode_idx = 0; mode_idx < ARRAY_SIZE(item->modes); mode_idx++) {
			if (!item->modes[mode_idx])
				break;
			if (item->modes[mode_idx] != mode)
				continue;
			/* Found a matching mode. */
			return item;
		}
	}
	return NULL;
}

SR_PRIV uint16_t ut181a_get_mode_from_mq_flags(enum sr_mq mq, enum sr_mqflag mqflags)
{
	size_t mq_idx;
	const struct mqopt_item *item;

	for (mq_idx = 0; mq_idx < ARRAY_SIZE(ut181a_mqopts); mq_idx++) {
		item = &ut181a_mqopts[mq_idx];
		if (mq != item->mq)
			continue;
		/* TODO Need finer checks? Masked? */
		if (mqflags != item->mqflags)
			continue;
		return item->modes[0];
	}
	return 0;
}

SR_PRIV GVariant *ut181a_get_mq_flags_list_item(enum sr_mq mq, enum sr_mqflag mqflag)
{
	GVariant *arr[2], *tuple;

	arr[0] = g_variant_new_uint32(mq);
	arr[1] = g_variant_new_uint64(mqflag);
	tuple = g_variant_new_tuple(arr, ARRAY_SIZE(arr));

	return tuple;
}

SR_PRIV GVariant *ut181a_get_mq_flags_list(void)
{
	GVariantBuilder gvb;
	GVariant *tuple, *list;
	size_t i;
	const struct mqopt_item *item;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
	for (i = 0; i < ARRAY_SIZE(ut181a_mqopts); i++) {
		item = &ut181a_mqopts[i];
		tuple = ut181a_get_mq_flags_list_item(item->mq, item->mqflags);
		g_variant_builder_add_value(&gvb, tuple);
	}
	list = g_variant_builder_end(&gvb);

	return list;
}

/*
 * See the Protocol.md document's "Range byte" section. Value 0 is said
 * to communicate "auto range", while values 1-8 are said to communicate
 * specific ranges which depend on the meter's current function. Yet
 * there is another misc flag for auto range.
 *
 * From this information, and observed packet content, it is assumed
 * that the following logic applies:
 * - Measurements (response packets) carry the "auto" flag, _and_ a
 *   "range" byte, to provide the information that auto ranging was in
 *   effect, and which specific range the automatic detection picked.
 * - "Set range" requests can request a specific range (values 1-8), or
 *   switch to auto range (value 0).
 *
 * This driver implementation returns non-settable string literals for
 * modes where auto ranging is not user adjustable (high current, diode,
 * continuity, conductivity, temperature). Setup requests get rejected.
 * (The local user interface neither responds to RANGE button presses.)
 */
static const char *range_auto = "auto";
static const char *ranges_volt_mv[] = {
	"60mV", "600mV", NULL,
};
static const char *ranges_volt_v[] = {
	"6V", "60V", "600V", "1000V", NULL,
};
static const char *ranges_volt_diode[] = {
	/* Diode is always auto, not user adjustable. */
	"3.0V", NULL,
};
static const char *ranges_amp_ua[] = {
	"600uA", "6000uA", NULL,
};
static const char *ranges_amp_ma[] = {
	"60mA", "600mA", NULL,
};
static const char *ranges_amp_a[] = {
	/* The 'A' range is always 20A (in the display, manual says 10A). */
	"20A", NULL,
};
static const char *ranges_ohm_res[] = {
	/* TODO
	 * Prefer "Ohm" (or "R" for sub-kilo ranges) instead? We try to
	 * keep usability in other places (micro), too, by letting users
	 * type regular non-umlaut text, and avoiding encoding issues.
	 */
	"600Ω", "6kΩ", "60kΩ", "600kΩ", "6MΩ", "60MΩ", NULL,
};
static const char *ranges_ohm_600[] = {
	/* Continuity is always 600R, not user adjustable. */
	"600Ω", NULL,
};
static const char *ranges_cond[] = {
	/* Conductivity is always 60nS, not user adjustable. */
	"60nS", NULL,
};
static const char *ranges_capa[] = {
	"6nF", "60nF", "600nF", "6uF", "60uF", "600uF", "6mF", "600mF", NULL,
};
static const char *ranges_freq_full[] = {
	"60Hz", "600Hz", "6kHz", "60kHz", "600kHz", "6MHz", "60MHz", NULL,
};
static const char *ranges_freq_60khz[] = {
	/* Duty cycle and period only support up to 60kHz. */
	"60Hz", "600Hz", "6kHz", "60kHz", NULL,
};
static const char *ranges_temp_c[] = {
	/* Temperature always is up to 1000 degree C, not user adjustable. */
	"1000°C", NULL,
};
static const char *ranges_temp_f[] = {
	/* Temperature always is up to 1832 F, not user adjustable. */
	"1832F", NULL,
};

static void ut181a_add_ranges_list(GVariantBuilder *b, const char **l)
{
	const char *range;

	while (l && *l && **l) {
		range = *l++;
		g_variant_builder_add(b, "s", range);
	}
}

SR_PRIV GVariant *ut181a_get_ranges_list(void)
{
	GVariantBuilder gvb;
	GVariant *list;

	/* Also list those ranges which cannot get set? */
#define WITH_RANGE_LIST_FIXED 1

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add(&gvb, "s", range_auto);
	ut181a_add_ranges_list(&gvb, ranges_volt_mv);
	ut181a_add_ranges_list(&gvb, ranges_volt_v);
	(void)ranges_volt_diode;
	ut181a_add_ranges_list(&gvb, ranges_amp_ua);
	ut181a_add_ranges_list(&gvb, ranges_amp_ma);
#if WITH_RANGE_LIST_FIXED
	ut181a_add_ranges_list(&gvb, ranges_amp_a);
#else
	(void)ranges_amp_a;
#endif
	ut181a_add_ranges_list(&gvb, ranges_ohm_res);
	(void)ranges_ohm_600;
	ut181a_add_ranges_list(&gvb, ranges_cond);
	ut181a_add_ranges_list(&gvb, ranges_capa);
	ut181a_add_ranges_list(&gvb, ranges_freq_full);
	(void)ranges_freq_60khz;
#if WITH_RANGE_LIST_FIXED
	ut181a_add_ranges_list(&gvb, ranges_temp_c);
	ut181a_add_ranges_list(&gvb, ranges_temp_f);
#else
	(void)ranges_temp_c;
	(void)ranges_temp_f;
#endif
	list = g_variant_builder_end(&gvb);

	return list;
}

SR_PRIV const char *ut181a_get_range_from_packet_bytes(struct dev_context *devc)
{
	uint16_t mode;
	uint8_t range;
	gboolean is_auto;
	const char **ranges;

	if (!devc)
		return NULL;
	mode = devc->info.meas_head.mode;
	range = devc->info.meas_head.range;
	is_auto = devc->info.meas_head.is_auto_range;

	/* Handle the simple cases of "auto" and out of (absolute) limits. */
	if (is_auto)
		return range_auto;
	if (!mode)
		return NULL;
	if (!range)
		return range_auto;
	if (range > MAX_RANGE_INDEX)
		return NULL;

	/* Lookup the list of ranges which depend on the meter's current mode. */
	switch (mode) {

	case MODE_V_AC:
	case MODE_V_AC_REL:
	case MODE_V_AC_Hz:
	case MODE_V_AC_PEAK:
	case MODE_V_AC_LOWPASS:
	case MODE_V_AC_LOWPASS_REL:
	case MODE_V_AC_dBV:
	case MODE_V_AC_dBV_REL:
	case MODE_V_AC_dBm:
	case MODE_V_AC_dBm_REL:
	case MODE_V_DC:
	case MODE_V_DC_REL:
	case MODE_V_DC_ACDC:
	case MODE_V_DC_ACDC_REL:
	case MODE_V_DC_PEAK:
		ranges = ranges_volt_v;
		break;
	case MODE_mV_AC:
	case MODE_mV_AC_REL:
	case MODE_mV_AC_Hz:
	case MODE_mV_AC_PEAK:
	case MODE_mV_AC_ACDC:
	case MODE_mV_AC_ACDC_REL:
	case MODE_mV_DC:
	case MODE_mV_DC_REL:
	case MODE_mV_DC_PEAK:
		ranges = ranges_volt_mv;
		break;
	case MODE_RES:
	case MODE_RES_REL:
		ranges = ranges_ohm_res;
		break;
	case MODE_CONT_SHORT:
	case MODE_CONT_OPEN:
		ranges = ranges_ohm_600;
		break;
	case MODE_COND:
	case MODE_COND_REL:
		ranges = ranges_cond;
		break;
	case MODE_CAP:
	case MODE_CAP_REL:
		ranges = ranges_capa;
		break;
	case MODE_FREQ:
	case MODE_FREQ_REL:
		ranges = ranges_freq_full;
		break;
	case MODE_DUTY:
	case MODE_DUTY_REL:
	case MODE_PULSEWIDTH:
	case MODE_PULSEWIDTH_REL:
		ranges = ranges_freq_60khz;
		break;
	case MODE_uA_DC:
	case MODE_uA_DC_REL:
	case MODE_uA_DC_ACDC:
	case MODE_uA_DC_ACDC_REL:
	case MODE_uA_DC_PEAK:
	case MODE_uA_AC:
	case MODE_uA_AC_REL:
	case MODE_uA_AC_Hz:
	case MODE_uA_AC_PEAK:
		ranges = ranges_amp_ua;
		break;
	case MODE_mA_DC:
	case MODE_mA_DC_REL:
	case MODE_mA_DC_ACDC:
	case MODE_mA_DC_ACDC_REL:
	case MODE_mA_DC_ACDC_PEAK:
	case MODE_mA_AC:
	case MODE_mA_AC_REL:
	case MODE_mA_AC_Hz:
	case MODE_mA_AC_PEAK:
		ranges = ranges_amp_ma;
		break;

	/* Some modes are neither flexible nor adjustable. */
	case MODE_TEMP_C_T1_and_T2:
	case MODE_TEMP_C_T1_and_T2_REL:
	case MODE_TEMP_C_T2_and_T1:
	case MODE_TEMP_C_T2_and_T1_REL:
	case MODE_TEMP_C_T1_minus_T2:
	case MODE_TEMP_C_T2_minus_T1:
		ranges = ranges_temp_c;
		break;
	case MODE_TEMP_F_T1_and_T2:
	case MODE_TEMP_F_T1_and_T2_REL:
	case MODE_TEMP_F_T2_and_T1:
	case MODE_TEMP_F_T2_and_T1_REL:
	case MODE_TEMP_F_T1_minus_T2:
	case MODE_TEMP_F_T2_minus_T1:
		ranges = ranges_temp_f;
		break;
	/* Diode, always 3V. */
	case MODE_DIODE:
	case MODE_DIODE_ALARM:
		ranges = ranges_volt_diode;
		break;
	/* High current (A range). Always 20A. */
	case MODE_A_DC:
	case MODE_A_DC_REL:
	case MODE_A_DC_ACDC:
	case MODE_A_DC_ACDC_REL:
	case MODE_A_DC_PEAK:
	case MODE_A_AC:
	case MODE_A_AC_REL:
	case MODE_A_AC_Hz:
	case MODE_A_AC_PEAK:
		ranges = ranges_amp_a;
		break;

	/* Unknown mode? Programming error? */
	default:
		return NULL;
	}

	/* Lookup the range in the list of the mode's ranges. */
	while (ranges && *ranges && **ranges && --range > 0) {
		ranges++;
	}
	if (!ranges || !*ranges || !**ranges)
		return NULL;
	return *ranges;
}

SR_PRIV int ut181a_set_range_from_text(const struct sr_dev_inst *sdi, const char *text)
{
	struct dev_context *devc;
	uint16_t mode;
	const char **ranges;
	uint8_t range;

	/* We must have determined the meter's current mode first. */
	if (!sdi)
		return SR_ERR_ARG;
	if (!text || !*text)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	mode = devc->info.meas_head.mode;
	if (!mode)
		return SR_ERR_ARG;

	/* Handle the simple case of "auto" caller spec. */
	if (strcmp(text, range_auto) == 0) {
		range = 0;
		return ut181a_send_cmd_setmode(sdi->conn, range);
	}

	/* Lookup the list of ranges which depend on the meter's current mode. */
	switch (mode) {

	/* Map "user servicable" modes to their respective ranges list. */
	case MODE_V_AC:
	case MODE_V_AC_REL:
	case MODE_V_AC_Hz:
	case MODE_V_AC_PEAK:
	case MODE_V_AC_LOWPASS:
	case MODE_V_AC_LOWPASS_REL:
	case MODE_V_AC_dBV:
	case MODE_V_AC_dBV_REL:
	case MODE_V_AC_dBm:
	case MODE_V_AC_dBm_REL:
	case MODE_V_DC:
	case MODE_V_DC_REL:
	case MODE_V_DC_ACDC:
	case MODE_V_DC_ACDC_REL:
	case MODE_V_DC_PEAK:
		ranges = ranges_volt_v;
		break;
	case MODE_mV_AC:
	case MODE_mV_AC_REL:
	case MODE_mV_AC_Hz:
	case MODE_mV_AC_PEAK:
	case MODE_mV_AC_ACDC:
	case MODE_mV_AC_ACDC_REL:
	case MODE_mV_DC:
	case MODE_mV_DC_REL:
	case MODE_mV_DC_PEAK:
		ranges = ranges_volt_mv;
		break;
	case MODE_RES:
	case MODE_RES_REL:
		ranges = ranges_ohm_res;
		break;
	case MODE_CAP:
	case MODE_CAP_REL:
		ranges = ranges_capa;
		break;
	case MODE_FREQ:
	case MODE_FREQ_REL:
		ranges = ranges_freq_full;
		break;
	case MODE_DUTY:
	case MODE_DUTY_REL:
	case MODE_PULSEWIDTH:
	case MODE_PULSEWIDTH_REL:
		ranges = ranges_freq_60khz;
		break;
	case MODE_uA_DC:
	case MODE_uA_DC_REL:
	case MODE_uA_DC_ACDC:
	case MODE_uA_DC_ACDC_REL:
	case MODE_uA_DC_PEAK:
	case MODE_uA_AC:
	case MODE_uA_AC_REL:
	case MODE_uA_AC_Hz:
	case MODE_uA_AC_PEAK:
		ranges = ranges_amp_ua;
		break;
	case MODE_mA_DC:
	case MODE_mA_DC_REL:
	case MODE_mA_DC_ACDC:
	case MODE_mA_DC_ACDC_REL:
	case MODE_mA_DC_ACDC_PEAK:
	case MODE_mA_AC:
	case MODE_mA_AC_REL:
	case MODE_mA_AC_Hz:
	case MODE_mA_AC_PEAK:
		ranges = ranges_amp_ma;
		break;

	/*
	 * Some modes use fixed ranges. Accept their specs or refuse to
	 * set a specific range? The meter's UI refuses MANUAL mode and
	 * remains in AUTO mode. So do we here.
	 */
	case MODE_CONT_SHORT:
	case MODE_CONT_OPEN:
		return SR_ERR_NA;
		ranges = ranges_ohm_600;
		break;
	case MODE_COND:
	case MODE_COND_REL:
		return SR_ERR_NA;
		ranges = ranges_cond;
		break;
	case MODE_TEMP_C_T1_and_T2:
	case MODE_TEMP_C_T1_and_T2_REL:
	case MODE_TEMP_C_T2_and_T1:
	case MODE_TEMP_C_T2_and_T1_REL:
	case MODE_TEMP_C_T1_minus_T2:
	case MODE_TEMP_C_T2_minus_T1:
		return SR_ERR_NA;
		ranges = ranges_temp_c;
		break;
	case MODE_TEMP_F_T1_and_T2:
	case MODE_TEMP_F_T1_and_T2_REL:
	case MODE_TEMP_F_T2_and_T1:
	case MODE_TEMP_F_T2_and_T1_REL:
	case MODE_TEMP_F_T1_minus_T2:
	case MODE_TEMP_F_T2_minus_T1:
		return SR_ERR_NA;
		ranges = ranges_temp_f;
		break;
	/* Diode, always 3V. */
	case MODE_DIODE:
	case MODE_DIODE_ALARM:
		return SR_ERR_NA;
		ranges = ranges_volt_diode;
		break;
	/* High current (A range). Always 20A. */
	case MODE_A_DC:
	case MODE_A_DC_REL:
	case MODE_A_DC_ACDC:
	case MODE_A_DC_ACDC_REL:
	case MODE_A_DC_PEAK:
	case MODE_A_AC:
	case MODE_A_AC_REL:
	case MODE_A_AC_Hz:
	case MODE_A_AC_PEAK:
		return SR_ERR_NA;
		ranges = ranges_amp_a;
		break;

	/* Unknown mode? Programming error? */
	default:
		return SR_ERR_BUG;
	}

	/* Lookup the range in the list of the mode's ranges. */
	range = 1;
	while (ranges && *ranges && **ranges) {
		if (strcmp(*ranges, text) != 0) {
			range++;
			ranges++;
			continue;
		}
		return ut181a_send_cmd_setrange(sdi->conn, range);
	}
	return SR_ERR_ARG;
}

/**
 * Parse a unit text into scale factor, MQ and flags, and unit.
 *
 * @param[out] mqs The scale/MQ/unit details to fill in.
 * @param[in] text The DMM's "unit text" (string label).
 *
 * @returns SR_OK upon success, SR_ERR_* upon error.
 *
 * UT181A unit text strings encode several details: They start with an
 * optional prefix (which communicates a scale factor), specify the unit
 * of the measured value (which hints towards the measured quantity),
 * and carry optional attributes (which MQ flags can get derived from).
 *
 * See unit.rs for the list of known input strings. Though there are
 * unexpected differences:
 * - \u{FFFD}C/F instead of 0xb0 for degree (local platform conversion?)
 * - 'u' seems to be used for micro, good (no 'micro' umlaut involved)
 * - '~' (tilde, 0x7e) for Ohm
 *
 * Prefixes: p n u m '' k M G
 *
 * Units:
 * - F Farad (m u n)
 * - dBV, dBm (no prefix)
 * - ~ (tilde, Ohm) (- k M)
 * - S Siemens (n)
 * - % percent (no prefix)
 * - s seconds (m)
 * - Hz Hertz (- k M)
 * - xC, xF degree (no prefix)
 *
 * Units with Flags:
 * - Aac+dc ampere AC+DC (- m u)
 * - AAC ampere AC (- m u)
 * - ADC ampere DC (- m u)
 * - Vac+dc volt AC+DC (- m)
 * - VAC volt AC (- m)
 * - VDC volt DC (- m)
 */
static int ut181a_get_mq_details_from_text(struct mq_scale_params *mqs, const char *text)
{
	char scale_char;
	int scale;
	enum sr_mq mq;
	enum sr_mqflag mqflags;
	enum sr_unit unit;

	if (!mqs)
		return SR_ERR_ARG;
	memset(mqs, 0, sizeof(*mqs));

	/* Start from unknown state, no modifiers. */
	scale = 0;
	unit = 0;
	mq = 0;
	mqflags = 0;

	/* Derive the scale factor from the optional prefix. */
	scale_char = *text++;
	if (scale_char == 'p')
		scale = -12;
	else if (scale_char == 'n')
		scale = -9;
	else if (scale_char == 'u')
		scale = -6;
	else if (scale_char == 'm')
		scale = -3;
	else if (scale_char == 'k')
		scale = +3;
	else if (scale_char == 'M')
		scale = +6;
	else if (scale_char == 'G')
		scale = +9;
	else
		text--;

	/* Guess the MQ (and flags) from the unit text. */
	if (g_str_has_prefix(text, "F")) {
		text += strlen("F");
		unit = SR_UNIT_FARAD;
		if (!mq)
			mq = SR_MQ_CAPACITANCE;
	} else if (g_str_has_prefix(text, "dBV")) {
		text += strlen("dBV");
		unit = SR_UNIT_DECIBEL_VOLT;
		if (!mq)
			mq = SR_MQ_GAIN;
	} else if (g_str_has_prefix(text, "dBm")) {
		text += strlen("dBm");
		unit = SR_UNIT_DECIBEL_MW;
		if (!mq)
			mq = SR_MQ_GAIN;
	} else if (g_str_has_prefix(text, "~")) {
		text += strlen("~");
		unit = SR_UNIT_OHM;
		if (!mq)
			mq = SR_MQ_RESISTANCE;
	} else if (g_str_has_prefix(text, "S")) {
		text += strlen("S");
		unit = SR_UNIT_SIEMENS;
		if (!mq)
			mq = SR_MQ_CONDUCTANCE;
	} else if (g_str_has_prefix(text, "%")) {
		text += strlen("%");
		unit = SR_UNIT_PERCENTAGE;
		if (!mq)
			mq = SR_MQ_DUTY_CYCLE;
	} else if (g_str_has_prefix(text, "s")) {
		text += strlen("s");
		unit = SR_UNIT_SECOND;
		if (!mq)
			mq = SR_MQ_PULSE_WIDTH;
	} else if (g_str_has_prefix(text, "Hz")) {
		text += strlen("Hz");
		unit = SR_UNIT_HERTZ;
		if (!mq)
			mq = SR_MQ_FREQUENCY;
	} else if (g_str_has_prefix(text, "\xb0" "C")) {
		text += strlen("\xb0" "C");
		unit = SR_UNIT_CELSIUS;
		if (!mq)
			mq = SR_MQ_TEMPERATURE;
	} else if (g_str_has_prefix(text, "\xb0" "F")) {
		text += strlen("\xb0" "F");
		unit = SR_UNIT_FAHRENHEIT;
		if (!mq)
			mq = SR_MQ_TEMPERATURE;
	} else if (g_str_has_prefix(text, "A")) {
		text += strlen("A");
		unit = SR_UNIT_AMPERE;
		if (!mq)
			mq = SR_MQ_CURRENT;
	} else if (g_str_has_prefix(text, "V")) {
		text += strlen("V");
		unit = SR_UNIT_VOLT;
		if (!mq)
			mq = SR_MQ_VOLTAGE;
	} else if (g_str_has_prefix(text, "timestamp")) {
		/*
		 * The meter never provides this "timestamp" label,
		 * but the driver re-uses common logic here to have
		 * the MQ details filled in for save/record stamps.
		 */
		text += strlen("timestamp");
		unit = SR_UNIT_SECOND;
		if (!mq)
			mq = SR_MQ_TIME;
	}

	/* Amend MQ flags from an optional suffix. */
	if (g_str_has_prefix(text, "ac+dc")) {
		text += strlen("ac+dc");
		mqflags |= SR_MQFLAG_AC | SR_MQFLAG_DC;
	} else if (g_str_has_prefix(text, "AC")) {
		text += strlen("AC");
		mqflags |= SR_MQFLAG_AC;
	} else if (g_str_has_prefix(text, "DC")) {
		text += strlen("DC");
		mqflags |= SR_MQFLAG_DC;
	}

	/* Put all previously determined details into the container. */
	mqs->scale = scale;
	mqs->mq = mq;
	mqs->mqflags = mqflags;
	mqs->unit = unit;

	return SR_OK;
}

/*
 * Break down a packed 32bit timestamp presentation, and create an epoch
 * value from it. The UT181A protocol encodes timestamps in a 32bit value:
 *
 *   [5:0] year - 2000
 *   [9:6] month
 *   [14:10] mday
 *   [19:15] hour
 *   [25:20] min
 *   [31:26] sec
 *
 * TODO Find a portable and correct conversion helper. The mktime() API
 * is said to involve timezone details, and modify the environment. Is
 * strftime("%s") a better approach? Until then mktime() might be good
 * enough an approach, assuming that the meter will be set to the user's
 * local time.
 */
static time_t ut181a_get_epoch_for_timestamp(uint32_t ts)
{
	struct tm t;

	memset(&t, 0, sizeof(t));
	t.tm_year = ((ts >> 0) & 0x3f) + 2000 - 1900;
	t.tm_mon = ((ts >> 6) & 0x0f) - 1;
	t.tm_mday = ((ts >> 10) & 0x1f);
	t.tm_hour = ((ts >> 15) & 0x1f);
	t.tm_min = ((ts >> 20) & 0x3f);
	t.tm_sec = ((ts >> 26) & 0x3f);
	t.tm_isdst = -1;

	return mktime(&t);
}

/**
 * Calculate UT181A specific checksum for serial data frame.
 *
 * @param[in] data The payload bytes to calculate the checksum for.
 * @param[in] dlen The number of payload bytes.
 *
 * @returns The checksum value.
 *
 * On the wire the checksum covers all fields after the magic and before
 * the checksum. In other words the checksum covers the length field and
 * the payload bytes.
 */
static uint16_t ut181a_checksum(const uint8_t *data, size_t dlen)
{
	uint16_t cs;

	cs = 0;
	while (dlen-- > 0)
		cs += *data++;

	return cs;
}

/**
 * Send payload bytes via serial comm, add frame envelope and transmit.
 *
 * @param[in] serial Serial port.
 * @param[in] data Payload bytes.
 * @param[in] dlen Payload length.
 *
 * @returns >= 0 upon success, negative upon failure (SR_ERR codes)
 */
static int ut181a_send_frame(struct sr_serial_dev_inst *serial,
	const uint8_t *data, size_t dlen)
{
	uint8_t frame_buff[SEND_BUFF_SIZE];
	size_t frame_off;
	const uint8_t *cs_data;
	size_t cs_dlen;
	uint16_t cs_value;
	int ret;

	if (FRAME_DUMP_BYTES && sr_log_loglevel_get() >= FRAME_DUMP_LEVEL) {
		GString *spew;
		spew = sr_hexdump_new(data, dlen);
		FRAME_DUMP_CALL("TX payload, %zu bytes: %s", dlen, spew->str);
		sr_hexdump_free(spew);
	}

	/*
	 * The frame buffer must hold the magic and length and payload
	 * bytes and checksum. Check for the available space.
	 */
	if (dlen > sizeof(frame_buff) - 3 * sizeof(uint16_t)) {
		return SR_ERR_ARG;
	}

	/*
	 * Create a frame for the payload bytes. The length field's value
	 * also includes the checksum field (spans the remainder of the
	 * frame). The checksum covers everything between the magic and
	 * the checksum field.
	 */
	frame_off = 0;
	WL16(&frame_buff[frame_off], FRAME_MAGIC);
	frame_off += sizeof(uint16_t);
	WL16(&frame_buff[frame_off], dlen + sizeof(uint16_t));
	frame_off += sizeof(uint16_t);
	memcpy(&frame_buff[frame_off], data, dlen);
	frame_off += dlen;
	cs_data = &frame_buff[sizeof(uint16_t)];
	cs_dlen = frame_off - sizeof(uint16_t);
	cs_value = ut181a_checksum(cs_data, cs_dlen);
	WL16(&frame_buff[frame_off], cs_value);
	frame_off += sizeof(uint16_t);

	if (FRAME_DUMP_FRAME && sr_log_loglevel_get() >= FRAME_DUMP_LEVEL) {
		GString *spew;
		spew = sr_hexdump_new(frame_buff, frame_off);
		FRAME_DUMP_CALL("TX frame, %zu bytes: %s", frame_off, spew->str);
		sr_hexdump_free(spew);
	}

	ret = serial_write_blocking(serial, frame_buff, frame_off, SEND_TO_MS);
	if (ret < 0)
		return ret;

	return SR_OK;
}

/* Construct and transmit "set mode" command. */
SR_PRIV int ut181a_send_cmd_setmode(struct sr_serial_dev_inst *serial, uint16_t mode)
{
	uint8_t cmd[sizeof(uint8_t) + sizeof(uint16_t)];
	size_t cmd_off;

	cmd_off = 0;
	cmd[cmd_off++] = CMD_CODE_SET_MODE;
	WL16(&cmd[cmd_off], mode);
	cmd_off += sizeof(uint16_t);

	return ut181a_send_frame(serial, cmd, cmd_off);
}

/* Construct and transmit "set range" command. */
SR_PRIV int ut181a_send_cmd_setrange(struct sr_serial_dev_inst *serial, uint8_t range)
{
	uint8_t cmd[sizeof(uint8_t) + sizeof(uint8_t)];
	size_t cmd_off;

	cmd_off = 0;
	cmd[cmd_off++] = CMD_CODE_SET_RANGE;
	cmd[cmd_off++] = range;

	return ut181a_send_frame(serial, cmd, cmd_off);
}

/* Construct and transmit "monitor on/off" command. */
SR_PRIV int ut181a_send_cmd_monitor(struct sr_serial_dev_inst *serial, gboolean on)
{
	uint8_t cmd[sizeof(uint8_t) + sizeof(uint8_t)];
	size_t cmd_off;

	cmd_off = 0;
	cmd[cmd_off++] = CMD_CODE_SET_MONITOR;
	cmd[cmd_off++] = on ? 1 : 0;

	return ut181a_send_frame(serial, cmd, cmd_off);
}

/* Construct and transmit "get saved measurements count" command. */
SR_PRIV int ut181a_send_cmd_get_save_count(struct sr_serial_dev_inst *serial)
{
	uint8_t cmd;

	cmd = CMD_CODE_GET_SAVED_COUNT;
	return ut181a_send_frame(serial, &cmd, sizeof(cmd));
}

/*
 * Construct and transmit "get saved measurement value" command.
 * Important: Callers use 0-based index, protocol needs 1-based index.
 */
SR_PRIV int ut181a_send_cmd_get_saved_value(struct sr_serial_dev_inst *serial, size_t idx)
{
	uint8_t cmd[sizeof(uint8_t) + sizeof(uint16_t)];
	size_t cmd_off;

	cmd_off = 0;
	cmd[cmd_off++] = CMD_CODE_GET_SAVED_MEAS;
	WL16(&cmd[cmd_off], idx + 1);
	cmd_off += sizeof(uint16_t);

	return ut181a_send_frame(serial, cmd, sizeof(cmd));
}

/* Construct and transmit "get recordings count" command. */
SR_PRIV int ut181a_send_cmd_get_recs_count(struct sr_serial_dev_inst *serial)
{
	uint8_t cmd;

	cmd = CMD_CODE_GET_RECS_COUNT;
	return ut181a_send_frame(serial, &cmd, sizeof(cmd));
}

/*
 * Construct and transmit "get recording information" command.
 * Important: Callers use 0-based index, protocol needs 1-based index.
 */
SR_PRIV int ut181a_send_cmd_get_rec_info(struct sr_serial_dev_inst *serial, size_t idx)
{
	uint8_t cmd[sizeof(uint8_t) + sizeof(uint16_t)];
	size_t cmd_off;

	cmd_off = 0;
	cmd[cmd_off++] = CMD_CODE_GET_REC_INFO;
	WL16(&cmd[cmd_off], idx + 1);
	cmd_off += sizeof(uint16_t);

	return ut181a_send_frame(serial, cmd, sizeof(cmd));
}

/*
 * Construct and transmit "get recording samples" command.
 * Important: Callers use 0-based index, protocol needs 1-based index.
 */
SR_PRIV int ut181a_send_cmd_get_rec_samples(struct sr_serial_dev_inst *serial, size_t idx, size_t off)
{
	uint8_t cmd[sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t)];
	size_t cmd_off;

	cmd_off = 0;
	cmd[cmd_off++] = CMD_CODE_GET_REC_SAMPLES;
	WL16(&cmd[cmd_off], idx + 1);
	cmd_off += sizeof(uint16_t);
	WL32(&cmd[cmd_off], off + 1);
	cmd_off += sizeof(uint32_t);

	return ut181a_send_frame(serial, cmd, sizeof(cmd));
}

/* TODO
 * Construct and transmit "record on/off" command. Requires a caption,
 * an interval, and a duration to start a recording. Recordings can get
 * stopped upon request, or end when the requested duration has passed.
 */

/**
 * Specify which kind of response to wait for.
 *
 * @param[in] devc The device context.
 * @param[in] want_code Reply code wanted, boolean.
 * @param[in] want_data Reply data wanted, boolean.
 * @param[in] want_rsp_type Special response type wanted.
 * @param[in] want_measure Measurement wanted, boolean.
 * @param[in] want_rec_count Records count wanted, boolean.
 * @param[in] want_save_count Saved count wanted, boolean.
 * @param[in] want_sample_count Samples count wanted, boolean.
 */
SR_PRIV int ut181a_configure_waitfor(struct dev_context *devc,
	gboolean want_code, enum ut181_cmd_code want_data,
	enum ut181_rsp_type want_rsp_type,
	gboolean want_measure, gboolean want_rec_count,
	gboolean want_save_count, gboolean want_sample_count)
{

	if (want_rec_count)
		want_data = CMD_CODE_GET_RECS_COUNT;
	if (want_save_count)
		want_data = CMD_CODE_GET_SAVED_COUNT;
	if (want_sample_count)
		want_data = CMD_CODE_GET_REC_SAMPLES;

	memset(&devc->wait_state, 0, sizeof(devc->wait_state));
	devc->wait_state.want_code = want_code;
	devc->wait_state.want_data = want_data;
	devc->wait_state.want_rsp_type = want_rsp_type;
	devc->wait_state.want_measure = want_measure;
	memset(&devc->last_data, 0, sizeof(devc->last_data));

	return SR_OK;
}

/**
 * Wait for a response (or timeout) after a command was sent.
 *
 * @param[in] sdi The device instance.
 * @param[in] timeout_ms The timeout in milliseconds.
 *
 * @returns SR_OK upon success, SR_ERR_* upon error.
 *
 * This routine waits for the complete reception of a response (any kind)
 * after a command was previously sent by the caller, or terminates when
 * the timeout has expired without reception of a response. Callers need
 * to check the kind of response (data values, or status, or error codes).
 */
SR_PRIV int ut181a_waitfor_response(const struct sr_dev_inst *sdi, int timeout_ms)
{
	struct dev_context *devc;
	gint64 deadline, delay;
	struct wait_state *state;

	devc = sdi->priv;
	state = &devc->wait_state;
	state->response_count = 0;

	deadline = g_get_monotonic_time();
	deadline += timeout_ms * 1000;
	delay = 0;
	while (1) {
		gboolean got_wanted;
		if (g_get_monotonic_time() >= deadline)
			return SR_ERR_DATA;
		if (delay)
			g_usleep(delay);
		delay = 100;
		ut181a_handle_events(-1, G_IO_IN, (void *)sdi);
		got_wanted = FALSE;
		if (state->want_code && state->got_code)
			got_wanted = TRUE;
		if (state->want_data && state->got_data)
			got_wanted = TRUE;
		if (state->want_rsp_type && state->got_rsp_type)
			got_wanted = TRUE;
		if (state->want_measure && state->got_measure)
			got_wanted = TRUE;
		if (state->want_data == CMD_CODE_GET_RECS_COUNT && state->got_rec_count)
			got_wanted = TRUE;
		if (state->want_data == CMD_CODE_GET_SAVED_COUNT && state->got_save_count)
			got_wanted = TRUE;
		if (state->want_data == CMD_CODE_GET_REC_INFO && state->got_sample_count)
			got_wanted = TRUE;
		if (got_wanted)
			return SR_OK;
	}
}

/**
 * Get measurement value and precision details from protocol's raw bytes.
 */
static int ut181a_get_value_params(struct value_params *params, float value, uint8_t prec)
{

	if (!params)
		return SR_ERR_ARG;

	memset(params, 0, sizeof(*params));
	params->value = value;
	params->digits = (prec >> 4) & 0x0f;
	params->ol_neg = (prec & (1 << 1)) ? 1 : 0;
	params->ol_pos = (prec & (1 << 0)) ? 1 : 0;

	return SR_OK;
}

static void ut181a_cond_stop_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi)
		return;
	devc = sdi->priv;
	if (!devc)
		return;

	if (sdi->status == SR_ST_ACTIVE)
		sr_dev_acquisition_stop(sdi);
}

/**
 * Send meta packet with samplerate to the session feed.
 *
 * @param[in] sdi The device instance.
 * @param[in] interval The sample interval in seconds.
 *
 * @returns SR_OK upon success, SR_ERR_* upon error.
 *
 * The DMM records data at intervals which are multiples of seconds.
 * The @ref SR_CONF_SAMPLERATE key cannot express the rate values which
 * are below 1Hz. Instead the @ref SR_CONF_SAMPLE_INTERVAL key is sent,
 * which applications may or may not support.
 */
static int ut181a_feed_send_rate(struct sr_dev_inst *sdi, int interval)
{
#if 1
	return sr_session_send_meta(sdi,
		SR_CONF_SAMPLE_INTERVAL, g_variant_new_uint64(interval));
#else
	uint64_t rate;

	/*
	 * In theory we know the sample interval, and could provide a
	 * corresponding sample rate. In practice the interval has a
	 * resolution of seconds, which translates to rates below 1Hz,
	 * which we cannot express. So let's keep the routine here for
	 * awareness, and send a rate of 0.
	 */
	(void)interval;
	rate = 0;

	return sr_session_send_meta(sdi,
		SR_CONF_SAMPLERATE, g_variant_new_uint64(rate));
#endif
}

/**
 * Initialize session feed buffer before submission of values.
 */
static int ut181a_feedbuff_initialize(struct feed_buffer *buff)
{

	memset(buff, 0, sizeof(*buff));

	/*
	 * NOTE: The 'digits' fields get updated later from sample data.
	 * As do the MQ and unit fields and the channel list.
	 */
	memset(&buff->packet, 0, sizeof(buff->packet));
	sr_analog_init(&buff->analog, &buff->encoding, &buff->meaning, &buff->spec, 0);
	buff->analog.meaning->mq = 0;
	buff->analog.meaning->mqflags = 0;
	buff->analog.meaning->unit = 0;
	buff->analog.meaning->channels = NULL;
	buff->analog.encoding->unitsize = sizeof(buff->main_value);
	buff->analog.encoding->digits = 0;
	buff->analog.spec->spec_digits = 0;
	buff->analog.num_samples = 1;
	buff->analog.data = &buff->main_value;
	buff->packet.type = SR_DF_ANALOG;
	buff->packet.payload = &buff->analog;

	return SR_OK;
}

/**
 * Setup feed buffer's MQ, MQ flags, and unit before submission of values.
 */
static int ut181a_feedbuff_setup_unit(struct feed_buffer *buff, const char *text)
{
	int ret;
	struct mq_scale_params scale;

	/* Derive MQ, flags, unit, and scale from caller's unit text. */
	ret = ut181a_get_mq_details_from_text(&scale, text);
	if (ret < 0)
		return ret;
	buff->scale = scale.scale;
	buff->analog.meaning->mq = scale.mq;
	buff->analog.meaning->mqflags = scale.mqflags;
	buff->analog.meaning->unit = scale.unit;

	return SR_OK;
}

/**
 * Setup feed buffer's measurement value details before submission of values.
 */
static int ut181a_feedbuff_setup_value(struct feed_buffer *buff,
	struct value_params *value)
{

	if (!buff || !value)
		return SR_ERR_ARG;

	if (buff->scale) {
		value->value *= pow(10, buff->scale);
		value->digits += -buff->scale;
	}
	if (value->ol_neg)
		value->value = -INFINITY;
	if (value->ol_pos)
		value->value = +INFINITY;

	buff->main_value = value->value;
	buff->analog.encoding->digits = value->digits;
	buff->analog.spec->spec_digits = value->digits;

	return SR_OK;
}

/**
 * Setup feed buffer's channel before submission of values.
 */
static int ut181a_feedbuff_setup_channel(struct feed_buffer *buff,
	enum ut181a_channel_idx ch, struct sr_dev_inst *sdi)
{

	if (!buff || !sdi)
		return SR_ERR_ARG;
	if (!buff->analog.meaning)
		return SR_ERR_ARG;

	g_slist_free(buff->analog.meaning->channels);
	buff->analog.meaning->channels = g_slist_append(NULL,
		g_slist_nth_data(sdi->channels, ch));

	return SR_OK;
}

/**
 * Send previously configured feed buffer's content to the session.
 */
static int ut181a_feedbuff_send_feed(struct feed_buffer *buff,
	struct sr_dev_inst *sdi, size_t count)
{
	int ret;
	struct dev_context *devc;

	if (!buff || !sdi)
		return SR_ERR_ARG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_OK;
	devc = sdi->priv;
	if (!devc || devc->disable_feed)
		return SR_OK;

	ret = sr_session_send(sdi, &buff->packet);
	if (ret == SR_OK && count && sdi->priv) {
		sr_sw_limits_update_samples_read(&devc->limits, count);
		if (sr_sw_limits_check(&devc->limits))
			ut181a_cond_stop_acquisition(sdi);
	}

	return ret;
}

/**
 * Release previously allocated resources in the feed buffer.
 */
static int ut181a_feedbuff_cleanup(struct feed_buffer *buff)
{
	if (!buff)
		return SR_ERR_ARG;

	if (buff->analog.meaning)
		g_slist_free(buff->analog.meaning->channels);

	return SR_OK;
}

static int ut181a_feedbuff_start_frame(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	if (devc->disable_feed)
		return SR_OK;
	if (devc->frame_started)
		return SR_OK;

	ret = std_session_send_df_frame_begin(sdi);
	if (ret == SR_OK)
		devc->frame_started = TRUE;

	return ret;
}

static int ut181a_feedbuff_count_frame(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	if (devc->disable_feed)
		return SR_OK;
	if (!devc->frame_started)
		return SR_OK;

	ret = std_session_send_df_frame_end(sdi);
	if (ret != SR_OK)
		return ret;
	devc->frame_started = FALSE;

	sr_sw_limits_update_frames_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits))
		ut181a_cond_stop_acquisition(sdi);

	return SR_OK;
}

/* Deserializing helpers which also advance the read pointer. */

static int check_len(size_t *got, size_t want)
{

	if (!got)
		return SR_ERR_ARG;
	if (want > *got)
		return SR_ERR_DATA;

	return SR_OK;
}

static void advance_len(const uint8_t **p, size_t *l, size_t sz)
{

	if (p)
		*p += sz;
	if (l)
		*l -= sz;
}

static int consume_u8(uint8_t *v, const uint8_t **p, size_t *l)
{
	size_t sz;
	int ret;

	if (v)
		*v = 0;

	sz = sizeof(uint8_t);
	ret = check_len(l, sz);
	if (ret != SR_OK)
		return ret;

	if (v)
		*v = R8(*p);
	advance_len(p, l, sz);

	return SR_OK;
}

static int consume_u16(uint16_t *v, const uint8_t **p, size_t *l)
{
	size_t sz;
	int ret;

	if (v)
		*v = 0;

	sz = sizeof(uint16_t);
	ret = check_len(l, sz);
	if (ret != SR_OK)
		return ret;

	if (v)
		*v = RL16(*p);
	advance_len(p, l, sz);

	return SR_OK;
}

static int consume_u32(uint32_t *v, const uint8_t **p, size_t *l)
{
	size_t sz;
	int ret;

	if (v)
		*v = 0;

	sz = sizeof(uint32_t);
	ret = check_len(l, sz);
	if (ret != SR_OK)
		return ret;

	if (v)
		*v = RL32(*p);
	advance_len(p, l, sz);

	return SR_OK;
}

static int consume_flt(float *v, const uint8_t **p, size_t *l)
{
	size_t sz;
	int ret;

	if (v)
		*v = 0;

	sz = sizeof(float);
	ret = check_len(l, sz);
	if (ret != SR_OK)
		return ret;

	if (v)
		*v = RLFL(*p);
	advance_len(p, l, sz);

	return SR_OK;
}

/*
 * Fills the caller's text buffer from input data. Also trims and NUL
 * terminates the buffer content so that callers don't have to.
 */
static int consume_str(char *buff, size_t sz, const uint8_t **p, size_t *l)
{
	int ret;
	const char *v;

	if (buff)
		*buff = '\0';

	ret = check_len(l, sz);
	if (ret != SR_OK)
		return ret;

	/*
	 * Quickly grab current position. Immediate bailout if there is
	 * no caller buffer to fill in. Simpilifies the remaining logic.
	 */
	v = (const char *)*p;
	advance_len(p, l, sz);
	if (!buff)
		return SR_OK;

	/*
	 * Trim leading space off the input text. Then copy the remaining
	 * input data to the caller's buffer. This operation is bounded,
	 * and adds the NUL termination. Then trim trailing space.
	 *
	 * The resulting buffer content is known to be NUL terminated.
	 * It has at most the requested size (modulo the termination).
	 * The content may be empty, which can be acceptable to callers.
	 * So these need to check for and handle that condition.
	 */
	memset(buff, 0, sz);
	while (sz && isspace(*v)) {
		v++;
		sz--;
	}
	if (sz)
		snprintf(buff, sz, "%s", v);
	buff[sz] = '\0';
	sz = strlen(buff);
	while (sz && isspace(buff[sz - 1])) {
		buff[--sz] = '\0';
	}

	return SR_OK;
}

/* Process a DMM packet (a frame in the serial protocol). */
static int process_packet(struct sr_dev_inst *sdi, uint8_t *pkt, size_t len)
{
	struct dev_context *devc;
	struct wait_state *state;
	struct ut181a_info *info;
	uint16_t got_magic, got_length, got_cs, want_cs;
	const uint8_t *cs_data, *payload;
	size_t cs_dlen, pl_dlen;
	uint8_t rsp_type;
	enum sr_mqflag add_mqflags;
	char unit_buff[8], rec_name_buff[11];
	const char *unit_text, *rec_name;
	struct feed_buffer feedbuff;
	struct value_params value;
	const struct mqopt_item *mqitem;
	int ret;
	uint8_t v8; uint16_t v16; uint32_t v32; float vf;

	/*
	 * Cope with different calling contexts. The packet parser can
	 * get invoked outside of data acquisition, during preparation
	 * or in shutdown paths.
	 */
	devc = sdi ? sdi->priv : NULL;
	state = devc ? &devc->wait_state : NULL;
	info = devc ? &devc->info : NULL;
	if (FRAME_DUMP_FRAME && sr_log_loglevel_get() >= FRAME_DUMP_LEVEL) {
		GString *spew;
		spew = sr_hexdump_new(pkt, len);
		FRAME_DUMP_CALL("RX frame, %zu bytes: %s", len, spew->str);
		sr_hexdump_free(spew);
	}

	/*
	 * Check the frame envelope. Redundancy with common reception
	 * logic is perfectly fine. Several code paths end up here, we
	 * need to gracefully deal with incomplete or incorrect data.
	 *
	 * This stage uses random access to arbitrary positions in the
	 * packet which surround the payload. Before the then available
	 * payload gets consumed in a strict serial manner.
	 */
	if (len < 3 * sizeof(uint16_t)) {
		/* Need at least magic, length, checksum. */
		if (FRAME_DUMP_CSUM) {
			FRAME_DUMP_CALL("Insufficient frame data, need %zu, got %zu.",
				3 * sizeof(uint16_t), len);
		}
		return SR_ERR_DATA;
	}

	got_magic = RL16(&pkt[0]);
	if (got_magic != FRAME_MAGIC) {
		if (FRAME_DUMP_CSUM) {
			FRAME_DUMP_CALL("Frame magic mismatch, want 0x%04x, got 0x%04x.",
				(unsigned int)FRAME_MAGIC, (unsigned int)got_magic);
		}
		return SR_ERR_DATA;
	}

	got_length = RL16(&pkt[sizeof(uint16_t)]);
	if (got_length != len - 2 * sizeof(uint16_t)) {
		if (FRAME_DUMP_CSUM) {
			FRAME_DUMP_CALL("Frame length mismatch, want %zu, got %u.",
				len - 2 * sizeof(uint16_t), got_length);
		}
		return SR_ERR_DATA;
	}

	payload = &pkt[2 * sizeof(uint16_t)];
	pl_dlen = got_length - sizeof(uint16_t);

	cs_data = &pkt[sizeof(uint16_t)];
	cs_dlen = len - 2 * sizeof(uint16_t);
	want_cs = ut181a_checksum(cs_data, cs_dlen);
	got_cs = RL16(&pkt[len - sizeof(uint16_t)]);
	if (got_cs != want_cs) {
		if (FRAME_DUMP_CSUM) {
			FRAME_DUMP_CALL("Frame checksum mismatch, want 0x%04x, got 0x%04x.",
				(unsigned int)want_cs, (unsigned int)got_cs);
		}
		return SR_ERR_DATA;
	}
	if (state)
		state->response_count++;
	if (FRAME_DUMP_BYTES && sr_log_loglevel_get() >= FRAME_DUMP_LEVEL) {
		GString *spew;
		spew = sr_hexdump_new(payload, pl_dlen);
		FRAME_DUMP_CALL("RX payload, %zu bytes: %s", pl_dlen, spew->str);
		sr_hexdump_free(spew);
	}

	/*
	 * Interpret the frame's payload data. The first byte contains
	 * a packet type which specifies how to interpret the remainder.
	 */
	ret = consume_u8(&v8, &payload, &pl_dlen);
	if (ret != SR_OK) {
		sr_err("Insufficient payload data, need packet type.");
		return ret;
	}
	rsp_type = v8;
	if (info)
		info->rsp_head.rsp_type = rsp_type;

	add_mqflags = 0;
	switch (rsp_type) {
	case RSP_TYPE_REPLY_CODE:
		/*
		 * Reply code: One 16bit item with either 'OK' or 'ER'
		 * "string literals" to communicate boolean state.
		 */
		ret = consume_u16(&v16, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		if (info) {
			info->reply_code.code = v16;
			info->reply_code.ok = v16 == REPLY_CODE_OK;
		}
		if (state && state->want_code) {
			state->got_code = TRUE;
			state->code_ok = v16 == REPLY_CODE_OK;
		}
		break;
	case RSP_TYPE_SAVE:
		/*
		 * Saved measurement: A 32bit timestamp, followed by a
		 * measurement (FALLTHROUGH).
		 */
		ret = consume_u32(&v32, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		if (info)
			info->save_time.stamp = v32;
		v32 = ut181a_get_epoch_for_timestamp(v32);
		if (info)
			info->save_time.epoch = v32;

#if UT181A_WITH_TIMESTAMP
		if (devc) {
			ret = ut181a_feedbuff_start_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			ret = SR_OK;
			ret |= ut181a_feedbuff_initialize(&feedbuff);
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_TIME, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, "timestamp");
			ret |= ut181a_get_value_params(&value, v32, 0x00);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			ret |= ut181a_feedbuff_cleanup(&feedbuff);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
#endif
		if (info)
			info->save_info.save_idx++;

		/* FALLTHROUGH */
	case RSP_TYPE_MEASUREMENT:
		/*
		 * A measurement. Starts with a common header, which
		 * specifies the layout of the remainder (variants, with
		 * optional fields, depending on preceeding fields).
		 *
		 * Only useful to process when 'info' (and thus 'devc')
		 * are available.
		 */
		if (!info)
			return SR_ERR_NA;

		/*
		 * Get the header fields (misc1, misc2, mode, and range),
		 * derive local packet type details and flags from them.
		 */
		ret = consume_u8(&v8, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		info->meas_head.misc1 = v8;
		info->meas_head.has_hold = (v8 & 0x80) ? 1 : 0;
		info->meas_head.is_type = (v8 & 0x70) >> 4;
		info->meas_head.is_norm = (info->meas_head.is_type == 0) ? 1 : 0;
		info->meas_head.is_rel = (info->meas_head.is_type == 1) ? 1 : 0;
		info->meas_head.is_minmax = (info->meas_head.is_type == 2) ? 1 : 0;
		info->meas_head.is_peak = (info->meas_head.is_type == 4) ? 1 : 0;
		info->meas_head.has_bar = (v8 & 0x8) ? 1 : 0;
		info->meas_head.has_aux2 = (v8 & 0x4) ? 1 : 0;
		info->meas_head.has_aux1 = (v8 & 0x2) ? 1 : 0;

		ret = consume_u8(&v8, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		info->meas_head.misc2 = v8;
		info->meas_head.is_rec = (v8 & 0x20) ? 1 : 0;
		if (devc)
			devc->is_recording = info->meas_head.is_rec;
		info->meas_head.is_comp = (v8 & 0x10) ? 1 : 0;
		info->meas_head.has_lead_err = (v8 & 0x8) ? 1 : 0;
		info->meas_head.has_high_volt = (v8 & 0x2) ? 1 : 0;
		info->meas_head.is_auto_range = (v8 & 0x1) ? 1 : 0;

		ret = consume_u16(&v16, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		info->meas_head.mode = v16;
		mqitem = ut181a_get_mqitem_from_mode(v16);
		if (!mqitem || !mqitem->mq)
			return SR_ERR_DATA;
		add_mqflags |= mqitem->mqflags;
		if (info->meas_head.has_hold)
			add_mqflags |= SR_MQFLAG_HOLD;
		if (info->meas_head.is_auto_range)
			add_mqflags |= SR_MQFLAG_AUTORANGE;
		if (add_mqflags & SR_MQFLAG_DIODE)
			add_mqflags |= SR_MQFLAG_DC;

		ret = consume_u8(&v8, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		info->meas_head.range = v8;

		if (state && state->want_measure)
			state->got_measure = TRUE;

		ret = ut181a_feedbuff_start_frame(sdi);
		if (ret != SR_OK)
			return SR_ERR_DATA;

		/*
		 * The remaining measurement's layout depends on type.
		 * - Normal measurement:
		 *   - Main value (4/1/8 value/precision/unit).
		 *   - Aux1 value (4/1/8 value/precision/unit) when AUX1
		 *     flag active.
		 *   - Aux2 value (4/1/8 value/precision/unit) when AUX2
		 *     flag active.
		 *   - Bargraph (4/8 value/unit) when BAR flag active.
		 *   - COMP result when COMP flag active.
		 *     - Always 1/1/1/4 mode/flags/digits/limit: type
		 *       of check, PASS/FAIL verdict, limit values'
		 *       precision, upper or only limit.
		 *     - Conditional 4 limit: Lower limit for checks
		 *       which involve two limit values.
		 * - Relative measurement:
		 *   - Relative value (4/1/8 value/precision/unit).
		 *   - Reference value (4/1/8 value/precision/unit),
		 *     when AUX1 active (practically always).
		 *   - Absolute value (4/1/8 value/precision/unit),
		 *     when AUX2 active (practically always).
		 *   - Bargraph (4/8 value/unit) when BAR flag active.
		 * - Min/Max measurement:
		 *   - All fields always present, no conditions.
		 *   - One common unit spec at the end which applies to
		 *     all curr/max/avg/min values.
		 *   - Current value (4/1 value/precision).
		 *   - Maximum value (4/1/4 value/precision/time).
		 *   - Average value (4/1/4 value/precision/time).
		 *   - Minimum value (4/1/4 value/precision/time).
		 *   - Common unit text (8).
		 * - Peak measurement:
		 *   - All fields always present.
		 *   - Maximum value (4/1/8 value/precision/unit).
		 *   - Minimum value (4/1/8 value/precision/unit).
		 */
		ret = ut181a_feedbuff_initialize(&feedbuff);
		if (info->meas_head.is_norm) {
			/* Main value, unconditional. Get details. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.main_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.main_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.norm.main_unit,
				sizeof(info->meas_data.norm.main_unit),
				"%s", unit_text);
			unit_text = info->meas_data.norm.main_unit;

			/* Submit main value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_MAIN, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= add_mqflags;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 1);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_norm && info->meas_head.has_aux1) {
			/* Aux1 value, optional. Get details. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.aux1_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.aux1_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.norm.aux1_unit,
				sizeof(info->meas_data.norm.aux1_unit),
				"%s", unit_text);
			unit_text = info->meas_data.norm.aux1_unit;

			/* Submit aux1 value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX1, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_norm && info->meas_head.has_aux2) {
			/* Aux2 value, optional. Get details. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.aux2_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.aux2_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.norm.aux2_unit,
				sizeof(info->meas_data.norm.aux2_unit),
				"%s", unit_text);
			unit_text = info->meas_data.norm.aux2_unit;

			/* Submit aux2 value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX2, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_norm && info->meas_head.has_bar) {
			/* Bargraph value, optional. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.norm.bar_value = vf;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.norm.bar_unit,
				sizeof(info->meas_data.norm.bar_unit),
				"%s", unit_text);
			unit_text = info->meas_data.norm.bar_unit;

			/* Submit bargraph value to session feed. */
			ret = 0;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_BAR, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			ret |= ut181a_get_value_params(&value, vf, 0x00);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_norm && info->meas_head.is_comp) {
			/* COMP result, optional. Get details. */
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			if (v8 > COMP_MODE_ABOVE)
				return SR_ERR_DATA;
			info->meas_data.comp.mode = v8;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.comp.fail = v8 ? TRUE : FALSE;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.comp.digits = v8 & 0x0f;
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.comp.limit_high = vf;
			if (info->meas_data.comp.mode <= COMP_MODE_OUTER) {
				ret = consume_flt(&vf, &payload, &pl_dlen);
				if (ret != SR_OK)
					return SR_ERR_DATA;
				info->meas_data.comp.limit_low = vf;
			}

			/* TODO
			 * How to present this result to the feed? This
			 * implementation extracts and interprets the
			 * fields, but does not pass the values to the
			 * session. Which MQ to use for PASS/FAIL checks?
			 */
			static const char *mode_text[] = {
				[COMP_MODE_INNER] = "INNER",
				[COMP_MODE_OUTER] = "OUTER",
				[COMP_MODE_BELOW] = "BELOW",
				[COMP_MODE_ABOVE] = "ABOVE",
			};

			if (info->meas_data.comp.mode <= COMP_MODE_OUTER) {
				sr_dbg("Unprocessed COMP result:"
					" mode %s, %s, digits %d, low %f, high %f",
					mode_text[info->meas_data.comp.mode],
					info->meas_data.comp.fail ? "FAIL" : "PASS",
					info->meas_data.comp.digits,
					info->meas_data.comp.limit_low,
					info->meas_data.comp.limit_high);
			} else {
				sr_dbg("Unprocessed COMP result:"
					" mode %s, %s, digits %d, limit %f",
					mode_text[info->meas_data.comp.mode],
					info->meas_data.comp.fail ? "FAIL" : "PASS",
					info->meas_data.comp.digits,
					info->meas_data.comp.limit_high);
			}
		}
		if (info->meas_head.is_norm) {
			/* Normal measurement code path done. */
			ret = ut181a_feedbuff_cleanup(&feedbuff);
			ret = ut181a_feedbuff_count_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			break;
		}

		if (info->meas_head.is_rel) {
			/* Relative value, unconditional. Get details. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.rel_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.rel_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.rel.rel_unit,
				sizeof(info->meas_data.rel.rel_unit),
				"%s", unit_text);
			unit_text = info->meas_data.rel.rel_unit;

			/* Submit relative value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_MAIN, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= add_mqflags;
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_RELATIVE;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 1);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_rel && info->meas_head.has_aux1) {
			/* Reference value, "conditional" in theory. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.ref_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.ref_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.rel.ref_unit,
				sizeof(info->meas_data.rel.ref_unit),
				"%s", unit_text);
			unit_text = info->meas_data.rel.ref_unit;

			/* Submit reference value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX1, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_REFERENCE;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_rel && info->meas_head.has_aux2) {
			/* Absolute value, "conditional" in theory. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.abs_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.abs_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.rel.abs_unit,
				sizeof(info->meas_data.rel.abs_unit),
				"%s", unit_text);
			unit_text = info->meas_data.rel.abs_unit;

			/* Submit absolute value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX2, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_rel && info->meas_head.has_bar) {
			/* Bargraph value, conditional. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.rel.bar_value = vf;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.rel.bar_unit,
				sizeof(info->meas_data.rel.bar_unit),
				"%s", unit_text);
			unit_text = info->meas_data.rel.bar_unit;

			/* Submit bargraph value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_BAR, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			ret |= ut181a_get_value_params(&value, vf, 0x00);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_rel) {
			/* Relative measurement code path done. */
			ret = ut181a_feedbuff_cleanup(&feedbuff);
			ret = ut181a_feedbuff_count_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			break;
		}

		if (info->meas_head.is_minmax) {
			/*
			 * Min/max measurement values, none of them are
			 * conditional in practice (all are present).
			 * This is special in that all of curr, max, avg,
			 * and min values share the same unit text which
			 * is only at the end of the data fields.
			 */
			ret = SR_OK;
			ret |= consume_flt(&info->meas_data.minmax.curr_value, &payload, &pl_dlen);
			ret |= consume_u8(&info->meas_data.minmax.curr_prec, &payload, &pl_dlen);
			ret |= consume_flt(&info->meas_data.minmax.max_value, &payload, &pl_dlen);
			ret |= consume_u8(&info->meas_data.minmax.max_prec, &payload, &pl_dlen);
			ret |= consume_u32(&info->meas_data.minmax.max_stamp, &payload, &pl_dlen);
			ret |= consume_flt(&info->meas_data.minmax.avg_value, &payload, &pl_dlen);
			ret |= consume_u8(&info->meas_data.minmax.avg_prec, &payload, &pl_dlen);
			ret |= consume_u32(&info->meas_data.minmax.avg_stamp, &payload, &pl_dlen);
			ret |= consume_flt(&info->meas_data.minmax.min_value, &payload, &pl_dlen);
			ret |= consume_u8(&info->meas_data.minmax.min_prec, &payload, &pl_dlen);
			ret |= consume_u32(&info->meas_data.minmax.min_stamp, &payload, &pl_dlen);
			ret |= consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.minmax.all_unit,
				sizeof(info->meas_data.minmax.all_unit),
				"%s", unit_text);
			unit_text = info->meas_data.minmax.all_unit;

			/* Submit the current value. */
			vf = info->meas_data.minmax.curr_value;
			v8 = info->meas_data.minmax.curr_prec;
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_MAIN, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= add_mqflags;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 1);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			/* Submit the maximum value. */
			vf = info->meas_data.minmax.max_value;
			v8 = info->meas_data.minmax.max_prec;
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX1, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_MAX;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			/* Submit the average value. */
			vf = info->meas_data.minmax.avg_value;
			v8 = info->meas_data.minmax.avg_prec;
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX2, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_AVG;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			/* Submit the minimum value. */
			vf = info->meas_data.minmax.min_value;
			v8 = info->meas_data.minmax.min_prec;
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX3, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_MIN;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_minmax) {
			/* Min/max measurement code path done. */
			ret = ut181a_feedbuff_cleanup(&feedbuff);
			ret = ut181a_feedbuff_count_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			break;
		}

		if (info->meas_head.is_peak) {
			/* Maximum value, unconditional. Get details. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.peak.max_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.peak.max_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.peak.max_unit,
				sizeof(info->meas_data.peak.max_unit),
				"%s", unit_text);
			unit_text = info->meas_data.peak.max_unit;

			/* Submit max value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX1, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= add_mqflags; /* ??? */
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_MAX;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 1);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			/* Minimum value, unconditional. Get details. */
			ret = consume_flt(&vf, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.peak.min_value = vf;
			ret = consume_u8(&v8, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			info->meas_data.peak.min_prec = v8;
			ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
			unit_text = &unit_buff[0];
			if (ret != SR_OK)
				return SR_ERR_DATA;
			snprintf(info->meas_data.peak.min_unit,
				sizeof(info->meas_data.peak.min_unit),
				"%s", unit_text);
			unit_text = info->meas_data.peak.min_unit;

			/* Submit min value to session feed. */
			ret = SR_OK;
			ret |= ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_AUX3, sdi);
			ret |= ut181a_feedbuff_setup_unit(&feedbuff, unit_text);
			feedbuff.analog.meaning->mqflags |= SR_MQFLAG_MIN;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 0);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		if (info->meas_head.is_peak) {
			/* Relative measurement code path done. */
			ret = ut181a_feedbuff_cleanup(&feedbuff);
			ret = ut181a_feedbuff_count_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;
			break;
		}

		/* ShouldNeverHappen(TM) */
		sr_dbg("Unhandled measurement type.");
		return SR_ERR_DATA;

	case RSP_TYPE_REC_INFO:
		/*
		 * Not useful to process without 'devc' or 'info'.
		 * The caller provided the recording's index (the
		 * protocol won't in the response).
		 */
		if (!devc || !info)
			return SR_ERR_ARG;

		/*
		 * Record information:
		 * - User specified recording's name (11 ASCIIZ chars).
		 * - Unit text (8).
		 * - Interval, duration, sample count (2/4/4).
		 * - Max/avg/min values and precision (4+1/4+1/4+1).
		 * - Time when recording started (4).
		 *
		 * Notice that the recording name needs to get trimmed
		 * due to limited text editing capabilities of the DMM
		 * UI. The name need not be unique, and typically isn't
		 * (again: because of limited editing, potential numbers
		 * in names are not auto incremented in the firmware).
		 */
		ret = consume_str(&rec_name_buff[0], 11, &payload, &pl_dlen);
		rec_name = &rec_name_buff[0];
		if (ret != SR_OK)
			return SR_ERR_DATA;
		if (!*rec_name)
			return SR_ERR_DATA;
		snprintf(devc->record_names[info->rec_info.rec_idx],
			sizeof(devc->record_names[info->rec_info.rec_idx]),
			"%s", rec_name);
		snprintf(info->rec_info.name, sizeof(info->rec_info.name),
			"%s", rec_name);
		ret = consume_str(&unit_buff[0], 8, &payload, &pl_dlen);
		unit_text = &unit_buff[0];
		if (ret != SR_OK)
			return SR_ERR_DATA;
		snprintf(info->rec_info.unit,
			sizeof(info->rec_info.unit),
			"%s", unit_text);
		unit_text = info->rec_info.unit;
		ret = SR_OK;
		ret |= consume_u16(&info->rec_info.interval, &payload, &pl_dlen);
		ret |= consume_u32(&info->rec_info.duration, &payload, &pl_dlen);
		ret |= consume_u32(&info->rec_info.samples, &payload, &pl_dlen);
		ret |= consume_flt(&info->rec_info.max_value, &payload, &pl_dlen);
		ret |= consume_u8(&info->rec_info.max_prec, &payload, &pl_dlen);
		ret |= consume_flt(&info->rec_info.avg_value, &payload, &pl_dlen);
		ret |= consume_u8(&info->rec_info.avg_prec, &payload, &pl_dlen);
		ret |= consume_flt(&info->rec_info.min_value, &payload, &pl_dlen);
		ret |= consume_u8(&info->rec_info.min_prec, &payload, &pl_dlen);
		ret |= consume_u32(&v32, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		info->rec_info.start_stamp = ut181a_get_epoch_for_timestamp(v32);

		/*
		 * Cheat, provide sample count as if it was reply data.
		 * Some api.c code paths assume to find this detail there.
		 * Keep the last unit text at hand, subsequent reception
		 * of record data will reference it.
		 */
		if (state && state->want_data == CMD_CODE_GET_REC_INFO) {
			state->got_sample_count = TRUE;
			state->data_value = info->rec_info.samples;
		}
		snprintf(devc->last_data.unit_text,
			sizeof(devc->last_data.unit_text),
			"%s", unit_text);

		/*
		 * Optionally automatically forward the sample interval
		 * to the session feed, before record data is sent.
		 */
		if (devc->info.rec_info.auto_feed) {
			ret = ut181a_feed_send_rate(sdi, info->rec_info.interval);
		}

		break;

	case RSP_TYPE_REC_DATA:
		/*
		 * We expect record data only during acquisitions from
		 * that data source, and depend on being able to feed
		 * data to the session.
		 */
		if (sdi->status != SR_ST_ACTIVE)
			break;
		if (!devc || devc->disable_feed || !info)
			break;
		ret = ut181a_feedbuff_initialize(&feedbuff);
		ret = ut181a_feedbuff_setup_channel(&feedbuff, UT181A_CH_MAIN, sdi);
		ret = ut181a_feedbuff_setup_unit(&feedbuff, devc->last_data.unit_text);

		/*
		 * Record data:
		 * - u8 sample count for this data chunk, then the
		 *   corresponding number of samples, each is 9 bytes:
		 *   - f32 value
		 *   - u8 precision
		 *   - u32 timestamp
		 */
		ret = consume_u8(&info->rec_data.samples_chunk, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		info->rec_data.samples_curr += info->rec_data.samples_chunk;
		while (info->rec_data.samples_chunk--) {
			/*
			 * Implementation detail: Consume all received
			 * data, yet skip processing when a limit was
			 * reached and previously terminated acquisition.
			 */
			ret = SR_OK;
			ret |= consume_flt(&vf, &payload, &pl_dlen);
			ret |= consume_u8(&v8, &payload, &pl_dlen);
			ret |= consume_u32(&v32, &payload, &pl_dlen);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			if (sdi->status != SR_ST_ACTIVE)
				continue;

			ret = ut181a_feedbuff_start_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			ret = SR_OK;
			ret |= ut181a_get_value_params(&value, vf, v8);
			ret |= ut181a_feedbuff_setup_value(&feedbuff, &value);
			ret |= ut181a_feedbuff_send_feed(&feedbuff, sdi, 1);
			if (ret != SR_OK)
				return SR_ERR_DATA;

			ret = ut181a_feedbuff_count_frame(sdi);
			if (ret != SR_OK)
				return SR_ERR_DATA;
		}
		ret = ut181a_feedbuff_cleanup(&feedbuff);
		break;

	case RSP_TYPE_REPLY_DATA:
		/*
		 * Reply data. Generic 16bit value preceeded by 8bit
		 * request code.
		 */
		ret = SR_OK;
		ret |= consume_u8(&v8, &payload, &pl_dlen);
		ret |= consume_u16(&v16, &payload, &pl_dlen);
		if (ret != SR_OK)
			return SR_ERR_DATA;
		if (info) {
			info->reply_data.code = v8;
			info->reply_data.data = v16;
		}
		if (state && state->want_data && state->want_data == v8) {
			state->got_data = TRUE;
			state->data_value = v16;
			if (v8 == CMD_CODE_GET_RECS_COUNT)
				state->got_rec_count = TRUE;
			if (v8 == CMD_CODE_GET_SAVED_COUNT)
				state->got_save_count = TRUE;
			if (v8 == CMD_CODE_GET_REC_INFO)
				state->got_sample_count = TRUE;
		}
		break;

	default:
		if (FRAME_DUMP_PARSE)
			FRAME_DUMP_CALL("Unhandled response type 0x%02x", rsp_type);
		return SR_ERR_NA;
	}
	if (state && state->want_rsp_type == rsp_type)
		state->got_rsp_type = TRUE;
	if (FRAME_DUMP_REMAIN && pl_dlen) {
		GString *txt;
		txt = sr_hexdump_new(payload, pl_dlen);
		FRAME_DUMP_CALL("Unprocessed response data: %s", txt->str);
		sr_hexdump_free(txt);
	}

	/* Unconditionally check, we may have hit a time limit. */
	if (sr_sw_limits_check(&devc->limits)) {
		ut181a_cond_stop_acquisition(sdi);
		return SR_OK;
	}

	/*
	 * Only emit next requests for chunked downloads after successful
	 * reception and consumption of the currently received item(s).
	 */
	if (devc) {
		struct sr_serial_dev_inst *serial;
		serial = sdi->conn;

		switch (rsp_type) {
		case RSP_TYPE_SAVE:
			if (!info)
				break;
			/* Sample count was incremented during reception above. */
			if (info->save_info.save_idx >= info->save_info.save_count) {
				ut181a_cond_stop_acquisition(sdi);
				break;
			}
			ret = ut181a_send_cmd_get_saved_value(serial, info->save_info.save_idx);
			if (ret < 0)
				ut181a_cond_stop_acquisition(sdi);
			break;
		case RSP_TYPE_REC_DATA:
			if (!info)
				break;
			/*
			 * The sample count was incremented above during
			 * reception, because of variable length chunks
			 * of sample data.
			 */
			if (info->rec_data.samples_curr >= info->rec_data.samples_total) {
				ut181a_cond_stop_acquisition(sdi);
				break;
			}
			ret = ut181a_send_cmd_get_rec_samples(serial,
				info->rec_data.rec_idx, info->rec_data.samples_curr);
			if (ret < 0)
				ut181a_cond_stop_acquisition(sdi);
			break;
		default:
			/* EMPTY */
			break;
		}
	}

	return SR_OK;
}

/* Process a previously received RX buffer. May find none or several packets. */
static int process_buffer(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t *pkt;
	uint16_t v16;
	size_t pkt_len, remain, idx;
	int ret;

	devc = sdi->priv;

	/*
	 * Specifically do not insist on finding the packet boundary at
	 * the edge of the most recently received data chunk. Serial ports
	 * might involve hardware buffers (FIFO). We want to sync as fast
	 * as possible.
	 *
	 * Handle the synchronized situation first. Process complete and
	 * valid packets that reside at the start of the buffer. Continue
	 * reception when partially valid data was received but does not
	 * yet span a complete frame. Break out if data was received that
	 * failed verification. Assume temporary failure and try to sync
	 * to the input stream again.
	 *
	 * This logic is a little more complex than the typical DMM parser
	 * because of the variable frame length of the UT181A protocol. A
	 * frame always contains a magic (u16) and a length (u16), then a
	 * number of bytes according to length. The frame ends there, the
	 * checksum field is covered by the length value. packet processing
	 * will verify the checksum.
	 */
	pkt = &devc->recv_buff[0];
	do {
		/* Search for (the start of) a valid packet. */
		if (devc->recv_count < 2 * sizeof(uint16_t)) {
			/* Need more RX data for magic and length. */
			return SR_OK;
		}
		v16 = RL16(&pkt[0]);
		if (v16 != FRAME_MAGIC) {
			/* Not the expected magic marker. */
			if (FRAME_DUMP_CSUM) {
				FRAME_DUMP_CALL("Not a frame marker -> re-sync");
			}
			break;
		}
		v16 = RL16(&pkt[sizeof(uint16_t)]);
		if (v16 < sizeof(uint16_t)) {
			/* Insufficient length value, need at least checksum. */
			if (FRAME_DUMP_CSUM) {
				FRAME_DUMP_CALL("Too small a length -> re-sync");
			}
			break;
		}
		/* TODO Can we expect a maximum length value? */
		pkt_len = 2 * sizeof(uint16_t) + v16;
		if (pkt_len >= sizeof(devc->recv_buff)) {
			/* Frame will never fit in RX buffer. Invalid RX data? */
			if (FRAME_DUMP_CSUM) {
				FRAME_DUMP_CALL("Excessive length -> re-sync");
			}
			break;
		}
		if (pkt_len > devc->recv_count) {
			/* Need more RX data to complete the frame. */
			return SR_OK;
		}

		/* Process the packet which completed reception. */
		if (FRAME_DUMP_CSUM && sr_log_loglevel_get() >= FRAME_DUMP_LEVEL) {
			GString *spew;
			spew = sr_hexdump_new(pkt, pkt_len);
			FRAME_DUMP_CALL("Found RX frame, %zu bytes: %s", pkt_len, spew->str);
			sr_hexdump_free(spew);
		}
		ret = process_packet(sdi, pkt, pkt_len);
		if (ret == SR_ERR_DATA) {
			/* Verification failed, might be invalid RX data. */
			if (FRAME_DUMP_CSUM) {
				FRAME_DUMP_CALL("RX frame processing failed -> re-sync");
			}
			break;
		}
		remain = devc->recv_count - pkt_len;
		if (remain)
			memmove(&pkt[0], &pkt[pkt_len], remain);
		devc->recv_count -= pkt_len;
	} while (1);
	if (devc->recv_count < 2 * sizeof(uint16_t)) {
		/* Assume incomplete reception. Re-check later. */
		return SR_OK;
	}

	/*
	 * Data was received but failed the test for a valid frame. Try to
	 * synchronize to the next frame marker. Make sure to skip the
	 * current position which might have been a marker yet the frame
	 * check failed.
	 */
	if (FRAME_DUMP_CSUM) {
		FRAME_DUMP_CALL("Trying to re-sync on RX frame");
	}
	for (idx = 1; idx < devc->recv_count; idx++) {
		if (devc->recv_count - idx < sizeof(uint16_t)) {
			/* Nothing found. Drop all but the last byte here. */
			pkt[0] = pkt[idx];
			devc->recv_count = 1;
			if (FRAME_DUMP_CSUM) {
				FRAME_DUMP_CALL("Dropping %zu bytes, still not in sync", idx);
			}
			return SR_OK;
		}
		v16 = RL16(&pkt[idx]);
		if (v16 != FRAME_MAGIC)
			continue;
		/*
		 * Found a frame marker at offset 'idx'. Discard data
		 * before the marker. Next receive starts another attempt
		 * to interpret the frame, and may search the next marker
		 * upon failure.
		 */
		if (FRAME_DUMP_CSUM) {
			FRAME_DUMP_CALL("Dropping %zu bytes, next marker found", idx);
		}
		remain = devc->recv_count - idx;
		if (remain)
			memmove(&pkt[0], &pkt[idx], remain);
		devc->recv_count -= idx;
		break;
	}

	return SR_OK;
}

/* Gets invoked when RX data is available. */
static int ut181a_receive_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	size_t len;
	uint8_t *data;
	ssize_t slen;
	GString *spew;

	devc = sdi->priv;
	serial = sdi->conn;

	/*
	 * Discard receive data when the buffer is exhausted. This shall
	 * allow to (re-)synchronize to the data stream when we find it
	 * in an arbitrary state. (Takes a while to exhaust the buffer.
	 * Data is seriously unusable when we get here.)
	 */
	if (devc->recv_count == sizeof(devc->recv_buff)) {
		if (FRAME_DUMP_RXDATA)
			FRAME_DUMP_CALL("Discarding RX buffer (space exhausted)");
		(void)process_packet(sdi, &devc->recv_buff[0], devc->recv_count);
		devc->recv_count = 0;
	}

	/*
	 * Drain more data from the serial port, and check the receive
	 * buffer for packets. Process what was found to be complete.
	 */
	len = sizeof(devc->recv_buff) - devc->recv_count;
	data = &devc->recv_buff[devc->recv_count];
	slen = serial_read_nonblocking(serial, data, len);
	if (slen < 0) {
		if (FRAME_DUMP_RXDATA)
			FRAME_DUMP_CALL("UART RX failed, rc %zd", slen);
		return 0;
	}
	len = slen;
	if (FRAME_DUMP_RXDATA && sr_log_loglevel_get() >= FRAME_DUMP_LEVEL) {
		spew = sr_hexdump_new(data, len);
		FRAME_DUMP_CALL("UART RX, %zu bytes: %s", len, spew->str);
		sr_hexdump_free(spew);
	}
	devc->recv_count += len;
	process_buffer(sdi);

	return 0;
}

SR_PRIV int ut181a_handle_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;

	(void)fd;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	serial = sdi->conn;
	if (!serial)
		return TRUE;
	devc = sdi->priv;

	if (revents & G_IO_IN)
		(void)ut181a_receive_data(sdi);

	if (sdi->status == SR_ST_STOPPING) {
		if (devc->data_source == DATA_SOURCE_LIVE) {
			sdi->status = SR_ST_INACTIVE;
			(void)ut181a_send_cmd_monitor(serial, FALSE);
			(void)ut181a_waitfor_response(sdi, 100);
		}
		serial_source_remove(sdi->session, serial);
		std_session_send_df_end(sdi);
	}

	return TRUE;
}
