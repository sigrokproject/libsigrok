/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define LOG_PREFIX "es51919"

#ifdef HAVE_SERIAL_COMM

/*
 * Cyrustek ES51919 LCR chipset host protocol.
 *
 * Public official documentation does not contain the protocol
 * description, so this is all based on reverse engineering.
 *
 * Packet structure (17 bytes):
 *
 * 0x00: header1 ?? (0x00)
 * 0x01: header2 ?? (0x0d)
 *
 * 0x02: flags
 *         bit 0 = hold enabled
 *         bit 1 = reference shown (in delta mode)
 *         bit 2 = delta mode
 *         bit 3 = calibration mode
 *         bit 4 = sorting mode
 *         bit 5 = LCR mode
 *         bit 6 = auto mode
 *         bit 7 = parallel measurement (vs. serial)
 *
 * 0x03: config
 *         bit 0-4 = ??? (0x10)
 *         bit 5-7 = test frequency
 *                     0 = 100 Hz
 *                     1 = 120 Hz
 *                     2 = 1 kHz
 *                     3 = 10 kHz
 *                     4 = 100 kHz
 *                     5 = 0 Hz (DC)
 *
 * 0x04: tolerance (sorting mode)
 *         0 = not set
 *         3 = +-0.25%
 *         4 = +-0.5%
 *         5 = +-1%
 *         6 = +-2%
 *         7 = +-5%
 *         8 = +-10%
 *         9 = +-20%
 *        10 = -20+80%
 *
 * 0x05-0x09: primary measurement
 *   0x05: measured quantity
 *           1 = inductance
 *           2 = capacitance
 *           3 = resistance
 *           4 = DC resistance
 *   0x06: measurement MSB  (0x4e20 = 20000 = outside limits)
 *   0x07: measurement LSB
 *   0x08: measurement info
 *           bit 0-2 = decimal point multiplier (10^-val)
 *           bit 3-7 = unit
 *                       0 = no unit
 *                       1 = Ohm
 *                       2 = kOhm
 *                       3 = MOhm
 *                       5 = uH
 *                       6 = mH
 *                       7 = H
 *                       8 = kH
 *                       9 = pF
 *                       10 = nF
 *                       11 = uF
 *                       12 = mF
 *                       13 = %
 *                       14 = degree
 *   0x09: measurement status
 *           bit 0-3 = status
 *                       0 = normal (measurement shown)
 *                       1 = blank (nothing shown)
 *                       2 = lines ("----")
 *                       3 = outside limits ("OL")
 *                       7 = pass ("PASS")
 *                       8 = fail ("FAIL")
 *                       9 = open ("OPEn")
 *                      10 = shorted ("Srt")
 *           bit 4-6 = ??? (maybe part of same field with 0-3)
 *           bit 7   = ??? (some independent flag)
 *
 * 0x0a-0x0e: secondary measurement
 *   0x0a: measured quantity
 *           0 = none
 *           1 = dissipation factor
 *           2 = quality factor
 *           3 = parallel AC resistance / ESR
 *           4 = phase angle
 *   0x0b-0x0e: like primary measurement
 *
 * 0x0f: footer1 (0x0d) ?
 * 0x10: footer2 (0x0a) ?
 */

static const double frequencies[] = {
	SR_HZ(0), SR_HZ(100), SR_HZ(120),
	SR_KHZ(1), SR_KHZ(10), SR_KHZ(100),
};

static const size_t freq_code_map[] = {
	1, 2, 3, 4, 5, 0,
};

static uint64_t get_frequency(size_t code)
{
	uint64_t freq;

	if (code >= ARRAY_SIZE(freq_code_map)) {
		sr_err("Unknown output frequency code %zu.", code);
		return frequencies[0];
	}

	code = freq_code_map[code];
	freq = frequencies[code];

	return freq;
}

enum { MODEL_NONE, MODEL_PAR, MODEL_SER, MODEL_AUTO, };

static const char *const circuit_models[] = {
	"NONE", "PARALLEL", "SERIES", "AUTO",
};

static const char *get_equiv_model(size_t code)
{
	if (code >= ARRAY_SIZE(circuit_models)) {
		sr_err("Unknown equivalent circuit model code %zu.", code);
		return "NONE";
	}

	return circuit_models[code];
}

static const uint8_t *pkt_to_buf(const uint8_t *pkt, int is_secondary)
{
	return is_secondary ? pkt + 10 : pkt + 5;
}

static int parse_mq(const uint8_t *pkt, int is_secondary, int is_parallel)
{
	const uint8_t *buf;

	buf = pkt_to_buf(pkt, is_secondary);

	switch (is_secondary << 8 | buf[0]) {
	case 0x001:
		return is_parallel ?
			SR_MQ_PARALLEL_INDUCTANCE : SR_MQ_SERIES_INDUCTANCE;
	case 0x002:
		return is_parallel ?
			SR_MQ_PARALLEL_CAPACITANCE : SR_MQ_SERIES_CAPACITANCE;
	case 0x003:
	case 0x103:
		return is_parallel ?
			SR_MQ_PARALLEL_RESISTANCE : SR_MQ_SERIES_RESISTANCE;
	case 0x004:
		return SR_MQ_RESISTANCE;
	case 0x100:
		return SR_MQ_DIFFERENCE;
	case 0x101:
		return SR_MQ_DISSIPATION_FACTOR;
	case 0x102:
		return SR_MQ_QUALITY_FACTOR;
	case 0x104:
		return SR_MQ_PHASE_ANGLE;
	}

	sr_err("Unknown quantity 0x%03x.", is_secondary << 8 | buf[0]);

	return 0;
}

static float parse_value(const uint8_t *buf, int *digits)
{
	static const int exponents[] = {0, -1, -2, -3, -4, -5, -6, -7};

	int exponent;
	int16_t val;
	float fval;

	exponent = exponents[buf[3] & 7];
	*digits = -exponent;
	val = (buf[1] << 8) | buf[2];
	fval = (float)val;
	fval *= powf(10, exponent);

	return fval;
}

static void parse_measurement(const uint8_t *pkt, float *floatval,
	struct sr_datafeed_analog *analog, int is_secondary)
{
	static const struct {
		int unit;
		int exponent;
	} units[] = {
		{ SR_UNIT_UNITLESS,   0 }, /* no unit */
		{ SR_UNIT_OHM,        0 }, /* Ohm */
		{ SR_UNIT_OHM,        3 }, /* kOhm */
		{ SR_UNIT_OHM,        6 }, /* MOhm */
		{ -1,                 0 }, /* ??? */
		{ SR_UNIT_HENRY,     -6 }, /* uH */
		{ SR_UNIT_HENRY,     -3 }, /* mH */
		{ SR_UNIT_HENRY,      0 }, /* H */
		{ SR_UNIT_HENRY,      3 }, /* kH */
		{ SR_UNIT_FARAD,    -12 }, /* pF */
		{ SR_UNIT_FARAD,     -9 }, /* nF */
		{ SR_UNIT_FARAD,     -6 }, /* uF */
		{ SR_UNIT_FARAD,     -3 }, /* mF */
		{ SR_UNIT_PERCENTAGE, 0 }, /* % */
		{ SR_UNIT_DEGREE,     0 }, /* degree */
	};

	const uint8_t *buf;
	int digits, exponent;
	int state;

	buf = pkt_to_buf(pkt, is_secondary);

	analog->meaning->mq = 0;
	analog->meaning->mqflags = 0;

	state = buf[4] & 0xf;

	if (state != 0 && state != 3)
		return;

	if (pkt[2] & 0x18) {
		/* Calibration and Sorting modes not supported. */
		return;
	}

	if (!is_secondary) {
		if (pkt[2] & 0x01)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		if (pkt[2] & 0x02)
			analog->meaning->mqflags |= SR_MQFLAG_REFERENCE;
	} else {
		if (pkt[2] & 0x04)
			analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;
	}

	if ((analog->meaning->mq = parse_mq(pkt, is_secondary, pkt[2] & 0x80)) == 0)
		return;

	if ((buf[3] >> 3) >= ARRAY_SIZE(units)) {
		sr_err("Unknown unit %u.", buf[3] >> 3);
		analog->meaning->mq = 0;
		return;
	}

	analog->meaning->unit = units[buf[3] >> 3].unit;

	exponent = units[buf[3] >> 3].exponent;
	*floatval = parse_value(buf, &digits);
	*floatval *= (state == 0) ? powf(10, exponent) : INFINITY;
	analog->encoding->digits = digits - exponent;
	analog->spec->spec_digits = digits - exponent;
}

static uint64_t parse_freq(const uint8_t *pkt)
{
	return get_frequency(pkt[3] >> 5);
}

static const char *parse_model(const uint8_t *pkt)
{
	size_t code;

	if (pkt[2] & 0x40)
		code = MODEL_AUTO;
	else if (parse_mq(pkt, 0, 0) == SR_MQ_RESISTANCE)
		code = MODEL_NONE;
	else
		code = (pkt[2] & 0x80) ? MODEL_PAR : MODEL_SER;

	return get_equiv_model(code);
}

SR_PRIV gboolean es51919_packet_valid(const uint8_t *pkt)
{

	/* Check for fixed 0x00 0x0d prefix. */
	if (pkt[0] != 0x00 || pkt[1] != 0x0d)
		return FALSE;

	/* Check for fixed 0x0d 0x0a suffix. */
	if (pkt[15] != 0x0d || pkt[16] != 0x0a)
		return FALSE;

	/* Packet appears to be valid. */
	return TRUE;
}

SR_PRIV int es51919_packet_parse(const uint8_t *pkt, float *val,
	struct sr_datafeed_analog *analog, void *info)
{
	struct lcr_parse_info *parse_info;

	parse_info = info;
	if (!parse_info->ch_idx) {
		parse_info->output_freq = parse_freq(pkt);
		parse_info->circuit_model = parse_model(pkt);
	}
	if (val && analog)
		parse_measurement(pkt, val, analog, parse_info->ch_idx == 1);

	return SR_OK;
}

/*
 * These are the get/set/list routines for the _chip_ specific parameters,
 * the _device_ driver resides in src/hardware/serial-lcr/ instead.
 */

SR_PRIV int es51919_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_DOUBLE,
			ARRAY_AND_SIZE(frequencies), sizeof(frequencies[0]));
		return SR_OK;
	case SR_CONF_EQUIV_CIRCUIT_MODEL:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(circuit_models));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
	/* UNREACH */
}

#endif
