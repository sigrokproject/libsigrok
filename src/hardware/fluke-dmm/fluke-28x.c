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

enum reading_id {
	READING_LIVE = 0,
	READING_REL_LIVE,
	READING_PRIMARY,
	READING_SECONDARY,
	READING_BARGRAPH,
	READING_MINIMUM,
	READING_MAXIMUM,
	READING_AVERAGE,
	READING_REL_REFERENCE,
	READING_DB_REF,
	READING_TEMP_OFFSET,

	READING_INVALID,
};

struct reading_id_mapping {
	const char *name;
	enum reading_id id;
};

static struct reading_id_mapping reading_id_map[] = {
	{ "LIVE", READING_LIVE },
	{ "PRIMARY", READING_PRIMARY },
	{ "SECONDARY", READING_SECONDARY },
	{ "REL_LIVE", READING_REL_LIVE },
	{ "BARGRAPH", READING_BARGRAPH },
	{ "MINIMUM", READING_MINIMUM },
	{ "MAXIMUM", READING_MAXIMUM },
	{ "AVERAGE", READING_AVERAGE },
	{ "REL_REFERENCE", READING_REL_REFERENCE },
	{ "DB_REF", READING_DB_REF },
	{ "TEMP_OFFSET", READING_TEMP_OFFSET },
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

enum range_state {
	RANGE_INVALID = 0,
	RANGE_AUTO,
	RANGE_MANUAL,
};

enum lightning_state {
	LIGHTNING_INVALID = 0,
	LIGHTNING_ON,
	LIGHTNING_OFF,
};

enum measurement_mode {
	MODE_INVALID = 0,
	MODE_AUTO_HOLD,
	MODE_HOLD,
	MODE_LOW_PASS_FILTER,
	MODE_MIN_MAX_AVG,
	MODE_RECORD,
	MODE_REL,
	MODE_REL_PERCENT,
};

struct measurement_mode_mapping {
	const char *name;
	enum measurement_mode mode;
	enum sr_mqflag flags;
};

struct measurement_mode_mapping measurement_mode_map[] = {
	{ "AUTO_HOLD", MODE_AUTO_HOLD, SR_MQFLAG_HOLD },
	{ "HOLD", MODE_HOLD, SR_MQFLAG_HOLD },
	{ "LOW_PASS_FILTER", MODE_LOW_PASS_FILTER, 0 },
	{ "MIN_MAX_AVG", MODE_MIN_MAX_AVG, 0 },
	{ "RECORD", MODE_RECORD, 0 },
	{ "REL", MODE_REL, SR_MQFLAG_RELATIVE },
	{ "REL_PERCENT", MODE_REL_PERCENT, SR_MQFLAG_RELATIVE },
};

struct qdda_reading {
	enum reading_id id;
	float value;
	const struct unit_mapping *unit;
	int unit_exp;
	int decimals;
	int display_digits;
	enum measurement_state state;
	enum measurement_attribute attr;
	double ts;
};

struct qdda_range {
	enum range_state state;
	const struct unit_mapping *unit;
	int number;
	int unit_exp;
};

struct qdda_message {
	const char *prim_fun;
	const char *sec_fun;
	struct qdda_range range;
	enum lightning_state lightning;
	double min_max_start;
	int num_modes;
	const struct measurement_mode_mapping **modes;
	int num_readings;
	struct qdda_reading *readings;
};

#define QDDA_MIN_FIELDS 10
#define QDDA_READING_FIELDS 9

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


static enum range_state parse_range_state(const char *state)
{
	if (!strcmp("AUTO", state)) {
		return RANGE_AUTO;
	} else if (!strcmp("MANUAL", state)) {
		return RANGE_MANUAL;
	} else {
		sr_warn("Unknown range state '%s'", state);
		return RANGE_INVALID;
	}
}

static enum lightning_state parse_lightning_state(const char *state)
{
	if (!strcmp("ON", state)) {
		return LIGHTNING_ON;
	} else if (!strcmp("OFF", state)) {
		return LIGHTNING_OFF;
	} else {
		sr_warn("Unknown lightning state '%s'", state);
		return LIGHTNING_INVALID;
	}
}

static const struct measurement_mode_mapping *parse_mode(const char *name)
{
	unsigned int i;

	if (!name)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(measurement_mode_map); i++) {
		if (!strcmp(measurement_mode_map[i].name, name))
			return &measurement_mode_map[i];
	}

	sr_warn("Unknown measurement mode '%s'", name);
	return NULL;
}

static enum reading_id parse_reading_id(const char *name)
{
	unsigned int i;

	if (!name)
		return READING_INVALID;

	for (i = 0; i < ARRAY_SIZE(reading_id_map); i++) {
		if (!strcmp(reading_id_map[i].name, name))
			return reading_id_map[i].id;
	}

	sr_warn("Unknown reading id '%s'", name);
	return READING_INVALID;
}

static struct qdda_message *parse_qdda(char **tokens, int tokenc)
{
	struct qdda_message *msg;
	struct qdda_reading *r;
	int cur;
	int i;

	if (tokenc < QDDA_MIN_FIELDS) {
		sr_err("Too few fields in QDDA response. Got %i, expected %i.",
		       tokenc, QDDA_MIN_FIELDS);
		return NULL;
	}

	cur = 0;
	msg = g_malloc(sizeof(struct qdda_message));
	msg->modes = NULL;
	msg->readings = NULL;

	msg->prim_fun = tokens[cur++];
	msg->sec_fun = tokens[cur++];
	msg->range.state = parse_range_state(tokens[cur++]);
	msg->range.unit = parse_unit(tokens[cur++]);
	if (!msg->range.unit ||
	    sr_atoi(tokens[cur++], &msg->range.number) != SR_OK ||
	    sr_atoi(tokens[cur++], &msg->range.unit_exp) != SR_OK)
		goto parse_error;
	msg->lightning = parse_lightning_state(tokens[cur++]);
	if (sr_atod(tokens[cur++], &msg->min_max_start) != SR_OK ||
	    sr_atoi(tokens[cur++], &msg->num_modes) != SR_OK)
		goto parse_error;

	if (tokenc - cur < msg->num_modes) {
		sr_err("Too few fields for QDDA response after mode count.");
		goto err_out;
	}
	msg->modes = g_malloc(msg->num_modes * sizeof(void *));
	for (i = 0; i < msg->num_modes; i++) {
		msg->modes[i] = parse_mode(tokens[cur++]);
		if (!msg->modes[i])
			goto parse_error;
	}

	if (sr_atoi(tokens[cur++], &msg->num_readings) != SR_OK)
		goto parse_error;
	if (tokenc - cur < msg->num_readings * QDDA_READING_FIELDS) {
		sr_err("Too few fields for QDDA response after readings count.");
		goto err_out;
	}
	msg->readings = g_malloc(msg->num_readings * sizeof(struct qdda_reading));
	for (i = 0; i < msg->num_readings; i++) {
		r = &msg->readings[i];

		r->id = parse_reading_id(tokens[cur++]);
		if (sr_atof(tokens[cur++], &r->value) != SR_OK)
			goto parse_error;
		r->unit = parse_unit(tokens[cur++]);
		if (!r->unit ||
		    sr_atoi(tokens[cur++], &r->unit_exp) != SR_OK ||
		    sr_atoi(tokens[cur++], &r->decimals) != SR_OK ||
		    sr_atoi(tokens[cur++], &r->display_digits) != SR_OK)
			goto parse_error;

		r->state = parse_measurement_state(tokens[cur++]);
		r->attr = parse_attribute(tokens[cur++]);
		if (sr_atod(tokens[cur++], &r->ts) != SR_OK)
		    goto parse_error;
	}

	if (cur != tokenc) {
		sr_warn("Unexpected number of QDDA fields. Parsed %i, expected %i.",
			cur, tokenc);
	}

	return msg;

parse_error:
	sr_err("Fatal error when parsing QDDA reply");
err_out:
	if (msg->modes)
		g_free(msg->modes);
	if (msg->readings)
		g_free(msg->readings);
	g_free(msg);
	return NULL;
}

SR_PRIV void fluke_handle_qdda_28x(const struct sr_dev_inst *sdi, char **tokens)
{
	struct dev_context *devc = devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	int num_tokens, i;
	struct qdda_message *qdda;
	enum sr_mqflag flags;

	num_tokens = g_strv_length(tokens);

	sr_dbg("Parsing QDDA response with %i tokens", num_tokens);
	qdda = parse_qdda(tokens, num_tokens);
	if (!qdda)
		return;

	flags = qdda->readings[0].unit->mqflags |
		(qdda->range.state == RANGE_AUTO ? SR_MQFLAG_AUTORANGE : 0);
	for (i = 0; i < qdda->num_modes; i++)
		flags |= qdda->modes[i]->flags;

	// TODO: qdda->state
	// TODO: qdda->attribute
	// TODO: Continuity measurements (prim./sec. func)

	sr_analog_init(&analog, &encoding, &meaning, &spec,
		qdda->readings[0].decimals - qdda->readings[0].unit_exp);
	analog.data = &qdda->readings[0].value;
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.meaning->mq = qdda->readings[0].unit->mq;
	analog.meaning->mqflags = flags;
	analog.meaning->unit = qdda->readings[0].unit->unit;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	sr_sw_limits_update_samples_read(&devc->limits, 1);

	g_free(qdda->modes);
	g_free(qdda->readings);
	g_free(qdda);
}
