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

SR_PRIV void fluke_handle_qm_18x(const struct sr_dev_inst *sdi, char **tokens)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float fvalue;
	char *e, *u;
	gboolean is_oor;
	int digits;
	int exponent;
	enum sr_mq mq;
	enum sr_unit unit;
	enum sr_mqflag mqflags;

	devc = sdi->priv;

	if (strcmp(tokens[0], "QM") || !tokens[1])
		return;

	if ((e = strstr(tokens[1], "Out of range"))) {
		is_oor = TRUE;
		fvalue = -1;
		digits = 0;
		while (*e && *e != '.')
			e++;
	} else {
		is_oor = FALSE;
		/* Delimit the float, since sr_atof_ascii() wants only
		 * a valid float here. */
		e = tokens[1];
		while (*e && *e != ' ')
			e++;
		*e++ = '\0';
		if (sr_atof_ascii_digits(tokens[1], &fvalue, &digits) != SR_OK) {
			/* Happens all the time, when switching modes. */
			sr_dbg("Invalid float: '%s'", tokens[1]);
			return;
		}
	}
	while (*e && *e == ' ')
		e++;

	if (is_oor)
		fvalue = NAN;

	mq = 0;
	unit = 0;
	exponent = 0;
	mqflags = 0;
	if ((u = strstr(e, "V DC")) || (u = strstr(e, "V AC"))) {
		mq = SR_MQ_VOLTAGE;
		unit = SR_UNIT_VOLT;
		if (!is_oor && e[0] == 'm')
			exponent = -3;
		/* This catches "V AC", "V DC" and "V AC+DC". */
		if (strstr(u, "AC"))
			mqflags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
		if (strstr(u, "DC"))
			mqflags |= SR_MQFLAG_DC;
	} else if ((u = strstr(e, "dBV")) || (u = strstr(e, "dBm"))) {
		mq = SR_MQ_VOLTAGE;
		if (u[2] == 'm')
			unit = SR_UNIT_DECIBEL_MW;
		else
			unit = SR_UNIT_DECIBEL_VOLT;
		mqflags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
	} else if ((u = strstr(e, "Ohms"))) {
		mq = SR_MQ_RESISTANCE;
		unit = SR_UNIT_OHM;
		if (is_oor)
			fvalue = INFINITY;
		else if (e[0] == 'k')
			exponent = 3;
		else if (e[0] == 'M')
			exponent = 6;
	} else if (!strcmp(e, "nS")) {
		mq = SR_MQ_CONDUCTANCE;
		unit = SR_UNIT_SIEMENS;
		exponent = -9;
	} else if ((u = strstr(e, "Farads"))) {
		mq = SR_MQ_CAPACITANCE;
		unit = SR_UNIT_FARAD;
		if (!is_oor) {
			if (e[0] == 'm')
				exponent = -3;
			else if (e[0] == 'u')
				exponent = -6;
			else if (e[0] == 'n')
				exponent = -9;
		}
	} else if ((u = strstr(e, "Deg C")) || (u = strstr(e, "Deg F"))) {
		mq = SR_MQ_TEMPERATURE;
		if (u[4] == 'C')
			unit = SR_UNIT_CELSIUS;
		else
			unit = SR_UNIT_FAHRENHEIT;
	} else if ((u = strstr(e, "A AC")) || (u = strstr(e, "A DC"))) {
		mq = SR_MQ_CURRENT;
		unit = SR_UNIT_AMPERE;
		/* This catches "A AC", "A DC" and "A AC+DC". */
		if (strstr(u, "AC"))
			mqflags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
		if (strstr(u, "DC"))
			mqflags |= SR_MQFLAG_DC;
		if (!is_oor) {
			if (e[0] == 'm')
				exponent = -3;
			else if (e[0] == 'u')
				exponent = -6;
		}
	} else if ((u = strstr(e, "Hz"))) {
		mq = SR_MQ_FREQUENCY;
		unit = SR_UNIT_HERTZ;
		if (e[0] == 'k')
			exponent = 3;
	} else if (!strcmp(e, "%")) {
		mq = SR_MQ_DUTY_CYCLE;
		unit = SR_UNIT_PERCENTAGE;
	} else if ((u = strstr(e, "ms"))) {
		mq = SR_MQ_PULSE_WIDTH;
		unit = SR_UNIT_SECOND;
		exponent = -3;
	}

	if (mq != 0) {
		/* Got a measurement. */
		digits -= exponent;
		fvalue *= pow(10.0f, exponent);

		sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
		analog.data = &fvalue;
		analog.num_samples = 1;
		analog.meaning->unit = unit;
		analog.meaning->mq = mq;
		analog.meaning->mqflags = mqflags;
		analog.meaning->channels = sdi->channels;

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}
}
