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

#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "radioshack-dmm.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>

static gboolean rs_22_812_is_checksum_valid(const rs_22_812_packet *data)
{
	uint8_t *raw = (void *) data;
	uint8_t sum = 0;
	size_t i;
	for(i = 0; i < RS_22_812_PACKET_SIZE - 1; i++)
		sum += raw[i];
	/* This is just a funky constant added to the checksum */
	sum += 57;
	sum -= data->checksum;
	return(sum == 0);
}

static gboolean rs_22_812_is_mode_valid(rs_22_812_mode mode)
{
	return(mode < RS_22_812_MODE_INVALID);
}

static gboolean rs_22_812_is_selection_good(const rs_22_812_packet *data)
{
	int n_postfix = 0;
	int n_type = 0;
	/* Does the packet have more than one multiplier ? */
	if(data->indicatrix1 & RS_22_812_IND1_KILO)
		n_postfix++;
	if(data->indicatrix1 & RS_22_812_IND1_MEGA)
		n_postfix++;
	if(data->indicatrix1 & RS_22_812_IND1_MILI)
		n_postfix++;
	if(data->indicatrix2 & RS_22_812_IND2_MICRO)
		n_postfix++;
	if(data->indicatrix2 & RS_22_812_IND2_NANO)
		n_postfix++;
	if(n_postfix > 1)
		return FALSE;

	/* Does the packet "measure" more than one type of value ?*/
	if(data->indicatrix1 & RS_22_812_IND1_HZ)
		n_type++;
	if(data->indicatrix1 & RS_22_812_IND1_OHM)
		n_type++;
	if(data->indicatrix1 & RS_22_812_IND1_FARAD)
		n_type++;
	if(data->indicatrix1 & RS_22_812_IND1_AMP)
		n_type++;
	if(data->indicatrix1 & RS_22_812_IND1_VOLT)
		n_type++;
	if(data->indicatrix2 & RS_22_812_IND2_DBM)
		n_type++;
	if(data->indicatrix2 & RS_22_812_IND2_SEC)
		n_type++;
	if(data->indicatrix2 & RS_22_812_IND2_DUTY)
		n_type++;
	if(data->indicatrix2 & RS_22_812_IND2_HFE)
		n_type++;
	if(n_type > 1)
		return FALSE;

	/* OK, no duplicates */
	return TRUE;
}

/* Since the RS 22-812 does not identify itslef in any way shape, or form,
 * we really don't know for sure who is sending the data. We must use every
 * possible check to filter out bad packets, especially since detection of the
 * 22-812 depends on how well we can filter the packets */
SR_PRIV gboolean rs_22_812_is_packet_valid(const rs_22_812_packet *packet)
{
	/* Unfortunately, the packet doesn't have a signature, so we must
	 * compute its checksum first */
	if(!rs_22_812_is_checksum_valid(packet))
		return FALSE;

	if(!rs_22_812_is_mode_valid(packet->mode))
		return FALSE;

	if(!rs_22_812_is_selection_good(packet)) {
		return FALSE;
	}
	/* Made it here, huh? Then this looks to be a valid packet */
	return TRUE;
}

static uint8_t rs_22_812_to_digit(uint8_t raw_digit)
{
	/* Take out the decimal point, so we can use a simple switch() */
	raw_digit &= ~RS_22_812_DP_MASK;
	switch(raw_digit)
	{
	case 0x00:
	case RS_22_812_LCD_0:
		return 0;
	case RS_22_812_LCD_1:
		return 1;
	case RS_22_812_LCD_2:
		return 2;
	case RS_22_812_LCD_3:
		return 3;
	case RS_22_812_LCD_4:
		return 4;
	case RS_22_812_LCD_5:
		return 5;
	case RS_22_812_LCD_6:
		return 6;
	case RS_22_812_LCD_7:
		return 7;
	case RS_22_812_LCD_8:
		return 8;
	case RS_22_812_LCD_9:
		return 9;
	default:
		return 0xff;
	}
}

typedef enum {
	READ_ALL,
	READ_TEMP,
} value_type;

static double lcdraw_to_double(rs_22_812_packet *rs_packet, value_type type)
{
	/* *********************************************************************
	 * Get a raw floating point value from the data
	 **********************************************************************/
	double rawval;
	double multiplier = 1;
	uint8_t digit;
	gboolean dp_reached = FALSE;
	int i, end;
	switch(type) {
	case READ_TEMP:
		/* Do not parse the last digit */
		end = 1;
		break;
	case READ_ALL:
	default:
		/* Parse all digits */
		end = 0;
	}
	/* We have 4 digits, and we start from the most significant */
	for(i = 3; i >= end; i--)
	{
		uint8_t raw_digit = *(&(rs_packet->digit4) + i);
		digit = rs_22_812_to_digit(raw_digit);
		if(digit == 0xff) {
			rawval = NAN;
			break;
		}
		/* Digit 1 does not have a decimal point. Instead, the decimal
		 * point is used to indicate MAX, so we must avoid testing it */
		if( (i < 3) && (raw_digit & RS_22_812_DP_MASK) )
			dp_reached = TRUE;
		if(dp_reached) multiplier /= 10;
		rawval = rawval * 10 + digit;
	}
	rawval *= multiplier;
	if(rs_packet->info & RS_22_812_INFO_NEG)
		rawval *= -1;

	/* See if we need to multiply our raw value by anything */
	if(rs_packet->indicatrix1 & RS_22_812_IND2_NANO) {
		rawval *= 1E-9;
	} else if(rs_packet->indicatrix2 & RS_22_812_IND2_MICRO) {
		rawval *= 1E-6;
	} else if(rs_packet->indicatrix1 & RS_22_812_IND1_MILI) {
		rawval *= 1E-3;
	} else if(rs_packet->indicatrix1 & RS_22_812_IND1_KILO) {
		rawval *= 1E3;
	} else if(rs_packet->indicatrix1 & RS_22_812_IND1_MEGA) {
		rawval *= 1E6;
	}

	return rawval;
}

static gboolean rs_22_812_is_celsius(rs_22_812_packet *rs_packet)
{
	return((rs_packet->digit4 & ~RS_22_812_DP_MASK) == RS_22_812_LCD_C);
}

static gboolean rs_22_812_is_shortcirc(rs_22_812_packet *rs_packet)
{
	return((rs_packet->digit2 & ~RS_22_812_DP_MASK) == RS_22_812_LCD_h);
}

static gboolean rs_22_812_is_logic_high(rs_22_812_packet *rs_packet)
{
	sr_spew("digit 2: %x", rs_packet->digit2 & ~RS_22_812_DP_MASK);
	return((rs_packet->digit2 & ~RS_22_812_DP_MASK) == RS_22_812_LCD_H);
}

static void rs_22_812_handle_packet(rs_22_812_packet *rs_packet,
				    rs_dev_ctx *devc)
{
	double rawval = lcdraw_to_double(rs_packet, READ_ALL);
	/* *********************************************************************
	 * Now see what the value means, and pass that on
	 **********************************************************************/
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;

	/* TODO: Check malloc return value. */
	analog = g_try_malloc0(sizeof(struct sr_datafeed_analog));
	analog->num_samples = 1;
	/* TODO: Check malloc return value. */
	analog->data = g_try_malloc(sizeof(float));
	*analog->data = (float)rawval;
	analog->mq = -1;

	switch(rs_packet->mode) {
	case RS_22_812_MODE_DC_V:
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_DC;
		break;
	case RS_22_812_MODE_AC_V:
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_AC;
		break;
	case RS_22_812_MODE_DC_UA:
	case RS_22_812_MODE_DC_MA:
	case RS_22_812_MODE_DC_A:
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
		analog->mqflags |= SR_MQFLAG_DC;
		break;
	case RS_22_812_MODE_AC_UA:
	case RS_22_812_MODE_AC_MA:
	case RS_22_812_MODE_AC_A:
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
		analog->mqflags |= SR_MQFLAG_AC;
		break;
	case RS_22_812_MODE_OHM:
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
		break;
	case RS_22_812_MODE_FARAD:
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
		break;
	case RS_22_812_MODE_CONT:
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*analog->data = rs_22_812_is_shortcirc(rs_packet);
		break;
	case RS_22_812_MODE_DIODE:
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
		break;
	case RS_22_812_MODE_HZ:
	case RS_22_812_MODE_VOLT_HZ:
	case RS_22_812_MODE_AMP_HZ:
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
		break;
	case RS_22_812_MODE_LOGIC:
		/* No matter whether or not we have an actual voltage reading,
		 * we are measuring voltage, so we set our MQ as VOLTAGE */
		analog->mq = SR_MQ_VOLTAGE;
		if(!isnan(rawval)) {
			/* We have an actual voltage */
			analog->unit = SR_UNIT_VOLT;
		} else {
			/* We have either HI or LOW */
			analog->unit = SR_UNIT_BOOLEAN;
			*analog->data = rs_22_812_is_logic_high(rs_packet);
		}
		break;
	case RS_22_812_MODE_HFE:
		analog->mq = SR_MQ_GAIN;
		analog->unit = SR_UNIT_UNITLESS;
		break;
	case RS_22_812_MODE_DUTY:
	case RS_22_812_MODE_VOLT_DUTY:
	case RS_22_812_MODE_AMP_DUTY:
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
		break;
	case RS_22_812_MODE_WIDTH:
	case RS_22_812_MODE_VOLT_WIDTH:
	case RS_22_812_MODE_AMP_WIDTH:
		analog->mq = SR_MQ_PULSE_WIDTH;
		analog->unit = SR_UNIT_SECOND;
	case RS_22_812_MODE_TEMP:
		analog->mq = SR_MQ_TEMPERATURE;
		/* We need to reparse */
		*analog->data = lcdraw_to_double(rs_packet, READ_TEMP);
		analog->unit = rs_22_812_is_celsius(rs_packet)?
				SR_UNIT_CELSIUS:SR_UNIT_FAHRENHEIT;
		break;
	case RS_22_812_MODE_DBM:
		analog->mq = SR_MQ_POWER;
		analog->unit = SR_UNIT_DECIBEL_MW;
		analog->mqflags |= SR_MQFLAG_AC;
		break;
	default:
		sr_warn("Unknown mode: %d.", rs_packet->mode);
		break;
	}

	if(rs_packet->info & RS_22_812_INFO_HOLD) {
		analog->mqflags |= SR_MQFLAG_HOLD;
	}
	if(rs_packet->digit4 & RS_22_812_DIG4_MAX) {
		analog->mqflags |= SR_MQFLAG_MAX;
	}
	if(rs_packet->indicatrix2 & RS_22_812_IND2_MIN) {
		analog->mqflags |= SR_MQFLAG_MIN;
	}
	if(rs_packet->info & RS_22_812_INFO_AUTO) {
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	}

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

static void handle_new_data(rs_dev_ctx *devc, int fd)
{
	int len;
	size_t i;
	size_t offset = 0;
	/* Try to get as much data as the buffer can hold */
	len = RS_DMM_BUFSIZE - devc->buflen;
	len = serial_read(fd, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error!");
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data */
	while((devc->buflen - offset) >= RS_22_812_PACKET_SIZE)
	{
		rs_22_812_packet * packet = (void *)(devc->buf + offset);
		if( rs_22_812_is_packet_valid(packet) )
		{
			rs_22_812_handle_packet(packet, devc);
			offset += RS_22_812_PACKET_SIZE;
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer */
	for(i = 0; i < devc->buflen - offset; i++)
		devc->buf[i] = devc->buf[offset + i];
	devc->buflen -= offset;
}

SR_PRIV int radioshack_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN)
	{
		/* Serial data arrived. */
		handle_new_data(devc, fd);
	}

	if (devc->num_samples >= devc->limit_samples) {
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	return TRUE;
}
