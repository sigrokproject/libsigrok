/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2013 Aurelien Jacobs <aurel@gnuage.org>
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
 * Cyrustek ES519XX protocol parser.
 *
 * Communication parameters: Unidirectional, 2400/7o1 or 19230/7o1
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "es519xx"

/* Factors for the respective measurement mode (0 means "invalid"). */
static const float factors_2400_11b[9][8] = {
	{1e-4,  1e-3,  1e-2,  1e-1, 1,    0,    0,    0   }, /* V */
	{1e-7,  1e-6,  0,     0,    0,    0,    0,    0   }, /* uA */
	{1e-5,  1e-4,  0,     0,    0,    0,    0,    0   }, /* mA */
	{1e-2,  0,     0,     0,    0,    0,    0,    0   }, /* A */
	{1e1,   1e2,   1e3,   1e4,  1e5,  1e6,  0,    0   }, /* RPM */
	{1e-1,  1,     1e1,   1e2,  1e3,  1e4,  0,    0   }, /* Resistance */
	{1,     1e1,   1e2,   1e3,  1e4,  1e5,  0,    0   }, /* Frequency */
	{1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5}, /* Capacitance */
	{1e-3,  0,     0,     0,    0,    0,    0,    0   }, /* Diode */
};
static const float factors_19200_11b_5digits[9][8] = {
	{1e-4,  1e-3,  1e-2,  1e-1, 1e-5, 0,    0,    0},    /* V */
	{1e-8,  1e-7,  0,     0,    0,    0,    0,    0},    /* uA */
	{1e-6,  1e-5,  0,     0,    0,    0,    0,    0},    /* mA */
	{0,     1e-3,  0,     0,    0,    0,    0,    0},    /* A */
	{1e-4,  1e-3,  1e-2,  1e-1, 1,    0,    0,    0},    /* Manual A */
	{1e-2,  1e-1,  1,     1e1,  1e2,  1e3,  1e4,  0},    /* Resistance */
	{1e-1,  0,     1,     1e1,  1e2,  1e3,  1e4,  0},    /* Frequency */
	{1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5}, /* Capacitance */
	{1e-4,  0,     0,     0,    0,    0,    0,    0   }, /* Diode */
};
static const float factors_19200_11b_clampmeter[9][8] = {
	{1e-3,  1e-2,  1e-1,  1,    1e-4, 0,    0,    0},    /* V */
	{1e-7,  1e-6,  0,     0,    0,    0,    0,    0},    /* uA */
	{1e-5,  1e-4,  0,     0,    0,    0,    0,    0},    /* mA */
	{1e-2,  0,     0,     0,    0,    0,    0,    0},    /* A */
	{1e-3,  1e-2,  1e-1,  1,    0,    0,    0,    0},    /* Manual A */
	{1e-1,  1,     1e1,   1e2,  1e3,  1e4,  0,    0},    /* Resistance */
	{1e-1,  0,     1,     1e1,  1e2,  1e3,  1e4,  0},    /* Frequency */
	{1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5}, /* Capacitance */
	{1e-3,  0,     0,     0,    0,    0,    0,    0   }, /* Diode */
};
static const float factors_19200_11b[9][8] = {
	{1e-3,  1e-2,  1e-1,  1,    1e-4, 0,    0,    0},    /* V */
	{1e-7,  1e-6,  0,     0,    0,    0,    0,    0},    /* uA */
	{1e-5,  1e-4,  0,     0,    0,    0,    0,    0},    /* mA */
	{1e-3,  1e-2,  0,     0,    0,    0,    0,    0},    /* A */
	{0,     0,     0,     0,    0,    0,    0,    0},    /* Manual A */
	{1e-1,  1,     1e1,   1e2,  1e3,  1e4,  0,    0},    /* Resistance */
	{1,     1e1,   1e2,   1e3,  1e4,  0,    0,    0},    /* Frequency */
	{1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 0},    /* Capacitance */
	{1e-3,  0,     0,     0,    0,    0,    0,    0},    /* Diode */
};
static const float factors_19200_14b[9][8] = {
	{1e-4,  1e-3,  1e-2,  1e-1, 1e-5, 0,    0,    0},    /* V */
	{1e-8,  1e-7,  0,     0,    0,    0,    0,    0},    /* uA */
	{1e-6,  1e-5,  0,     0,    0,    0,    0,    0},    /* mA */
	{1e-3,  0,     0,     0,    0,    0,    0,    0},    /* A */
	{1e-4,  1e-3,  1e-2,  1e-1, 1,    0,    0,    0},    /* Manual A */
	{1e-2,  1e-1,  1,     1e1,  1e2,  1e3,  1e4,  0},    /* Resistance */
	{1e-2,  1e-1,  0,     1,    1e1,  1e2,  1e3,  1e4},  /* Frequency */
	{1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5}, /* Capacitance */
	{1e-4,  0,     0,     0,    0,    0,    0,    0   }, /* Diode */
};

static int parse_value(const uint8_t *buf, struct es519xx_info *info,
                       float *result)
{
	int i, intval, num_digits;
	float floatval;

	num_digits = 4 + ((info->packet_size == 14) ? 1 : 0);

	/* Bytes 1-4 (or 5): Value (4 or 5 decimal digits) */
	if (info->is_ol) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (info->is_ul) {
		sr_spew("Under limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (!isdigit(buf[1]) || !isdigit(buf[2]) ||
	           !isdigit(buf[3]) || !isdigit(buf[4]) ||
	           (num_digits == 5 && !isdigit(buf[5]))) {
		sr_dbg("Value contained invalid digits: %02x %02x %02x %02x "
		       "(%c %c %c %c).", buf[1], buf[2], buf[3], buf[4],
		       buf[1], buf[2], buf[3], buf[4]);
		return SR_ERR;
	}
	intval = (info->is_digit4) ? 1 : 0;
	for (i = 0; i < num_digits; i++)
		intval = 10 * intval + (buf[i + 1] - '0');

	/* Apply sign. */
	intval *= info->is_sign ? -1 : 1;

	floatval = (float)intval;

	/* Note: The decimal point position will be parsed later. */

	sr_spew("The display value is %f.", floatval);

	*result = floatval;

	return SR_OK;
}

static int parse_range(uint8_t b, float *floatval,
                       const struct es519xx_info *info)
{
	int idx, mode;
	float factor = 0;

	idx = b - '0';

	if (idx < 0 || idx > 7) {
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
	else if (info->is_current && info->is_auto)
		mode = 3; /* A */
	else if (info->is_current && !info->is_auto)
		mode = 4; /* Manual A */
	else if (info->is_rpm)
		/* Not a typo, it's really index 4 in factors_2400_11b[][]. */
		mode = 4; /* RPM */
	else if (info->is_resistance || info->is_continuity)
		mode = 5; /* Resistance */
	else if (info->is_frequency)
		mode = 6; /* Frequency */
	else if (info->is_capacitance)
		mode = 7; /* Capacitance */
	else if (info->is_diode)
		mode = 8; /* Diode */
	else if (info->is_duty_cycle)
		mode = 0; /* Dummy, unused */
	else {
		sr_dbg("Invalid mode, range byte was: 0x%02x.", b);
		return SR_ERR;
	}

	if (info->is_vbar) {
		if (info->is_micro)
			factor = (const float[]){1e-1, 1}[idx];
		else if (info->is_milli)
			factor = (const float[]){1e-2, 1e-1}[idx];
	}
	else if (info->is_duty_cycle)
		factor = 1e-1;
	else if (info->baudrate == 2400)
		factor = factors_2400_11b[mode][idx];
	else if (info->fivedigits)
		factor = factors_19200_11b_5digits[mode][idx];
	else if (info->clampmeter)
		factor = factors_19200_11b_clampmeter[mode][idx];
	else if (info->packet_size == 11)
		factor = factors_19200_11b[mode][idx];
	else if (info->packet_size == 14)
		factor = factors_19200_14b[mode][idx];

	if (factor == 0) {
		sr_dbg("Invalid factor for range byte: 0x%02x.", b);
		return SR_ERR;
	}

	/* Apply respective factor (mode-dependent) on the value. */
	*floatval *= factor;
	sr_dbg("Applying factor %f, new value is %f.", factor, *floatval);

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct es519xx_info *info)
{
	int function, status;

	function = 5 + ((info->packet_size == 14) ? 1 : 0);
	status = function + 1;

	/* Status byte */
	if (info->alt_functions) {
		info->is_sign  = (buf[status] & (1 << 3)) != 0;
		info->is_batt  = (buf[status] & (1 << 2)) != 0; /* Bat. low */
		info->is_ol    = (buf[status] & (1 << 1)) != 0; /* Overflow */
		info->is_ol   |= (buf[status] & (1 << 0)) != 0; /* Overflow */
	} else {
		info->is_judge = (buf[status] & (1 << 3)) != 0;
		info->is_sign  = (buf[status] & (1 << 2)) != 0;
		info->is_batt  = (buf[status] & (1 << 1)) != 0; /* Bat. low */
		info->is_ol    = (buf[status] & (1 << 0)) != 0; /* Overflow */
	}

	if (info->packet_size == 14) {
		/* Option 1 byte */
		info->is_max  = (buf[8] & (1 << 3)) != 0;
		info->is_min  = (buf[8] & (1 << 2)) != 0;
		info->is_rel  = (buf[8] & (1 << 1)) != 0;
		info->is_rmr  = (buf[8] & (1 << 0)) != 0;

		/* Option 2 byte */
		info->is_ul   = (buf[9] & (1 << 3)) != 0; /* Underflow */
		info->is_pmax = (buf[9] & (1 << 2)) != 0; /* Max. peak value */
		info->is_pmin = (buf[9] & (1 << 1)) != 0; /* Min. peak value */

		/* Option 3 byte */
		info->is_dc   = (buf[10] & (1 << 3)) != 0;
		info->is_ac   = (buf[10] & (1 << 2)) != 0;
		info->is_auto = (buf[10] & (1 << 1)) != 0;
		info->is_vahz = (buf[10] & (1 << 0)) != 0;

		/* LPF: Low-pass filter(s) */
		if (info->selectable_lpf) {
			/* Option 4 byte */
			info->is_hold = (buf[11] & (1 << 3)) != 0;
			info->is_vbar = (buf[11] & (1 << 2)) != 0;
			info->is_lpf1 = (buf[11] & (1 << 1)) != 0;
			info->is_lpf0 = (buf[11] & (1 << 0)) != 0;
		} else {
			/* Option 4 byte */
			info->is_vbar = (buf[11] & (1 << 2)) != 0;
			info->is_hold = (buf[11] & (1 << 1)) != 0;
			info->is_lpf1 = (buf[11] & (1 << 0)) != 0;
		}
	} else if (info->alt_functions) {
		/* Option 2 byte */
		info->is_dc   = (buf[8] & (1 << 3)) != 0;
		info->is_auto = (buf[8] & (1 << 2)) != 0;
		info->is_apo  = (buf[8] & (1 << 0)) != 0;
		info->is_ac   = !info->is_dc;
	} else {
		/* Option 1 byte */
		if (info->baudrate == 2400) {
			info->is_pmax   = (buf[7] & (1 << 3)) != 0;
			info->is_pmin   = (buf[7] & (1 << 2)) != 0;
			info->is_vahz   = (buf[7] & (1 << 0)) != 0;
		} else if (info->fivedigits) {
			info->is_ul     = (buf[7] & (1 << 3)) != 0;
			info->is_pmax   = (buf[7] & (1 << 2)) != 0;
			info->is_pmin   = (buf[7] & (1 << 1)) != 0;
			info->is_digit4 = (buf[7] & (1 << 0)) != 0;
		} else if (info->clampmeter) {
			info->is_ul     = (buf[7] & (1 << 3)) != 0;
			info->is_vasel  = (buf[7] & (1 << 2)) != 0;
			info->is_vbar   = (buf[7] & (1 << 1)) != 0;
		} else {
			info->is_hold   = (buf[7] & (1 << 3)) != 0;
			info->is_max    = (buf[7] & (1 << 2)) != 0;
			info->is_min    = (buf[7] & (1 << 1)) != 0;
		}

		/* Option 2 byte */
		info->is_dc   = (buf[8] & (1 << 3)) != 0;
		info->is_ac   = (buf[8] & (1 << 2)) != 0;
		info->is_auto = (buf[8] & (1 << 1)) != 0;
		if (info->baudrate == 2400)
			info->is_apo  = (buf[8] & (1 << 0)) != 0;
		else
			info->is_vahz = (buf[8] & (1 << 0)) != 0;
	}

	/* Function byte */
	if (info->alt_functions) {
		switch (buf[function]) {
		case 0x3f: /* A */
			info->is_current = info->is_auto = TRUE;
			break;
		case 0x3e: /* uA */
			info->is_current = info->is_micro = info->is_auto = TRUE;
			break;
		case 0x3d: /* mA */
			info->is_current = info->is_milli = info->is_auto = TRUE;
			break;
		case 0x3c: /* V */
			info->is_voltage = TRUE;
			break;
		case 0x37: /* Resistance */
			info->is_resistance = TRUE;
			break;
		case 0x36: /* Continuity */
			info->is_continuity = TRUE;
			break;
		case 0x3b: /* Diode */
			info->is_diode = TRUE;
			break;
		case 0x3a: /* Frequency */
			info->is_frequency = TRUE;
			break;
		case 0x34: /* ADP0 */
		case 0x35: /* ADP0 */
			info->is_adp0 = TRUE;
			break;
		case 0x38: /* ADP1 */
		case 0x39: /* ADP1 */
			info->is_adp1 = TRUE;
			break;
		case 0x32: /* ADP2 */
		case 0x33: /* ADP2 */
			info->is_adp2 = TRUE;
			break;
		case 0x30: /* ADP3 */
		case 0x31: /* ADP3 */
			info->is_adp3 = TRUE;
			break;
		default:
			sr_dbg("Invalid function byte: 0x%02x.", buf[function]);
			break;
		}
	} else {
		/* Note: Some of these mappings are fixed up later. */
		switch (buf[function]) {
		case 0x3b: /* V */
			info->is_voltage = TRUE;
			break;
		case 0x3d: /* uA */
			info->is_current = info->is_micro = info->is_auto = TRUE;
			break;
		case 0x3f: /* mA */
			info->is_current = info->is_milli = info->is_auto = TRUE;
			break;
		case 0x30: /* A */
			info->is_current = info->is_auto = TRUE;
			break;
		case 0x39: /* Manual A */
			info->is_current = TRUE;
			info->is_auto = FALSE; /* Manual mode */
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
		case 0x32: /* Frequency / RPM / duty cycle */
			if (info->packet_size == 14) {
				if (info->is_judge)
					info->is_duty_cycle = TRUE;
				else
					info->is_frequency = TRUE;
			} else {
				if (info->is_judge)
					info->is_rpm = TRUE;
				else
					info->is_frequency = TRUE;
			}
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
		case 0x3e: /* ADP0 */
			info->is_adp0 = TRUE;
			break;
		case 0x3c: /* ADP1 */
			info->is_adp1 = TRUE;
			break;
		case 0x38: /* ADP2 */
			info->is_adp2 = TRUE;
			break;
		case 0x3a: /* ADP3 */
			info->is_adp3 = TRUE;
			break;
		default:
			sr_dbg("Invalid function byte: 0x%02x.", buf[function]);
			break;
		}
	}

	if (info->is_vahz && (info->is_voltage || info->is_current)) {
		info->is_voltage = FALSE;
		info->is_current = FALSE;
		info->is_milli = info->is_micro = FALSE;
		if (info->packet_size == 14) {
			if (info->is_judge)
				info->is_duty_cycle = TRUE;
			else
				info->is_frequency = TRUE;
		} else {
			if (info->is_judge)
				info->is_rpm = TRUE;
			else
				info->is_frequency = TRUE;
		}
	}

	if (info->is_current && (info->is_micro || info->is_milli) && info->is_vasel) {
		info->is_current = info->is_auto = FALSE;
		info->is_voltage = TRUE;
	}

	if (info->baudrate == 2400) {
		/* Inverted mapping between mA and A, and no manual A. */
		if (info->is_current && (info->is_milli || !info->is_auto)) {
			info->is_milli = !info->is_milli;
			info->is_auto = TRUE;
		}
	}
}

static void handle_flags(struct sr_datafeed_analog *analog,
			 float *floatval, const struct es519xx_info *info)
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
		*floatval = (*floatval < 0.0 || *floatval > 25.0) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_rpm) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_REVOLUTIONS_PER_MINUTE;
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
	if (info->is_diode)
		analog->mqflags |= SR_MQFLAG_DIODE;
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
	if (info->is_ul)
		sr_spew("Input underflow.");
	if (info->is_pmax)
		sr_spew("pMAX active, LCD shows max. peak value.");
	if (info->is_pmin)
		sr_spew("pMIN active, LCD shows min. peak value.");
	if (info->is_vahz)
		sr_spew("VAHZ active.");
	if (info->is_apo)
		sr_spew("Auto-Power-Off enabled.");
	if (info->is_vbar)
		sr_spew("VBAR active.");
	if ((!info->selectable_lpf && info->is_lpf1) ||
	    (info->selectable_lpf && (!info->is_lpf0 || !info->is_lpf1)))
		sr_spew("Low-pass filter feature is active.");
}

static gboolean flags_valid(const struct es519xx_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count  = (info->is_micro) ? 1 : 0;
	count += (info->is_milli) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count  = (info->is_voltage) ? 1 : 0;
	count += (info->is_current) ? 1 : 0;
	count += (info->is_resistance) ? 1 : 0;
	count += (info->is_frequency) ? 1 : 0;
	count += (info->is_capacitance) ? 1 : 0;
	count += (info->is_temperature) ? 1 : 0;
	count += (info->is_continuity) ? 1 : 0;
	count += (info->is_diode) ? 1 : 0;
	count += (info->is_rpm) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_dbg("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	return TRUE;
}

static gboolean sr_es519xx_packet_valid(const uint8_t *buf,
                                        struct es519xx_info *info)
{
	int s;

	s = info->packet_size;

	if (s == 11 && memcmp(buf, buf + s, s))
		return FALSE;

	if (buf[s - 2] != '\r' || buf[s - 1] != '\n')
		return FALSE;

	parse_flags(buf, info);

	if (!flags_valid(info))
		return FALSE;

	return TRUE;
}

static int sr_es519xx_parse(const uint8_t *buf, float *floatval,
                            struct sr_datafeed_analog *analog,
                            struct es519xx_info *info)
{
	int ret;

	if (!sr_es519xx_packet_valid(buf, info))
		return SR_ERR;

	if ((ret = parse_value(buf, info, floatval)) != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	if ((ret = parse_range(buf[0], floatval, info)) != SR_OK)
		return ret;

	handle_flags(analog, floatval, info);
	return SR_OK;
}

/*
 * Functions for 2400 baud / 11 bytes protocols.
 * This includes ES51962, ES51971, ES51972, ES51978 and ES51989.
 */
SR_PRIV gboolean sr_es519xx_2400_11b_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 2400;
	info.packet_size = 11;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_2400_11b_parse(const uint8_t *buf, float *floatval,
				struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 2400;
	info_local->packet_size = 11;

	return sr_es519xx_parse(buf, floatval, analog, info);
}

/*
 * Functions for 2400 baud / 11 byte protocols.
 * This includes ES51960, ES51977 and ES51988.
 */
SR_PRIV gboolean sr_es519xx_2400_11b_altfn_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 2400;
	info.packet_size = 11;
	info.alt_functions = TRUE;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_2400_11b_altfn_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 2400;
	info_local->packet_size = 11;
	info_local->alt_functions = TRUE;

	return sr_es519xx_parse(buf, floatval, analog, info);
}

/*
 * Functions for 19200 baud / 11 bytes protocols with 5 digits display.
 * This includes ES51911, ES51916 and ES51918.
 */
SR_PRIV gboolean sr_es519xx_19200_11b_5digits_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 19200;
	info.packet_size = 11;
	info.fivedigits = TRUE;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_19200_11b_5digits_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 19200;
	info_local->packet_size = 11;
	info_local->fivedigits = TRUE;

	return sr_es519xx_parse(buf, floatval, analog, info);
}

/*
 * Functions for 19200 baud / 11 bytes protocols with clamp meter support.
 * This includes ES51967 and ES51969.
 */
SR_PRIV gboolean sr_es519xx_19200_11b_clamp_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 19200;
	info.packet_size = 11;
	info.clampmeter = TRUE;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_19200_11b_clamp_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 19200;
	info_local->packet_size = 11;
	info_local->clampmeter = TRUE;

	return sr_es519xx_parse(buf, floatval, analog, info);
}

/*
 * Functions for 19200 baud / 11 bytes protocols.
 * This includes ES51981, ES51982, ES51983, ES51984 and ES51986.
 */
SR_PRIV gboolean sr_es519xx_19200_11b_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 19200;
	info.packet_size = 11;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_19200_11b_parse(const uint8_t *buf, float *floatval,
			struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 19200;
	info_local->packet_size = 11;

	return sr_es519xx_parse(buf, floatval, analog, info);
}

/*
 * Functions for 19200 baud / 14 bytes protocols.
 * This includes ES51921 and ES51922.
 */
SR_PRIV gboolean sr_es519xx_19200_14b_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 19200;
	info.packet_size = 14;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_19200_14b_parse(const uint8_t *buf, float *floatval,
			struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 19200;
	info_local->packet_size = 14;

	return sr_es519xx_parse(buf, floatval, analog, info);
}

/*
 * Functions for 19200 baud / 14 bytes protocols with selectable LPF.
 * This includes ES51931 and ES51932.
 */
SR_PRIV gboolean sr_es519xx_19200_14b_sel_lpf_packet_valid(const uint8_t *buf)
{
	struct es519xx_info info;

	memset(&info, 0, sizeof(struct es519xx_info));
	info.baudrate = 19200;
	info.packet_size = 14;
	info.selectable_lpf = TRUE;

	return sr_es519xx_packet_valid(buf, &info);
}

SR_PRIV int sr_es519xx_19200_14b_sel_lpf_parse(const uint8_t *buf,
		float *floatval, struct sr_datafeed_analog *analog, void *info)
{
	struct es519xx_info *info_local;

	info_local = info;
	memset(info_local, 0, sizeof(struct es519xx_info));
	info_local->baudrate = 19200;
	info_local->packet_size = 14;
	info_local->selectable_lpf = TRUE;

	return sr_es519xx_parse(buf, floatval, analog, info);
}
