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
 * MASTECH MS8250D protocol parser.
 *
 * Sends 18 bytes.
 * 40 02 32 75 53 33 35 5303 10 00 00 00 00 00 00 10 00
 *
 * - Communication parameters: Unidirectional, 2400/8n1
 * - CP2102 USB to UART bridge controller
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ms8250d"

/*
 * Main display (7-segment LCD value): xxDGA xxEF xxxx xxCB
 * https://en.wikipedia.org/wiki/Seven-segment_display
 */
static int parse_digit(uint16_t b)
{
	switch (b) {
	case 0x0: /* 7-segment not active */
		return 0;
	case 0x430: /* Overflow */
		return 0xF;
	case 0x533:
		return 0;
	case 0x003:
		return 1;
	case 0x721:
		return 2;
	case 0x703:
		return 3;
	case 0x213:
		return 4;
	case 0x712:
		return 5;
	case 0x732:
		return 6;
	case 0x103:
		return 7;
	case 0x733:
		return 8;
	case 0x713:
		return 9;
	default:
		sr_dbg("Invalid digit byte: 0x%02x.", b);
		return -1;
	}
}

/* Parse second display. */
static int parse_digit2(uint16_t b)
{
	switch (b) {
	case 0x00:
		return 0;
	case 0x7D:
		return 0;
	case 0x05:
		return 1;
	case 0x1B:
		return 2;
	case 0x1F:
		return 3;
	case 0x27:
		return 4;
	case 0x3E:
		return 5;
	case 0x7E:
		return 6;
	case 0x15:
		return 7;
	case 0x7F:
		return 8;
	case 0x3F:
		return 9;
	default:
		sr_dbg("Invalid second display digit byte: 0x%02x.", b);
		return -1;
	}
}

static void parse_flags(const uint8_t *buf, struct ms8250d_info *info)
{
	info->is_volt   = (buf[9]  & (1 << 4)) ? 1 : 0;
	info->is_ohm    = (buf[9]  & (1 << 6)) ? 1 : 0;
	info->is_ampere = (buf[10] & (1 << 0)) ? 1 : 0;
	info->is_hz     = (buf[10] & (1 << 2)) ? 1 : 0;
	info->is_farad  = (buf[10] & (1 << 1)) ? 1 : 0;

	/* Micro */
	if (!info->is_farad)
		info->is_micro = (buf[8] & (1 << 4)) ? 1 : 0;
	else
		info->is_micro = (buf[9] & (1 << 1)) ? 1 : 0; /* uF */

	info->is_nano  = (buf[8] & (1 << 5)) ? 1 : 0;
	info->is_milli = (buf[9] & (1 << 0)) ? 1 : 0;
	info->is_kilo  = (buf[9] & (1 << 2)) ? 1 : 0;
	info->is_mega  = (buf[8] & (1 << 6)) ? 1 : 0;

	info->is_autotimer = (buf[1]  & (1 << 0)) ? 1 : 0; /* Auto off timer */
	info->is_rs232     = (buf[1]  & (1 << 1)) ? 1 : 0; /* RS232 via USB */
	info->is_ac        = (buf[1]  & (1 << 4)) ? 1 : 0;
	info->is_dc        = (buf[2]  & (1 << 1)) ? 1 : 0;
	info->is_auto      = (buf[16] & (1 << 4)) ? 1 : 0;
	info->is_bat       = (buf[1]  & (1 << 5)) ? 1 : 0; /* Low battery */
	info->is_min       = (buf[16] & (1 << 2)) ? 1 : 0;
	info->is_max       = (buf[16] & (1 << 1)) ? 1 : 0;
	info->is_rel       = (buf[15] & (1 << 7)) ? 1 : 0;
	info->is_hold      = (buf[16] & (1 << 3)) ? 1 : 0;
	info->is_diode     = (buf[11] & (1 << 0)) ? 1 : 0;
	info->is_beep      = (buf[11] & (1 << 1)) ? 1 : 0;
	info->is_ncv       = (buf[0]  & (1 << 0)) ? 1 : 0;
}

static gboolean flags_valid(const struct ms8250d_info *info)
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
		int *exponent, const struct ms8250d_info *info)
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

SR_PRIV gboolean sr_ms8250d_packet_valid(const uint8_t *buf)
{
	struct ms8250d_info info;

	sr_dbg("DMM packet: %02x %02x %02x %02x %02x %02x %02x "
		"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
		buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13],
		buf[14], buf[15], buf[16], buf[17]);

	parse_flags(buf, &info);

	if ((buf[17] == 0x00) && flags_valid(&info))
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
SR_PRIV int sr_ms8250d_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	int exponent = 0, sec_exponent = 0, sign;
	float sec_floatval;

	/* buf[0] bar display. */
	/* buf[1] bar display. */

	/* Parse seven segment digit. */
	int16_t digit4 = parse_digit(((buf[7] & 0x73) << 4) | (buf[8] & 0x3));

	int16_t digit3 = parse_digit(((buf[6] & 0x07) << 8) | (buf[5] & 0x30) \
					| ((buf[6] & 0x30) >> 4));

	int16_t digit2 = parse_digit(((buf[4] & 0x73) << 4) | (buf[5] & 0x03));

	int16_t digit1 = parse_digit(((buf[3] & 0x07) << 8) | (buf[2] & 0x30) \
					| ((buf[3] & 0x30) >> 4));

	sr_dbg("Digits: %d %d %d %d.", digit1, digit2, digit3, digit4);

	/* Decimal point position. */
	if ((buf[3] & (1 << 6)) != 0) {
		exponent = -3;
		sr_spew("Decimal point after first digit.");
	} else if ((buf[5] & (1 << 6)) != 0) {
		exponent = -2;
		sr_spew("Decimal point after second digit.");
	} else if ((buf[7] & (1 << 2)) != 0) {
		exponent = -1;
		sr_spew("Decimal point after third digit.");
	} else {
		exponent = 0;
		sr_spew("No decimal point in the number.");
	}

	struct ms8250d_info *info_local;

	info_local = info;

	parse_flags(buf, info_local);

	/* Sign */
	sign = (buf[0] & (1 << 2)) ? -1 : 1;

	/* Parse second display. */
	int16_t sec_digit4 = parse_digit2(buf[12] & 0x7F);
	int16_t sec_digit3 = parse_digit2(buf[13] & 0x7F);
	int16_t sec_digit2 = parse_digit2(buf[14] & 0x7F);
	int16_t sec_digit1 = parse_digit2(buf[15] & 0x7F);

	sr_dbg("Digits (2nd display): %d %d %d %d.",
		sec_digit1, sec_digit2, sec_digit3, sec_digit4);

	/* Second display decimal point position. */
	if ((buf[14] & (1 << 7)) != 0) {
		sec_exponent = -3;
		sr_spew("Sec decimal point after first digit.");
	} else if ((buf[13] & (1 << 7)) != 0) {
		sec_exponent = -2;
		sr_spew("Sec decimal point after second digit.");
	} else if ((buf[12] & (1 << 7)) != 0) {
		sec_exponent = -1;
		sr_spew("Sec decimal point after third digit.");
	} else {
		sec_exponent = 0;
		sr_spew("Sec no decimal point in the number.");
	}

	*floatval = (double)((digit1 * 1000) + (digit2 * 100) + (digit3 * 10) + digit4);

	sec_floatval = (double)(sec_digit1 * 1000) + (sec_digit2 * 100) + (sec_digit3 * 10) + sec_digit4;
	sec_floatval *= powf(10, sec_exponent);

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
	sr_spew("The 2nd display value is %f.", sec_floatval);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}
