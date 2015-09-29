/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
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
 * Brymen BM25x serial protocol parser.
 */

#include <config.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "brymen-bm25x"

#define MAX_DIGITS 4

SR_PRIV gboolean sr_brymen_bm25x_packet_valid(const uint8_t *buf)
{
	int i;

	if (buf[0] != 2)
		return FALSE;

	for (i = 1; i < BRYMEN_BM25X_PACKET_SIZE; i++)
		if ((buf[i] >> 4) != i)
			return FALSE;

	return TRUE;
}

static int decode_digit(int num, const uint8_t *buf)
{
	int val;

	val = (buf[3 + 2 * num] & 0xe) | ((buf[4 + 2 * num] << 4) & 0xf0);

	switch (val) {
	case 0xbe: return 0;
	case 0xa0: return 1;
	case 0xda: return 2;
	case 0xf8: return 3;
	case 0xe4: return 4;
	case 0x7c: return 5;
	case 0x7e: return 6;
	case 0xa8: return 7;
	case 0xfe: return 8;
	case 0xfc: return 9;
	case 0x00: return ' ';
	case 0x40: return '-';
	case 0x16: return 'L';
	case 0x1e: return 'C';
	case 0x4e: return 'F';
	case 0x5e: return 'E';
	case 0x62: return 'n';
	case 0x42: return 'r';
	default:
		sr_dbg("Unknown digit: 0x%02x.", val);
		return -1;
	}
}

static int decode_point(const uint8_t *buf)
{
	int i, p = 0;

	for (i = 1; i < MAX_DIGITS; i++) {
		if ((buf[11 - 2 * i] & 1) == 0)
			continue;
		if (p != 0) {
			sr_spew("Multiple decimal points found!");
			return -1;
		}
		p = i;
	}

	return p;
}

static float scale_value(float val, int point, int digits)
{
	int pos;

	pos = point ? point + digits - MAX_DIGITS : 0;

	switch (pos) {
	case 0: return val;
	case 1: return val * 1e-1;
	case 2: return val * 1e-2;
	case 3: return val * 1e-3;
	}

	sr_dbg("Invalid decimal point %d (%d digits).", point, digits);

	return NAN;
}

static float decode_prefix(const uint8_t *buf)
{
	if (buf[11] & 2) return 1e+6;
	if (buf[11] & 1) return 1e+3;
	if (buf[13] & 1) return 1e-3;
	if (buf[13] & 2) return 1e-6;
	if (buf[12] & 1) return 1e-9;

	return 1.0f;
}

static float decode_value(const uint8_t *buf)
{
	float val = 0.0f;
	int i, digit;

	for (i = 0; i < MAX_DIGITS; i++) {
		digit = decode_digit(i, buf);
		if (i == 3 && (digit == 'C' || digit == 'F'))
			break;
		if (digit < 0 || digit > 9)
			goto special;
		val = 10.0 * val + digit;
	}

	return scale_value(val, decode_point(buf), i);

special:
	if (decode_digit(1, buf) == 0 && decode_digit(2, buf) == 'L')
		return INFINITY;

	return NAN;
}

SR_PRIV int sr_brymen_bm25x_parse(const uint8_t *buf, float *floatval,
				struct sr_datafeed_analog_old *analog, void *info)
{
	float val;

	(void)info;

	analog->mq = SR_MQ_GAIN;
	analog->unit = SR_UNIT_UNITLESS;
	analog->mqflags = 0;

	if (buf[1] & 8)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (buf[1] & 4)
		analog->mqflags |= SR_MQFLAG_DC;
	if (buf[1] & 2)
		analog->mqflags |= SR_MQFLAG_AC;
	if (buf[1] & 1)
		analog->mqflags |= SR_MQFLAG_RELATIVE;
	if (buf[11] & 8)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (buf[13] & 8)
		analog->mqflags |= SR_MQFLAG_MAX;
	if (buf[14] & 8)
		analog->mqflags |= SR_MQFLAG_MIN;

	if (buf[14] & 4) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		if ((analog->mqflags & (SR_MQFLAG_DC | SR_MQFLAG_AC)) == 0)
			analog->mqflags |= SR_MQFLAG_DIODE;
	}
	if (buf[14] & 2) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (buf[12] & 4) {
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (buf[13] & 4) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (buf[12] & 2) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}

	if (decode_digit(3, buf) == 'C') {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
	if (decode_digit(3, buf) == 'F') {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_FAHRENHEIT;
	}

	val = decode_value(buf) * decode_prefix(buf);

	if (buf[3] & 1)
		val = -val;

	*floatval = val;

	return SR_OK;
}
