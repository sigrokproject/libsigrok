/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2018 Stefan Mandl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * HYELEC MS8236 protocol parser.
 *
 * Sends 22 bytes.
 * aa 55 52 24 01 10 6b b6 6b 00 2c 03 00 00 00 00 00 00 20 01 00 0a
 *
 * Protocol described in https://sigrok.org/wiki/HYELEC_MS8236
 *
 * - Communication parameters: Unidirectional, 2400/8n1
 * - CH340 USB to UART bridge controller
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ms8236"

/*
 * Main display (7-segment LCD value): xxDBG xxAC xxxx xxEF
 * https://en.wikipedia.org/wiki/Seven-segment_display
 */
static int parse_digit(uint16_t b)
{
	switch ( (b & 0x7F) ) {
	case 0x0: /* 7-segment not active */
		return 0;
	case 0x79: /* Overflow */
		return 0xF;
	case 0x58: /* Overflow */
		return 0xF;
	case 0x5F:
		return 0;
	case 0x06:
		return 1;
	case 0x6B:
		return 2;
	case 0x2F:
		return 3;
	case 0x36:
		return 4;
	case 0x3D:
		return 5;
	case 0x7D:
		return 6;
	case 0x07:
		return 7;
	case 0x7F:
		return 8;
	case 0x3F:
		return 9;
	default:
		sr_dbg("Invalid digit word: 0x%04x.", b);
		return -1;
	}
}

static void parse_flags(const uint8_t *buf, struct ms8236_info *info)
{
	info->is_volt   = (buf[21]  & (1 << 3)) ? 1 : 0;
	info->is_ohm    = (buf[21]  & (1 << 6)) ? 1 : 0;
	info->is_ampere = (buf[21] & (1 << 2)) ? 1 : 0;
	info->is_hz     = (buf[21] & (1 << 7)) ? 1 : 0;
	info->is_farad  = (buf[20] & (1 << 7)) ? 1 : 0;

	/* Micro */
	if (!info->is_farad)
		info->is_micro = (buf[21] & (1 << 0)) ? 1 : 0;
	else
		info->is_micro = (buf[20] & (1 << 5)) ? 1 : 0; /* uF */

	info->is_nano  = (buf[20] & (1 << 6)) ? 1 : 0;
	info->is_milli = (buf[21] & (1 << 1)) ? 1 : 0;
	info->is_kilo  = (buf[21] & (1 << 5)) ? 1 : 0;
	info->is_mega  = (buf[21] & (1 << 4)) ? 1 : 0;

	//info->is_autotimer = (buf[1]  & (1 << 0)) ? 1 : 0; /* Auto off timer */
	info->is_autotimer = 0; /* Auto off timer */
	//info->is_rs232     = (buf[1]  & (1 << 1)) ? 1 : 0; /* RS232 via USB */
	info->is_rs232     = 1; /* RS232 via USB */
	info->is_ac        = (buf[10]  & (1 << 1)) ? 1 : 0;
	info->is_dc        = (buf[10]  & (1 << 2)) ? 1 : 0;
	info->is_auto      = (buf[18] & (1 << 6)) ? 1 : 0;
	info->is_bat       = (buf[10]  & (1 << 8)) ? 1 : 0; /* Low battery */
	info->is_min       = (buf[19] & (1 << 4)) ? 1 : 0;
	info->is_max       = (buf[19] & (1 << 2)) ? 1 : 0;
	info->is_rel       = (buf[18] & (1 << 7)) ? 1 : 0;
	info->is_hold      = (buf[18] & (1 << 6)) ? 1 : 0;
	info->is_diode     = (buf[10] & (1 << 0)) ? 1 : 0;
	//info->is_beep      = (buf[11] & (1 << 1)) ? 1 : 0;
	info->is_beep      = 0;
	//info->is_ncv       = (buf[0]  & (1 << 0)) ? 1 : 0;
	info->is_ncv       = 0;
}

static gboolean flags_valid(const struct ms8236_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count = 0;
	count += (info->is_nano) ? 1 : 0;
	count += (info->is_micro) ? 1 : 0;
	count += (info->is_milli) ? 1 : 0;
	count += (info->is_kilo) ? 1 : 0;
	count += (info->is_mega) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count = 0;
	count += (info->is_hz) ? 1 : 0;
	count += (info->is_ohm) ? 1 : 0;
	count += (info->is_farad) ? 1 : 0;
	count += (info->is_ampere) ? 1 : 0;
	count += (info->is_volt) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_dbg("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	/* RS232 flag set? */
	if (!info->is_rs232) {
		sr_dbg("No RS232 flag detected in packet.");
		return FALSE;
	}

	return TRUE;
}

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
		int *exponent, const struct ms8236_info *info)
{
	/* Factors */
	if (info->is_nano)
		*exponent -= 9;
	if (info->is_micro)
		*exponent -= 6;
	if (info->is_milli)
		*exponent -= 3;
	if (info->is_kilo)
		*exponent += 3;
	if (info->is_mega)
		*exponent += 6;
	*floatval *= powf(10, *exponent);

	/* Measurement modes */
	if (info->is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_ampere) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (info->is_ohm) {
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (info->is_hz) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (info->is_farad) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (info->is_beep) {
		analog->meaning->mq = SR_MQ_CONTINUITY;
		analog->meaning->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval == INFINITY) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_percent) {
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	}

	/* Measurement related flags */
	if (info->is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	if (info->is_auto)
		analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
	if (info->is_diode)
		analog->meaning->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
	if (info->is_hold)
		analog->meaning->mqflags |= SR_MQFLAG_HOLD;
	if (info->is_rel)
		analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (info->is_rs232)
		sr_spew("RS232 enabled.");
	if (info->is_bat)
		sr_spew("Battery is low.");
	if (info->is_beep)
		sr_spew("Beep is active");
}

SR_PRIV gboolean sr_ms8236_packet_valid(const uint8_t *buf)
{
	struct ms8236_info info;

	sr_dbg("DMM packet: %02x %02x %02x %02x %02x %02x %02x "
		"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
		buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13],
		buf[14], buf[15], buf[16], buf[17], buf[18], buf[19], buf[20], buf[21]);

	parse_flags(buf, &info);

	if ((buf[0] == 0xaa) && flags_valid(&info))
		return TRUE;

	return FALSE;
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the 18-byte protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will contain the
 *                 result value upon parsing success. Must not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct ms8250d_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_ms8236_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	int exponent = 0, sign;

	/* buf[0] bar display. */
	/* buf[1] bar display. */

	/* Parse seven segment digit. */
	int16_t digit1 = parse_digit(buf[9]);
	int16_t digit2 = parse_digit(buf[8]);
	int16_t digit3 = parse_digit(buf[7]);
	int16_t digit4 = parse_digit(buf[6]);

	sr_dbg("Digits: %d %d %d %d.", digit1, digit2, digit3, digit4);

	/* Decimal point position. */
	if ((buf[8] & (1 << 7)) != 0) {
		exponent = -3;
		sr_spew("Decimal point after first digit.");
	} else if ((buf[7] & (1 << 7)) != 0) {
		exponent = -2;
		sr_spew("Decimal point after second digit.");
	} else if ((buf[6] & (1 << 7)) != 0) {
		exponent = -1;
		sr_spew("Decimal point after third digit.");
	} else {
		exponent = 0;
		sr_spew("No decimal point in the number.");
	}

	struct ms8236_info *info_local;

	info_local = info;

	parse_flags(buf, info_local);

	/* Sign */
	sign = (buf[10] & (1 << 3)) ? -1 : 1;

	*floatval = (double)((digit1 * 1000) + (digit2 * 100) + (digit3 * 10) + digit4);

	/* Apply sign. */
	*floatval *= sign;

	handle_flags(analog, floatval, &exponent, info_local);

	/* Check for "OL". */
	if (digit3 == 0x0F) {
		sr_spew("Over limit.");
		*floatval = INFINITY;
		return SR_OK;
	}

	sr_spew("The display value is %f.", (double)*floatval);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}
