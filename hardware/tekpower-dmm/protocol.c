/*
 * This file is part of the sigrok project.
 *
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
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static gboolean lcd14_is_sync_valid(const struct lcd14_packet *packet)
{
	int i;
	uint8_t sync;

	/* Check the syncronization nibbles, and make sure they all match. */
	for (i = 0; i < LCD14_PACKET_SIZE; i++) {
		sync = (packet->raw[i] & LCD14_SYNC_MASK) >> 4;
		if (sync != (i + 1))
			return FALSE;
	}
	return TRUE;
}

static gboolean lcd14_is_selection_good(const struct lcd14_data *data)
{
	int n_postfix = 0, n_type = 0;

	/* Does the packet have more than one multiplier? */
	if (data->flags & LCD14_NANO)
		n_postfix++;
	if (data->flags & LCD14_MICRO)
		n_postfix++;
	if (data->flags & LCD14_MILLI)
		n_postfix++;
	if (data->flags & LCD14_KILO)
		n_postfix++;
	if (data->flags & LCD14_MEGA)
		n_postfix++;

	if (n_postfix > 1)
		return FALSE;

	/* Does the packet "measure" more than one type of value? */
	if (data->flags & LCD14_HZ)
		n_type++;
	if (data->flags & LCD14_OHM)
		n_type++;
	if (data->flags & LCD14_FARAD)
		n_type++;
	if (data->flags & LCD14_AMP)
		n_type++;
	if (data->flags & LCD14_VOLT)
		n_type++;
	if (data->flags & LCD14_DUTY)
		n_type++;
	if (data->flags & LCD14_CELSIUS)
		n_type++;
	/* Do not test for hFE. hFE is not implemented and always '1'. */
	if (n_type > 1)
		return FALSE;

	/* Both AC and DC? */
	if ((data->flags & LCD14_AC) && (data->flags & LCD14_DC))
		return FALSE;

	/* OK, no duplicates. */
	return TRUE;
}

/* We "cook" a raw lcd14_pcaket into a more pallatable form, lcd14_data. */
static void lcd14_cook_raw(const struct lcd14_packet *packet,
			   struct lcd14_data *data)
{
	int i, j;

	/* Get the digits out. */
	for (i = 0; i < 4; i++) {
		j = (i << 1) + 1;
		data->digit[i] = ((packet->raw[j] & ~LCD14_SYNC_MASK) << 4) |
				 ((packet->raw[j + 1] & ~LCD14_SYNC_MASK));
	}

	/* Now extract the flags. */
	data->flags = ((packet->raw[0]  & ~LCD14_SYNC_MASK) << 20) |
		      ((packet->raw[9]  & ~LCD14_SYNC_MASK) << 16) |
		      ((packet->raw[10] & ~LCD14_SYNC_MASK) << 12) |
		      ((packet->raw[11] & ~LCD14_SYNC_MASK) << 8) |
		      ((packet->raw[12] & ~LCD14_SYNC_MASK) << 4) |
		      ((packet->raw[13] & ~LCD14_SYNC_MASK));
}

/*
 * Since the DMM does not identify itself in any way shape, or form, we really
 * don't know for sure who is sending the data. We must use every possible
 * check to filter out bad packets, especially since the detection mechanism
 * depends on how well we can filter out bad packets packets.
 */
SR_PRIV gboolean lcd14_is_packet_valid(const struct lcd14_packet *packet,
				       struct lcd14_data *data)
{
	struct lcd14_data placeholder;

	/* Callers not interested in the data, pass NULL. */
	if (data == NULL)
		data = &placeholder;

	if (!lcd14_is_sync_valid(packet))
		return FALSE;

	lcd14_cook_raw(packet, data);

	if (!lcd14_is_selection_good(data))
		return FALSE;

	/* If we made it here, this looks to be a valid packet. */
	return TRUE;
}

static uint8_t lcd14_to_digit(uint8_t raw_digit)
{
	/* Take out the decimal point, so we can use a simple switch(). */
	raw_digit &= ~LCD14_DP_MASK;

	switch (raw_digit) {
	case 0x00:
	case LCD14_LCD_0:
		return 0;
	case LCD14_LCD_1:
		return 1;
	case LCD14_LCD_2:
		return 2;
	case LCD14_LCD_3:
		return 3;
	case LCD14_LCD_4:
		return 4;
	case LCD14_LCD_5:
		return 5;
	case LCD14_LCD_6:
		return 6;
	case LCD14_LCD_7:
		return 7;
	case LCD14_LCD_8:
		return 8;
	case LCD14_LCD_9:
		return 9;
	default:
		return LCD14_LCD_INVALID;
	}
}

/* Get a raw floating point value from the data. */
static double lcdraw_to_double(struct lcd14_data *data)
{
	double rawval;
	double multiplier = 1;
	uint8_t digit, raw_digit;
	gboolean dp_reached = FALSE;
	int i;

	/* We have 4 digits, and we start from the most significant. */
	for (i = 0; i < 4; i++) {
		raw_digit = data->digit[i];
		digit = lcd14_to_digit(raw_digit);
		if (digit == LCD14_LCD_INVALID) {
			rawval = NAN;
			break;
		}

		/*
		 * Digit 1 does not have a decimal point. Instead, the decimal
		 * point is used to indicate MAX, so we must avoid testing it.
		 */
		if ((i > 0) && (raw_digit & LCD14_DP_MASK))
			dp_reached = TRUE;
		if (dp_reached)
			multiplier /= 10;
		rawval = rawval * 10 + digit;
	}
	rawval *= multiplier;
	if (data->digit[0] & LCD14_D0_NEG)
		rawval *= -1;

	/* See if we need to multiply our raw value by anything. */
	if (data->flags & LCD14_NANO)
		rawval *= 1E-9;
	else if (data->flags & LCD14_MICRO)
		rawval *= 1E-6;
	else if (data->flags & LCD14_MILLI)
		rawval *= 1E-3;
	else if (data->flags & LCD14_KILO)
		rawval *= 1E3;
	else if (data->flags & LCD14_MEGA)
		rawval *= 1E6;

	return rawval;
}

/* Now see what the value means, and pass that on. */
static void lcd14_handle_packet(struct lcd14_data *data,
				struct dev_context *devc)
{
	double rawval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;

	if (!(analog = g_try_malloc0(sizeof(struct sr_datafeed_analog)))) {
		sr_err("Failed to malloc packet.");
		return;
	}

	if (!(analog->data = g_try_malloc(sizeof(float)))) {
		sr_err("Failed to malloc data.");
		g_free(analog);
		return;
	}

	rawval = lcdraw_to_double(data);

	analog->num_samples = 1;
	*analog->data = (float)rawval;

	analog->mq = -1;

	/* What does the data mean? */
	if (data->flags & LCD14_VOLT) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		if (data->flags & LCD14_AC)
			analog->mqflags |= SR_MQFLAG_AC;
		else
			analog->mqflags |= SR_MQFLAG_DC;
	} else if (data->flags & LCD14_AMP) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
		if (data->flags & LCD14_AC)
			analog->mqflags |= SR_MQFLAG_AC;
		else
			analog->mqflags |= SR_MQFLAG_DC;
	} else if (data->flags & LCD14_OHM) {
		if (data->flags & LCD14_BEEP)
			analog->mq = SR_MQ_CONTINUITY;
		else
			analog->mq = SR_MQ_RESISTANCE;
		if (!isnan(rawval))
			analog->unit = SR_UNIT_OHM;
		else {
			analog->unit = SR_UNIT_BOOLEAN;
			*analog->data = FALSE;
		}
	} else if (data->flags & LCD14_FARAD) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	} else if (data->flags & LCD14_CELSIUS) {
		analog->mq = SR_MQ_TEMPERATURE;
		/* No Kelvin or Fahrenheit from the device, just Celsius. */
		analog->unit = SR_UNIT_CELSIUS;
	} else if (data->flags & LCD14_HZ) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	} else if (data->flags & LCD14_DUTY) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	} else if (data->flags & LCD14_HFE) {
		analog->mq = SR_MQ_GAIN;
		analog->unit = SR_UNIT_UNITLESS;
	} else if (data->flags & LCD14_DIODE) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		analog->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
	} else {
		sr_warn("Unable to identify measurement mode.");
	}

	/* What other flags are associated with the data? */
	if (data->flags & LCD14_HOLD)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (data->flags & LCD14_AUTO)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (data->flags & LCD14_REL)
		analog->mqflags |= SR_MQFLAG_RELATIVE;

	if (analog->mq != -1) {
		/* Got a measurement. */
		sr_spew("Measurement value is %f.", rawval);
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
	int len, i, offset = 0;
	struct lcd14_packet *packet;
	struct lcd14_data data;

	/* Try to get as much data as the buffer can hold. */
	len = DMM_BUFSIZE - devc->buflen;
	len = serial_read(fd, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= LCD14_PACKET_SIZE) {
		packet = (void *)(devc->buf + offset);
		if (lcd14_is_packet_valid(packet, &data)) {
			lcd14_handle_packet(&data, devc);
			offset += LCD14_PACKET_SIZE;
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	for (i = 0; i < devc->buflen - offset; i++)
		devc->buf[i] = devc->buf[offset + i];
	devc->buflen -= offset;
}

SR_PRIV int tekpower_dmm_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
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
