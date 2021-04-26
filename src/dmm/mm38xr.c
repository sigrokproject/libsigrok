/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Peter Skarpetis <peters@skarpetis.com>
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

/*
 * Meterman 38XR protocol parser
 *
 * Communication parameters: Unidirectional, 9600/8n1
 *
 * The user guide can be downloaded from:
 * https://assets.tequipment.net/assets/1/26/Documents/38XR_Manual.pdf
 *
 * Protocol is described in a PDF available at:
 * https://www.elfadistrelec.fi/Web/Downloads/od/es/fj38XR-Serial-Output-Codes.pdf
 *
 * There is also a disussion about the protocol at the NI forum:
 * https://forums.ni.com/t5/Digital-Multimeters-DMMs-and/Meterman-DMM/td-p/179597?profile.language=en
 *
 * EEVBlog discussion thread about the meter
 * https://www.eevblog.com/forum/chat/meterman-38xr/
 */

/**
 * @file
 *
 * Meterman 38XR ASCII protocol parser.
 */

#include <config.h>

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <math.h>
#include <string.h>

#define LOG_PREFIX "mm38xr"

#define METERMAN_DIGITS_OVERLOAD 0xb0dd
#define METERMAN_DIGITS_BAD_INPUT_JACK 0xbaab
#define METERMAN_BARGRAPH_NO_SEGMENTS = 0x2a

enum mm38xr_func_code {
	FUNC_CODE_UNUSED = 0x01,
	FUNC_CODE_TEMPERATURE_FARENHEIGHT = 0x02,
	FUNC_CODE_CURRENT_4_20_MAMPS = 0x03, /* 4-20 mA */
	FUNC_CODE_DIODE_TEST = 0x04,
	FUNC_CODE_INDUCTANCE_HENRIES = 0x05,
	FUNC_CODE_TEMPERATURE_CELSIUS = 0x06,
	FUNC_CODE_CURRENT_UAMPS = 0x07, /* uA */
	FUNC_CODE_RESISTANCE_OHMS = 0x08,
	FUNC_CODE_INDUCTANCE_MHENRIES = 0x09, /* mH */
	FUNC_CODE_CURRENT_10_AMPS = 0x0a,
	FUNC_CODE_CAPACITANCE = 0x0b,
	FUNC_CODE_VOLTS_DC = 0x0c,
	FUNC_CODE_LOGIC = 0x0d,
	FUNC_CODE_CURRENT_MAMPS = 0x0e, /* mA */
	FUNC_CODE_FREQUENCY_HZ = 0x0f, /* and duty cycle */
	FUNC_CODE_VOLTS_AC = 0x10, /* and dBm */
};

enum mm38xr_meas_mode {
	/* This is used to index into the digits and exponent arrays below. */
	MEAS_MODE_VOLTS,
	MEAS_MODE_RESISTANCE_OHMS,
	MEAS_MODE_CURRENT_UAMPS, /* uA */
	MEAS_MODE_CURRENT_MAMPS, /* mA */
	MEAS_MODE_CURRENT_AMPS,
	MEAS_MODE_CAPACITANCE,
	MEAS_MODE_DIODE_TEST,
	MEAS_MODE_TEMPERATURE_C,
	MEAS_MODE_TEMPERATURE_F,
	MEAS_MODE_FREQUENCY_HZ,
	MEAS_MODE_INDUCTANCE_H,
	MEAS_MODE_INDUCTANCE_MH, /* mH */
	MEAS_MODE_DBM,
	MEAS_MODE_DUTY_CYCLE,
	MEAS_MODE_CONTINUITY,
	/* For internal purposes. */
	MEAS_MODE_UNDEFINED,
};

enum mm38xr_adcd_mode {
	ACDC_MODE_NONE = 1000,
	ACDC_MODE_DC,
	ACDC_MODE_AC,
	ACDC_MODE_AC_AND_DC,
};

struct meterman_info {
	enum mm38xr_func_code functioncode; /* columns 0, 1 */
	unsigned int reading;               /* columns 2,3,4,5; LCD digits */
	unsigned int bargraphsegments;      /* columns 6, 7; max 40 segments, 0x2A = no bargraph */
	size_t rangecode;                   /* column 8 */
	unsigned int ampsfunction;          /* column 9 */
	unsigned int peakstatus;            /* column 10 */
	unsigned int rflag_h;               /* column 11 */
	unsigned int rflag_l;               /* column 12 */

	/* calculated values */
	enum mm38xr_meas_mode meas_mode;
	enum mm38xr_adcd_mode acdc;
};

static const int decimal_digits[][7] = {
	[MEAS_MODE_VOLTS]           = { 1, 3, 2, 1, 0, 0, 0, },
	[MEAS_MODE_RESISTANCE_OHMS] = { 2, 3, 4, 2, 3, 1, 0, },
	[MEAS_MODE_CURRENT_UAMPS]   = { 2, 1, 0, 0, 0, 0, 0, },
	[MEAS_MODE_CURRENT_MAMPS]   = { 3, 2, 1, 0, 0, 0, 0, },
	[MEAS_MODE_CURRENT_AMPS]    = { 3, 0, 0, 0, 0, 0, 0, },
	[MEAS_MODE_CAPACITANCE]     = { 2, 1, 3, 2, 1, 0, 0, },
	[MEAS_MODE_DIODE_TEST]      = { 0, 3, 0, 0, 0, 0, 0, },
	[MEAS_MODE_TEMPERATURE_C]   = { 0, 0, 0, 0, 0, 0, 0, },
	[MEAS_MODE_TEMPERATURE_F]   = { 0, 0, 0, 0, 0, 0, 0, },
	[MEAS_MODE_FREQUENCY_HZ]    = { 2, 1, 3, 2, 1, 3, 2, },
	[MEAS_MODE_INDUCTANCE_H]    = { 0, 0, 0, 3, 2, 0, 0, },
	[MEAS_MODE_INDUCTANCE_MH]   = { 3, 2, 1, 0, 0, 0, 0, },
	[MEAS_MODE_DBM]             = { 2, 2, 2, 2, 2, 2, 2, },
	[MEAS_MODE_DUTY_CYCLE]      = { 2, 2, 2, 2, 2, 2, 2, },
	[MEAS_MODE_CONTINUITY]      = { 0, 0, 0, 0, 0, 1, 0, },
};

static const int units_exponents[][7] = {
	[MEAS_MODE_VOLTS]           = { -3,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_RESISTANCE_OHMS] = {  6,  6,  6,  3,  3,  0,  0, },
	[MEAS_MODE_CURRENT_UAMPS]   = { -6, -6,  0,  0,  0,  0,  0, },
	[MEAS_MODE_CURRENT_MAMPS]   = { -3, -3, -3,  0,  0,  0,  0, },
	[MEAS_MODE_CURRENT_AMPS]    = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_CAPACITANCE]     = { -9, -9, -6, -6, -6,  0,  0, },
	[MEAS_MODE_DIODE_TEST]      = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_TEMPERATURE_C]   = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_TEMPERATURE_F]   = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_FREQUENCY_HZ]    = {  0,  0,  3,  3,  3,  6,  6, },
	[MEAS_MODE_INDUCTANCE_H]    = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_INDUCTANCE_MH]   = { -3, -3, -3,  0,  0,  0,  0, },
	[MEAS_MODE_DBM]             = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_DUTY_CYCLE]      = {  0,  0,  0,  0,  0,  0,  0, },
	[MEAS_MODE_CONTINUITY]      = {  0,  0,  0,  0,  0,  0,  0, },
};

/* Assumes caller has already checked data fall within 0..9 and A..F */
static uint32_t meterman_38xr_hexnibble_to_uint(uint8_t v)
{
	return (v <= '9') ? v - '0' : v - 'A' + 10;
}

static uint32_t meterman_38xr_func_code(const uint8_t *buf)
{
	uint32_t v;

	v = meterman_38xr_hexnibble_to_uint(buf[0]) << 4 |
		meterman_38xr_hexnibble_to_uint(buf[1]);
	return v;
}

static uint32_t meterman_38xr_barsegments(const uint8_t *buf)
{
	uint32_t v;

	v = meterman_38xr_hexnibble_to_uint(buf[6]) << 4 |
		meterman_38xr_hexnibble_to_uint(buf[7]);
	return v;
}

static uint32_t meterman_38xr_reading(const uint8_t *buf)
{
	uint32_t v;

	if (buf[2] > 'A') { /* overload */
		v = meterman_38xr_hexnibble_to_uint(buf[2]) << 12 |
			meterman_38xr_hexnibble_to_uint(buf[3]) << 8 |
			meterman_38xr_hexnibble_to_uint(buf[4]) << 4 |
			meterman_38xr_hexnibble_to_uint(buf[5]) << 0;
	} else {
		v = meterman_38xr_hexnibble_to_uint(buf[2]) * 1000 +
			meterman_38xr_hexnibble_to_uint(buf[3]) * 100 +
			meterman_38xr_hexnibble_to_uint(buf[4]) * 10 +
			meterman_38xr_hexnibble_to_uint(buf[5]) * 1;
	}
	return v;
}

static gboolean meterman_38xr_is_negative(struct meterman_info *mi)
{

	if (mi->rflag_l == 0x01)
		return TRUE;
	if (mi->meas_mode == MEAS_MODE_DBM && mi->rflag_l == 0x05)
		return TRUE;
	return FALSE;
}

static int currentACDC(struct meterman_info *mi)
{

	if (mi->ampsfunction == 0x01)
		return ACDC_MODE_AC;
	if (mi->ampsfunction == 0x02)
		return ACDC_MODE_AC_AND_DC;
	return ACDC_MODE_DC;
}

static int meterman_38xr_decode(const uint8_t *buf, struct meterman_info *mi)
{

	if (!meterman_38xr_packet_valid(buf))
		return SR_ERR;

	mi->functioncode = meterman_38xr_func_code(buf);
	if (mi->functioncode < 2 || mi->functioncode > 0x10)
		return SR_ERR;
	mi->reading = meterman_38xr_reading(buf);
	mi->bargraphsegments = meterman_38xr_barsegments(buf);
	mi->rangecode = meterman_38xr_hexnibble_to_uint(buf[8]);
	if (mi->rangecode > 6)
		return SR_ERR;
	mi->ampsfunction = meterman_38xr_hexnibble_to_uint(buf[9]);
	mi->peakstatus = meterman_38xr_hexnibble_to_uint(buf[10]);
	mi->rflag_h = meterman_38xr_hexnibble_to_uint(buf[11]);
	mi->rflag_l = meterman_38xr_hexnibble_to_uint(buf[12]);

	mi->acdc = ACDC_MODE_NONE;
	switch (mi->functioncode) {
	case FUNC_CODE_TEMPERATURE_FARENHEIGHT:
		mi->meas_mode = MEAS_MODE_TEMPERATURE_F;
		break;

	case FUNC_CODE_CURRENT_4_20_MAMPS:
		mi->meas_mode = MEAS_MODE_CURRENT_MAMPS;
		mi->acdc = currentACDC(mi);
		break;

	case FUNC_CODE_DIODE_TEST:
		mi->meas_mode = MEAS_MODE_DIODE_TEST;
		mi->acdc = ACDC_MODE_DC;
		break;

	case FUNC_CODE_INDUCTANCE_HENRIES:
		mi->meas_mode = MEAS_MODE_INDUCTANCE_H;
		break;

	case FUNC_CODE_TEMPERATURE_CELSIUS:
		mi->meas_mode = MEAS_MODE_TEMPERATURE_C;
		break;

	case FUNC_CODE_CURRENT_UAMPS:
		mi->meas_mode = MEAS_MODE_CURRENT_UAMPS;
		mi->acdc = currentACDC(mi);
		break;

	case FUNC_CODE_RESISTANCE_OHMS:
		mi->meas_mode = (mi->rflag_l == 0x08)
			? MEAS_MODE_CONTINUITY
			: MEAS_MODE_RESISTANCE_OHMS;
		break;

	case FUNC_CODE_INDUCTANCE_MHENRIES:
		mi->meas_mode = MEAS_MODE_INDUCTANCE_MH;
		break;

	case FUNC_CODE_CURRENT_10_AMPS:
		mi->meas_mode = MEAS_MODE_CURRENT_AMPS;
		mi->acdc = currentACDC(mi);
		break;

	case FUNC_CODE_CAPACITANCE:
		mi->meas_mode = MEAS_MODE_CAPACITANCE;
		break;

	case FUNC_CODE_VOLTS_DC:
		mi->meas_mode = MEAS_MODE_VOLTS;
		mi->acdc = (mi->rflag_l == 0x02)
			? ACDC_MODE_AC_AND_DC : ACDC_MODE_DC;
		break;

	case FUNC_CODE_CURRENT_MAMPS:
		mi->meas_mode = MEAS_MODE_CURRENT_MAMPS;
		mi->acdc = currentACDC(mi);
		break;

	case FUNC_CODE_FREQUENCY_HZ:
		mi->meas_mode = (mi->rflag_h == 0x0B)
			? MEAS_MODE_DUTY_CYCLE
			: MEAS_MODE_FREQUENCY_HZ;
		break;

	case FUNC_CODE_VOLTS_AC:
		mi->meas_mode = (mi->rflag_l == 0x04 || mi->rflag_l == 0x05)
			? MEAS_MODE_DBM : MEAS_MODE_VOLTS;
		mi->acdc = ACDC_MODE_AC;
		break;

	default:
		mi->meas_mode = MEAS_MODE_UNDEFINED;
		return SR_ERR;

	}
	return SR_OK;
}

SR_PRIV gboolean meterman_38xr_packet_valid(const uint8_t *buf)
{
	size_t i;
	uint32_t fcode;

	if ((buf[13] != '\r') || (buf[14] != '\n'))
		return FALSE;

	/* Check for all hex digits */
	for (i = 0; i < 13; i++) {
		if (buf[i] < '0')
			return FALSE;
		if (buf[i] > '9' && buf[i] < 'A')
			return FALSE;
		if (buf[i] > 'F')
			return FALSE;
	}
	fcode = meterman_38xr_func_code(buf);
	if (fcode < 0x01 || fcode > 0x10)
		return FALSE;

	return TRUE;
}

SR_PRIV int meterman_38xr_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, void *info)
{
	gboolean is_overload, is_bad_jack;
	int exponent;
	int digits;
	struct meterman_info mi;

	(void)info;

	if (meterman_38xr_decode(buf, &mi) != SR_OK)
		return SR_ERR;

	if (mi.meas_mode != MEAS_MODE_CONTINUITY) {
		is_overload = mi.reading == METERMAN_DIGITS_OVERLOAD;
		is_bad_jack = mi.reading == METERMAN_DIGITS_BAD_INPUT_JACK;
		if (is_overload || is_bad_jack) {
			sr_spew("Over limit.");
			*floatval = INFINITY; /* overload */
			return SR_OK;
		}
	}
	switch (mi.meas_mode) {
	case MEAS_MODE_VOLTS:
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		break;
	case MEAS_MODE_RESISTANCE_OHMS:
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
		break;
	case MEAS_MODE_CURRENT_UAMPS:
	case MEAS_MODE_CURRENT_MAMPS:
	case MEAS_MODE_CURRENT_AMPS:
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
		break;
	case MEAS_MODE_CAPACITANCE:
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
		break;
	case MEAS_MODE_DIODE_TEST:
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mqflags |= SR_MQFLAG_DIODE;
		break;
	case MEAS_MODE_TEMPERATURE_C:
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_CELSIUS;
		break;
	case MEAS_MODE_TEMPERATURE_F:
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_FAHRENHEIT;
		break;
	case MEAS_MODE_FREQUENCY_HZ:
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
		break;
	case MEAS_MODE_INDUCTANCE_H:
		analog->meaning->mq = SR_MQ_SERIES_INDUCTANCE;
		analog->meaning->unit = SR_UNIT_HENRY;
		break;
	case MEAS_MODE_INDUCTANCE_MH:
		analog->meaning->mq = SR_MQ_SERIES_INDUCTANCE;
		analog->meaning->unit = SR_UNIT_HENRY;
		break;
	case MEAS_MODE_DBM:
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		analog->meaning->mqflags |= SR_MQFLAG_AC;
		break;
	case MEAS_MODE_DUTY_CYCLE:
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
		break;
	case MEAS_MODE_CONTINUITY:
		analog->meaning->mq = SR_MQ_CONTINUITY;
		analog->meaning->unit = SR_UNIT_BOOLEAN;
		*floatval = (mi.reading == METERMAN_DIGITS_OVERLOAD) ? 0.0 : 1.0;
		break;
	default:
		return SR_ERR;
	}
	switch (mi.acdc) {
	case ACDC_MODE_DC:
		analog->meaning->mqflags |= SR_MQFLAG_DC;
		break;
	case ACDC_MODE_AC:
		analog->meaning->mqflags |= SR_MQFLAG_AC;
		break;
	case ACDC_MODE_AC_AND_DC:
		analog->meaning->mqflags |= SR_MQFLAG_DC | SR_MQFLAG_AC;
		break;
	default:
		break;
	}
	if (mi.peakstatus == 0x02 || mi.peakstatus == 0x0a)
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
	if (mi.peakstatus == 0x03 || mi.peakstatus == 0x0b)
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
	if (mi.rflag_h == 0x0a || mi.peakstatus == 0x0b)
		analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
	if (mi.meas_mode != MEAS_MODE_CONTINUITY) {
		digits = decimal_digits[mi.meas_mode][mi.rangecode];
		exponent = units_exponents[mi.meas_mode][mi.rangecode];

		*floatval = mi.reading;
		if (meterman_38xr_is_negative(&mi)) {
			*floatval *= -1.0f;
		}
		*floatval *= powf(10, -digits);
		*floatval *= powf(10, exponent);
	}
	analog->encoding->digits = 4;
	analog->spec->spec_digits = 4;

	return SR_OK;
}
