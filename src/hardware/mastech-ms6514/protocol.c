/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Dave Buechi <db@pflutsch.ch>
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
#include <string.h>
#include <math.h>
#include "protocol.h"

static const uint8_t channel_assignment[16][2] = {
		 /* MAIN   AUX       */	
	{0, 1},  /* T1     T2        */
	{1, 0},  /* T2     T1        */
	{2, 0},  /* T1-T2  T1        */
	{2, 1},  /* T1-T2  T2        */
	{0, 0},  /* T1     T1 MAX    */
	{1, 1},  /* T2     T2 MAX    */
	{2, 2},  /* T1-T2  T1-T2 MAX */
	{2, 2},  /* T1-T2  T1-T2 MAX */
	{0, 0},  /* T1     T1 MIN    */
	{1, 1},  /* T2     T2 MIN    */
	{2, 2},  /* T1-T2  T1-T2 MIN */
	{2, 2},  /* T1-T2  T1-T2 MIN */
	{0, 0},  /* T1     T1 AVG    */
	{1, 1},  /* T2     T2 AVG    */
	{2, 2},  /* T1-T2  T1-T2 AVG */
	{2, 2},  /* T1-T2  T1-T2 AVG */
};

SR_PRIV gboolean mastech_ms6514_packet_valid(const uint8_t *buf)
{
	if ((buf[0]  == 0x65) && (buf[1]  == 0x14) && 
	    (buf[16] == 0x0D) && (buf[17] == 0x0A))
		return TRUE;

	return FALSE;
}

static uint64_t mastech_ms6514_flags(const uint8_t *buf, const uint8_t channel_index)
{
	uint64_t flags;

	flags = 0;
	if ((buf[10] & 0x40) == 0x40)
		flags |= SR_MQFLAG_HOLD;

	if (channel_index == 0) {
		if ((buf[11] & 0x03) > 0x01)
			flags |= SR_MQFLAG_RELATIVE;
	}

	if (channel_index == 1) {
		switch (buf[12] & 0x03) {
		case 0x01:
			flags |= SR_MQFLAG_MAX;
			break;
		case 0x02:
			flags |= SR_MQFLAG_MIN;
			break;
		case 0x03:
			flags |= SR_MQFLAG_AVG;
			break;
		}
	}

	return flags;
}

static enum sr_unit mastech_ms6514_unit(const uint8_t *buf)
{
	enum sr_unit unit;

	switch (buf[10] & 0x03) {
	case 0x01:
		unit = SR_UNIT_CELSIUS;
		break;
	case 0x02:
		unit = SR_UNIT_FAHRENHEIT;
		break;
	case 0x03:
		unit = SR_UNIT_KELVIN;
		break;
	default:
		unit = SR_UNIT_UNITLESS;
		break;
	}

	return unit;
}

static uint8_t mastech_ms6514_channel_assignment(const uint8_t *buf, const uint8_t index)
{
	return channel_assignment[((buf[12] & 0x03) << 2) + (buf[11] & 0x03)][index];
}

static uint8_t mastech_ms6514_data_source(const uint8_t *buf)
{
	if ((buf[2] & 0x01) == 0x01)
		return DATA_SOURCE_MEMORY;
	else
		return DATA_SOURCE_LIVE;
}

static float mastech_ms6514_temperature(const uint8_t *buf, const uint8_t channel_index, int *digits)
{
	float value;
	uint8_t modifiers;

	*digits = 0;
	value = (buf[5 + channel_index * 2] << 8) + buf[6 + channel_index * 2];
	modifiers = buf[11 + channel_index];

	if ((modifiers & 0x80) == 0x80)
		value = -value;

	if ((modifiers & 0x08) == 0x08) {
		value /= 10.0;
		*digits = 1;
	}

	if ((modifiers & 0x40) == 0x40)
		value = INFINITY;

	return value;
}

static void mastech_ms6514_data(struct sr_dev_inst *sdi, const uint8_t *buf)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_channel *ch;
	float value;
	int i, digits;

	devc = sdi->priv;

	if ((devc->data_source == DATA_SOURCE_MEMORY) && \
			(mastech_ms6514_data_source(buf) == DATA_SOURCE_LIVE)) {
		sr_dev_acquisition_stop(sdi);
		return;
	}

	for (i = 0; i < MASTECH_MS6514_NUM_CHANNELS; i++) {
		ch = g_slist_nth_data(sdi->channels, i);
		if (!ch->enabled)
			continue;

		value = mastech_ms6514_temperature(buf, i, &digits); 
		sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
		analog.num_samples = 1;
		analog.data = &value;
		analog.meaning->mq = SR_MQ_TEMPERATURE;
		analog.meaning->unit = mastech_ms6514_unit(buf);
		analog.meaning->mqflags = mastech_ms6514_flags(buf, i);
		
		analog.meaning->channels = g_slist_append(NULL,
			g_slist_nth_data(sdi->channels,
			mastech_ms6514_channel_assignment(buf, i)));

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		g_slist_free(analog.meaning->channels);
	}

	sr_sw_limits_update_samples_read(&devc->limits, 1);
}

static const uint8_t *mastech_ms6514_parse_data(struct sr_dev_inst *sdi,
		const uint8_t *buf, int len)
{
	if (len < MASTECH_MS6514_FRAME_SIZE)
		return NULL; /* Not enough data for a full packet. */

	if (buf[0] != 0x65 || buf[1] != 0x14)
		return buf + 1;	/* Try to re-synchronize on a packet start. */

	if (buf[16] != 0x0D || buf[17] != 0x0A)
		return buf + MASTECH_MS6514_FRAME_SIZE; /* Valid start but no valid end -> skip. */

	mastech_ms6514_data(sdi, buf);

	return buf + MASTECH_MS6514_FRAME_SIZE;
}

SR_PRIV int mastech_ms6514_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	const uint8_t *ptr, *next_ptr, *end_ptr;
	int len;

	(void)fd;

	if (!(sdi = cb_data) || !(devc = sdi->priv) || revents != G_IO_IN)
		return TRUE;
	serial = sdi->conn;

	/* Try to get as much data as the buffer can hold. */
	len = sizeof(devc->buf) - devc->buf_len;
	len = serial_read_nonblocking(serial, devc->buf + devc->buf_len, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return FALSE;
	}
	devc->buf_len += len;

	/* Now look for packets in that data. */
	ptr = devc->buf;
	end_ptr = ptr + devc->buf_len;
	while ((next_ptr = mastech_ms6514_parse_data(sdi, ptr, end_ptr - ptr)))
		ptr = next_ptr;

	/* If we have any data left, move it to the beginning of our buffer. */
	memmove(devc->buf, ptr, end_ptr - ptr);
	devc->buf_len -= ptr - devc->buf;

	/* If buffer is full and no valid packet was found, wipe buffer. */
	if (devc->buf_len >= sizeof(devc->buf)) {
		devc->buf_len = 0;
		return FALSE;
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	return TRUE;
}
