/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

/* Byte 1 of the packet, and the modes it represents */
#define IND1_HZ		0x80
#define IND1_OHM	0x40
#define IND1_KILO	0x20
#define IND1_MEGA	0x10
#define IND1_FARAD	0x08
#define IND1_AMP	0x04
#define IND1_VOLT	0x02
#define IND1_MILI	0x01
/* Byte 2 of the packet, and the modes it represents */
#define IND2_MICRO	0x80
#define IND2_NANO	0x40
#define IND2_DBM	0x20
#define IND2_SEC	0x10
#define IND2_DUTY	0x08
#define IND2_HFE	0x04
#define IND2_REL	0x02
#define IND2_MIN	0x01
/* Byte 7 of the packet, and the modes it represents */
#define INFO_BEEP	0x80
#define INFO_DIODE	0x30
#define INFO_BAT	0x20
#define INFO_HOLD	0x10
#define INFO_NEG	0x08
#define INFO_AC		0x04
#define INFO_RS232	0x02
#define INFO_AUTO	0x01
/* Instead of a decimal point, digit 4 carries the MAX flag */
#define DIG4_MAX	0x08
/* Mask to remove the decimal point from a digit */
#define DP_MASK		0x08

/* What the LCD values represent */
#define LCD_0		0xd7
#define LCD_1		0x50
#define LCD_2		0xb5
#define LCD_3		0xf1
#define LCD_4		0x72
#define LCD_5		0xe3
#define LCD_6		0xe7
#define LCD_7		0x51
#define LCD_8		0xf7
#define LCD_9		0xf3

#define LCD_C		0x87
#define LCD_E
#define LCD_F
#define LCD_h		0x66
#define LCD_H		0x76
#define LCD_I
#define LCD_n
#define LCD_P		0x37
#define LCD_r

enum {
	MODE_DC_V	= 0,
	MODE_AC_V	= 1,
	MODE_DC_UA	= 2,
	MODE_DC_MA	= 3,
	MODE_DC_A 	= 4,
	MODE_AC_UA	= 5,
	MODE_AC_MA	= 6,
	MODE_AC_A	= 7,
	MODE_OHM	= 8,
	MODE_FARAD	= 9,
	MODE_HZ		= 10,
	MODE_VOLT_HZ	= 11,
	MODE_AMP_HZ	= 12,
	MODE_DUTY	= 13,
	MODE_VOLT_DUTY	= 14,
	MODE_AMP_DUTY	= 15,
	MODE_WIDTH	= 16,
	MODE_VOLT_WIDTH	= 17,
	MODE_AMP_WIDTH	= 18,
	MODE_DIODE	= 19,
	MODE_CONT	= 20,
	MODE_HFE	= 21,
	MODE_LOGIC	= 22,
	MODE_DBM	= 23,
	// MODE_EF	= 24,
	MODE_TEMP	= 25,
	MODE_INVALID	= 26,
};

enum {
	READ_ALL,
	READ_TEMP,
};

static gboolean checksum_valid(const struct rs_22_812_packet *rs_packet)
{
	uint8_t *raw;
	uint8_t sum = 0;
	int i;

	raw = (void *)rs_packet;

	for (i = 0; i < RS_22_812_PACKET_SIZE - 1; i++)
		sum += raw[i];

	/* This is just a funky constant added to the checksum. */
	sum += 57;
	sum -= rs_packet->checksum;
	return (sum == 0);
}

static gboolean selection_good(const struct rs_22_812_packet *rs_packet)
{
	int count;

	/* Does the packet have more than one multiplier ? */
	count = 0;
	count += (rs_packet->indicatrix1 & IND1_KILO)  ? 1 : 0;
	count += (rs_packet->indicatrix1 & IND1_MEGA)  ? 1 : 0;
	count += (rs_packet->indicatrix1 & IND1_MILI)  ? 1 : 0;
	count += (rs_packet->indicatrix2 & IND2_MICRO) ? 1 : 0;
	count += (rs_packet->indicatrix2 & IND2_NANO)  ? 1 : 0;
	if (count > 1) {
		sr_err("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count = 0;
	count += (rs_packet->indicatrix1 & IND1_HZ)    ? 1 : 0;
	count += (rs_packet->indicatrix1 & IND1_OHM)   ? 1 : 0;
	count += (rs_packet->indicatrix1 & IND1_FARAD) ? 1 : 0;
	count += (rs_packet->indicatrix1 & IND1_AMP)   ? 1 : 0;
	count += (rs_packet->indicatrix1 & IND1_VOLT)  ? 1 : 0;
	count += (rs_packet->indicatrix2 & IND2_DBM)   ? 1 : 0;
	count += (rs_packet->indicatrix2 & IND2_SEC)   ? 1 : 0;
	count += (rs_packet->indicatrix2 & IND2_DUTY)  ? 1 : 0;
	count += (rs_packet->indicatrix2 & IND2_HFE)   ? 1 : 0;
	if (count > 1) {
		sr_err("More than one measurement type detected in packet.");
		return FALSE;
	}

	return TRUE;
}

/*
 * Since the 22-812 does not identify itself in any way, shape, or form,
 * we really don't know for sure who is sending the data. We must use every
 * possible check to filter out bad packets, especially since detection of the
 * 22-812 depends on how well we can filter the packets.
 */
SR_PRIV gboolean rs_22_812_packet_valid(const struct rs_22_812_packet *rs_packet)
{
	if (!checksum_valid(rs_packet))
		return FALSE;

	if (!(rs_packet->mode < MODE_INVALID))
		return FALSE;

	if (!selection_good(rs_packet))
		return FALSE;

	return TRUE;
}

static uint8_t decode_digit(uint8_t raw_digit)
{
	/* Take out the decimal point, so we can use a simple switch(). */
	raw_digit &= ~DP_MASK;

	switch (raw_digit) {
	case 0x00:
	case LCD_0:
		return 0;
	case LCD_1:
		return 1;
	case LCD_2:
		return 2;
	case LCD_3:
		return 3;
	case LCD_4:
		return 4;
	case LCD_5:
		return 5;
	case LCD_6:
		return 6;
	case LCD_7:
		return 7;
	case LCD_8:
		return 8;
	case LCD_9:
		return 9;
	default:
		sr_err("Invalid digit byte: 0x%02x.", raw_digit);
		return 0xff;
	}
}

static double lcd_to_double(const struct rs_22_812_packet *rs_packet, int type)
{
	double rawval, multiplier = 1;
	uint8_t digit, raw_digit;
	gboolean dp_reached = FALSE;
	int i, end;

	/* end = 1: Don't parse last digit. end = 0: Parse all digits. */
	end = (type == READ_TEMP) ? 1 : 0;

	/* We have 4 digits, and we start from the most significant. */
	for (i = 3; i >= end; i--) {
		raw_digit = *(&(rs_packet->digit4) + i);
		digit = decode_digit(raw_digit);
		if (digit == 0xff) {
			rawval = NAN;
			break;
		}
		/*
		 * Digit 1 does not have a decimal point. Instead, the decimal
		 * point is used to indicate MAX, so we must avoid testing it.
		 */
		if ((i < 3) && (raw_digit & DP_MASK))
			dp_reached = TRUE;
		if (dp_reached)
			multiplier /= 10;
		rawval = rawval * 10 + digit;
	}
	rawval *= multiplier;
	if (rs_packet->info & INFO_NEG)
		rawval *= -1;

	/* See if we need to multiply our raw value by anything. */
	if (rs_packet->indicatrix1 & IND2_NANO) {
		rawval *= 1E-9;
	} else if (rs_packet->indicatrix2 & IND2_MICRO) {
		rawval *= 1E-6;
	} else if (rs_packet->indicatrix1 & IND1_MILI) {
		rawval *= 1E-3;
	} else if (rs_packet->indicatrix1 & IND1_KILO) {
		rawval *= 1E3;
	} else if (rs_packet->indicatrix1 & IND1_MEGA) {
		rawval *= 1E6;
	}

	return rawval;
}

static gboolean is_celsius(struct rs_22_812_packet *rs_packet)
{
	return ((rs_packet->digit4 & ~DP_MASK) == LCD_C);
}

static gboolean is_shortcirc(struct rs_22_812_packet *rs_packet)
{
	return ((rs_packet->digit2 & ~DP_MASK) == LCD_h);
}

static gboolean is_logic_high(struct rs_22_812_packet *rs_packet)
{
	sr_spew("Digit 2: 0x%02x.", rs_packet->digit2 & ~DP_MASK);
	return ((rs_packet->digit2 & ~DP_MASK) == LCD_H);
}

static void handle_packet(struct rs_22_812_packet *rs_packet,
			  struct dev_context *devc)
{
	double rawval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;

	rawval = lcd_to_double(rs_packet, READ_ALL);

	/* TODO: Check malloc return value. */
	analog = g_try_malloc0(sizeof(struct sr_datafeed_analog));
	analog->num_samples = 1;
	/* TODO: Check malloc return value. */
	analog->data = g_try_malloc(sizeof(float));
	*analog->data = (float)rawval;
	analog->mq = -1;

	switch (rs_packet->mode) {
	case MODE_DC_V:
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_DC;
		break;
	case MODE_AC_V:
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_AC;
		break;
	case MODE_DC_UA:
	case MODE_DC_MA:
	case MODE_DC_A:
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
		analog->mqflags |= SR_MQFLAG_DC;
		break;
	case MODE_AC_UA:
	case MODE_AC_MA:
	case MODE_AC_A:
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
		analog->mqflags |= SR_MQFLAG_AC;
		break;
	case MODE_OHM:
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
		break;
	case MODE_FARAD:
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
		break;
	case MODE_CONT:
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*analog->data = is_shortcirc(rs_packet);
		break;
	case MODE_DIODE:
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
		break;
	case MODE_HZ:
	case MODE_VOLT_HZ:
	case MODE_AMP_HZ:
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
		break;
	case MODE_LOGIC:
		/*
		 * No matter whether or not we have an actual voltage reading,
		 * we are measuring voltage, so we set our MQ as VOLTAGE.
		 */
		analog->mq = SR_MQ_VOLTAGE;
		if (!isnan(rawval)) {
			/* We have an actual voltage. */
			analog->unit = SR_UNIT_VOLT;
		} else {
			/* We have either HI or LOW. */
			analog->unit = SR_UNIT_BOOLEAN;
			*analog->data = is_logic_high(rs_packet);
		}
		break;
	case MODE_HFE:
		analog->mq = SR_MQ_GAIN;
		analog->unit = SR_UNIT_UNITLESS;
		break;
	case MODE_DUTY:
	case MODE_VOLT_DUTY:
	case MODE_AMP_DUTY:
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
		break;
	case MODE_WIDTH:
	case MODE_VOLT_WIDTH:
	case MODE_AMP_WIDTH:
		analog->mq = SR_MQ_PULSE_WIDTH;
		analog->unit = SR_UNIT_SECOND;
	case MODE_TEMP:
		analog->mq = SR_MQ_TEMPERATURE;
		/* We need to reparse. */
		*analog->data = lcd_to_double(rs_packet, READ_TEMP);
		analog->unit = is_celsius(rs_packet) ?
		               SR_UNIT_CELSIUS : SR_UNIT_FAHRENHEIT;
		break;
	case MODE_DBM:
		analog->mq = SR_MQ_POWER;
		analog->unit = SR_UNIT_DECIBEL_MW;
		analog->mqflags |= SR_MQFLAG_AC;
		break;
	default:
		sr_err("Unknown mode: %d.", rs_packet->mode);
		break;
	}

	if (rs_packet->info & INFO_HOLD)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (rs_packet->digit4 & DIG4_MAX)
		analog->mqflags |= SR_MQFLAG_MAX;
	if (rs_packet->indicatrix2 & IND2_MIN)
		analog->mqflags |= SR_MQFLAG_MIN;
	if (rs_packet->info & INFO_AUTO)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;

	if (analog->mq != -1) {
		/* Got a measurement. */
		sr_spew("Value: %f.", rawval);
		packet.type = SR_DF_ANALOG;
		packet.payload = analog;
		sr_session_send(devc->cb_data, &packet);
		devc->num_samples++;
	}
	g_free(analog->data);
	g_free(analog);
}

static void handle_new_data(struct dev_context *devc, int fd)
{
	int len;
	size_t i, offset = 0;
	struct rs_22_812_packet *rs_packet;

	/* Try to get as much data as the buffer can hold. */
	len = RS_DMM_BUFSIZE - devc->buflen;
	len = serial_read(fd, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error.");
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= RS_22_812_PACKET_SIZE) {
		rs_packet = (void *)(devc->buf + offset);
		if (rs_22_812_packet_valid(rs_packet)) {
			handle_packet(rs_packet, devc);
			offset += RS_22_812_PACKET_SIZE;
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	for (i = 0; i < devc->buflen - offset; i++)
		devc->buf[i] = devc->buf[offset + i];
	devc->buflen -= offset;
}

SR_PRIV int radioshack_dmm_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		handle_new_data(devc, fd);
	}

	if (devc->num_samples >= devc->limit_samples) {
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	return TRUE;
}
