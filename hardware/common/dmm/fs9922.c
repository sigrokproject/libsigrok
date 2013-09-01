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

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "fs9922: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

static gboolean flags_valid(const struct fs9922_info *info)
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
		sr_err("More than one multiplier detected in packet.");
		return FALSE;
	}

	/*
	 * Does the packet "measure" more than one type of value?
	 *
	 * Note: In "diode mode", both is_diode and is_volt will be set.
	 * That is a valid use-case, so we don't want to error out below
	 * if it happens. Thus, we don't check for is_diode here.
	 */
	count = 0;
	// count += (info->is_diode) ? 1 : 0;
	count += (info->is_percent) ? 1 : 0;
	count += (info->is_volt) ? 1 : 0;
	count += (info->is_ampere) ? 1 : 0;
	count += (info->is_ohm) ? 1 : 0;
	count += (info->is_hfe) ? 1 : 0;
	count += (info->is_hertz) ? 1 : 0;
	count += (info->is_farad) ? 1 : 0;
	count += (info->is_celsius) ? 1 : 0;
	count += (info->is_fahrenheit) ? 1 : 0;
	if (count > 1) {
		sr_err("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_err("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	/* Both Celsius and Fahrenheit set? */
	if (info->is_celsius && info->is_fahrenheit) {
		sr_err("Both Celsius and Fahrenheit flags detected in packet.");
		return FALSE;
	}

	return TRUE;
}

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

static void parse_flags(const uint8_t *buf, struct fs9922_info *info)
{
	/* Z1/Z2/Z3/Z4 are bits for user-defined LCD symbols (on/off). */

	/* Byte 7 */
	/* Bit 7: Always 0 */
	/* Bit 6: Always 0 */
	info->is_auto       = (buf[7] & (1 << 5)) != 0;
	info->is_dc         = (buf[7] & (1 << 4)) != 0;
	info->is_ac         = (buf[7] & (1 << 3)) != 0;
	info->is_rel        = (buf[7] & (1 << 2)) != 0;
	info->is_hold       = (buf[7] & (1 << 1)) != 0;
	info->is_bpn        = (buf[7] & (1 << 0)) != 0; /* Bargraph shown */

	/* Byte 8 */
	info->is_z1         = (buf[8] & (1 << 7)) != 0; /* User symbol 1 */
	info->is_z2         = (buf[8] & (1 << 6)) != 0; /* User symbol 2 */
	info->is_max        = (buf[8] & (1 << 5)) != 0;
	info->is_min        = (buf[8] & (1 << 4)) != 0;
	info->is_apo        = (buf[8] & (1 << 3)) != 0; /* Auto-poweroff on */
	info->is_bat        = (buf[8] & (1 << 2)) != 0; /* Battery low */
	info->is_nano       = (buf[8] & (1 << 1)) != 0;
	info->is_z3         = (buf[8] & (1 << 0)) != 0; /* User symbol 3 */

	/* Byte 9 */
	info->is_micro      = (buf[9] & (1 << 7)) != 0;
	info->is_milli      = (buf[9] & (1 << 6)) != 0;
	info->is_kilo       = (buf[9] & (1 << 5)) != 0;
	info->is_mega       = (buf[9] & (1 << 4)) != 0;
	info->is_beep       = (buf[9] & (1 << 3)) != 0;
	info->is_diode      = (buf[9] & (1 << 2)) != 0;
	info->is_percent    = (buf[9] & (1 << 1)) != 0;
	info->is_z4         = (buf[9] & (1 << 0)) != 0; /* User symbol 4 */

	/* Byte 10 */
	info->is_volt       = (buf[10] & (1 << 7)) != 0;
	info->is_ampere     = (buf[10] & (1 << 6)) != 0;
	info->is_ohm        = (buf[10] & (1 << 5)) != 0;
	info->is_hfe        = (buf[10] & (1 << 4)) != 0;
	info->is_hertz      = (buf[10] & (1 << 3)) != 0;
	info->is_farad      = (buf[10] & (1 << 2)) != 0;
	info->is_celsius    = (buf[10] & (1 << 1)) != 0; /* Only FS9922-DMM4 */
	info->is_fahrenheit = (buf[10] & (1 << 0)) != 0; /* Only FS9922-DMM4 */

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
	if (info->is_bpn) {
		info->bargraph_sign = ((buf[11] & (1 << 7)) != 0) ? -1 : 1;
		info->bargraph_value = (buf[11] & 0x7f);
		info->bargraph_value *= info->bargraph_sign;
	}

	/* Byte 12: Always '\r' (carriage return, 0x0d, 13) */

	/* Byte 13: Always '\n' (newline, 0x0a, 10) */
}

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
			 const struct fs9922_info *info)
{
	/* Factors */
	if (info->is_nano)
		*floatval /= 1000000000;
	if (info->is_micro)
		*floatval /= 1000000;
	if (info->is_milli)
		*floatval /= 1000;
	if (info->is_kilo)
		*floatval *= 1000;
	if (info->is_mega)
		*floatval *= 1000000;

	/* Measurement modes */
	if (info->is_volt || info->is_diode) {
		/* Note: In "diode mode" both is_diode and is_volt are set. */
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_ampere) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (info->is_ohm) {
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (info->is_hfe) {
		analog->mq = SR_MQ_GAIN;
		analog->unit = SR_UNIT_UNITLESS;
	}
	if (info->is_hertz) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (info->is_farad) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (info->is_celsius) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
	if (info->is_fahrenheit) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_FAHRENHEIT;
	}
	if (info->is_beep) {
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval == INFINITY) ? 0.0 : 1.0;
	}
	if (info->is_percent) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	}

	/* Measurement related flags */
	if (info->is_ac)
		analog->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->mqflags |= SR_MQFLAG_DC;
	if (info->is_auto)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (info->is_diode)
		analog->mqflags |= SR_MQFLAG_DIODE;
	if (info->is_hold)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (info->is_max)
		analog->mqflags |= SR_MQFLAG_MAX;
	if (info->is_min)
		analog->mqflags |= SR_MQFLAG_MIN;
	if (info->is_rel)
		analog->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (info->is_apo)
		sr_spew("Automatic power-off function is active.");
	if (info->is_bat)
		sr_spew("Battery is low.");
	if (info->is_z1)
		sr_spew("User-defined LCD symbol 1 is active.");
	if (info->is_z2)
		sr_spew("User-defined LCD symbol 2 is active.");
	if (info->is_z3)
		sr_spew("User-defined LCD symbol 3 is active.");
	if (info->is_z4)
		sr_spew("User-defined LCD symbol 4 is active.");
	if (info->is_bpn)
		sr_spew("The bargraph value is %d.", info->bargraph_value);
	else
		sr_spew("The bargraph is not active.");

}

SR_PRIV gboolean sr_fs9922_packet_valid(const uint8_t *buf)
{
	struct fs9922_info info;

	/* Byte 0: Sign (must be '+' or '-') */
	if (buf[0] != '+' && buf[0] != '-')
		return FALSE;

	/* Byte 12: Always '\r' (carriage return, 0x0d, 13) */
	/* Byte 13: Always '\n' (newline, 0x0a, 10) */
	if (buf[12] != '\r' || buf[13] != '\n')
		return FALSE;

	parse_flags(buf, &info);

	return flags_valid(&info);
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will contain the
 *                 result value upon parsing success. Must not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct fs9922_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_fs9922_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info)
{
	int ret;
	struct fs9922_info *info_local;

	info_local = (struct fs9922_info *)info;

	if ((ret = parse_value(buf, floatval)) != SR_OK) {
		sr_err("Error parsing value: %d.", ret);
		return ret;
	}

	parse_flags(buf, info_local);
	handle_flags(analog, floatval, info_local);

	return SR_OK;
}
