/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
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

/**
 * @file
 *
 * qm1578.c
 *
 * Digitech QM1576 serial protocol parser for libsigrok.
 * QM1576 is a 600 count RMS DMM, with Bluetooth 4.0 support.
 * https://www.jaycar.com.au/true-rms-digital-multimeter-with-bluetooth-connectivity/p/QM1578
 *
 * The protocol is described at https://www.airspayce.com/mikem/QM1578/protocol.txt
 *
 * You can use this decoder with libsigrok and Digitech QM1578 via ESP32 Bluetooth-Serial converter
 * available from the author at:
 * https://www.airspayce.com/mikem/QM1578/QM1578BluetoothClient.ino
 * which connects to the QM1578 over Bluetooth LE, fetches the
 * data stream and sends it on the serial port to the host, where this driver can read it
 * with this command for example:
 * sigrok-cli --driver digitech-qm1578:conn=/dev/ttyUSB1 --continuous
 *
 * See https://www.airspayce.com/mikem/QM1578//README for more data
 *
 * Author: mikem@airspayce.com
 */

#include <config.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "digitech-qm1578"

/* Number of digits the meter supports */
#define MAX_DIGITS 4

/* Decode the multiplier prefix from buf[11] */
static int decode_prefix(const uint8_t *buf)
{
	switch (buf[11]) {
	case 0x00: return 0;
	case 0x01: return 3;
	case 0x02: return 6;
	case 0x03: return -9;
	case 0x04: return -6;
	case 0x05: return -3; /* For amps */
	case 0x06: return -3; /* For volts */
	default:
		sr_dbg("Unknown multiplier: 0x%02x.", buf[11]);
		return -1;
	}
}

static float decode_value(const uint8_t *buf, int *exponent)
{
	float val = 0.0f;
	int i, digit;

	/* On overload, digits 4 to 1 are: 0x0b 0x0a 0x00 0x0b */
	if (buf[8] == 0x0b)
		return INFINITY;

	/* Decode the 4 digits */
	for (i = 0; i < MAX_DIGITS; i++) {
		digit = buf[8 - i];
		if (digit < 0 || digit > 9)
			continue;
		val = 10.0 * val + digit;
	}

	*exponent = -buf[9];
	return val;
}

SR_PRIV gboolean sr_digitech_qm1578_packet_valid(const uint8_t *buf)
{
	/*
	 * First 4 digits on my meter are always 0d5 0xf0 0x00 0x0a
	 * Dont know if thats the same for all meters or just mine, so ignore them
	 * and just use the presence of the trailing record separator
	 */
	if (buf[14] != 0x0d)
		return FALSE;

	return TRUE;
}

SR_PRIV int sr_digitech_qm1578_parse(const uint8_t *buf, float *floatval,
				     struct sr_datafeed_analog *analog, void *info)
{
	int exponent = 0;
	float val;

	(void)info;

	/* serial-dmm will dump the contents of packet by using -l 4 */

	/* Defaults */
	analog->meaning->mq = SR_MQ_GAIN;
	analog->meaning->unit = SR_UNIT_UNITLESS;
	analog->meaning->mqflags = 0;

	/* Decode sone flags */
	if (buf[13] & 0x10)
		analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
	if (buf[13] & 0x40)
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	if (buf[13] & 0x80)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (buf[13] & 0x20)
		analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;
	if (buf[12] & 0x40)
		analog->meaning->mqflags |= SR_MQFLAG_HOLD;
	if ((buf[13] & 0x0c) == 0x0c)
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
	if ((buf[13] & 0x0c) == 0x08)
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
	if ((buf[13] & 0x0c) == 0x0c)
		analog->meaning->mqflags |= SR_MQFLAG_AVG;

	/* Decode the meter setting. Caution: there may be others on other meters: hFE? */
	if (buf[4] == 0x01) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mqflags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
	}
	if (buf[4] == 0x02) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	}
	/* what is 03 ? */
	if (buf[4] == 0x04) {
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (buf[4] == 0x05) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (buf[4] == 0x06) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		if (buf[10] == 0x08)
			analog->meaning->unit = SR_UNIT_CELSIUS;
		else
			analog->meaning->unit = SR_UNIT_FAHRENHEIT;
	}
	if (buf[4] == 0x07 || buf[4] == 0x08 ||buf[4] == 0x09) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	}
	/* 0x0a ? 0x0b? */
	if (buf[4] == 0x0c || buf[4] == 0x0d || buf[4] == 0x0e) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
		analog->meaning->mqflags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
	}
	if (buf[4] == 0x0f) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mqflags |= SR_MQFLAG_DIODE;
	}
	if (buf[4] == 0x10) {
		if (buf[10] == 0x04) {
			analog->meaning->mq = SR_MQ_FREQUENCY;
			analog->meaning->unit = SR_UNIT_HERTZ;
		}
		else {
			analog->meaning->mq = SR_MQ_DUTY_CYCLE;
			analog->meaning->unit = SR_UNIT_PERCENTAGE;
		}
	}
	if (buf[4] == 0x20) {
		analog->meaning->mq = SR_MQ_CONTINUITY;
		analog->meaning->unit = SR_UNIT_OHM;
	}


	val = decode_value(buf, &exponent);
	exponent += decode_prefix(buf);
	val *= powf(10, exponent);

	if (buf[12] & 0x80)
		val = -val;

	*floatval = val;
	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;

}
