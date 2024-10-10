/*
 * This file is part of the libsigrok project.
 *
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

static void handle_qm_19x_meta(const struct sr_dev_inst *sdi, char **tokens)
{
	struct dev_context *devc;
	int meas_type, meas_unit, meas_char, i;

	/* Make sure we have 7 valid tokens. */
	for (i = 0; tokens[i] && i < 7; i++);
	if (i != 7)
		return;

	if (strcmp(tokens[1], "1"))
		/* Invalid measurement. */
		return;

	if (strcmp(tokens[2], "3"))
		/* Only interested in input from the meter mode source. */
		return;

	devc = sdi->priv;

	/* Measurement type 11 == absolute, 19 = relative */
	meas_type = strtol(tokens[0], NULL, 10);
	if (meas_type != 11 && meas_type != 19)
		/* Device is in some mode we don't support. */
		return;

	/* We might get metadata for absolute and relative mode (if the device
	 * is in relative mode). In that case, relative takes precedence. */
	if (meas_type == 11 && devc->meas_type == 19)
		return;

	meas_unit = strtol(tokens[3], NULL, 10);
	if (meas_unit == 0)
		/* Device is turned off. Really. */
		return;
	meas_char = strtol(tokens[4], NULL, 10);

	devc->mq = 0;
	devc->unit = 0;
	devc->mqflags = 0;

	switch (meas_unit) {
	case 1:
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		if (meas_char == 1)
			devc->mqflags |= SR_MQFLAG_DC;
		else if (meas_char == 2)
			devc->mqflags |= SR_MQFLAG_AC;
		else if (meas_char == 3)
			devc->mqflags |= SR_MQFLAG_DC | SR_MQFLAG_AC;
		else if (meas_char == 15)
			devc->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
		break;
	case 2:
		devc->mq = SR_MQ_CURRENT;
		devc->unit = SR_UNIT_AMPERE;
		if (meas_char == 1)
			devc->mqflags |= SR_MQFLAG_DC;
		else if (meas_char == 2)
			devc->mqflags |= SR_MQFLAG_AC;
		else if (meas_char == 3)
			devc->mqflags |= SR_MQFLAG_DC | SR_MQFLAG_AC;
		break;
	case 3:
		if (meas_char == 1) {
			devc->mq = SR_MQ_RESISTANCE;
			devc->unit = SR_UNIT_OHM;
		} else if (meas_char == 16) {
			devc->mq = SR_MQ_CONTINUITY;
			devc->unit = SR_UNIT_BOOLEAN;
		}
		break;
	case 12:
		devc->mq = SR_MQ_TEMPERATURE;
		devc->unit = SR_UNIT_CELSIUS;
		break;
	case 13:
		devc->mq = SR_MQ_TEMPERATURE;
		devc->unit = SR_UNIT_FAHRENHEIT;
		break;
	default:
		sr_dbg("unknown unit: %d", meas_unit);
	}
	if (devc->mq == 0 && devc->unit == 0)
		return;

	/* If we got here, we know how to interpret the measurement. */
	devc->meas_type = meas_type;
	if (meas_type == 11)
		/* Absolute meter reading. */
		devc->is_relative = FALSE;
	else if (!strcmp(tokens[0], "19"))
		/* Relative meter reading. */
		devc->is_relative = TRUE;

}

static void handle_qm_19x_data(const struct sr_dev_inst *sdi, char **tokens)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float fvalue;
	int digits;

	digits = 2;
	if (!strcmp(tokens[0], "9.9E+37")) {
		/* An invalid measurement shows up on the display as "OL", but
		 * comes through like this. Since comparing 38-digit floats
		 * is rather problematic, we'll cut through this here. */
		fvalue = NAN;
	} else {
		if (sr_atof_ascii_digits(tokens[0], &fvalue, &digits) != SR_OK ||
		    fvalue == 0.0) {
			sr_err("Invalid float '%s'.", tokens[0]);
			return;
		}
	}

	devc = sdi->priv;
	if (devc->mq == 0 || devc->unit == 0)
		/* Don't have valid metadata yet. */
		return;

	if (devc->mq == SR_MQ_RESISTANCE && isnan(fvalue))
		fvalue = INFINITY;
	else if (devc->mq == SR_MQ_CONTINUITY) {
		if (isnan(fvalue))
			fvalue = 0.0;
		else
			fvalue = 1.0;
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &fvalue;
	analog.meaning->mq = devc->mq;
	analog.meaning->unit = devc->unit;
	analog.meaning->mqflags = 0;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	sr_sw_limits_update_samples_read(&devc->limits, 1);
}

SR_PRIV void fluke_handle_qm_190(const struct sr_dev_inst *sdi, char **tokens)
{
	struct dev_context *devc = sdi->priv;
	int num_tokens, i;

	num_tokens = g_strv_length(tokens);
	if (num_tokens < 7) {
		/* Response to QM <n> measurement request. */
		handle_qm_19x_data(sdi, tokens);
		return;
	}

	/*
	 * Response to QM: This is a comma-separated list of
	 * fields with metadata about the measurement. This
	 * format can return multiple sets of metadata,
	 * split into sets of 7 tokens each.
	 */
	devc->meas_type = 0;
	for (i = 0; i < num_tokens; i += 7)
		handle_qm_19x_meta(sdi, tokens + i);
}
