/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Martin Eitzenberger <x@cymaphore.net>
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

/**
 * @file
 *
 * Interface to APPA B (150/208/506) Series based Multimeters and Clamps
 *
 * For most of the documentation, please see the appa_b.h file!
 *
 */

#include "config.h"

#include "appa_b.h"

#include <ctype.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include <strings.h>

#include "libsigrok/libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "appa_b"

/* ********************************************************************** */
/* ********************************************************************** */
/* ********************************************************************** */

/* API FUNCTIONS */

static gboolean appa_b_is_wordcode(const int arg_wordcode)
{
	return arg_wordcode >= APPA_B_WORDCODE_TABLE_MIN;
}

static int appa_b_decode_reading(const struct appa_b_frame_display_reading_s *arg_display_reading)
{
	return
	arg_display_reading->reading_b0
		| arg_display_reading->reading_b1 << 8
		| arg_display_reading->reading_b2 << 16

		/* Fill Signed/Unsigned */
		| ((arg_display_reading->reading_b2 >> 7 == 1) ? 0xff : 0) << 24;
}

static const char *appa_b_model_id_name(const enum appa_b_model_id_e arg_model_id)
{
	switch (arg_model_id) {
	case APPA_B_MODEL_ID_INVALID:
		return APPA_B_STRING_NA;
	case APPA_B_MODEL_ID_150:
		return "APPA 150";
	case APPA_B_MODEL_ID_150B:
		return "APPA 150B";
	case APPA_B_MODEL_ID_208:
		return "APPA 208";
	case APPA_B_MODEL_ID_208B:
		return "APPA 208B";
	case APPA_B_MODEL_ID_506:
		return "APPA 506";
	case APPA_B_MODEL_ID_506B:
		return "APPA 506B";
	}

	return APPA_B_STRING_NA;
}

static const char *appa_b_wordcode_name(const enum appa_b_wordcode_e arg_wordcode)
{
	switch (arg_wordcode) {
	case APPA_B_WORDCODE_SPACE:
		return "";
	case APPA_B_WORDCODE_FULL:
		return "Full";
	case APPA_B_WORDCODE_BEEP:
		return "Beeo";
	case APPA_B_WORDCODE_APO:
		return "Auto Power-Off";
	case APPA_B_WORDCODE_B_LIT:
		return "Backlight";
	case APPA_B_WORDCODE_HAZ:
		return "Hazard";
	case APPA_B_WORDCODE_ON:
		return "On";
	case APPA_B_WORDCODE_OFF:
		return "Off";
	case APPA_B_WORDCODE_RESET:
		return "Reset";
	case APPA_B_WORDCODE_START:
		return "Start";
	case APPA_B_WORDCODE_VIEW:
		return "View";
	case APPA_B_WORDCODE_PAUSE:
		return "Pause";
	case APPA_B_WORDCODE_FUSE:
		return "Fuse";
	case APPA_B_WORDCODE_PROBE:
		return "Probe";
	case APPA_B_WORDCODE_DEF:
		return "Definition";
	case APPA_B_WORDCODE_CLR:
		return "Clr";
	case APPA_B_WORDCODE_ER:
		return "Er";
	case APPA_B_WORDCODE_ER1:
		return "Er1";
	case APPA_B_WORDCODE_ER2:
		return "Er2";
	case APPA_B_WORDCODE_ER3:
		return "Er3";
	case APPA_B_WORDCODE_DASH:
		return "-----";
	case APPA_B_WORDCODE_DASH1:
		return "-";
	case APPA_B_WORDCODE_TEST:
		return "Test";
	case APPA_B_WORDCODE_DASH2:
		return "--";
	case APPA_B_WORDCODE_BATT:
		return "Battery";
	case APPA_B_WORDCODE_DISLT:
		return "diSLt";
	case APPA_B_WORDCODE_NOISE:
		return "Noise";
	case APPA_B_WORDCODE_FILTR:
		return "Filter";
	case APPA_B_WORDCODE_PASS:
		return "PASS";
	case APPA_B_WORDCODE_NULL:
		return "null";
	case APPA_B_WORDCODE_0_20:
		return "0 - 20";
	case APPA_B_WORDCODE_4_20:
		return "4 - 20";
	case APPA_B_WORDCODE_RATE:
		return "Rate";
	case APPA_B_WORDCODE_SAVE:
		return "Save";
	case APPA_B_WORDCODE_LOAD:
		return "Load";
	case APPA_B_WORDCODE_YES:
		return "Yes";
	case APPA_B_WORDCODE_SEND:
		return "Send";
	case APPA_B_WORDCODE_AHOLD:
		return "Auto Hold";
	case APPA_B_WORDCODE_AUTO:
		return "Auto";
	case APPA_B_WORDCODE_CNTIN:
		return "Continuity";
	case APPA_B_WORDCODE_CAL:
		return "CAL";
	case APPA_B_WORDCODE_VERSION:
		return "Version";
	case APPA_B_WORDCODE_OL:
		return "OL";
	case APPA_B_WORDCODE_BAT_FULL:
		return "FULL";
	case APPA_B_WORDCODE_BAT_HALF:
		return "HALF";
	case APPA_B_WORDCODE_LO:
		return "Lo";
	case APPA_B_WORDCODE_HI:
		return "Hi";
	case APPA_B_WORDCODE_DIGIT:
		return "Digits";
	case APPA_B_WORDCODE_RDY:
		return "Ready";
	case APPA_B_WORDCODE_DISC:
		return "dISC";
	case APPA_B_WORDCODE_OUTF:
		return "outF";
	case APPA_B_WORDCODE_OLA:
		return "OLA";
	case APPA_B_WORDCODE_OLV:
		return "OLV";
	case APPA_B_WORDCODE_OLVA:
		return "OLVA";
	case APPA_B_WORDCODE_BAD:
		return "BAD";
	case APPA_B_WORDCODE_TEMP:
		return "TEMP";
	}

	return APPA_B_STRING_NA;
}

static double appa_b_dot_factor(const enum appa_b_dot_e arg_dot)
{
	switch (arg_dot) {
	case APPA_B_DOT_NONE:
		return 1.0;
	case APPA_B_DOT_9999_9:
		return 0.1;
	case APPA_B_DOT_999_99:
		return 0.01;
	case APPA_B_DOT_99_999:
		return 0.001;
	case APPA_B_DOT_9_9999:
		return 0.0001;
	}

	return 1.0;
}

static int appa_b_dot_precision_after_dot(const enum appa_b_dot_e arg_dot)
{
	switch (arg_dot) {
	case APPA_B_DOT_NONE:
		return 0;
	case APPA_B_DOT_9999_9:
		return 1;
	case APPA_B_DOT_999_99:
		return 2;
	case APPA_B_DOT_99_999:
		return 3;
	case APPA_B_DOT_9_9999:
		return 4;
	}

	return 0;
}

static int appa_b_dot_precision_before_dot(const enum appa_b_dot_e arg_dot)
{
	switch (arg_dot) {
	case APPA_B_DOT_NONE:
		return 5;
	case APPA_B_DOT_9999_9:
		return 4;
	case APPA_B_DOT_999_99:
		return 3;
	case APPA_B_DOT_99_999:
		return 2;
	case APPA_B_DOT_9_9999:
		return 1;
	}

	return 0;
}

static u_int8_t appa_b_checksum(const u_int8_t* arg_data, int arg_size)
{
	u_int8_t checksum;

	if (arg_data == NULL)
		return 0;

	checksum = 0;
	while (arg_size-- > 0)
		checksum += arg_data[arg_size];
	return checksum;
}

static int appa_b_write_frame_information_request(u_int8_t *arg_buf, int arg_len)
{
	if (arg_buf == NULL)
		return SR_ERR_ARG;

	if (arg_len < 5)
		return SR_ERR_ARG;

	arg_buf[0] = APPA_B_FRAME_START_VALUE_BYTE;
	arg_buf[1] = APPA_B_FRAME_START_VALUE_BYTE;
	arg_buf[2] = APPA_B_COMMAND_READ_INFORMATION;
	arg_buf[3] = 0;
	arg_buf[4] = appa_b_checksum(arg_buf, 4);

	return SR_OK;

}

static int appa_b_write_frame_display_request(u_int8_t *arg_buf, int arg_len)
{

	if (arg_buf == NULL)
		return SR_ERR_ARG;

	if (arg_len < 5)
		return SR_ERR_ARG;

	arg_buf[0] = APPA_B_FRAME_START_VALUE_BYTE;
	arg_buf[1] = APPA_B_FRAME_START_VALUE_BYTE;
	arg_buf[2] = APPA_B_COMMAND_READ_DISPLAY;
	arg_buf[3] = 0;
	arg_buf[4] = appa_b_checksum(arg_buf, 4);

	return SR_OK;
}

static int appa_b_read_frame_display_response(const u_int8_t *arg_buf, struct appa_b_frame_display_response_data_s* arg_display_response_data)
{
	if (arg_buf == NULL
		|| arg_display_response_data == NULL)
		return SR_ERR;

	if (arg_buf[0] != APPA_B_FRAME_START_VALUE_BYTE
		|| arg_buf[1] != APPA_B_FRAME_START_VALUE_BYTE)
		return SR_ERR_IO;

	if (arg_buf[2] != APPA_B_COMMAND_READ_DISPLAY)
		return SR_ERR_IO;

	if (arg_buf[3] != APPA_B_DATA_LENGTH_RESPONSE_READ_DISPLAY)
		return SR_ERR_IO;

	arg_display_response_data->function_code = arg_buf[4] & 0x7f;
	arg_display_response_data->auto_test = arg_buf[4] >> 7;

	arg_display_response_data->range_code = arg_buf[5] & 0x7f;
	arg_display_response_data->auto_range = arg_buf[5] >> 7;

	arg_display_response_data->main_display_data.reading_b0 = arg_buf[6];
	arg_display_response_data->main_display_data.reading_b1 = arg_buf[7];
	arg_display_response_data->main_display_data.reading_b2 = arg_buf[8];

	arg_display_response_data->main_display_data.dot = arg_buf[9] & 0x7;
	arg_display_response_data->main_display_data.unit = arg_buf[9] >> 3;

	arg_display_response_data->main_display_data.data_content = arg_buf[10] & 0x7f;
	arg_display_response_data->main_display_data.overload = arg_buf[10] >> 7;

	arg_display_response_data->sub_display_data.reading_b0 = arg_buf[11];
	arg_display_response_data->sub_display_data.reading_b1 = arg_buf[12];
	arg_display_response_data->sub_display_data.reading_b2 = arg_buf[13];

	arg_display_response_data->sub_display_data.dot = arg_buf[14] & 0x7;
	arg_display_response_data->sub_display_data.unit = arg_buf[14] >> 3;

	arg_display_response_data->sub_display_data.data_content = arg_buf[15] & 0x7f;
	arg_display_response_data->sub_display_data.overload = arg_buf[15] >> 7;

	return SR_OK;
}

/* ********************************************************************** */
/* ********************************************************************** */
/* ********************************************************************** */

/* SIGROK FUNCTIONS */

#ifdef HAVE_SERIAL_COMM

/**
 * Request frame from device
 *
 * Response will contain both display readings
 *
 * @param serial Serial data
 * @return @sr_error_code Status code
 */
SR_PRIV int sr_appa_b_serial_packet_request(struct sr_serial_dev_inst *serial)
{
	u_int8_t buf[5];

	if (serial == NULL)
		return SR_ERR_ARG;

#ifdef APPA_B_ENABLE_FLUSH
	if (serial_flush(serial) != SR_OK)
		return SR_ERR_IO;
#endif/*APPA_B_ENABLE_FLUSH*/

	if (appa_b_write_frame_display_request(buf, sizeof(buf)) != SR_OK)
		return SR_ERR;

#ifdef APPA_B_ENABLE_NON_BLOCKING
	if (serial_write_nonblocking(serial, &buf, sizeof(buf)) != sizeof(buf))
		return SR_ERR_IO;
#else/*APPA_B_ENABLE_NON_BLOCKING*/
	if (serial_write_blocking(serial, &buf, sizeof(buf), APPA_B_WRITE_BLOCKING_TIMEOUT) != sizeof(buf))
		return SR_ERR_IO;
#endif/*APPA_B_ENABLE_NON_BLOCKING*/

	return SR_OK;
}

#endif/*HAVE_SERIAL_COMM*/

/**
 * Validate APPA-Frame
 *
 * @param buf
 * @return TRUE if checksum is fine
 */
SR_PRIV gboolean sr_appa_b_packet_valid(const uint8_t *buf)
{
	int frame_length;
	u_int8_t checksum;

	if (buf == NULL)
		return FALSE;

	frame_length = APPA_B_PAYLOAD_LENGTH(APPA_B_DATA_LENGTH_RESPONSE_READ_DISPLAY);
	checksum = appa_b_checksum(buf, frame_length);

	return
	checksum == buf[frame_length]
		&& buf[0] == APPA_B_FRAME_START_VALUE_BYTE
		&& buf[1] == APPA_B_FRAME_START_VALUE_BYTE;
}

/**
 * Parse APPA-Frame and assign values to virtual channels
 *
 * @TODO include display reading as debug output?
 *
 * @param buf Buffer from Serial or BTLE
 * @param floatval Return display reading
 * @param analog Metadata of the reading
 * @param info Channel information and other things
 * @return @sr_error_code Status
 */
SR_PRIV int sr_appa_b_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, void *info)
{
	struct appa_b_info *info_local;
	struct appa_b_frame_display_response_data_s display_response_data;
	struct appa_b_frame_display_reading_s *display_reading;

	gboolean is_sub;

	double unit_factor;
	int display_reading_value_raw;
	double display_rading_value;
	int8_t digits;

	if (buf == NULL
		|| floatval == NULL
		|| analog == NULL
		|| info == NULL)
		return SR_ERR_ARG;

	info_local = info;

	is_sub = (info_local->ch_idx == 1);

	if (appa_b_read_frame_display_response(buf, &display_response_data) != SR_OK)
		return SR_ERR_DATA;

	if (!is_sub)
		display_reading = &display_response_data.main_display_data;
	else
		display_reading = &display_response_data.sub_display_data;

	unit_factor = 1;
	digits = 0;

	display_reading_value_raw = appa_b_decode_reading(display_reading);
	display_rading_value = (double) display_reading_value_raw;

	switch (display_reading->dot) {

	default: case APPA_B_DOT_NONE:
		digits = 0;
		unit_factor /= 1;
		break;

	case APPA_B_DOT_9999_9:
		digits = 1;
		unit_factor /= 10;
		break;

	case APPA_B_DOT_999_99:
		digits = 2;
		unit_factor /= 100;
		break;

	case APPA_B_DOT_99_999:
		digits = 3;
		unit_factor /= 1000;
		break;

	case APPA_B_DOT_9_9999:
		digits = 4;
		unit_factor /= 10000;
		break;

	}

	switch (display_reading->data_content) {

	case APPA_B_DATA_CONTENT_MAXIMUM:
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
		break;

	case APPA_B_DATA_CONTENT_MINIMUM:
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
		break;

	case APPA_B_DATA_CONTENT_AVERAGE:
		analog->meaning->mqflags |= SR_MQFLAG_AVG;
		break;

	case APPA_B_DATA_CONTENT_PEAK_HOLD_MAX:
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
		if (is_sub)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		break;

	case APPA_B_DATA_CONTENT_PEAK_HOLD_MIN:
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
		if (is_sub)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		break;

	case APPA_B_DATA_CONTENT_AUTO_HOLD:
		if (is_sub)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		break;

	case APPA_B_DATA_CONTENT_HOLD:
		if (is_sub)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		break;

	}

	if (display_response_data.auto_range == APPA_B_AUTO_RANGE) {

		analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;

	}
	
	switch (display_reading->unit) {

	default: case APPA_B_UNIT_NONE:
		analog->meaning->unit = SR_UNIT_UNITLESS;
		break;

	case APPA_B_UNIT_MV:
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mq = SR_MQ_VOLTAGE;
		unit_factor /= 1000;
		digits += 3;
		break;

	case APPA_B_UNIT_V:
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mq = SR_MQ_VOLTAGE;
		break;

	case APPA_B_UNIT_UA:
		analog->meaning->unit = SR_UNIT_AMPERE;
		analog->meaning->mq = SR_MQ_CURRENT;
		unit_factor /= 1000000;
		digits += 6;
		break;
	case APPA_B_UNIT_MA:
		analog->meaning->unit = SR_UNIT_AMPERE;
		analog->meaning->mq = SR_MQ_CURRENT;
		unit_factor /= 1000;
		digits += 3;
		break;

	case APPA_B_UNIT_A:
		analog->meaning->unit = SR_UNIT_AMPERE;
		analog->meaning->mq = SR_MQ_CURRENT;
		break;

	case APPA_B_UNIT_DB:
		analog->meaning->unit = SR_UNIT_DECIBEL_VOLT;
		analog->meaning->mq = SR_MQ_POWER;
		break;

	case APPA_B_UNIT_DBM:
		analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		analog->meaning->mq = SR_MQ_POWER;
		break;

	case APPA_B_UNIT_NF:
		analog->meaning->unit = SR_UNIT_FARAD;
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		unit_factor /= 1000000000;
		digits += 9;
		break;

	case APPA_B_UNIT_UF:
		analog->meaning->unit = SR_UNIT_FARAD;
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		unit_factor /= 1000000;
		digits += 6;
		break;

	case APPA_B_UNIT_MF:
		analog->meaning->unit = SR_UNIT_FARAD;
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		unit_factor /= 1000;
		digits += 3;
		break;

	case APPA_B_UNIT_GOHM:
		analog->meaning->unit = SR_UNIT_OHM;
		analog->meaning->mq = SR_MQ_RESISTANCE;
		unit_factor *= 1000000000;
		digits -= 9;
		break;

	case APPA_B_UNIT_MOHM:
		analog->meaning->unit = SR_UNIT_OHM;
		analog->meaning->mq = SR_MQ_RESISTANCE;
		unit_factor *= 1000000;
		digits -= 6;
		break;

	case APPA_B_UNIT_KOHM:
		analog->meaning->unit = SR_UNIT_OHM;
		analog->meaning->mq = SR_MQ_RESISTANCE;
		unit_factor *= 1000;
		digits -= 3;
		break;

	case APPA_B_UNIT_OHM:
		analog->meaning->unit = SR_UNIT_OHM;
		analog->meaning->mq = SR_MQ_RESISTANCE;
		break;

	case APPA_B_UNIT_PERCENT:
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
		analog->meaning->mq = SR_MQ_DIFFERENCE;
		break;

	case APPA_B_UNIT_MHZ:
		analog->meaning->unit = SR_UNIT_HERTZ;
		analog->meaning->mq = SR_MQ_FREQUENCY;
		unit_factor *= 1000000;
		digits -= 6;
		break;

	case APPA_B_UNIT_KHZ:
		analog->meaning->unit = SR_UNIT_HERTZ;
		analog->meaning->mq = SR_MQ_FREQUENCY;
		unit_factor *= 1000;
		digits -= 3;
		break;

	case APPA_B_UNIT_HZ:
		analog->meaning->unit = SR_UNIT_HERTZ;
		analog->meaning->mq = SR_MQ_FREQUENCY;
		break;

	case APPA_B_UNIT_DEGC:
		analog->meaning->unit = SR_UNIT_CELSIUS;
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		break;

	case APPA_B_UNIT_DEGF:
		analog->meaning->unit = SR_UNIT_FAHRENHEIT;
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		break;

	case APPA_B_UNIT_NS:
		analog->meaning->unit = SR_UNIT_SECOND;
		unit_factor /= 1000000000;
		digits += 9;
		break;

	case APPA_B_UNIT_US:
		analog->meaning->unit = SR_UNIT_SECOND;
		unit_factor /= 1000000;
		digits += 6;
		break;

	case APPA_B_UNIT_MS:
		analog->meaning->unit = SR_UNIT_SECOND;
		unit_factor /= 1000;
		digits += 3;
		break;

	case APPA_B_UNIT_SEC:
		analog->meaning->unit = SR_UNIT_SECOND;
		break;

	case APPA_B_UNIT_MIN:
		analog->meaning->unit = SR_UNIT_SECOND;
		unit_factor *= 60;
		break;

	case APPA_B_UNIT_KW:
		analog->meaning->unit = SR_UNIT_WATT;
		analog->meaning->mq = SR_MQ_POWER;
		unit_factor *= 1000;
		digits -= 3;
		break;

	case APPA_B_UNIT_PF:
		analog->meaning->unit = SR_UNIT_UNITLESS;
		analog->meaning->mq = SR_MQ_POWER_FACTOR;
		break;

	}

	switch (display_response_data.function_code) {

	case APPA_B_FUNCTIONCODE_PEAK_HOLD_UA:
	case APPA_B_FUNCTIONCODE_AC_UA:
	case APPA_B_FUNCTIONCODE_AC_MV:
	case APPA_B_FUNCTIONCODE_AC_MA:
	case APPA_B_FUNCTIONCODE_LPF_MV:
	case APPA_B_FUNCTIONCODE_LPF_MA:
	case APPA_B_FUNCTIONCODE_AC_V:
	case APPA_B_FUNCTIONCODE_AC_A:
	case APPA_B_FUNCTIONCODE_LPF_V:
	case APPA_B_FUNCTIONCODE_LPF_A:
	case APPA_B_FUNCTIONCODE_LOZ_AC_V:
	case APPA_B_FUNCTIONCODE_AC_W:
	case APPA_B_FUNCTIONCODE_LOZ_LPF_V:
	case APPA_B_FUNCTIONCODE_V_HARM:
	case APPA_B_FUNCTIONCODE_INRUSH:
	case APPA_B_FUNCTIONCODE_A_HARM:
	case APPA_B_FUNCTIONCODE_FLEX_INRUSH:
	case APPA_B_FUNCTIONCODE_FLEX_A_HARM:
		if(analog->meaning->unit == SR_UNIT_AMPERE
			|| analog->meaning->unit == SR_UNIT_VOLT
			|| analog->meaning->unit == SR_UNIT_WATT) {
			analog->meaning->mqflags |= SR_MQFLAG_AC;
			analog->meaning->mqflags |= SR_MQFLAG_RMS;
		}
		break;

	case APPA_B_FUNCTIONCODE_DC_UA:
	case APPA_B_FUNCTIONCODE_DC_MV:
	case APPA_B_FUNCTIONCODE_DC_MA:
	case APPA_B_FUNCTIONCODE_DC_V:
	case APPA_B_FUNCTIONCODE_DC_A:
	case APPA_B_FUNCTIONCODE_DC_A_OUT:
	case APPA_B_FUNCTIONCODE_DC_A_OUT_SLOW_LINEAR:
	case APPA_B_FUNCTIONCODE_DC_A_OUT_FAST_LINEAR:
	case APPA_B_FUNCTIONCODE_DC_A_OUT_SLOW_STEP:
	case APPA_B_FUNCTIONCODE_DC_A_OUT_FAST_STEP:
	case APPA_B_FUNCTIONCODE_LOOP_POWER:
	case APPA_B_FUNCTIONCODE_LOZ_DC_V:
	case APPA_B_FUNCTIONCODE_DC_W:
	case APPA_B_FUNCTIONCODE_FLEX_AC_A:
	case APPA_B_FUNCTIONCODE_FLEX_LPF_A:
	case APPA_B_FUNCTIONCODE_FLEX_PEAK_HOLD_A:
		analog->meaning->mqflags |= SR_MQFLAG_DC;
		break;

	case APPA_B_FUNCTIONCODE_CONTINUITY:
		analog->meaning->mq = SR_MQ_CONTINUITY;
		break;

	case APPA_B_FUNCTIONCODE_DIODE:
		analog->meaning->mqflags |= SR_MQFLAG_DIODE;
		analog->meaning->mqflags |= SR_MQFLAG_DC;
		break;

	case APPA_B_FUNCTIONCODE_AC_DC_MV:
	case APPA_B_FUNCTIONCODE_AC_DC_MA:
	case APPA_B_FUNCTIONCODE_AC_DC_V:
	case APPA_B_FUNCTIONCODE_AC_DC_A:
	case APPA_B_FUNCTIONCODE_VOLT_SENSE:
	case APPA_B_FUNCTIONCODE_LOZ_AC_DC_V:
		if(analog->meaning->unit == SR_UNIT_AMPERE
			|| analog->meaning->unit == SR_UNIT_VOLT
			|| analog->meaning->unit == SR_UNIT_WATT) {
			analog->meaning->mqflags |= SR_MQFLAG_AC;
			analog->meaning->mqflags |= SR_MQFLAG_DC;
			analog->meaning->mqflags |= SR_MQFLAG_RMS;
		}
		break;

	}

	analog->spec->spec_digits = digits;
	analog->encoding->digits = digits;

	display_rading_value *= unit_factor;

	if (display_reading_value_raw == APPA_B_WORDCODE_BATT) {
		sr_err("Battery low");
	}

	if (display_reading->overload == APPA_B_OVERLOAD
		|| appa_b_is_wordcode(display_reading_value_raw))
		*floatval = INFINITY;
	else
		*floatval = display_rading_value;

	info_local->ch_idx++;

	return SR_OK;
}

SR_PRIV const char *sr_appa_b_channel_formats[APPA_B_DISPLAY_COUNT] = {
	"main",
	"sub",
};
