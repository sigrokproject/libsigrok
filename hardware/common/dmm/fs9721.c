/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/*
 * Fortune Semiconductor FS9721_LP3/FS9721B protocol parser.
 *
 * FS9721_LP3: 4000 counts (3 3/4 digits)
 * FS9721B/Q100: 2400 counts (3 2/3 digits)
 *
 * Same for both chips:
 *  - Packages: Bare die (78 pins) or QFP-100
 *  - Communication parameters: Unidirectional, 2400/8n1
 *  - The protocol seems to be exactly the same.
 */

#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "fs9721: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

static int parse_digit(uint8_t b)
{
	switch (b) {
	case 0x7d:
		return 0;
	case 0x05:
		return 1;
	case 0x5b:
		return 2;
	case 0x1f:
		return 3;
	case 0x27:
		return 4;
	case 0x3e:
		return 5;
	case 0x7e:
		return 6;
	case 0x15:
		return 7;
	case 0x7f:
		return 8;
	case 0x3f:
		return 9;
	default:
		sr_err("Invalid digit byte: 0x%02x.", b);
		return -1;
	}
}

/**
 * Parse the numerical value from a protocol packet.
 *
 * @param buf Buffer containing the 14-byte protocol packet.
 * @param result Pointer to a float variable. That variable will contain the
 *               result value upon parsing success.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the result
 *         variable contents are undefined and should not be used.
 */
static int parse_value(const uint8_t *buf, float *result)
{
	int i, sign, intval = 0, digits[4];
	uint8_t digit_bytes[4];
	float floatval;

	/* Byte 1: LCD SEG2 */
	sign = ((buf[1] & (1 << 3)) != 0) ? -1 : 1;

	/*
	 * Bytes 1-8: Value (4 decimal digits, sign, decimal point)
	 *
	 * Over limit: "0L" (LCD), 0x00 0x7d 0x68 0x00 (digit bytes).
	 */

	/* Merge the two nibbles for a digit into one byte. */
	for (i = 0; i < 4; i++) {
		digit_bytes[i] = ((buf[1 + (i * 2)] & 0x0f) << 4);
		digit_bytes[i] |= (buf[1 + (i * 2) + 1] & 0x0f);

		/* Bit 7 in the byte is not part of the digit. */
		digit_bytes[i] &= ~(1 << 7);
	}

	/* Check for "OL". */
	if (digit_bytes[0] == 0x00 && digit_bytes[1] == 0x7d &&
	    digit_bytes[2] == 0x68 && digit_bytes[3] == 0x00) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	}

	/* Parse the digits. */
	for (i = 0; i < 4; i++)
		digits[i] = parse_digit(digit_bytes[i]);
	sr_spew("Digits: %02x %02x %02x %02x (%d%d%d%d).",
		digit_bytes[0], digit_bytes[1], digit_bytes[2], digit_bytes[3],
		digits[0], digits[1], digits[2], digits[3]);

	/* Merge all digits into an integer value. */
	for (i = 0; i < 4; i++) {
		intval *= 10;
		intval += digits[i];
	}

	/* Store the value in a float variable. */
	floatval = (float)intval;

	/* Decimal point position. */
	if ((buf[3] & (1 << 3)) != 0) {
		floatval /= 1000;
		sr_spew("Decimal point after first digit.");
	} else if ((buf[5] & (1 << 3)) != 0) {
		floatval /= 100;
		sr_spew("Decimal point after second digit.");
	} else if ((buf[7] & (1 << 3)) != 0) {
		floatval /= 10;
		sr_spew("Decimal point after third digit.");
	} else {
		sr_spew("No decimal point in the number.");
	}

	/* Apply sign. */
	floatval *= sign;

	sr_spew("The display value is %f.", floatval);

	*result = floatval;

	return SR_OK;
}

/**
 * Parse various flags in a protocol packet.
 *
 * @param buf Buffer containing the 14-byte protocol packet.
 * @param floatval Pointer to a float variable which should contain the value
 *                 parsed using parse_value(). That variable will be modified
 *                 in-place depending on the flags in the protocol packet.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with the relevant data according to the flags in the
 *               protocol packet.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the 'floatval'
 *         and 'analog' variable contents are undefined and should not be used.
 */
static int parse_flags(const uint8_t *buf, float *floatval,
		       struct sr_datafeed_analog *analog)
{
	gboolean is_ac, is_dc, is_auto, is_rs232, is_micro, is_nano, is_kilo;
	gboolean is_diode, is_milli, is_percent, is_mega, is_beep, is_farad;
	gboolean is_ohm, is_rel, is_hold, is_ampere, is_volt, is_hz, is_bat;
	gboolean is_c2c1_11, is_c2c1_10, is_c2c1_01, is_c2c1_00;

	/* Byte 0: LCD SEG1 */
	is_ac         = (buf[0] & (1 << 3)) != 0;
	is_dc         = (buf[0] & (1 << 2)) != 0;
	is_auto       = (buf[0] & (1 << 1)) != 0;
	is_rs232      = (buf[0] & (1 << 0)) != 0;

	/* Byte 9: LCD SEG10 */
	is_micro      = (buf[9] & (1 << 3)) != 0;
	is_nano       = (buf[9] & (1 << 2)) != 0;
	is_kilo       = (buf[9] & (1 << 1)) != 0;
	is_diode      = (buf[9] & (1 << 0)) != 0;

	/* Byte 10: LCD SEG11 */
	is_milli      = (buf[10] & (1 << 3)) != 0;
	is_percent    = (buf[10] & (1 << 2)) != 0;
	is_mega       = (buf[10] & (1 << 1)) != 0;
	is_beep       = (buf[10] & (1 << 0)) != 0;

	/* Byte 11: LCD SEG12 */
	is_farad      = (buf[11] & (1 << 3)) != 0;
	is_ohm        = (buf[11] & (1 << 2)) != 0;
	is_rel        = (buf[11] & (1 << 1)) != 0;
	is_hold       = (buf[11] & (1 << 0)) != 0;

	/* Byte 12: LCD SEG13 */
	is_ampere     = (buf[12] & (1 << 3)) != 0;
	is_volt       = (buf[12] & (1 << 2)) != 0;
	is_hz         = (buf[12] & (1 << 1)) != 0;
	is_bat        = (buf[12] & (1 << 0)) != 0;

	/* Byte 13: LCD SEG14 */
	is_c2c1_11    = (buf[13] & (1 << 3)) != 0;
	is_c2c1_10    = (buf[13] & (1 << 2)) != 0;
	is_c2c1_01    = (buf[13] & (1 << 1)) != 0;
	is_c2c1_00    = (buf[13] & (1 << 0)) != 0;

	/* Factors */
	if (is_nano)
		*floatval /= 1000000000;
	if (is_micro)
		*floatval /= 1000000;
	if (is_milli)
		*floatval /= 1000;
	if (is_kilo)
		*floatval *= 1000;
	if (is_mega)
		*floatval *= 1000000;

	/* Measurement modes */
	if (is_volt) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (is_ampere) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (is_ohm) {
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (is_hz) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (is_farad) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (is_beep) {
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval < 0.0) ? 0.0 : 1.0;
	}
	if (is_diode) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (is_percent) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	}

	/* Measurement related flags */
	if (is_ac)
		analog->mqflags |= SR_MQFLAG_AC;
	if (is_dc)
		analog->mqflags |= SR_MQFLAG_DC;
	if (is_auto)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (is_hold)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (is_rel)
		analog->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (is_rs232)
		sr_spew("RS232 enabled.");
	if (is_bat)
		sr_spew("Battery is low.");
	if (is_c2c1_00)
		sr_spew("User-defined LCD symbol 0 is active.");
	if (is_c2c1_01)
		sr_spew("User-defined LCD symbol 1 is active.");
	if (is_c2c1_10)
		sr_spew("User-defined LCD symbol 2 is active.");
	if (is_c2c1_11)
		sr_spew("User-defined LCD symbol 3 is active.");

	return SR_OK;
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the 14-byte protocol packet.
 * @param floatval Pointer to a float variable. That variable will be modified
 *                 in-place depending on the protocol packet.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_dmm_parse_fs9721(const uint8_t *buf, float *floatval,
				struct sr_datafeed_analog *analog)
{
	int ret;

	if ((ret = parse_value(buf, floatval)) != SR_OK) {
		sr_err("Error parsing value: %d.", ret);
		return ret;
	}

	if ((ret = parse_flags(buf, floatval, analog)) != SR_OK) {
		sr_err("Error parsing flags: %d.", ret);
		return ret;
	}

	return SR_OK;
}
