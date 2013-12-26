/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

#include <math.h>
#include <string.h>
#include "protocol.h"

/** Set or clear flags in devc->mqflags. */
static void setmqf(struct dev_context *devc, uint64_t flags, gboolean set)
{
	if (set)
		devc->mqflags |= flags;
	else
		devc->mqflags &= ~flags;
}

/** Decode current type and measured value, Metrahit 12-16. */
static void decode_ctmv_16(uint8_t ctmv, struct dev_context *devc)
{
	devc->mq = 0;
	devc->unit = 0;
	devc->mqflags = 0;

	switch (ctmv) {
	case 0x00: /* 0000 - */
		break;
	case 0x01: /* 0001 mV DC */
		devc->scale1000 = -1; /* Fall through */
	case 0x02: /* 0010 V DC */
	case 0x03: /* 0011 V AC+DC */
	case 0x04: /* 0100 V AC */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		if (ctmv <= 0x03)
			devc->mqflags |= SR_MQFLAG_DC;
		if (ctmv >= 0x03) {
			devc->mqflags |= SR_MQFLAG_AC;
			if (devc->model >= SR_METRAHIT_16S)
				devc->mqflags |= SR_MQFLAG_RMS;
		}
		break;
	case 0x05: /* 0101 Hz (15S/16S only) */
	case 0x06: /* 0110 kHz (15S/16S only) */
		devc->mq = SR_MQ_FREQUENCY;
		devc->unit = SR_UNIT_HERTZ;
		if (ctmv == 0x06)
			devc->scale1000 = 1;
		break;
	case 0x07: /* 0111 % (15S/16S only) */
		devc->mq = SR_MQ_DUTY_CYCLE;
		devc->unit = SR_UNIT_PERCENTAGE;
		break;
	case 0x08: /* 1000 Diode */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		devc->mqflags |= SR_MQFLAG_DIODE;
		break;
	case 0x09: /* 1001 Ohm, °C */
	case 0x0a: /* 1010 kOhm */
	case 0x0b: /* 1011 MOhm */
		devc->mq = SR_MQ_RESISTANCE; /* Changed to temp. later if req.*/
		devc->unit = SR_UNIT_OHM;
		devc->scale1000 = ctmv - 0x09;
		break;
	case 0x0c: /* 1100 nF (15S/16S only) */
	case 0x0d: /* 1101 µF (15S/16S only) */
		devc->mq = SR_MQ_CAPACITANCE;
		devc->unit = SR_UNIT_FARAD;
		if (ctmv == 0x0c)
			devc->scale1000 = -3;
		else
			devc->scale1000 = -2;
		break;
	case 0x0e: /* mA, µA */
		devc->scale1000 = -1; /* Fall through. */
	case 0x0f: /* A */
		devc->mq = SR_MQ_CURRENT;
		devc->unit = SR_UNIT_AMPERE;
		if (devc->model == SR_METRAHIT_16S)
			devc->mqflags |= SR_MQFLAG_RMS;
		/* 16I A only with clamp, RMS questionable. */
		break;
	}
}

/**
 * Decode range/sign/acdc byte special chars (Metrahit 12-16).
 *
 * @param[in] spc Special characters 1 and 2 (s1 | (s2 << 4)).
 */
static void decode_rs_16(uint8_t rs, struct dev_context *devc)
{
	sr_spew("decode_rs_16(%d) scale = %f", rs, devc->scale);

	if (rs & 0x08) /* Sign */
		devc->scale *= -1.0;

	if (devc->mq == SR_MQ_CURRENT) {
		if (rs & 0x04) /* Current is AC */
			devc->mqflags |= SR_MQFLAG_AC;
		else
			devc->mqflags |= SR_MQFLAG_DC;
	}

	switch (rs & 0x03) {
	case 0:
		if (devc->mq == SR_MQ_VOLTAGE) /* V */
			devc->scale *= 0.1;
		else if (devc->mq == SR_MQ_CURRENT) /* 000.0 µA */
			devc->scale *= 0.0000001; /* Untested! */
		else if (devc->mq == SR_MQ_RESISTANCE) {
			if (devc->buflen >= 10) {
				/* °C with 10 byte msg type, otherwise GOhm. */
				devc->mq = SR_MQ_TEMPERATURE;
				devc->unit = SR_UNIT_CELSIUS;
				devc->scale *= 0.01;
			} else if ((devc->scale1000 == 2)) {
				/* 16I Iso 500/1000V 3 GOhm */
				devc->scale *= 0.1;
			}
		}
		break;
	case 1:
		devc->scale *= 0.0001;
		break;
	case 2:
		devc->scale *= 0.001;
		break;
	case 3:
		devc->scale *= 0.01;
		break;
	}
}

/**
 * Decode special chars, Metrahit 12-16.
 *
 * @param[in] spc Special characters 1 and 2 (s1 | (s2 << 4)).
 */
static void decode_spc_16(uint8_t spc, struct dev_context *devc)
{
	/* xxxx1xxx ON */
	/* TODO: What does that mean? Power on? The 16I sets this. */
	/* xxxxx1xx BEEP */
	/* xxxxxx1x Low battery */
	/* xxxxxxx1 FUSE */
	/* 1xxxxxxx MIN */
	setmqf(devc, SR_MQFLAG_MIN, spc & 0x80);

	/* x1xxxxxx MAN */
	setmqf(devc, SR_MQFLAG_AUTORANGE, !(spc & 0x40));

	/* xx1xxxxx DATA */
	setmqf(devc, SR_MQFLAG_HOLD, spc & 0x20);

	/* xxx1xxxx MAX */
	setmqf(devc, SR_MQFLAG_MAX, spc & 0x10);
}

/** Decode current type and measured value, Metrahit 18. */
static void decode_ctmv_18(uint8_t ctmv, struct dev_context *devc)
{
	devc->mq = 0;
	devc->unit = 0;
	devc->mqflags = 0;

	switch (ctmv) {
	case 0x00: /* 0000 - */
		break;
	case 0x01: /* 0001 V AC */
	case 0x02: /* 0010 V AC+DC */
	case 0x03: /* 0011 V DC */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		if (ctmv <= 0x02)
			devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_RMS);
		if (ctmv >= 0x02)
			devc->mqflags |= SR_MQFLAG_DC;
		break;
	case 0x04: /* 0100 Ohm/Ohm with buzzer */
		devc->mq = SR_MQ_RESISTANCE;
		devc->unit = SR_UNIT_OHM;
		break;
	case 0x05: /* 0101 Diode/Diode with buzzer */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		devc->mqflags |= SR_MQFLAG_DIODE;
		break;
	case 0x06: /* 0110 °C */
		devc->mq = SR_MQ_TEMPERATURE;
		devc->unit = SR_UNIT_CELSIUS;
		break;
	case 0x07: /* 0111 F */
		devc->mq = SR_MQ_CAPACITANCE;
		devc->unit = SR_UNIT_FARAD;
		break;
	case 0x08: /* 1000 mA DC */
	case 0x09: /* 1001 A DC */
	case 0x0a: /* 1010 mA AC+DC */
	case 0x0b: /* 1011 A AC+DC */
		devc->mq = SR_MQ_CURRENT;
		devc->unit = SR_UNIT_AMPERE;
		devc->mqflags |= SR_MQFLAG_DC;
		if (ctmv >= 0x0a)
			devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_RMS);
		if ((ctmv == 0x08) || (ctmv == 0x0a))
			devc->scale1000 = -1;
		break;
	case 0x0c: /* 1100 Hz */
		devc->mq = SR_MQ_FREQUENCY;
		devc->unit = SR_UNIT_HERTZ;
		break;
	case 0x0d: /* 1101 dB */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_DECIBEL_VOLT;
		devc->mqflags |= SR_MQFLAG_AC; /* dB available for AC only */
		break;
	case 0x0e: /* 1110 Events AC, Events AC+DC. Actually delivers just
				* current voltage via IR, nothing more. */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		devc->mqflags |= SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS;
		break;
	case 0x0f: /* 1111 Clock */
		devc->mq = SR_MQ_TIME;
		devc->unit = SR_UNIT_SECOND;
		devc->mqflags |= SR_MQFLAG_DURATION;
		break;
	}
}

/**
 * Decode range/sign/acdc byte special chars, Metrahit 18.
 *
 * @param[in] rs Rance/sign byte.
 */
static void decode_rs_18(uint8_t rs, struct dev_context *devc)
{
	int range;

	/* Sign */
	if (((devc->scale > 0) && (rs & 0x08)) ||
		((devc->scale < 0) && !(rs & 0x08)))
		devc->scale *= -1.0;

	/* Range */
	range = rs & 0x07;
	switch (devc->mq) {
	case SR_MQ_VOLTAGE:
		if (devc->unit == SR_UNIT_DECIBEL_VOLT) {
			devc->scale *= pow(10.0, -2);
			/*
			 * When entering relative mode, the device switches
			 * from 10 byte to 6 byte msg format. Unfortunately
			 * it switches back to 10 byte when the second value
			 * is measured, so that's not sufficient to
			 * identify relative mode.
			 */
		}
		else if (devc->vmains_29S)
			devc->scale *= pow(10.0, range - 2);
		else
			devc->scale *= pow(10.0, range - 5);
		break;
	case SR_MQ_CURRENT:
		if (devc->scale1000 == -1)
			devc->scale *= pow(10.0, range - 5);
		else
			devc->scale *= pow(10.0, range - 4);
		break;
	case SR_MQ_RESISTANCE:
		devc->scale *= pow(10.0, range - 2);
		break;
	case SR_MQ_FREQUENCY:
		devc->scale *= pow(10.0, range - 3);
		break;
	case SR_MQ_TEMPERATURE:
		devc->scale *= pow(10.0, range - 2);
		break;
	case SR_MQ_CAPACITANCE:
		devc->scale *= pow(10.0, range - 14);
		break;
	/* TODO: 29S Mains measurements. */
	}
}

/**
 * Decode special chars, Metrahit 18.
 *
 * @param[in] spc Special characters 1 and 2 (s1 | (s2 << 4)).
 */
static void decode_spc_18(uint8_t spc, struct dev_context *devc)
{
	/* xxxx1xxx ZERO */
	/* xxxxx1xx BEEP */
	/* xxxxxx1x Low battery */
	/* xxxxxxx1 Fuse */

	if (devc->mq == SR_MQ_TIME) {
		/* xxx1xxxx Clock running: 1; stop: 0 */
		sr_spew("Clock running: %d", spc >> 4);
	} else {
		/* 1xxxxxxx MAN */
		setmqf(devc, SR_MQFLAG_AUTORANGE, !(spc & 0x80));

		/* x1xxxxxx MIN */
		setmqf(devc, SR_MQFLAG_MIN, spc & 0x40);

		/* xx1xxxxx MAX */
		setmqf(devc, SR_MQFLAG_MAX, spc & 0x20);

		/* xxx1xxxx DATA */
		setmqf(devc, SR_MQFLAG_HOLD, spc & 0x10);
	}
}

/**
 * Decode current type and measured value, Metrahit 2x.
 *
 * @param[in] ctmv Current type and measured value (v1 | (v2 << 4)).
 */
static void decode_ctmv_2x(uint8_t ctmv, struct dev_context *devc)
{
	if ((ctmv > 0x1c) || (!devc)) {
		sr_err("decode_ctmv_2x(%d): invalid param(s)!", ctmv);
		return;
	}

	devc->mq = 0;
	devc->unit = 0;
	devc->mqflags = 0;

	switch (ctmv) {
	/* 00000 unused */
	case 0x01: /* 00001 V DC */
	case 0x02: /* 00010 V AC+DC */
	case 0x03: /* 00011 V AC */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		if (ctmv <= 0x02)
			devc->mqflags |= SR_MQFLAG_DC;
		if (ctmv >= 0x02) {
			devc->mqflags |= SR_MQFLAG_AC;
			if (devc->model >= SR_METRAHIT_24S)
				devc->model |= SR_MQFLAG_RMS;
		}
		break;
	case 0x04: /* 00100 mA DC */
	case 0x05: /* 00101 mA AC+DC */
		devc->scale1000 = -1;
	case 0x06: /* 00110 A DC */
	case 0x07: /* 00111 A AC+DC */
		devc->mq = SR_MQ_CURRENT;
		devc->unit = SR_UNIT_AMPERE;
		devc->mqflags |= SR_MQFLAG_DC;
		if ((ctmv == 0x05) || (ctmv == 0x07)) {
			devc->mqflags |= SR_MQFLAG_AC;
			if (devc->model >= SR_METRAHIT_24S)
				devc->mqflags |= SR_MQFLAG_RMS;
		}
		break;
	case 0x08: /* 01000 Ohm */
		devc->mq = SR_MQ_RESISTANCE;
		devc->unit = SR_UNIT_OHM;
		break;
	case 0x09: /* 01001 F */
		devc->mq = SR_MQ_CAPACITANCE;
		devc->unit = SR_UNIT_FARAD;
		devc->scale *= 0.1;
		break;
	case 0x0a: /* 01010 dB */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_DECIBEL_VOLT;
		devc->mqflags |= SR_MQFLAG_AC;
		break;
	case 0x0b: /* 01011 Hz U ACDC */
	case 0x0c: /* 01100 Hz U AC */
		devc->mq = SR_MQ_FREQUENCY;
		devc->unit = SR_UNIT_HERTZ;
		devc->mqflags |= SR_MQFLAG_AC;
		if (ctmv <= 0x0b)
			devc->mqflags |= SR_MQFLAG_DC;
		break;
	case 0x0d: /* 01101 W on power, mA range (29S only) */
	case 0x0e: /* 01110 W on power, A range (29S only) */
		/* TODO: Differences between Send Mode and bidir protocol here */
		devc->mq = SR_MQ_POWER;
		devc->unit = SR_UNIT_WATT;
		break;
	case 0x0f: /* 01111 Diode */
	case 0x10: /* 10000 Diode with buzzer (actually cont. with voltage) */
		devc->unit = SR_UNIT_VOLT;
		if (ctmv == 0x0f) {
			devc->mq = SR_MQ_VOLTAGE;
			devc->mqflags |= SR_MQFLAG_DIODE;
			devc->scale *= 0.1;
		} else {
			devc->mq = SR_MQ_CONTINUITY;
			devc->scale *= 0.00001;
		}
		devc->unit = SR_UNIT_VOLT;
		break;
	case 0x11: /* 10001 Ohm with buzzer */
		devc->mq = SR_MQ_CONTINUITY;
		devc->unit = SR_UNIT_OHM;
		devc->scale1000 = -1;
		break;
	case 0x12: /* 10010 Temperature */
		devc->mq = SR_MQ_TEMPERATURE;
		devc->unit = SR_UNIT_CELSIUS;
		/* This can be Fahrenheit. That is detected by range=4 later. */
		break;
	/* 0x13 10011, 0x14 10100 unsed */
	case 0x15: /* 10101 Press (29S only) */
		/* TODO: What does that mean? Possibly phase shift?
		   Then we need a unit/flag for it. */
		devc->mq = SR_MQ_GAIN;
		devc->unit = SR_UNIT_PERCENTAGE;
		break;
	case 0x16: /* 10110 Pulse W (29S only) */
		/* TODO: Own unit and flag for this! */
		devc->mq = SR_MQ_POWER;
		devc->unit = SR_UNIT_WATT;
		break;
	case 0x17: /* 10111 TRMS V on mains (29S only) */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_RMS);
		devc->vmains_29S = TRUE;
		break;
	case 0x18: /* 11000 Counter (zero crossings of a signal) */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_UNITLESS;
		break;
	case 0x19: /* 11001 Events U ACDC */
	case 0x1a: /* 11010 Events U AC */
		/* TODO: No unit or flags for this yet! */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_UNITLESS;
		devc->mqflags |= SR_MQFLAG_AC;
		if (ctmv <= 0x19)
			devc->mqflags |= SR_MQFLAG_DC;
		break;
	case 0x1b: /* 11011 pulse on mains (29S only) */
		/* TODO: No unit or flags for this yet! */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_UNITLESS;
		devc->mqflags |= SR_MQFLAG_AC;
		break;
	case 0x1c: /* 11100 dropout on mains (29S only) */
		/* TODO: No unit or flags for this yet! */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_UNITLESS;
		devc->mqflags |= SR_MQFLAG_AC;
		break;
	default:
		sr_err("decode_ctmv_2x(%d, ...): Unknown ctmv!");
		break;
	}
}

/**
 * Decode range/sign/acdc byte special chars, Metrahit 2x.
 *
 * @param[in] rs Rance/sign byte.
 */
static void decode_rs_2x(uint8_t rs, struct dev_context *devc)
{
	int range;

	/* Sign */
	if (((devc->scale > 0) && (rs & 0x08)) ||
		((devc->scale < 0) && !(rs & 0x08)))
		devc->scale *= -1.0;

	/* Range */
	range = rs & 0x07;
	switch (devc->mq) {
	case SR_MQ_VOLTAGE:
		if (devc->unit == SR_UNIT_DECIBEL_VOLT)
			devc->scale *= pow(10.0, -3);
		else if (devc->vmains_29S)
			devc->scale *= pow(10.0, range - 2);
		else if(devc->mqflags & SR_MQFLAG_AC)
			devc->scale *= pow(10.0, range - 6);
		else /* "Undocumented feature": Between AC and DC
			scaling differs by 1. */
			devc->scale *= pow(10.0, range - 5);
		break;
	case SR_MQ_CURRENT:
		if (devc->scale1000 == -1)
			devc->scale *= pow(10.0, range - 5);
		else
			devc->scale *= pow(10.0, range - 4);
		break;
	case SR_MQ_RESISTANCE:
		devc->scale *= pow(10.0, range - 3);
		break;
	case SR_MQ_FREQUENCY:
		devc->scale *= pow(10.0, range - 3);
		break;
	case SR_MQ_TEMPERATURE:
		if (range == 4) /* Indicator for °F */
			devc->unit = SR_UNIT_FAHRENHEIT;
		devc->scale *= pow(10.0, - 2);
		break;
	case SR_MQ_CAPACITANCE:
		devc->scale *= pow(10.0, range - 13);
		break;
	/* TODO: 29S Mains measurements. */
	}
}

/**
 * Decode special chars (Metrahit 2x).
 *
 * @param[in] spc Special characters 1 and 2 (s1 | (s2 << 4)).
 */
static void decode_spc_2x(uint8_t spc, struct dev_context *devc)
{
	/* xxxxxxx1 Fuse */

	/* xxxxxx1x Low battery */

	/* xxxxx1xx BEEP */

	/* xxxx1xxx ZERO */

	/* xxx1xxxx DATA */
	setmqf(devc, SR_MQFLAG_HOLD, spc & 0x10);

	/* x11xxxxx unused */
	/* 1xxxxxxx MAN */
	setmqf(devc, SR_MQFLAG_AUTORANGE, !(spc & 0x80));
}

/** Clean range and sign. */
static void clean_rs_v(struct dev_context *devc)
{
	devc->value = 0.0;
	devc->scale = 1.0;
}

/** Clean current type, measured variable, range and sign. */
static void clean_ctmv_rs_v(struct dev_context *devc)
{
	devc->mq = 0;
	devc->unit = 0;
	devc->mqflags = 0;
	devc->scale1000 = 0;
	devc->vmains_29S = FALSE;
	clean_rs_v(devc);
}

/** Send prepared value. */
static void send_value(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_analog analog;
	struct sr_datafeed_packet packet;

	devc = sdi->priv;

	memset(&analog, 0, sizeof(analog));
	analog.probes = sdi->probes;
	analog.num_samples = 1;
	analog.mq = devc->mq;
	analog.unit = devc->unit;
	analog.mqflags = devc->mqflags;
	analog.data = &devc->value;

	memset(&packet, 0, sizeof(packet));
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	devc->num_samples++;
}

/** Process 6-byte data message, Metrahit 1x/2x. */
static void process_msg_dta_6(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int cnt;
	uint8_t dgt;

	devc = sdi->priv;
	clean_rs_v(devc);

	/* Byte 0, range and sign */
	if (devc->model <= SR_METRAHIT_16X)
		decode_rs_16(bc(devc->buf[0]), devc);
	else if (devc->model < SR_METRAHIT_2X)
		decode_rs_18(bc(devc->buf[0]), devc);
	else
		decode_rs_2x(bc(devc->buf[0]), devc);

	/* Bytes 1-5, digits (ls first). */
	for (cnt = 0; cnt < 5; cnt++) {
		dgt = bc(devc->buf[1 + cnt]);
		if (dgt >= 10) {
			/* 10 Overload; on model <= 16X also 11 possible. */
			devc->value = NAN;
			devc->scale = 1.0;
			break;
		}
		devc->value += pow(10.0, cnt) * dgt;
	}
	sr_spew("process_msg_dta_6() value=%f scale=%f scalet=%d",
		devc->value, devc->scale, devc->scale1000);
	if (devc->value != NAN)
		devc->value *= devc->scale * pow(1000.0, devc->scale1000);

	/* Create and send packet. */
	send_value(sdi);
}

/** Process 5-byte info message, Metrahit 1x/2x. */
static void process_msg_inf_5(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	enum model model;

	devc = sdi->priv;

	clean_ctmv_rs_v(devc);

	/* Process byte 0 */
	model = sr_gmc_decode_model_sm(bc(devc->buf[0]));
	if (model != devc->model) {
		sr_warn("Model mismatch in data: Detected %s, now %s",
			sr_gmc_model_str(devc->model),
			sr_gmc_model_str(model));
	}

	/* Process bytes 1-4 */
	if (devc->model <= SR_METRAHIT_16X) {
		decode_ctmv_16(bc(devc->buf[1]), devc);
		decode_spc_16(bc(devc->buf[2]) | (bc(devc->buf[3]) << 4), devc);
		decode_rs_16(bc(devc->buf[4]), devc);
	} else if (devc->model <= SR_METRAHIT_18S) {
		decode_ctmv_18(bc(devc->buf[1]), devc);
		decode_spc_18(bc(devc->buf[2]) | (bc(devc->buf[3]) << 4), devc);
		decode_rs_18(bc(devc->buf[4]), devc);
	} else { /* Must be Metrahit 2x */
		decode_ctmv_2x(bc(devc->buf[1]), devc);
		decode_spc_2x(bc(devc->buf[2]) | (bc(devc->buf[3]) << 4), devc);
		decode_rs_2x(bc(devc->buf[4]), devc);
	}
}

/** Process 10-byte info/data message, Metrahit 15+. */
static void process_msg_inf_10(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int cnt;
	uint8_t dgt;

	devc = sdi->priv;

	process_msg_inf_5(sdi);

	/* Now decode numbers */
	for (cnt = 0; cnt < 5; cnt++) {
		dgt = bc(devc->buf[5 + cnt]);
		if (dgt >= 10) { /* Overload */
			devc->value = NAN;
			devc->scale = 1.0;
			break;
		}
		devc->value += pow(10.0, cnt) * dgt;
	}
	sr_spew("process_msg_inf_10() value=%f scale=%f scalet=%d",
			devc->value, devc->scale,  devc->scale1000);

	if (devc->value != NAN)
		devc->value *= devc->scale * pow(1000.0, devc->scale1000);

	/* Create and send packet. */
	send_value(sdi);
}

/** Decode send interval (Metrahit 2x only). */
static const char *decode_send_interval(uint8_t si)
{
	switch (si) {
	case 0x00:
		return "0.05";
	case 0x01:
		return "0.1";
	case 0x02:
		return "0.2";
	case 0x03:
		return "0.5";
	case 0x04:
		return "00:01";
	case 0x05:
		return "00:02";
	case 0x06:
		return "00:05";
	case 0x07:
		return "00:10";
	case 0x08:
		return "00:20";
	case 0x09:
		return "00:30";
	case 0x0a:
		return "01:00";
	case 0x0b:
		return "02:00";
	case 0x0c:
		return "05:00";
	case 0x0d:
		return "10:00";
	case 0x0e:
		return "----";
	case 0x0f:
		return "data";
	default:
		return "Unknown value";
	}
}

/** Process 13-byte info/data message, Metrahit 2x. */
static void process_msg_inf_13(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	enum model model;
	int cnt;
	uint8_t dgt;

	devc = sdi->priv;

	clean_ctmv_rs_v(devc);

	/* Byte 0, model. */
	model = sr_gmc_decode_model_sm(bc(devc->buf[0]));
	if (model != devc->model) {
		sr_warn("Model mismatch in data: Detected %s, now %s",
			sr_gmc_model_str(devc->model),
			sr_gmc_model_str(model));
	}

	/* Bytes 1-4, 11. */
	decode_ctmv_2x(bc(devc->buf[1]) | (bc(devc->buf[11]) << 4), devc);
	decode_spc_2x(bc(devc->buf[2]) | (bc(devc->buf[3]) << 4), devc);
	decode_rs_2x(bc(devc->buf[4]), devc);

	/* Bytes 5-10, digits (ls first). */
	for (cnt = 0; cnt < 6; cnt++) {
		dgt = bc(devc->buf[5 + cnt]);
		if (dgt == 10) { /* Overload */
			devc->value = NAN;
			devc->scale = 1.0;
			break;
		}
		devc->value += pow(10.0, cnt) * dgt;
	}
	sr_spew("process_msg_inf_13() value=%f scale=%f scale1000=%d mq=%d "
		"unit=%d mqflags=0x%02llx", devc->value, devc->scale,
		devc->scale1000, devc->mq, devc->unit, devc->mqflags);
	if (devc->value != NAN)
		devc->value *= devc->scale * pow(1000.0, devc->scale1000);

	/* Byte 12, Send Interval */
	sr_spew("Send interval: %s", decode_send_interval(bc(devc->buf[12])));

	/* Create and send packet. */
	send_value(sdi);
}

SR_PRIV int gmc_mh_1x_2x_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t buf, msgt;
	int len;
	gdouble elapsed_s;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) { /* Serial data arrived. */
		while (GMC_BUFSIZE - devc->buflen - 1 > 0) {
			len = serial_read(serial, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;
			buf = *(devc->buf + devc->buflen);
			sr_spew("read 0x%02x/%d/%d", buf, buf, buf & MSGC_MASK);
			devc->buflen += len;
			if (!devc->settings_ok) {
				/*
				 * If no device type/settings record processed
				 * yet, wait for one.
				 */
				if ((devc->buf[0] & MSGID_MASK) != MSGID_INF) {
					devc->buflen = 0;
					continue;
				}
				devc->settings_ok = TRUE;
			}

			msgt = devc->buf[0] & MSGID_MASK;
			switch (msgt) {
			case MSGID_INF:
				if (devc->buflen == 13) {
					process_msg_inf_13(sdi);
					devc->buflen = 0;
					continue;
				} else if ((devc->buflen == 10) &&
					 (devc->model <= SR_METRAHIT_18S)) {
					process_msg_inf_10(sdi);
					devc->buflen = 0;
					continue;
				}
				else if ((devc->buflen >= 5) &&
					(devc->buf[devc->buflen - 1] &
					MSGID_MASK) != MSGID_DATA) {
					/*
					 * Char just received is beginning
					 * of next message.
					 */
					process_msg_inf_5(sdi);
					devc->buf[0] =
						devc->buf[devc->buflen - 1];
					devc->buflen = 1;
					continue;
				}
				break;
			case MSGID_DTA:
			case MSGID_D10:
				if (devc->buflen == 6) {
					process_msg_dta_6(sdi);
					devc->buflen = 0;
				}
				break;
			case MSGID_DATA:
				sr_err("Comm error, unexpected data byte!");
				devc->buflen = 0;
				break;
			}
		}
	}

	/* If number of samples or time limit reached, stop aquisition. */
	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples))
		sdi->driver->dev_acquisition_stop(sdi, cb_data);

	if (devc->limit_msec) {
		elapsed_s = g_timer_elapsed(devc->elapsed_msec, NULL);
		if ((elapsed_s * 1000) >= devc->limit_msec)
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	return TRUE;
}

/** Decode model in "send mode". */
SR_PRIV int sr_gmc_decode_model_sm(uint8_t mcode)
{
	if (mcode > 0xf) {
		sr_err("decode_model(%d): Model code 0..15 expected!", mcode);
		return SR_METRAHIT_NONE;
	}

	switch(mcode) {
	case 0x04: /* 0100b */
		return SR_METRAHIT_12S;
	case 0x08: /* 1000b */
		return SR_METRAHIT_13S14A;
	case 0x09: /* 1001b */
		return SR_METRAHIT_14S;
	case 0x0A: /* 1010b */
		return SR_METRAHIT_15S;
	case 0x0B: /* 1011b */
		return SR_METRAHIT_16S;
	case 0x06: /* 0110b (undocumented by GMC!) */
		return SR_METRAHIT_16I;
	case 0x0D: /* 1101b */
		return SR_METRAHIT_18S;
	case 0x02: /* 0010b */
		return SR_METRAHIT_22SM;
	case 0x03: /* 0011b */
		return SR_METRAHIT_23S;
	case 0x0f: /* 1111b */
		return SR_METRAHIT_24S;
	case 0x05: /* 0101b */
		return SR_METRAHIT_25SM;
	case 0x01: /* 0001b */
		return SR_METRAHIT_26S;
	case 0x0c: /* 1100b */
		return SR_METRAHIT_28S;
	case 0x0e: /* 1110b */
		return SR_METRAHIT_29S;
	default:
		sr_err("Unknown model code %d!", mcode);
		return SR_METRAHIT_NONE;
	}
}

/**
 * Decode model in bidirectional mode.
 *
 * @param[in] mcode Model code.
 *
 * @return Model code.
 */
SR_PRIV int sr_gmc_decode_model_bidi(uint8_t mcode)
{
	switch (mcode) {
	case 2:
		return SR_METRAHIT_22SM;
	case 3:
		return SR_METRAHIT_23S;
	case 4:
		return SR_METRAHIT_24S;
	case 5:
		return SR_METRAHIT_25SM;
	case 1:
		return SR_METRAHIT_26S;
	case 12:
		return SR_METRAHIT_28S;
	case 14:
		return SR_METRAHIT_29S;
	default:
		sr_err("Unknown model code %d!", mcode);
		return SR_METRAHIT_NONE;
	}
}

SR_PRIV const char *sr_gmc_model_str(enum model mcode)
{
	switch (mcode) {
	case SR_METRAHIT_NONE:
		return "-uninitialized model variable-";
	case SR_METRAHIT_12S:
		return "METRAHit 12S";
	case SR_METRAHIT_13S14A:
		return "METRAHit 13S/14A";
	case SR_METRAHIT_14S:
		return "METRAHit 14S";
	case SR_METRAHIT_15S:
		return "METRAHit 15S";
	case SR_METRAHIT_16S:
		return "METRAHit 16S";
	case SR_METRAHIT_16I:
		return "METRAHit 16I";
	case SR_METRAHIT_18S:
		return "METRAHit 18S";
	case SR_METRAHIT_22SM:
		return "METRAHit 22S/M";
	case SR_METRAHIT_23S:
		return "METRAHit 23S";
	case SR_METRAHIT_24S:
		return "METRAHit 24S";
	case SR_METRAHIT_25SM:
		return "METRAHit 25S/M";
	case SR_METRAHIT_26S:
		return "METRAHit 26S";
	case SR_METRAHIT_28S:
		return "METRAHit 28S";
	case SR_METRAHIT_29S:
		return "METRAHit 29S";
	default:
		return "Unknown model code";
	}
}
