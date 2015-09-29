/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Uwe Hermann <uwe@hermann-uwe.de>
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
 * UNI-T UT71x protocol parser.
 *
 * Communication parameters: Unidirectional, 2400/7o1
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ut71x"

/*
 * Factors for the respective measurement mode (0 means "invalid").
 *
 * The Conrad/Voltcraft protocol descriptions have a typo (they suggest
 * index 0 for the 10A range (which is incorrect, it's range 1).
 */
static const float factors[16][8] = {
	{1e-5, 0,     0,     0,     0,    0,    0,    0   }, /* AC mV */
	{0,    1e-4,  1e-3,  1e-2,  1e-1, 0,    0,    0   }, /* DC V */
	{0,    1e-4,  1e-3,  1e-2,  1e-1, 0,    0,    0   }, /* AC V */
	{1e-5, 0,     0,     0,     0,    0,    0,    0   }, /* DC mV */
	{0,    1e-1,  1,     1e1,   1e2,  1e3,  1e4,  0   }, /* Resistance */
	{0,    1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6}, /* Capacitance */
	{1e-1, 0,     0,     0,     0,    0,    0,    0   }, /* Temp (C) */
	{1e-8, 1e-7,  0,     0,     0,    0,    0,    0   }, /* uA */
	{1e-6, 1e-5,  0,     0,     0,    0,    0,    0   }, /* mA */
	{0,    1e-3,  0,     0,     0,    0,    0,    0   }, /* 10A */
	{1e-1, 0,     0,     0,     0,    0,    0,    0   }, /* Continuity */
	{1e-4, 0,     0,     0,     0,    0,    0,    0   }, /* Diode */
	{1e-3, 1e-2,  1e-1,  1,     1e1,  1e2,  1e3,  1e4 }, /* Frequency */
	{1e-1, 0,     0,     0,     0,    0,    0,    0   }, /* Temp (F) */
	{0,    0,     0,     1,     0,    0,    0,    0   }, /* Power */
	{1e-2, 0,     0,     0,     0,    0,    0,    0   }, /* Loop current */
};

static int parse_value(const uint8_t *buf, struct ut71x_info *info, float *result)
{
	int i, intval, num_digits = 5;

	/* Bytes 0-4: Value (5 decimal digits) */
	if (!strncmp((const char *)buf, "::0<:", 5)) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (!strncmp((const char *)buf, ":<0::", 5)) {
		sr_spew("Under limit.");
		*result = INFINITY;
		return SR_OK;
	} else if (buf[4] == ':') {
		sr_dbg("4000 count mode, only 4 digits used.");
		num_digits = 4;
	} else if (!isdigit(buf[0]) || !isdigit(buf[1]) ||
	           !isdigit(buf[2]) || !isdigit(buf[3]) || !isdigit(buf[4])) {
		sr_dbg("Invalid digits: %02x %02x %02x %02x %02x (%c %c "
		       "%c %c %c).", buf[0], buf[1], buf[2], buf[3], buf[4],
		       buf[0], buf[1], buf[2], buf[3], buf[4]);
		return SR_ERR;
	}
	for (i = 0, intval = 0; i < num_digits; i++)
		intval = 10 * intval + (buf[i] - '0');

	/* Apply sign. */
	intval *= info->is_sign ? -1 : 1;

	/* Note: The decimal point position will be parsed later. */
	*result = (float)intval;
	sr_spew("The display value is %f.", *result);

	return SR_OK;
}

static int parse_range(const uint8_t *buf, float *floatval)
{
	int idx, mode;
	float factor = 0;

	idx = buf[5] - '0';
	if (idx < 0 || idx > 7) {
		sr_dbg("Invalid range byte 0x%02x (idx 0x%02x).", buf[5], idx);
		return SR_ERR;
	}

	mode = buf[6] - '0';
	if (mode < 0 || mode > 15) {
		sr_dbg("Invalid mode byte 0x%02x (idx 0x%02x).", buf[6], mode);
		return SR_ERR;
	}

	sr_spew("mode/idx = %d/%d", mode, idx);

	factor = factors[mode][idx];
	if (factor == 0) {
		sr_dbg("Invalid factor for range byte: 0x%02x.", buf[5]);
		return SR_ERR;
	}

	/* Apply respective factor (mode-dependent) on the value. */
	*floatval *= factor;
	sr_dbg("Applying factor %f, new value is %f.", factor, *floatval);

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct ut71x_info *info)
{
	/* Function byte */
	switch (buf[6] - '0') {
	case 0: /* AC mV */
		info->is_voltage = info->is_ac = TRUE;
		break;
	case 1: /* DC V */
		info->is_voltage = info->is_dc = TRUE;
		break;
	case 2: /* AC V */
		info->is_voltage = info->is_ac = TRUE;
		break;
	case 3: /* DC mV */
		info->is_voltage = info->is_dc = TRUE;
		break;
	case 4: /* Resistance */
		info->is_resistance = TRUE;
		break;
	case 5: /* Capacitance */
		info->is_capacitance = TRUE;
		break;
	case 6: /* Temperature (Celsius) */
		info->is_temperature = info->is_celsius = TRUE;
		break;
	case 7: /* uA */
		info->is_current = info->is_dc = TRUE;
		break;
	case 8: /* mA */
		info->is_current = info->is_dc = TRUE;
		break;
	case 9: /* 10A */
		info->is_current = info->is_dc = TRUE;
		break;
	case 10: /* Continuity */
		info->is_continuity = TRUE;
		break;
	case 11: /* Diode */
		info->is_diode = TRUE;
		break;
	case 12: /* Frequency */
		info->is_frequency = TRUE;
		break;
	case 13: /* Temperature (F) */
		info->is_temperature = info->is_fahrenheit = TRUE;
		break;
	case 14: /* Power */
		/* Note: Only available on UT71E (range 0-2500W). */
		info->is_power = TRUE;
		break;
	case 15: /* DC loop current, percentage display (range 4-20mA) */
		info->is_loop_current = TRUE;
		break;
	default:
		sr_dbg("Invalid function byte: 0x%02x.", buf[6]);
		break;
	}

	/*
	 * State 1 byte: bit 0 = AC, bit 1 = DC
	 * Either AC or DC or both or none can be set at the same time.
	 */
	info->is_ac = (buf[7] & (1 << 0)) != 0;
	info->is_dc = (buf[7] & (1 << 1)) != 0;

	/*
	 * State 2 byte: bit 0 = auto, bit 1 = manual, bit 2 = sign
	 *
	 * The Conrad/Voltcraft protocol descriptions have a typo
	 * (they suggest bit 3 as sign bit, which is incorrect).
	 *
	 * For modes where there's only one possible range (e.g. AC mV)
	 * neither the "auto" nor the "manual" bits will be set.
	 */
	info->is_auto   = (buf[8] & (1 << 0)) != 0;
	info->is_manual = (buf[8] & (1 << 1)) != 0;
	info->is_sign   = (buf[8] & (1 << 2)) != 0;

	/* Note: "Frequency mode + sign bit" means "duty cycle mode". */
	if (info->is_frequency && info->is_sign) {
		info->is_duty_cycle = TRUE;
		info->is_frequency = info->is_sign = FALSE;
	}
}

static void handle_flags(struct sr_datafeed_analog_old *analog,
		float *floatval, const struct ut71x_info *info)
{
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
		*floatval = (*floatval < 0.0 || *floatval > 60.0) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_duty_cycle) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	}
	if (info->is_power) {
		analog->mq = SR_MQ_POWER;
		analog->unit = SR_UNIT_WATT;
	}
	if (info->is_loop_current) {
		/* 4mA = 0%, 20mA = 100% */
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_PERCENTAGE;
	}

	/* Measurement related flags */
	if (info->is_ac)
		analog->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->mqflags |= SR_MQFLAG_DC;
	if (info->is_ac)
		/* All AC modes do True-RMS measurements. */
		analog->mqflags |= SR_MQFLAG_RMS;
	if (info->is_auto)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (info->is_diode)
		analog->mqflags |= SR_MQFLAG_DIODE;
}

static gboolean flags_valid(const struct ut71x_info *info)
{
	int count;

	/* Does the packet "measure" more than one type of value? */
	count  = (info->is_voltage) ? 1 : 0;
	count += (info->is_current) ? 1 : 0;
	count += (info->is_resistance) ? 1 : 0;
	count += (info->is_capacitance) ? 1 : 0;
	count += (info->is_frequency) ? 1 : 0;
	count += (info->is_temperature) ? 1 : 0;
	count += (info->is_continuity) ? 1 : 0;
	count += (info->is_diode) ? 1 : 0;
	count += (info->is_power) ? 1 : 0;
	count += (info->is_loop_current) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Auto and manual can't be active at the same time. */
	if (info->is_auto && info->is_manual) {
		sr_dbg("Auto and manual modes are both active.");
		return FALSE;
	}

	return TRUE;
}

SR_PRIV gboolean sr_ut71x_packet_valid(const uint8_t *buf)
{
	struct ut71x_info info;

	memset(&info, 0, sizeof(struct ut71x_info));

	if (buf[9] != '\r' || buf[10] != '\n')
		return FALSE;

	parse_flags(buf, &info);

	return flags_valid(&info);
}

SR_PRIV int sr_ut71x_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog_old *analog, void *info)
{
	int ret;
	struct ut71x_info *info_local;

	info_local = (struct ut71x_info *)info;
	memset(info_local, 0, sizeof(struct ut71x_info));

	if (!sr_ut71x_packet_valid(buf))
		return SR_ERR;

	parse_flags(buf, info_local);

	if ((ret = parse_value(buf, info, floatval)) != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	if ((ret = parse_range(buf, floatval)) != SR_OK)
		return ret;

	handle_flags(analog, floatval, info);

	return SR_OK;
}
