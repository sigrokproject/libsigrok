/*
 * This file is part of the libsigrok project.
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
 * Fortune Semiconductor FS9922-DMM3/FS9922-DMM4 protocol parser.
 */

#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "fs9922: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

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
	int sign, intval;
	float floatval;

	/* Byte 0: Sign ('+' or '-') */
	if (buf[0] == '+') {
		sign = 1;
	} else if (buf[0] == '-') {
		sign = -1;
	} else {
		sr_err("Invalid sign byte: 0x%02x.", buf[0]);
		return SR_ERR;
	}

	/*
	 * Bytes 1-4: Value (4 decimal digits)
	 *
	 * Over limit: "0.L" on the display, "?0:?" as protocol "digits".
	 */
	if (buf[1] == '?' && buf[2] == '0' && buf[3] == ':' && buf[4] == '?') {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (!isdigit(buf[1]) || !isdigit(buf[2]) ||
		   !isdigit(buf[3]) || !isdigit(buf[4])) {
		sr_err("Value contained invalid digits: %02x %02x %02x %02x ("
		       "%c %c %c %c).", buf[1], buf[2], buf[3], buf[4]);
		return SR_ERR;
	}
	intval = 0;
	intval += (buf[1] - '0') * 1000;
	intval += (buf[2] - '0') * 100;
	intval += (buf[3] - '0') * 10;
	intval += (buf[4] - '0') * 1;

	floatval = (float)intval;

	/* Byte 5: Always ' ' (space, 0x20) */

	/*
	 * Byte 6: Decimal point position ('0', '1', '2', or '4')
	 *
	 * Note: The Fortune Semiconductor FS9922-DMM3/4 datasheets both have
	 * an error/typo here. They claim that the values '0'/'1'/'2'/'3' are
	 * used, but '0'/'1'/'2'/'4' is actually correct.
	 */
	if (buf[6] != '0' && buf[6] != '1' && buf[6] != '2' && buf[6] != '4') {
		sr_err("Invalid decimal point value: 0x%02x.", buf[6]);
		return SR_ERR;
	}
	if (buf[6] == '0')
		floatval /= 1;
	else if (buf[6] == '1')
		floatval /= 1000;
	else if (buf[6] == '2')
		floatval /= 100;
	else if (buf[6] == '4')
		floatval /= 10;

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
	gboolean is_auto, is_dc, is_ac, is_rel, is_hold, is_bpn, is_z1, is_z2;
	gboolean is_max, is_min, is_apo, is_bat, is_nano, is_z3, is_micro;
	gboolean is_milli, is_kilo, is_mega, is_beep, is_diode, is_percent;
	gboolean is_z4, is_volt, is_ampere, is_ohm, is_hfe, is_hertz, is_farad;
	gboolean is_celsius, is_fahrenheit;
	int bargraph_sign, bargraph_value;

	/* Z1/Z2/Z3/Z4 are bits for user-defined LCD symbols (on/off). */

	/* Byte 7 */
	/* Bit 7: Always 0 */
	/* Bit 6: Always 0 */
	is_auto       = (buf[7] & (1 << 5)) != 0;
	is_dc         = (buf[7] & (1 << 4)) != 0;
	is_ac         = (buf[7] & (1 << 3)) != 0;
	is_rel        = (buf[7] & (1 << 2)) != 0;
	is_hold       = (buf[7] & (1 << 1)) != 0;
	is_bpn        = (buf[7] & (1 << 0)) != 0; /* Bargraph shown */

	/* Byte 8 */
	is_z1         = (buf[8] & (1 << 7)) != 0; /* User-defined symbol 1 */
	is_z2         = (buf[8] & (1 << 6)) != 0; /* User-defined symbol 2 */
	is_max        = (buf[8] & (1 << 5)) != 0;
	is_min        = (buf[8] & (1 << 4)) != 0;
	is_apo        = (buf[8] & (1 << 3)) != 0; /* Auto-poweroff active */
	is_bat        = (buf[8] & (1 << 2)) != 0; /* Battery low */
	is_nano       = (buf[8] & (1 << 1)) != 0;
	is_z3         = (buf[8] & (1 << 0)) != 0; /* User-defined symbol 3 */

	/* Byte 9 */
	is_micro      = (buf[9] & (1 << 7)) != 0;
	is_milli      = (buf[9] & (1 << 6)) != 0;
	is_kilo       = (buf[9] & (1 << 5)) != 0;
	is_mega       = (buf[9] & (1 << 4)) != 0;
	is_beep       = (buf[9] & (1 << 3)) != 0;
	is_diode      = (buf[9] & (1 << 2)) != 0;
	is_percent    = (buf[9] & (1 << 1)) != 0;
	is_z4         = (buf[8] & (1 << 0)) != 0; /* User-defined symbol 4 */

	/* Byte 10 */
	is_volt       = (buf[10] & (1 << 7)) != 0;
	is_ampere     = (buf[10] & (1 << 6)) != 0;
	is_ohm        = (buf[10] & (1 << 5)) != 0;
	is_hfe        = (buf[10] & (1 << 4)) != 0;
	is_hertz      = (buf[10] & (1 << 3)) != 0;
	is_farad      = (buf[10] & (1 << 2)) != 0;
	is_celsius    = (buf[10] & (1 << 1)) != 0; /* Only FS9922-DMM4 */
	is_fahrenheit = (buf[10] & (1 << 0)) != 0; /* Only FS9922-DMM4 */

	/*
	 * Byte 11: Bar graph
	 *
	 * Bit 7 contains the sign of the bargraph number (if the bit is set,
	 * the number is negative), bits 6..0 contain the actual number.
	 * Valid range: 0-40 (FS9922-DMM3), 0-60 (FS9922-DMM4).
	 *
	 * Upon "over limit" the bargraph value is 1 count above the highest
	 * valid number (i.e. 41 or 61, depending on chip).
	 */
	if (is_bpn) {
		bargraph_sign = ((buf[11] & (1 << 7)) != 0) ? -1 : 1;
		bargraph_value = (buf[11] & 0x7f);
		bargraph_value *= bargraph_sign;
		sr_spew("The bargraph value is %d.", bargraph_value);
	} else {
		sr_spew("The bargraph is not active.");
	}

	/* Byte 12: Always '\r' (carriage return, 0x0d, 13) */

	/* Byte 13: Always '\n' (newline, 0x0a, 10) */

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
	if (is_hfe) {
		analog->mq = SR_MQ_GAIN;
		analog->unit = SR_UNIT_UNITLESS;
	}
	if (is_hertz) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (is_farad) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (is_celsius) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
	if (is_fahrenheit) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_FAHRENHEIT;
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
	if (is_max)
		analog->mqflags |= SR_MQFLAG_MAX;
	if (is_min)
		analog->mqflags |= SR_MQFLAG_MIN;
	if (is_rel)
		analog->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (is_apo)
		sr_spew("Automatic power-off function is active.");
	if (is_bat)
		sr_spew("Battery is low.");
	if (is_z1)
		sr_spew("User-defined LCD symbol 1 is active.");
	if (is_z2)
		sr_spew("User-defined LCD symbol 2 is active.");
	if (is_z3)
		sr_spew("User-defined LCD symbol 3 is active.");
	if (is_z4)
		sr_spew("User-defined LCD symbol 4 is active.");

	return SR_OK;
}

/**
 * Parse a Fortune Semiconductor FS9922-DMM3/4 protocol packet.
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
SR_PRIV int sr_dmm_parse_fs9922(const uint8_t *buf, float *floatval,
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
