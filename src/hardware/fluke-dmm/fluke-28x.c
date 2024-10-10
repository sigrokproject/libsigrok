/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019-2020, 2024 Andreas Sandberg <andreas@sandberg.uk>
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

enum measurement_state {
	MEAS_S_INVALID = 0,
	MEAS_S_NORMAL,
	MEAS_S_BLANK,
	MEAS_S_DISCHARGE,
	MEAS_S_OL,
	MEAS_S_OL_MINUS,
	MEAS_S_OPEN_TC,
};

struct state_mapping {
	const char *name;
	enum measurement_state state;
};

static struct state_mapping state_map[] = {
	{ "INVALID", MEAS_S_INVALID },
	{ "NORMAL", MEAS_S_NORMAL },
	{ "BLANK", MEAS_S_BLANK },
	{ "DISCHARGE", MEAS_S_DISCHARGE },
	{ "OL", MEAS_S_OL },
	{ "OL_MINUS", MEAS_S_OL_MINUS },
	{ "OPEN_TC", MEAS_S_OPEN_TC },
};

enum measurement_attribute {
	MEAS_A_INVALID = 0,
	MEAS_A_NONE,
	MEAS_A_OPEN_CIRCUIT,
	MEAS_A_SHORT_CIRCUIT,
	MEAS_A_GLITCH_CIRCUIT,
	MEAS_A_GOOD_DIODE,
	MEAS_A_LO_OHMS,
	MEAS_A_NEGATIVE_EDGE,
	MEAS_A_POSITIVE_EDGE,
	MEAS_A_HIGH_CURRENT,
};

struct attribute_mapping {
	const char *name;
	enum measurement_attribute attribute;
};

static struct attribute_mapping attribute_map[] = {
	{ "NONE", MEAS_A_NONE },
	{ "OPEN_CIRCUIT", MEAS_A_OPEN_CIRCUIT },
	{ "SHORT_CIRCUIT", MEAS_A_SHORT_CIRCUIT },
	{ "GLITCH_CIRCUIT", MEAS_A_GLITCH_CIRCUIT },
	{ "GOOD_DIODE", MEAS_A_GOOD_DIODE },
	{ "LO_OHMS", MEAS_A_LO_OHMS },
	{ "NEGATIVE_EDGE", MEAS_A_NEGATIVE_EDGE },
	{ "POSITIVE_EDGE", MEAS_A_POSITIVE_EDGE },
	{ "HIGH_CURRENT", MEAS_A_HIGH_CURRENT },
};

struct unit_mapping {
	const char *name;
	enum sr_mq mq;
	enum sr_unit unit;
	enum sr_mqflag mqflags;
};

static struct unit_mapping unit_map[] = {
	{ "VDC", SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC },
	{ "VAC", SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_AC | SR_MQFLAG_RMS },
	{ "ADC", SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC },
	{ "AAC", SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_AC | SR_MQFLAG_RMS },
	{ "VAC_PLUS_DC", SR_MQ_VOLTAGE, SR_UNIT_VOLT, 0 },
	{ "AAC_PLUS_DC", SR_MQ_VOLTAGE, SR_UNIT_VOLT, 0 },
	/* Used in peak */
	{ "V", SR_MQ_VOLTAGE, SR_UNIT_VOLT, 0 },
	/* Used in peak */
	{ "A", SR_MQ_VOLTAGE, SR_UNIT_AMPERE, 0 },
	{ "OHM", SR_MQ_RESISTANCE, SR_UNIT_OHM, 0 },
	{ "SIE", SR_MQ_CONDUCTANCE, SR_UNIT_SIEMENS, 0 },
	{ "Hz", SR_MQ_FREQUENCY, SR_UNIT_HERTZ, 0 },
	{ "S", SR_MQ_PULSE_WIDTH, SR_UNIT_SECOND, 0 },
	{ "F", SR_MQ_CAPACITANCE, SR_UNIT_FARAD, 0 },
	{ "CEL", SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS, 0 },
	{ "FAR", SR_MQ_TEMPERATURE, SR_UNIT_FAHRENHEIT, 0 },
	{ "PCT", SR_MQ_DUTY_CYCLE, SR_UNIT_PERCENTAGE, 0 },
	{ "dBm", SR_MQ_VOLTAGE, SR_UNIT_DECIBEL_MW, SR_MQFLAG_AC | SR_MQFLAG_RMS },
	{ "dBV", SR_MQ_VOLTAGE, SR_UNIT_DECIBEL_VOLT, SR_MQFLAG_AC | SR_MQFLAG_RMS },
};

static const struct unit_mapping *parse_unit(const char *name)
{
	unsigned int i;

	if (!name)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(unit_map); i++) {
		if (!strcmp(unit_map[i].name, name))
			return &unit_map[i];
	}

	sr_warn("Unknown unit '%s'", name);
	return NULL;
}

static enum measurement_state parse_measurement_state(const char *name)
{
	unsigned int i;

	if (!name)
		return MEAS_S_INVALID;

	for (i = 0; i < ARRAY_SIZE(state_map); i++) {
		if (!strcmp(state_map[i].name, name))
			return state_map[i].state;
	}

	sr_warn("Unknown measurement state '%s'", name);
	return MEAS_S_INVALID;
}

static enum measurement_attribute parse_attribute(const char *name)
{
	unsigned int i;

	if (!name)
		return MEAS_A_INVALID;

	for (i = 0; i < ARRAY_SIZE(attribute_map); i++) {
		if (!strcmp(attribute_map[i].name, name))
			return attribute_map[i].attribute;
	}

	sr_warn("Unknown measurement attribute '%s'", name);
	return MEAS_A_INVALID;
}

SR_PRIV void fluke_handle_qm_28x(const struct sr_dev_inst *sdi, char **tokens)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	float fvalue;
	int digits;
	const struct unit_mapping *unit;
	enum measurement_state state;
	enum measurement_attribute attr;

	devc = sdi->priv;

	/* We should have received four values:
	 * value, unit, state, attribute
	 */
	if (sr_atof_ascii_digits(tokens[0], &fvalue, &digits) != SR_OK) {
		sr_err("Invalid float '%s'.", tokens[0]);
		return;
	}

	unit = parse_unit(tokens[1]);
	if (!unit) {
		sr_err("Invalid unit '%s'.", tokens[1]);
		return;
	}

	state = parse_measurement_state(tokens[2]);
	attr = parse_attribute(tokens[3]);

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.data = &fvalue;
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.meaning->mq = unit->mq;
	analog.meaning->mqflags = unit->mqflags;
	analog.meaning->unit = unit->unit;

	if (unit->mq == SR_MQ_RESISTANCE) {
		switch (attr) {
		case MEAS_A_NONE:
			/* Normal reading */
			break;
		case MEAS_A_OPEN_CIRCUIT:
		case MEAS_A_SHORT_CIRCUIT:
			/* Continuity measurement */
			analog.meaning->mq = SR_MQ_CONTINUITY;
			analog.meaning->unit = SR_UNIT_BOOLEAN;
			fvalue = attr == MEAS_A_OPEN_CIRCUIT ? 0.0 : 1.0;
			break;
		default:
			analog.meaning->mq = 0;
			break;
		};
	}

	switch (state) {
	case MEAS_S_NORMAL:
		break;

	case MEAS_S_OL:
		fvalue = INFINITY;
		break;

	case MEAS_S_OL_MINUS:
		fvalue = -INFINITY;
		break;

	case MEAS_S_OPEN_TC:
		fvalue = NAN;
		break;

	default:
		analog.meaning->mq = 0;
		break;
	}

	if (analog.meaning->mq) {
		/* Got a measurement. */
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}
}
