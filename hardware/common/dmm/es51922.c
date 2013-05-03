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
 * Cyrustek ES51922 protocol parser.
 *
 * Communication parameters: Unidirectional, 19230/7o1
 */

#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "es51922: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

/* Factors for the respective measurement mode (0 means "invalid"). */
static const float factors[8][8] = {
	{1e-4,  1e-3,  1e-2,  1e-1, 1e-5, 0,    0,    0},    /* V */
	{1e-8,  1e-7,  0,     0,    0,    0,    0,    0},    /* uA */
	{1e-6,  1e-5,  0,     0,    0,    0,    0,    0},    /* mA */
	{1e-3,  0,     0,     0,    0,    0,    0,    0},    /* 22A */
	{1e-4,  1e-3,  1e-2,  1e-1, 1,    0,    0,    0},    /* Manual A */
	{1e-2,  1e-1,  1,     1e1,  1e2,  1e3,  1e4,  0},    /* Resistance */
	{1e-2,  1e-1,  0,     1,    1e1,  1e2,  1e3,  1e4},  /* Frequency */
	{1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5}, /* Capacitance */
};

static int parse_value(const uint8_t *buf, float *result)
{
	int sign, intval;
	float floatval;

	/*
	 * Bytes 1-5: Value (5 decimal digits)
	 *
	 * Over limit: "0L." on the display, "22580" as protocol "digits".
	 *             (chip max. value is 22000, so 22580 is out of range)
	 *
	 * Example: "OL.", auto-range mega-ohm mode
	 *          Hex:   36  32 32 35 38 30  33 31 30 30 32 30 0d 0a
	 *          ASCII:      2  2  5  8  0
	 */
	if (!strncmp((const char *)&buf[1], "22580", 5)) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (!isdigit(buf[1]) || !isdigit(buf[2]) ||
		   !isdigit(buf[3]) || !isdigit(buf[4]) || !isdigit(buf[5])) {
		sr_err("Value contained invalid digits: %02x %02x %02x %02x "
		       "%02x (%c %c %c %c %c).",
			buf[1], buf[2], buf[3], buf[4], buf[5]);
		return SR_ERR;
	}
	intval = 0;
	intval += (buf[1] - '0') * 10000;
	intval += (buf[2] - '0') * 1000;
	intval += (buf[3] - '0') * 100;
	intval += (buf[4] - '0') * 10;
	intval += (buf[5] - '0') * 1;

	floatval = (float)intval;

	/* Note: The decimal point position will be parsed later. */

	/* Byte 7: Sign bit (and other stuff) */
	sign = ((buf[7] & (1 << 2)) != 0) ? -1 : 1;

	/* Apply sign. */
	floatval *= sign;

	sr_spew("The display value is %f.", floatval);

	*result = floatval;

	return SR_OK;
}

static int parse_range(uint8_t b, float *floatval,
		       const struct es51922_info *info)
{
	int idx, mode;

	idx = b - '0';

	if (!(idx >= 0 && idx <= 7)) {
		sr_dbg("Invalid range byte / index: 0x%02x / 0x%02x.", b, idx);
		return SR_ERR;
	}

	/* Parse range byte (depends on the measurement mode). */
	if (info->is_voltage)
		mode = 0; /* V */
	else if (info->is_current && info->is_micro)
		mode = 1; /* uA */
	else if (info->is_current && info->is_milli)
		mode = 2; /* mA */
	else if (info->is_current && !info->is_micro && !info->is_milli)
		mode = 3; /* 22A */
	else if (info->is_current && !info->is_auto)
		mode = 4; /* Manual A */
	else if (info->is_resistance)
		mode = 5; /* Resistance */
	else if (info->is_frequency)
		mode = 6; /* Frequency */
	else if (info->is_capacitance)
		mode = 7; /* Capacitance */
	else {
		sr_dbg("Invalid mode, range byte was: 0x%02x.", b);
		return SR_ERR;
	}

	if (factors[mode][idx] == 0) {
		sr_dbg("Invalid factor for range byte: 0x%02x.", b);
		return SR_ERR;
	}

	/* Apply respective factor (mode-dependent) on the value. */
	*floatval *= factors[mode][idx];
	sr_dbg("Applying factor %f, new value is %f.",
	       factors[mode][idx], *floatval);

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct es51922_info *info)
{
	/* Get is_judge and is_vbar early on, we'll need it. */
	info->is_judge = (buf[7] & (1 << 3)) != 0;
	info->is_vbar = (buf[11] & (1 << 2)) != 0;

	/* Byte 6: Function */
	switch (buf[6]) {
	case 0x3b: /* V */
		info->is_voltage = TRUE;
		break;
	case 0x3d: /* uA */
		info->is_auto = info->is_micro = info->is_current = TRUE;
		break;
	case 0x3f: /* mA */
		info->is_auto = info->is_milli = info->is_current = TRUE;
		break;
	case 0x30: /* 22A */
		info->is_current = TRUE;
		break;
	case 0x39: /* Manual A */
		info->is_auto = FALSE; /* Manual mode */
		info->is_current = TRUE;
		break;
	case 0x33: /* Resistance */
		info->is_resistance = TRUE;
		break;
	case 0x35: /* Continuity */
		info->is_continuity = TRUE;
		break;
	case 0x31: /* Diode */
		info->is_diode = TRUE;
		break;
	case 0x32: /* Frequency / duty cycle */
		if (info->is_judge)
			info->is_frequency = TRUE;
		else
			info->is_duty_cycle = TRUE;
		break;
	case 0x36: /* Capacitance */
		info->is_capacitance = TRUE;
		break;
	case 0x34: /* Temperature */
		info->is_temperature = TRUE;
		if (info->is_judge)
			info->is_celsius = TRUE;
		else
			info->is_fahrenheit = TRUE;
		/* IMPORTANT: The digits always represent Celsius! */
		break;
	case 0x3e: /* ADP */
		info->is_adp = TRUE;
		break;
	default:
		sr_err("Invalid function byte: 0x%02x.", buf[6]);
		break;
	}

	/* Byte 7: Status */
	/* Bits [6:4]: Always 0b011 */
	info->is_judge = (buf[7] & (1 << 3)) != 0;
	info->is_sign  = (buf[7] & (1 << 2)) != 0;
	info->is_batt  = (buf[7] & (1 << 1)) != 0; /* Battery low */
	info->is_ol    = (buf[7] & (1 << 0)) != 0; /* Input overflow */

	/* Byte 8: Option 1 */
	/* Bits [6:4]: Always 0b011 */
	info->is_max   = (buf[8] & (1 << 3)) != 0;
	info->is_min   = (buf[8] & (1 << 2)) != 0;
	info->is_rel   = (buf[8] & (1 << 1)) != 0;
	info->is_rmr   = (buf[8] & (1 << 0)) != 0;

	/* Byte 9: Option 2 */
	/* Bits [6:4]: Always 0b011 */
	info->is_ul    = (buf[9] & (1 << 3)) != 0;
	info->is_pmax  = (buf[9] & (1 << 2)) != 0; /* Max. peak value */
	info->is_pmin  = (buf[9] & (1 << 1)) != 0; /* Min. peak value */
	/* Bit 0: Always 0 */

	/* Byte 10: Option 3 */
	/* Bits [6:4]: Always 0b011 */
	info->is_dc    = (buf[10] & (1 << 3)) != 0;
	info->is_ac    = (buf[10] & (1 << 2)) != 0;
	info->is_auto  = (buf[10] & (1 << 1)) != 0;
	info->is_vahz  = (buf[10] & (1 << 0)) != 0;

	/* Byte 11: Option 4 */
	/* Bits [6:3]: Always 0b0110 */
	info->is_vbar  = (buf[11] & (1 << 2)) != 0;
	info->is_hold  = (buf[11] & (1 << 1)) != 0;
	info->is_lpf   = (buf[11] & (1 << 0)) != 0; /* Low pass filter on */

	/* Byte 12: Always '\r' (carriage return, 0x0d, 13) */

	/* Byte 13: Always '\n' (newline, 0x0a, 10) */
}

static void handle_flags(struct sr_datafeed_analog *analog,
			 float *floatval, const struct es51922_info *info)
{
	/*
	 * Note: is_micro etc. are not used directly to multiply/divide
	 * floatval, this is handled via parse_range() and factors[][].
	 */

	/* Measurement modes */
	if (info->is_voltage) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_current) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (info->is_resistance) {
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (info->is_frequency) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (info->is_capacitance) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (info->is_temperature && info->is_celsius) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
	if (info->is_temperature && info->is_fahrenheit) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_FAHRENHEIT;
	}
	if (info->is_continuity) {
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval < 0.0) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_duty_cycle) {
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
	if (info->is_hold)
		/*
		* Note: HOLD only affects the number displayed on the LCD,
		* but not the value sent via the protocol! It also does not
		* affect the bargraph on the LCD.
		*/
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (info->is_max)
		analog->mqflags |= SR_MQFLAG_MAX;
	if (info->is_min)
		analog->mqflags |= SR_MQFLAG_MIN;
	if (info->is_rel)
		analog->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (info->is_judge)
		sr_spew("Judge bit is set.");
	if (info->is_batt)
		sr_spew("Battery is low.");
	if (info->is_ol)
		sr_spew("Input overflow.");
	if (info->is_pmax)
		sr_spew("pMAX active, LCD shows max. peak value.");
	if (info->is_pmin)
		sr_spew("pMIN active, LCD shows min. peak value.");
	if (info->is_vahz)
		sr_spew("VAHZ active.");
	if (info->is_vbar)
		sr_spew("VBAR active.");
	if (info->is_lpf)
		sr_spew("Low-pass filter feature is active.");
}

static gboolean flags_valid(const struct es51922_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count = 0;
	count += (info->is_nano) ? 1 : 0;
	count += (info->is_micro) ? 1 : 0;
	count += (info->is_milli) ? 1 : 0;
	/* Note: No 'kilo' or 'mega' bits per se in this protocol. */
	if (count > 1) {
		sr_err("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count = 0;
	count += (info->is_voltage) ? 1 : 0;
	count += (info->is_current) ? 1 : 0;
	count += (info->is_resistance) ? 1 : 0;
	count += (info->is_frequency) ? 1 : 0;
	count += (info->is_capacitance) ? 1 : 0;
	count += (info->is_temperature) ? 1 : 0;
	count += (info->is_continuity) ? 1 : 0;
	count += (info->is_diode) ? 1 : 0;
	count += (info->is_duty_cycle) ? 1 : 0;
	if (count > 1) {
		sr_err("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_err("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	return TRUE;
}

SR_PRIV gboolean sr_es51922_packet_valid(const uint8_t *buf)
{
	struct es51922_info info;

	memset(&info, 0x00, sizeof(struct es51922_info));
	parse_flags(buf, &info);

	if (!flags_valid(&info))
		return FALSE;

	if (buf[12] != '\r' || buf[13] != '\n') {
		sr_spew("Packet doesn't end with \\r\\n.");
		return FALSE;
	}

	return TRUE;
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
 * @param info Pointer to a struct es51922_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_es51922_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info)
{
	int ret;
	struct es51922_info *info_local;

	info_local = (struct es51922_info *)info;

	if ((ret = parse_value(buf, floatval)) != SR_OK) {
		sr_err("Error parsing value: %d.", ret);
		return ret;
	}

	memset(info_local, 0x00, sizeof(struct es51922_info));
	parse_flags(buf, info_local);
	handle_flags(analog, floatval, info_local);

	return parse_range(buf[0], floatval, info_local);
}
