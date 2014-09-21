/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013, 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

/** @file
 *  Gossen Metrawatt Metrahit 1x/2x drivers
 *  @internal
 */

#include <math.h>
#include <string.h>
#include "protocol.h"

/* Internal Headers */
static guchar calc_chksum_14(guchar* dta);
static int chk_msg14(struct sr_dev_inst *sdi);

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
			if (devc->model >= METRAHIT_16S)
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
		if (devc->model == METRAHIT_16S)
			devc->mqflags |= SR_MQFLAG_RMS;
		/* 16I A only with clamp, RMS questionable. */
		break;
	}
}

/**
 * Decode range/sign/acdc byte special chars (Metrahit 12-16).
 *
 * @param[in] rs Range and sign byte.
 */
static void decode_rs_16(uint8_t rs, struct dev_context *devc)
{
	sr_spew("decode_rs_16(%d) scale = %f", rs, devc->scale);

	if (rs & 0x04) /* Sign */
		devc->scale *= -1.0;

	if (devc->mq == SR_MQ_CURRENT) {
		if (rs & 0x08) /* Current is AC */
			devc->mqflags |= SR_MQFLAG_AC;
		else
			devc->mqflags |= SR_MQFLAG_DC;
	}

	switch (rs & 0x03) {
	case 0:
		if (devc->mq == SR_MQ_VOLTAGE) /* V */
			devc->scale *= 0.1;
		else if (devc->mq == SR_MQ_CURRENT) /* 000.0 µA */
			devc->scale *= 0.00001;
		else if (devc->mq == SR_MQ_RESISTANCE) {
			if (devc->buflen >= 10) {
				/* °C with 10 byte msg type, otherwise GOhm. */
				devc->mq = SR_MQ_TEMPERATURE;
				devc->unit = SR_UNIT_CELSIUS;
				devc->scale *= 0.01;
			} else if (devc->scale1000 == 2) {
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
		devc->scale *= pow(10.0, range - 2);
		break;
	case SR_MQ_TEMPERATURE:
		devc->scale *= pow(10.0, range - 2);
		break;
	case SR_MQ_CAPACITANCE:
		devc->scale *= pow(10.0, range - 13);
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
	if ((ctmv > 0x20) || (!devc)) {
		sr_err("decode_ctmv_2x(0x%x): invalid param(s)!", ctmv);
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
			if (devc->model >= METRAHIT_24S)
				devc->mqflags |= SR_MQFLAG_RMS;
		}
		break;
	case 0x04: /* 00100 mA DC */
	case 0x05: /* 00101 mA AC+DC */
		devc->scale1000 = -1;
		/* Fall through! */
	case 0x06: /* 00110 A DC */
	case 0x07: /* 00111 A AC+DC */
		devc->mq = SR_MQ_CURRENT;
		devc->unit = SR_UNIT_AMPERE;
		devc->mqflags |= SR_MQFLAG_DC;
		if ((ctmv == 0x05) || (ctmv == 0x07)) {
			devc->mqflags |= SR_MQFLAG_AC;
			if (devc->model >= METRAHIT_24S)
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
	case 0x0a: /* 01010 V dB */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_DECIBEL_VOLT;
		devc->mqflags |= SR_MQFLAG_AC;
		if (devc->model >= METRAHIT_24S)
			devc->mqflags |= SR_MQFLAG_RMS;
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
		devc->scale *= 0.1;
		/* Fall through! */
	case 0x0e: /* 01110 W on power, A range (29S only) */
		devc->scale *= 0.1;
		devc->scale1000 = -1;
		devc->mq = SR_MQ_POWER;
		devc->unit = SR_UNIT_WATT;
		devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS);
		break;
	case 0x0f: /* 01111 Diode */
	case 0x10: /* 10000 Diode with buzzer (actually cont. with voltage) */
		devc->unit = SR_UNIT_VOLT;
		if (ctmv == 0x0f) {
			devc->mq = SR_MQ_VOLTAGE;
			devc->mqflags |= SR_MQFLAG_DIODE;
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
	/* 0x13 10011 unused */
	/* 0x14 10100 unused */
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
		devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS);
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
	case 0x1b: /* 11011 Milliamperes in power mode (29S only); error in docs, "pulse on mains" */
		devc->mq = SR_MQ_CURRENT;
		devc->unit = SR_UNIT_AMPERE;
		devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS);
		devc->scale1000 = -1;
		break;
	case 0x1c: /* 11100 dropout on mains (29S only) */
		/* TODO: No unit or flags for this yet! */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_UNITLESS;
		devc->mqflags |= SR_MQFLAG_AC;
		break;
	case 0x1d: /* 11101 Voltage in power mode (29S); undocumented! */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		devc->mqflags |= (SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS);
		break;
	/* 0x1e: 11110 Undocumented */
	case 0x1f: /* 11111 25S in stopwatch mode; undocumented!
			The value is voltage, not time, so treat it such. */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_VOLT;
		devc->mqflags |= SR_MQFLAG_DC;
		break;
	case 0x20: /* 100000 25S in event count mode; undocumented!
			Value is 0 anyway. */
		devc->mq = SR_MQ_VOLTAGE;
		devc->unit = SR_UNIT_UNITLESS;
		break;
	default:
		sr_err("decode_ctmv_2x(%d, ...): Unknown ctmv!", ctmv);
		break;
	}
}

/**
 * Decode range/sign/acdc byte special chars, Metrahit 2x, table TR.
 *
 * @param[in] rs Range/sign byte.
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
		else
			devc->scale *= pow(10.0, range - 6);
		break;
	case SR_MQ_CURRENT:
		if (devc->scale1000 != -1) /* uA, mA */
			range += 1;/* mA and A ranges differ by 10^4, not 10^3!*/
		devc->scale *= pow(10.0, range - 6);
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
		if (range == 7)
			range -= 1; /* Same value as range 6 */
		devc->scale *= pow(10.0, range - 13);
		break;
	/* TODO: 29S Mains measurements. */
	}
}

/**
 * Decode range/sign/acdc byte special chars, Metrahit 2x, table TR 2.
 *
 * @param[in] rs Range/sign byte.
 */
static void decode_rs_2x_TR2(uint8_t rs, struct dev_context *devc)
{
	int range;

	/* Range */
	range = rs & 0x07;
	switch (devc->mq) {
	case SR_MQ_CURRENT:
		if (devc->scale1000 == -1) /* mA */
			switch(range) {
			case 0: case 1:	/* 100, 300 µA */
				devc->scale *= pow(10.0, -6);
				break;
			case 2: case 3:	/* 1, 3 mA */
				devc->scale *= pow(10.0, -5);
				break;
			case 4: case 5:	/* 10, 30 mA */
				devc->scale *= pow(10.0, -4);
				break;
			case 6: case 7:	/* 100, 300 mA */
				devc->scale *= pow(10.0, -3);
				break;
			}
		else /* A */
			switch(range) {
			case 0: case 1:	/* 1, 3 A */
				devc->scale *= pow(10.0, -5);
				break;
			case 2: /* 10 A */
				devc->scale *= pow(10.0, -4);
				break;
			}
		break;
	default:
		decode_rs_2x(rs, devc);
		return;
	}

	/* Sign */
	if (((devc->scale > 0) && (rs & 0x08)) ||
			((devc->scale < 0) && !(rs & 0x08)))
		devc->scale *= -1.0;
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
	analog.channels = sdi->channels;
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

/** Process 6-byte data message, Metrahit 1x/2x send mode. */
static void process_msg_dta_6(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int cnt;
	uint8_t dgt;

	devc = sdi->priv;
	clean_rs_v(devc);

	/* Byte 0, range and sign */
	if (devc->model <= METRAHIT_16X)
		decode_rs_16(bc(devc->buf[0]), devc);
	else if (devc->model < METRAHIT_2X)
		decode_rs_18(bc(devc->buf[0]), devc);
	else {
		decode_rs_2x(bc(devc->buf[0]), devc);
		devc->scale *= 10; /* Compensate for format having only 5 digits, decode_rs_2x() assumes 6. */
	}

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

	sr_spew("process_msg_dta_6() value=%f scale=%f scale1000=%d",
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
	model = gmc_decode_model_sm(bc(devc->buf[0]));
	if (model != devc->model) {
		sr_warn("Model mismatch in data: Detected %s, now %s",
			gmc_model_str(devc->model), gmc_model_str(model));
	}

	/* Process bytes 1-4 */
	if (devc->model <= METRAHIT_16X) {
		decode_ctmv_16(bc(devc->buf[1]), devc);
		decode_spc_16(bc(devc->buf[2]) | (bc(devc->buf[3]) << 4), devc);
		decode_rs_16(bc(devc->buf[4]), devc);
	} else if (devc->model <= METRAHIT_18S) {
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
		if (dgt == 11) { /* Empty digit */
			dgt = 0;
		}
		else if (dgt >= 12) { /* Overload */
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
	model = gmc_decode_model_sm(bc(devc->buf[0]));
	if (model != devc->model) {
		sr_warn("Model mismatch in data: Detected %s, now %s",
			gmc_model_str(devc->model), gmc_model_str(model));
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

/** Dump contents of 14-byte message.
 *  @param buf Pointer to array of 14 data bytes.
 *  @param[in] raw Write only data bytes, no interpretation.
 */
void dump_msg14(guchar* buf, gboolean raw)
{
	if (!buf)
		return;

	if (raw)
		sr_spew("msg14: 0x %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x %02x",
			buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
				buf[7], buf[8], buf[9], buf[10], buf[11], buf[12],
				buf[13]);
	else
		sr_spew("msg14: 0x a=%d c1=%02x c2=%02x cmd=%02x dta=%02x "
			"%02x %02x %02x %02x %02x %02x %02x %02x chs=%02x",
			buf[1] == 0x2b?buf[0] >> 2:buf[0] % 0x0f, buf[1], buf[2], buf[3], buf[4], buf[5],
				buf[6],	buf[7], buf[8], buf[9], buf[10], buf[11],
				buf[12], buf[13]);
}

/** Calc checksum for 14 byte message type.
 *
 *  @param[in] dta Pointer to array of 13 data bytes.
 *  @return Checksum.
 */
static guchar calc_chksum_14(guchar* dta)
{
	guchar cnt, chs;

	for (chs = 0, cnt = 0; cnt < 13; cnt++)
		chs += dta[cnt];

	return (64 - chs) & MASK_6BITS;
}

/** Check 14-byte message, Metrahit 2x. */
static int chk_msg14(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int retc;
	gboolean isreq; /* Message is request to multimeter (otherwise response) */
	uint8_t addr;  /* Adaptor address */

	retc = SR_OK;

	/* Check parameters and message */
	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	if (devc->buflen != 14) {
		sr_err("process_msg_14(): Msg len 14 expected!");
		return SR_ERR_ARG;
	}

	isreq = devc->buf[1] == 0x2b;
	if (isreq)
		addr = devc->buf[0] >> 2;
	else
		addr = devc->buf[0] & 0x0f;

	if ((devc->addr != addr) && !(isreq && (addr == 0))) {
		sr_err("process_msg_14(): Address mismatch, msg for other device!");
		retc = SR_ERR_ARG;
	}

	if (devc->buf[1] == 0) { /* Error msg from device! */
		retc = SR_ERR_ARG;
		switch (devc->buf[2]) {
		case 1: /* Not used */
			sr_err("Device: Illegal error code!");
			break;
		case 2: /* Incorrect check sum of received block */
			sr_err("Device: Incorrect checksum in cmd!");
			break;
		case 3: /* Incorrect length of received block */
			sr_err("Device: Incorrect block length in cmd!");
			break;
		case 4: /* Incorrect 2nd or 3rd byte */
			sr_err("Device: Incorrect byte 2 or 3 in cmd!");
			break;
		case 5: /* Parameter out of range */
			sr_err("Device: Parameter out of range!");
			break;
		default:
			sr_err("Device: Unknown error code!");
		}
		retc = SR_ERR_ARG;
	}
	else if (!isreq && ((devc->buf[1] != 0x27) || (devc->buf[2] != 0x3f))) {
		sr_err("process_msg_14(): byte 1/2 unexpected!");
		retc = SR_ERR_ARG;
	}

	if (calc_chksum_14(devc->buf) != devc->buf[13]) {
		sr_err("process_msg_14(): Invalid checksum!");
		retc = SR_ERR_ARG;
	}

	if (retc != SR_OK)
		dump_msg14(devc->buf, TRUE);

	return retc;
}

/** Check 14-byte message, Metrahit 2x. */
SR_PRIV int process_msg14(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int retc;
	uint8_t addr;
	uint8_t cnt, dgt;

	if ((retc = chk_msg14(sdi)) != SR_OK)
		return retc;

	devc = sdi->priv;

	clean_ctmv_rs_v(devc);
	addr = devc->buf[0] & MASK_6BITS;
	if (addr != devc->addr)
		sr_info("Device address mismatch %d/%d!", addr, devc->addr);

	switch (devc->buf[3]) { /* That's the command this reply is for */
	/* 0 cannot occur, the respective message is not a 14-byte message */
	case 1: /* Read first free and occupied address */
		sr_spew("Cmd %d unimplemented!", devc->buf[3]);
		break;
	case 2: /* Clear all RAM in multimeter */
		sr_spew("Cmd %d unimplemented!", devc->buf[3]);
		break;
	case 3: /* Read firmware version and status */
		sr_spew("Cmd 3, Read firmware and status", devc->buf[3]);
		switch (devc->cmd_idx) {
		case 0:
			devc->fw_ver_maj = devc->buf[5];
			devc->fw_ver_min = devc->buf[4];
			sr_spew("Firmware version %d.%d", (int)devc->fw_ver_maj, (int)devc->fw_ver_min);
			sr_spew("Rotary Switch Position (1..10): %d", (int)devc->buf[6]);
			/** Docs say values 0..9, but that's not true */
			sr_spew("Measurement Function: %d ", (int)devc->buf[7]);
			decode_ctmv_2x(devc->buf[7], devc);
			sr_spew("Range: 0x%x", devc->buf[8]);
			decode_rs_2x_TR2(devc->buf[8] & 0x0f, devc);  /* Docs wrong, uses conversion table TR_2! */
			devc->autorng = (devc->buf[8] & 0x20) == 0;
			// TODO 9, 10: 29S special functions
			devc->ubatt = 0.1 * (float)devc->buf[11];
			devc->model = gmc_decode_model_bd(devc->buf[12]);
			sr_spew("Model=%s, battery voltage=%2.1f V", gmc_model_str(devc->model), (double)devc->ubatt);
			break;
		case 1:
			sr_spew("Internal version %d.%d", (int)devc->buf[5], (int)devc->buf[4]);
			sr_spew("Comm mode: 0x%x", (int)devc->buf[6]);
			sr_spew("Block cnt%%64: %d", (int)devc->buf[7]);
			sr_spew("drpCi: %d  drpCh: %d", (int)devc->buf[8], (int)devc->buf[9]);
			// Semantics undocumented. Possibly Metrahit 29S dropouts stuff?
			break;
		default:
			sr_spew("Cmd 3: Unknown cmd_idx=%d", devc->cmd_idx);
			break;
		}
		break;
	case 4: /* Set real time, date, sample rate, trigger, ... */
		sr_spew("Cmd %d unimplemented!", devc->buf[3]);
		break;
	case 5: /* Read real time, date, sample rate, trigger... */
		sr_spew("Cmd %d unimplemented!", devc->buf[3]);
		break;
	case 6: /* Set modes or power off */
		sr_spew("Cmd %d unimplemented!", devc->buf[3]);
		break;
	case 7: /* Set measurement function, range, autom/man. */
		sr_spew("Cmd %d unimplemented!", devc->buf[3]);
		break;
	case 8: /* Get one measurement value */
		sr_spew("Cmd 8, get one measurement value");
		sr_spew("Measurement Function: %d ", (int)devc->buf[5]);
		decode_ctmv_2x(devc->buf[5], devc);
		if (!(devc->buf[6] & 0x10)) /* If bit4=0, old data. */
			return SR_OK;

		decode_rs_2x_TR2(devc->buf[6] & 0x0f, devc); // The docs say conversion table TR_3, but that does not work
		setmqf(devc, SR_MQFLAG_AUTORANGE, devc->autorng);
		/* 6 digits */
		for (cnt = 0; cnt < 6; cnt++) {
			dgt = bc(devc->buf[7 + cnt]);
			if (dgt == 10) { /* Overload */
				devc->value = NAN;
				devc->scale = 1.0;
				break;
			}
			else if (dgt == 13) { /* FUSE */
				sr_err("FUSE!");
			}
			else if (dgt == 14) { /* Function recognition mode, OPEN */
				sr_info("Function recognition mode, OPEN!");
				devc->value = NAN;
				devc->scale = 1.0;
				break;
			}
			devc->value += pow(10.0, cnt) * dgt;
		}
		sr_spew("process_msg14() value=%f scale=%f scale1000=%d mq=%d "
			"unit=%d mqflags=0x%02llx", devc->value, devc->scale,
			devc->scale1000, devc->mq, devc->unit, devc->mqflags);
		if (devc->value != NAN)
			devc->value *= devc->scale * pow(1000.0, devc->scale1000);

		send_value(sdi);

		break;
	default:
		sr_spew("Unknown cmd %d!", devc->buf[3]);
		break;
	}

	return SR_OK;
}

/** Data reception callback function. */
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
			len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
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
					   (devc->model <= METRAHIT_18S)) {
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

	/* If number of samples or time limit reached, stop acquisition. */
	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples))
		sdi->driver->dev_acquisition_stop(sdi, cb_data);

	if (devc->limit_msec) {
		elapsed_s = g_timer_elapsed(devc->elapsed_msec, NULL);
		if ((elapsed_s * 1000) >= devc->limit_msec)
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	return TRUE;
}

SR_PRIV int gmc_mh_2x_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t buf;
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
			len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;
			buf = *(devc->buf + devc->buflen);
			sr_spew("read 0x%02x/%d/%d", buf, buf, buf & MASK_6BITS);
			devc->buf[devc->buflen] &= MASK_6BITS;
			devc->buflen += len;

			if (devc->buflen == 14) {
				devc->response_pending = FALSE;
				sr_spew("gmc_mh_2x_receive_data processing msg");
				process_msg14(sdi);
				devc->buflen = 0;
			}
		}
	}

	/* If number of samples or time limit reached, stop acquisition. */
	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples))
		sdi->driver->dev_acquisition_stop(sdi, cb_data);

	if (devc->limit_msec) {
		elapsed_s = g_timer_elapsed(devc->elapsed_msec, NULL);
		if ((elapsed_s * 1000) >= devc->limit_msec)
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	/* Request next data set, if required */
	if (sdi->status == SR_ST_ACTIVE) {
		if (devc->response_pending) {
			gint64 elapsed_us = g_get_monotonic_time() - devc->req_sent_at;
			if (elapsed_us > 1*1000*1000) /* Timeout! */
				devc->response_pending = FALSE;
		}
		if (!devc->response_pending) {
			devc->cmd_seq++;
			if (devc->cmd_seq % 10 == 0) {
				if (req_stat14(sdi, FALSE) != SR_OK)
					return FALSE;
			}
			else if (req_meas14(sdi) != SR_OK)
				return FALSE;
		}
	}

	return TRUE;
}

/** Create 14 (42) byte command for Metrahit 2x multimeter in bidir mode.
 *
 *  Actually creates 42 bytes due to the encoding method used.
 *  @param[in] addr Device address (0=adapter, 1..15 multimeter; for byte 0).
 *  @param[in] func Function code (byte 3).
 *  @param[in] params Further parameters (9 bytes)
 *  @param[out] buf Buffer to create msg in (42 bytes).
 */
void create_cmd_14(guchar addr, guchar func, guchar* params, guchar* buf)
{
	uint8_t dta[14];	/* Unencoded message */
	int cnt;

	if (!params || !buf)
		return;

	/* 0: Address */
	dta[0] = ((addr << 2) | 0x03) & MASK_6BITS;

	/* 1-3: Set command header */
	dta[1] = 0x2b;
	dta[2] = 0x3f;
	dta[3] = func;

	/* 4-12: Copy further parameters */
	for (cnt = 0; cnt < 9; cnt++)
		dta[cnt+4] = (params[cnt] & MASK_6BITS);

	/* 13: Checksum (b complement) */
	dta[13] = calc_chksum_14(dta);

	/* The whole message is packed into 3 bytes per byte now (lower 6 bits only) the most
	 * peculiar way I have ever seen. Possibly to improve IR communication? */
	for (cnt = 0; cnt < 14; cnt++) {
		buf[3*cnt] = (dta[cnt] & 0x01 ? 0x0f : 0) | (dta[cnt] & 0x02 ? 0xf0 : 0);
		buf[3*cnt + 1] = (dta[cnt] & 0x04 ? 0x0f : 0) | (dta[cnt] & 0x08 ? 0xf0 : 0);
		buf[3*cnt + 2] = (dta[cnt] & 0x10 ? 0x0f : 0) | (dta[cnt] & 0x20 ? 0xf0 : 0);
	}
}

/** Request one measurement from 2x multimeter (msg 8).
 *
 */
int req_meas14(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t params[9];
	uint8_t msg[42];

	if (!sdi || !(devc = sdi->priv) || !(serial = sdi->conn))
		return SR_ERR;

	memset(params, 0, sizeof(params));
	params[0] = 0;
	devc->cmd_idx = 0;
	create_cmd_14(devc->addr, 8, params, msg);
	devc->req_sent_at = g_get_monotonic_time();
	if (serial_write(serial, msg, sizeof(msg)) == -1) {
		return SR_ERR;
	}

	devc->response_pending = TRUE;

	return SR_OK;
}

/** Request status from 2x multimeter (msg 3).
 *  @param[in] power_on Try to power on powered off multimeter by sending additional messages.
 */
int req_stat14(const struct sr_dev_inst *sdi, gboolean power_on)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t params[9];
	uint8_t msg[42];

	if (!sdi || !(devc = sdi->priv) || !(serial = sdi->conn))
		return SR_ERR;

	memset(params, 0, sizeof(params));
	params[0] = 0;
	devc->cmd_idx = 0;
	create_cmd_14(devc->addr, 3, params, msg);

	if (power_on) {
		sr_info("Write some data and wait 3s to turn on powered off device...");
		if (serial_write(serial, msg, sizeof(msg)) < 0)
			return SR_ERR;
		g_usleep(1*1000*1000);
		if (serial_write(serial, msg, sizeof(msg)) < 0)
			return SR_ERR;
		g_usleep(1*1000*1000);
		if (serial_write(serial, msg, sizeof(msg)) < 0)
			return SR_ERR;
		g_usleep(1*1000*1000);
		serial_flush(serial);
	}

	/* Write message and wait for reply */
	devc->req_sent_at = g_get_monotonic_time();
	if (serial_write(serial, msg, sizeof(msg)) == -1) {
		return SR_ERR;
	}

	devc->response_pending = TRUE;

	return SR_OK;
}

/** Decode model in "send mode".
 *
 * @param[in] mcode Model code.
 * @return Model code.
 */
SR_PRIV int gmc_decode_model_sm(uint8_t mcode)
{
	if (mcode > 0xf) {
		sr_err("decode_model(%d): Model code 0..15 expected!", mcode);
		return METRAHIT_NONE;
	}

	switch(mcode) {
	case 0x04: /* 0100b */
		return METRAHIT_12S;
	case 0x08: /* 1000b */
		return METRAHIT_13S14A;
	case 0x09: /* 1001b */
		return METRAHIT_14S;
	case 0x0A: /* 1010b */
		return METRAHIT_15S;
	case 0x0B: /* 1011b */
		return METRAHIT_16S;
	case 0x06: /* 0110b (undocumented by GMC!) */
		return METRAHIT_16I;
	case 0x07: /* 0111b (undocumented by GMC!) */
		return METRAHIT_16T;
	case 0x0D: /* 1101b */
		return METRAHIT_18S;
	case 0x02: /* 0010b */
		return METRAHIT_22SM;
	case 0x03: /* 0011b */
		return METRAHIT_23S;
	case 0x0F: /* 1111b */
		return METRAHIT_24S;
	case 0x05: /* 0101b */
		return METRAHIT_25S;
	case 0x01: /* 0001b */
		return METRAHIT_26SM;
	case 0x0C: /* 1100b */
		return METRAHIT_28S;
	case 0x0E: /* 1110b */
		return METRAHIT_29S;
	default:
		sr_err("Unknown model code %d!", mcode);
		return METRAHIT_NONE;
	}
}

/** Convert GMC model code in bidirectional mode to sigrok-internal one.
 *
 *  @param[in] mcode Model code.
 *
 *  @return Model code.
 */
SR_PRIV int gmc_decode_model_bd(uint8_t mcode)
{
	switch (mcode & 0x1f) {
	case 2:
		if (mcode & 0x20)
			return METRAHIT_22M;
		else
			return METRAHIT_22S;
	case 3:
		return METRAHIT_23S;
	case 4:
		return METRAHIT_24S;
	case 5:
		return METRAHIT_25S;
	case 1:
		if (mcode & 0x20)
			return METRAHIT_26M;
		else
			return METRAHIT_26S;
	case 12:
		return METRAHIT_28S;
	case 14:
		return METRAHIT_29S;
	default:
		sr_err("Unknown model code %d!", mcode);
		return METRAHIT_NONE;
	}
}

/** Convert sigrok-internal model code to string.
 *
 *  @param[in] mcode Model code.
 *
 *  @return Model code string.
 */
SR_PRIV const char *gmc_model_str(enum model mcode)
{
	switch (mcode) {
	case METRAHIT_NONE:
		return "-uninitialized model variable-";
	case METRAHIT_12S:
		return "METRAHit 12S";
	case METRAHIT_13S14A:
		return "METRAHit 13S/14A";
	case METRAHIT_14S:
		return "METRAHit 14S";
	case METRAHIT_15S:
		return "METRAHit 15S";
	case METRAHIT_16S:
		return "METRAHit 16S";
	case METRAHIT_16I:
		return "METRAHit 16I/16L";
	case METRAHIT_16T:
		return "METRAHit 16T/16U/KMM2002";
	case METRAHIT_18S:
		return "METRAHit 18S";
	case METRAHIT_22SM:
		return "METRAHit 22S/M";
	case METRAHIT_22S:
		return "METRAHit 22S";
	case METRAHIT_22M:
		return "METRAHit 22M";
	case METRAHIT_23S:
		return "METRAHit 23S";
	case METRAHIT_24S:
		return "METRAHit 24S";
	case METRAHIT_25S:
		return "METRAHit 25S";
	case METRAHIT_26SM:
		return "METRAHit 26S/M";
	case METRAHIT_26S:
		return "METRAHit 26S";
	case METRAHIT_26M:
		return "METRAHit 26M";
	case METRAHIT_28S:
		return "METRAHit 28S";
	case METRAHIT_29S:
		return "METRAHit 29S";
	default:
		return "Unknown model code";
	}
}


/** @copydoc sr_dev_driver.config_set
 */
SR_PRIV int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint8_t params[9];
	uint8_t msg[42];

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (key) {
	case SR_CONF_POWER_OFF:
		if (devc->model < METRAHIT_2X)
			return SR_ERR_NA;
		if (!g_variant_get_boolean(data))
			return SR_ERR;
		sr_info("Powering device off.");

		memset(params, 0, sizeof(params));
		params[0] = 5;
		params[1] = 5;
		create_cmd_14(devc->addr, 6, params, msg);
		if (serial_write(sdi->conn, msg, sizeof(msg)) == -1)
			return SR_ERR;
		else
			g_usleep(2000000); /* Wait to ensure transfer before interface switched off. */
		break;
	case SR_CONF_LIMIT_MSEC:
		if (g_variant_get_uint64(data) == 0) {
			sr_err("LIMIT_MSEC can't be 0.");
			return SR_ERR;
		}
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
			devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
			devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}
