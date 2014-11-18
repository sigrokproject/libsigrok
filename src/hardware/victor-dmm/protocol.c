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

#include <glib.h>
#include <string.h>
#include <math.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

/* Reverse the high nibble into the low nibble */
static uint8_t decode_digit(uint8_t in)
{
	uint8_t out, i;

	out = 0;
	in >>= 4;
	for (i = 0x08; i; i >>= 1) {
		out >>= 1;
		if (in & i)
			out |= 0x08;
	}

	return out;
}

static void decode_buf(struct sr_dev_inst *sdi, unsigned char *data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog2 analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;
	long factor, ivalue;
	uint8_t digits[4];
	gboolean is_duty, is_continuity, is_diode, is_ac, is_dc, is_auto;
	gboolean is_hold, is_max, is_min, is_relative, minus;
	float fvalue;

	devc = sdi->priv;

	digits[0] = decode_digit(data[12]);
	digits[1] = decode_digit(data[11]);
	digits[2] = decode_digit(data[10]);
	digits[3] = decode_digit(data[9]);

	if (digits[0] == 0x0f && digits[1] == 0x00 && digits[2] == 0x0a &&
			digits[3] == 0x0f)
		/* The "over limit" (OL) display comes through like this */
		ivalue = -1;
	else if (digits[0] > 9 || digits[1] > 9 || digits[2] > 9 || digits[3] > 9)
		/* An invalid digit in any position denotes no value. */
		ivalue = -2;
	else {
		ivalue = digits[0] * 1000;
		ivalue += digits[1] * 100;
		ivalue += digits[2] * 10;
		ivalue += digits[3];
	}

	/* Decimal point position */
	factor = 0;
	switch (data[7] >> 4) {
	case 0x00:
		factor = 0;
		break;
	case 0x02:
		factor = 1;
		break;
	case 0x04:
		factor = 2;
		break;
	case 0x08:
		factor = 3;
		break;
	default:
		sr_err("Unknown decimal point byte: 0x%.2x.", data[7]);
		break;
	}

	/* Minus flag */
	minus = data[2] & 0x01;

	/* Mode detail symbols on the right side of the digits */
	is_duty = is_continuity = is_diode = FALSE;
	switch (data[4]) {
	case 0x00:
		/* None. */
		break;
	case 0x01:
		/* Micro */
		factor += 6;
		break;
	case 0x02:
		/* Milli */
		factor += 3;
		break;
	case 0x04:
		/* Kilo */
		ivalue *= 1000;
		break;
	case 0x08:
		/* Mega */
		ivalue *= 1000000;
		break;
	case 0x10:
		/* Continuity shows up as Ohm + this bit */
		is_continuity = TRUE;
		break;
	case 0x20:
		/* Diode tester is Volt + this bit */
		is_diode = TRUE;
		break;
	case 0x40:
		is_duty = TRUE;
		break;
	case 0x80:
		/* Never seen */
		sr_dbg("Unknown mode right detail: 0x%.2x.", data[4]);
		break;
	default:
		sr_dbg("Unknown/invalid mode right detail: 0x%.2x.", data[4]);
		break;
	}

	/* Scale flags on the right, continued */
	is_max = is_min = FALSE;
	if (data[5] & 0x04)
		is_max = TRUE;
	if (data[5] & 0x08)
		is_min = TRUE;
	if (data[5] & 0x40)
		/* Nano */
		factor += 9;

	/* Mode detail symbols on the left side of the digits */
	is_auto = is_dc = is_ac = is_hold = is_relative = FALSE;
	if (data[6] & 0x04)
		is_auto = TRUE;
	if (data[6] & 0x08)
		is_dc = TRUE;
	if (data[6] & 0x10)
		is_ac = TRUE;
	if (data[6] & 0x20)
		is_relative = TRUE;
	if (data[6] & 0x40)
		is_hold = TRUE;

	fvalue = (float)ivalue / pow(10, factor);
	if (minus)
		fvalue = -fvalue;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 4);

	/* Measurement mode */
	meaning.channels = sdi->channels;
	meaning.mq = 0;
	switch (data[3]) {
	case 0x00:
		if (is_duty) {
			meaning.mq = SR_MQ_DUTY_CYCLE;
			meaning.unit = SR_UNIT_PERCENTAGE;
		} else
			sr_dbg("Unknown measurement mode: %.2x.", data[3]);
		break;
	case 0x01:
		if (is_diode) {
			meaning.mq = SR_MQ_VOLTAGE;
			meaning.unit = SR_UNIT_VOLT;
			meaning.mqflags |= SR_MQFLAG_DIODE;
			if (ivalue < 0)
				fvalue = NAN;
		} else {
			if (ivalue < 0)
				break;
			meaning.mq = SR_MQ_VOLTAGE;
			meaning.unit = SR_UNIT_VOLT;
			if (is_ac)
				meaning.mqflags |= SR_MQFLAG_AC;
			if (is_dc)
				meaning.mqflags |= SR_MQFLAG_DC;
		}
		break;
	case 0x02:
		meaning.mq = SR_MQ_CURRENT;
		meaning.unit = SR_UNIT_AMPERE;
		if (is_ac)
			meaning.mqflags |= SR_MQFLAG_AC;
		if (is_dc)
			meaning.mqflags |= SR_MQFLAG_DC;
		break;
	case 0x04:
		if (is_continuity) {
			meaning.mq = SR_MQ_CONTINUITY;
			meaning.unit = SR_UNIT_BOOLEAN;
			fvalue = ivalue < 0 ? 0.0 : 1.0;
		} else {
			meaning.mq = SR_MQ_RESISTANCE;
			meaning.unit = SR_UNIT_OHM;
			if (ivalue < 0)
				fvalue = INFINITY;
		}
		break;
	case 0x08:
		/* Never seen */
		sr_dbg("Unknown measurement mode: 0x%.2x.", data[3]);
		break;
	case 0x10:
		meaning.mq = SR_MQ_FREQUENCY;
		meaning.unit = SR_UNIT_HERTZ;
		break;
	case 0x20:
		meaning.mq = SR_MQ_CAPACITANCE;
		meaning.unit = SR_UNIT_FARAD;
		break;
	case 0x40:
		meaning.mq = SR_MQ_TEMPERATURE;
		meaning.unit = SR_UNIT_CELSIUS;
		break;
	case 0x80:
		meaning.mq = SR_MQ_TEMPERATURE;
		meaning.unit = SR_UNIT_FAHRENHEIT;
		break;
	default:
		sr_dbg("Unknown/invalid measurement mode: 0x%.2x.", data[3]);
		break;
	}
	if (meaning.mq == 0)
		return;

	if (is_auto)
		meaning.mqflags |= SR_MQFLAG_AUTORANGE;
	if (is_hold)
		meaning.mqflags |= SR_MQFLAG_HOLD;
	if (is_max)
		meaning.mqflags |= SR_MQFLAG_MAX;
	if (is_min)
		meaning.mqflags |= SR_MQFLAG_MIN;
	if (is_relative)
		meaning.mqflags |= SR_MQFLAG_RELATIVE;

	analog.data = &fvalue;
	analog.num_samples = 1;

	packet.type = SR_DF_ANALOG2;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	devc->num_samples++;
}

SR_PRIV int victor_dmm_receive_data(struct sr_dev_inst *sdi, unsigned char *buf)
{
	GString *dbg;
	int i;
	unsigned char data[DMM_DATA_SIZE];
	unsigned char obfuscation[DMM_DATA_SIZE] = "jodenxunickxia";
	unsigned char shuffle[DMM_DATA_SIZE] = {
		6, 13, 5, 11, 2, 7, 9, 8, 3, 10, 12, 0, 4, 1
	};

	for (i = 0; i < DMM_DATA_SIZE && buf[i] == 0; i++);
	if (i == DMM_DATA_SIZE) {
		/* This DMM outputs all zeroes from time to time, just ignore it. */
		sr_dbg("Received all zeroes.");
		return SR_OK;
	}

	/* Deobfuscate and reorder data. */
	for (i = 0; i < DMM_DATA_SIZE; i++)
		data[shuffle[i]] = (buf[i] - obfuscation[i]) & 0xff;

	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		dbg = g_string_sized_new(128);
		g_string_printf(dbg, "Deobfuscated.");
		for (i = 0; i < DMM_DATA_SIZE; i++)
			g_string_append_printf(dbg, " %.2x", data[i]);
		sr_spew("%s", dbg->str);
		g_string_free(dbg, TRUE);
	}

	decode_buf(sdi, data);

	return SR_OK;
}
