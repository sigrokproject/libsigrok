/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014-2015 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "vc870"

/* Exponents for the respective measurement mode. */
static const int exponents[][8] = {
	{  -4,  -3,  -2, -1,  0,  0,  0,  0 }, /* DCV */
	{  -3,  -2,  -1,  0,  0,  0,  0,  0 }, /* ACV */
	{  -5,   0,   0,  0,  0,  0,  0,  0 }, /* DCmV */
	{  -1,   0,   0,  0,  0,  0,  0,  0 }, /* Temperature (C) */
//	{  -2,   0,   0,  0,  0,  0,  0,  0 }, /* TODO: Temperature (F) */
	/*
	 * Note: The sequence -1 -> 1 for the resistance
	 * value is correct and verified in practice!
	 * Don't trust the vendor docs on this.
	 */
	{  -2,  -1,   1,  2,  3,  4,  0,  0 }, /* Resistance */
	{  -2,   0,   0,  0,  0,  0,  0,  0 }, /* Continuity */
	{ -12, -11, -10, -9, -8, -7, -6,  0 }, /* Capacitance */
	{  -4,   0,   0,  0,  0,  0,  0,  0 }, /* Diode */
	{  -3,  -2,  -1,  0,  1,  2,  3,  4 }, /* Frequency */
	{  -2,   0,   0,  0,  0,  0,  0,  0 }, /* Loop current */
	/*
	 * Note: Measurements showed that AC and DC differ
	 * in the exponents used, although docs say they should
	 * be the same.
	 */
	{  -8,  -7,   0,  0,  0,  0,  0,  0 }, /* DCµA */
	{  -7,  -6,   0,  0,  0,  0,  0,  0 }, /* ACµA */
	{  -6,  -5,   0,  0,  0,  0,  0,  0 }, /* DCmA */
	{  -5,  -4,   0,  0,  0,  0,  0,  0 }, /* ACmA */
	{  -3,   0,   0,  0,  0,  0,  0,  0 }, /* DCA */
	/* TODO: Verify exponent for ACA */
	{  -3,   0,   0,  0,  0,  0,  0,  0 }, /* ACA */
	{  -1,   0,   0,  0,  0,  0,  0,  0 }, /* Act+apparent power */
	{  -3,   0,   0,  0,  0,  0,  0,  0 }, /* Power exponent / freq */
	{  -1,   0,   0,  0,  0,  0,  0,  0 }, /* V eff + A eff */
};

static int parse_value(const uint8_t *buf, struct vc870_info *info,
                       float *result)
{
	int i, intval;

	/* Bytes 3-7: Main display value (5 decimal digits) */
	if (info->is_open || info->is_ol1) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (!isdigit(buf[3]) || !isdigit(buf[4]) ||
	           !isdigit(buf[5]) || !isdigit(buf[6]) || !isdigit(buf[7])) {
		sr_dbg("Invalid digits: %02x %02x %02x %02x %02X "
			"(%c %c %c %c %c).",
			buf[3], buf[4], buf[5], buf[6], buf[7],
			buf[3], buf[4], buf[5], buf[6], buf[7]);
		return SR_ERR;
	}

	intval = 0;
	for (i = 0; i < 5; i++)
		intval = 10 * intval + (buf[i + 3] - '0'); /* Main display. */
		// intval = 10 * intval + (buf[i + 8] - '0'); /* TODO: Aux display. */

	/* Apply sign. */
	intval *= info->is_sign1 ? -1 : 1;
	// intval *= info->is_sign2 ? -1 : 1; /* TODO: Fahrenheit / aux display. */

	/* Note: The decimal point position will be parsed later. */

	sr_spew("The display value without comma is %05d.", intval);

	*result = (float)intval;

	return SR_OK;
}

static int parse_range(uint8_t b, float *floatval, int *exponent,
                       const struct vc870_info *info)
{
	int idx, mode;

	idx = b - '0';

	if (idx < 0 || idx > 7) {
		sr_dbg("Invalid range byte / index: 0x%02x / 0x%02x.", b, idx);
		return SR_ERR;
	}

	/* Parse range byte (depends on the measurement mode). */
	if (info->is_voltage && info->is_dc && !info->is_milli)
		mode = 0; /* DCV */
	else if (info->is_voltage && info->is_ac)
		mode = 1; /* ACV */
	else if (info->is_voltage && info->is_dc && info->is_milli)
		mode = 2; /* DCmV */
	else if (info->is_temperature)
		mode = 3; /* Temperature */
	else if (info->is_resistance || info->is_continuity)
		mode = 4; /* Resistance */
	else if (info->is_continuity)
		mode = 5; /* Continuity */
	else if (info->is_capacitance)
		mode = 6; /* Capacitance */
	else if (info->is_diode)
		mode = 7; /* Diode */
	else if (info->is_frequency)
		mode = 8; /* Frequency */
	else if (info->is_loop_current)
		mode = 9; /* Loop current */
	else if (info->is_current && info->is_micro && info->is_dc)
		mode = 10; /* DCµA */
	else if (info->is_current && info->is_micro && info->is_ac)
		mode = 11; /* ACµA */
	else if (info->is_current && info->is_milli && info->is_dc)
		mode = 12; /* DCmA */
	else if (info->is_current && info->is_milli && info->is_ac)
		mode = 13; /* ACmA */
	else if (info->is_current && !info->is_milli && !info->is_micro && info->is_dc)
		mode = 14; /* DCA */
	else if (info->is_current && !info->is_milli && !info->is_micro && info->is_ac)
		mode = 15; /* ACA */
	else if (info->is_power_apparent_power)
		mode = 16; /* Act+apparent power */
	else if (info->is_power_factor_freq)
		mode = 17; /* Power factor / freq */
	else if (info->is_v_a_rms_value)
		mode = 18; /* V eff + A eff */
	else {
		sr_dbg("Invalid mode, range byte was: 0x%02x.", b);
		return SR_ERR;
	}

	*exponent = exponents[mode][idx];

	/* Apply respective exponent (mode-dependent) on the value. */
	*floatval *= powf(10, *exponent);
	sr_dbg("Applying exponent %d, new value is %f.", *exponent, *floatval);

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct vc870_info *info)
{
	/* Bytes 0/1: Function / function select */
	/* Note: Some of these mappings are fixed up later. */
	switch (buf[0]) {
	case 0x30: /* DCV / ACV */
		info->is_voltage = TRUE;
		info->is_dc = (buf[1] == 0x30);
		info->is_ac = (buf[1] == 0x31);
		break;
	case 0x31: /* DCmV / Celsius */
		if (buf[1] == 0x30)
			info->is_voltage = info->is_milli = info->is_dc = TRUE;
		else if (buf[1] == 0x31)
			info->is_temperature = TRUE;
		break;
	case 0x32: /* Resistance / Short-circuit test */
		info->is_resistance = (buf[1] == 0x30);
		info->is_continuity = (buf[1] == 0x31);
		break;
	case 0x33: /* Capacitance */
		info->is_capacitance = (buf[1] == 0x30);
		break;
	case 0x34: /* Diode */
		info->is_diode = (buf[1] == 0x30);
		break;
	case 0x35: /* (4~20mA)% */
		info->is_frequency = (buf[1] == 0x30);
		info->is_loop_current = (buf[1] == 0x31);
		break;
	case 0x36: /* DCµA / ACµA */
		info->is_current = info->is_micro = TRUE;
		info->is_dc = (buf[1] == 0x30);
		info->is_ac = (buf[1] == 0x31);
		break;
	case 0x37: /* DCmA / ACmA */
		info->is_current = info->is_milli = TRUE;
		info->is_dc = (buf[1] == 0x30);
		info->is_ac = (buf[1] == 0x31);
		break;
	case 0x38: /* DCA / ACA */
		info->is_current = TRUE;
		info->is_dc = (buf[1] == 0x30);
		info->is_ac = (buf[1] == 0x31);
		break;
	case 0x39: /* Active power + apparent power / power factor + frequency */
		if (buf[1] == 0x30)
			/* Active power + apparent power */
			info->is_power_apparent_power = TRUE;
		else if (buf[1] == 0x31)
			/* Power factor + frequency */
			info->is_power_factor_freq = TRUE;
		else if (buf[1] == 0x32)
			/* Voltage effective value + current effective value */
			info->is_v_a_rms_value = TRUE;
		break;
	default:
		sr_dbg("Invalid function bytes: %02x %02x.", buf[0], buf[1]);
		break;
	}

	/* Byte 2: Range */

	/* Byte 3-7: Main display digits */

	/* Byte 8-12: Auxiliary display digits */

	/* Byte 13: TODO: "Simulate strip tens digit". */

	/* Byte 14: TODO: "Simulate strip the single digit". */

	/* Byte 15: Status */
	info->is_sign2        = (buf[15] & (1 << 3)) != 0;
	info->is_sign1        = (buf[15] & (1 << 2)) != 0;
	info->is_batt         = (buf[15] & (1 << 1)) != 0; /* Bat. low */
	info->is_ol1          = (buf[15] & (1 << 0)) != 0; /* Overflow (main display) */

	/* Byte 16: Option 1 */
	info->is_max          = (buf[16] & (1 << 3)) != 0;
	info->is_min          = (buf[16] & (1 << 2)) != 0;
	info->is_maxmin       = (buf[16] & (1 << 1)) != 0;
	info->is_rel          = (buf[16] & (1 << 0)) != 0;

	/* Byte 17: Option 2 */
	info->is_ol2          = (buf[17] & (1 << 3)) != 0;
	info->is_open         = (buf[17] & (1 << 2)) != 0;
	info->is_manu         = (buf[17] & (1 << 1)) != 0; /* Manual mode */
	info->is_hold         = (buf[17] & (1 << 0)) != 0; /* Hold */

	/* Byte 18: Option 3 */
	info->is_light        = (buf[18] & (1 << 3)) != 0;
	info->is_usb          = (buf[18] & (1 << 2)) != 0; /* Always on */
	info->is_warning      = (buf[18] & (1 << 1)) != 0; /* Never seen? */
	info->is_auto_power   = (buf[18] & (1 << 0)) != 0; /* Always on */

	/* Byte 19: Option 4 */
	info->is_misplug_warn = (buf[19] & (1 << 3)) != 0; /* Never gets set? */
	info->is_lo           = (buf[19] & (1 << 2)) != 0;
	info->is_hi           = (buf[19] & (1 << 1)) != 0;
	info->is_open2        = (buf[19] & (1 << 0)) != 0; /* TODO: Unknown. */

	/* Byte 20: Dual display bit */
	info->is_dual_display = (buf[20] & (1 << 0)) != 0;

	/* Byte 21: Always '\r' (carriage return, 0x0d, 13) */

	/* Byte 22: Always '\n' (newline, 0x0a, 10) */

	info->is_auto = !info->is_manu;
}

static void handle_flags(struct sr_datafeed_analog *analog,
			 float *floatval, const struct vc870_info *info)
{
	/*
	 * Note: is_micro etc. are not used directly to multiply/divide
	 * floatval, this is handled via parse_range() and exponents[][].
	 */

	/* Measurement modes */
	if (info->is_voltage) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_current) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (info->is_resistance) {
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (info->is_frequency) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (info->is_capacitance) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (info->is_temperature) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_CELSIUS;
		/* TODO: Handle Fahrenheit in auxiliary display. */
		// analog->meaning->unit = SR_UNIT_FAHRENHEIT;
	}
	if (info->is_continuity) {
		analog->meaning->mq = SR_MQ_CONTINUITY;
		analog->meaning->unit = SR_UNIT_BOOLEAN;
		/* Vendor docs: "< 20 Ohm acoustic" */
		*floatval = (*floatval < 0.0 || *floatval > 20.0) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_loop_current) {
		/* 4mA = 0%, 20mA = 100% */
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	}
	if (info->is_power) {
		analog->meaning->mq = SR_MQ_POWER;
		analog->meaning->unit = SR_UNIT_WATT;
	}
	if (info->is_power_apparent_power) {
		analog->meaning->mq = SR_MQ_POWER;
		analog->meaning->unit = SR_UNIT_WATT;
		/* TODO: Handle apparent power. */
		// analog->meaning->mq = SR_MQ_APPARENT_POWER;
		// analog->meaning->unit = SR_UNIT_VOLT_AMPERE;
	}
	if (info->is_power_factor_freq) {
		analog->meaning->mq = SR_MQ_POWER_FACTOR;
		analog->meaning->unit = SR_UNIT_UNITLESS;
		/* TODO: Handle frequency. */
		// analog->meaning->mq = SR_MQ_FREQUENCY;
		// analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (info->is_v_a_rms_value) {
		analog->meaning->mqflags |= SR_MQFLAG_RMS;
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		/* TODO: Handle effective current value */
		// analog->meaning->mq = SR_MQ_CURRENT;
		// analog->meaning->unit = SR_UNIT_AMPERE;
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
		/*
		 * Note: HOLD only affects the number displayed on the LCD,
		 * but not the value sent via the protocol! It also does not
		 * affect the bargraph on the LCD.
		 */
		analog->meaning->mqflags |= SR_MQFLAG_HOLD;
	if (info->is_max)
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
	if (info->is_min)
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
	if (info->is_rel)
		analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (info->is_batt)
		sr_spew("Battery is low.");
	if (info->is_auto_power)
		sr_spew("Auto-Power-Off enabled.");
}

static gboolean flags_valid(const struct vc870_info *info)
{
	(void)info;

	/* TODO: Implement. */
	return TRUE;
}

SR_PRIV gboolean sr_vc870_packet_valid(const uint8_t *buf)
{
	struct vc870_info info;

	/* Byte 21: Always '\r' (carriage return, 0x0d, 13) */
	/* Byte 22: Always '\n' (newline, 0x0a, 10) */
	if (buf[21] != '\r' || buf[22] != '\n')
		return FALSE;

	parse_flags(buf, &info);

	return flags_valid(&info);
}

SR_PRIV int sr_vc870_parse(const uint8_t *buf, float *floatval,
			   struct sr_datafeed_analog *analog, void *info)
{
	int ret, exponent = 0;
	struct vc870_info *info_local;

	info_local = (struct vc870_info *)info;
	memset(info_local, 0, sizeof(struct vc870_info));

	if (!sr_vc870_packet_valid(buf))
		return SR_ERR;

	parse_flags(buf, info_local);

	if ((ret = parse_value(buf, info_local, floatval)) != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	if ((ret = parse_range(buf[2], floatval, &exponent, info_local)) != SR_OK)
		return ret;

	handle_flags(analog, floatval, info_local);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}
