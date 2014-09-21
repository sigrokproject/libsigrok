/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

/* States */
enum {
	SEND_INIT,
	GET_INIT_REPLY,
	SEND_PACKET_REQUEST,
	GET_PACKET,
};

static void parse_packet(const uint8_t *buf, float *floatval,
			 struct sr_datafeed_analog *analog)
{
	gboolean is_a, is_fast;
	uint16_t intval;
	uint8_t level = 0, level_bits;

	/* Byte 0 [7:7]: 0 = A, 1 = C */
	is_a = ((buf[0] & (1 << 7)) == 0);

	/* Byte 0 [6:6]: Unknown/unused? */

	/* Byte 0 [5:4]: Level (00 = 40, 01 = 60, 10 = 80, 11 = 100) */
	level_bits = (buf[0] >> 4) & 0x03;
	if (level_bits == 0)
		level = 40;
	else if (level_bits == 1)
		level = 60;
	else if (level_bits == 2)
		level = 80;
	else if (level_bits == 3)
		level = 100;

	/* Byte 0 [3:3]: 0 = fast, 1 = slow */
	is_fast = ((buf[0] & (1 << 3)) == 0);

	/* Byte 0 [2:0]: value[10..8] */
	/* Byte 1 [7:0]: value[7..0] */
	intval = (buf[0] & 0x7) << 8;
	intval |= buf[1];

	*floatval = (float)intval;

	/* The value on the display always has one digit after the comma. */
	*floatval /= 10;

	analog->mq = SR_MQ_SOUND_PRESSURE_LEVEL;
	analog->unit = SR_UNIT_DECIBEL_SPL;

	if (is_a)
		analog->mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_A;
	else
		analog->mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_C;

	if (is_fast)
		analog->mqflags |= SR_MQFLAG_SPL_TIME_WEIGHT_F;
	else
		analog->mqflags |= SR_MQFLAG_SPL_TIME_WEIGHT_S;

	/* TODO: How to handle level? */
	(void)level;
}

static void decode_packet(struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct dev_context *devc;
	float floatval;

	devc = sdi->priv;
	memset(&analog, 0, sizeof(struct sr_datafeed_analog));

	parse_packet(devc->buf, &floatval, &analog);

	/* Send a sample packet with one analog value. */
	analog.channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &floatval;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	devc->num_samples++;
}

int tondaj_sl_814_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t buf[3];
	int ret;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	serial = sdi->conn;
	devc = sdi->priv;

	/* TODO: Parts of this code need to be improved later. */

	/* State machine. */
	if (devc->state == SEND_INIT) {
		/* On the first run, send the "init" command. */
		buf[0] = 0x10;
		buf[1] = 0x04;
		buf[2] = 0x0d;
		sr_spew("Sending init command: %02x %02x %02x.",
			buf[0], buf[1], buf[2]);
		if ((ret = serial_write_blocking(serial, buf, 3)) < 0) {
			sr_err("Error sending init command: %d.", ret);
			return FALSE;
		}
		devc->state = GET_INIT_REPLY;
	} else if (devc->state == GET_INIT_REPLY) {
		/* If we just sent the "init" command, get its reply. */
		if ((ret = serial_read_blocking(serial, buf, 2)) < 0) {
			sr_err("Error reading init reply: %d.", ret);
			return FALSE;
		}
		sr_spew("Received init reply: %02x %02x.", buf[0], buf[1]);
		/* Expected reply: 0x05 0x0d */
		if (buf[0] != 0x05 || buf[1] != 0x0d) {
			sr_err("Received incorrect init reply, retrying.");
			devc->state = SEND_INIT;
			return TRUE;
		}
		devc->state = SEND_PACKET_REQUEST;
	} else if (devc->state == SEND_PACKET_REQUEST) {
		/* Request a packet (send 0x30 ZZ 0x0d). */
		buf[0] = 0x30;
		buf[1] = 0x00; /* ZZ */
		buf[2] = 0x0d;
		sr_spew("Sending data request command: %02x %02x %02x.",
			buf[0], buf[1], buf[2]);
		if ((ret = serial_write_blocking(serial, buf, 3)) < 0) {
			sr_err("Error sending request command: %d.", ret);
			return FALSE;
		}
		devc->buflen = 0;
		devc->state = GET_PACKET;
	} else if (devc->state == GET_PACKET) {
		/* Read a packet from the device. */
		ret = serial_read_nonblocking(serial, devc->buf + devc->buflen,
				  4 - devc->buflen);
		if (ret < 0) {
			sr_err("Error reading packet: %d.", ret);
			return TRUE;
		}

		devc->buflen += ret;

		/* Didn't receive all 4 bytes, yet. */
		if (devc->buflen != 4)
			return TRUE;

		sr_spew("Received packet: %02x %02x %02x %02x.", devc->buf[0],
			devc->buf[1], devc->buf[2], devc->buf[3]);

		/* Expected reply: AA BB ZZ+1 0x0d */
		if (devc->buf[2] != 0x01 || devc->buf[3] != 0x0d) {
			sr_err("Received incorrect request reply, retrying.");
			devc->state = SEND_PACKET_REQUEST;
			return TRUE;
		}

		decode_packet(sdi);

		devc->state = SEND_PACKET_REQUEST;
	} else {
		sr_err("Invalid state: %d.", devc->state);
		return FALSE;
	}

	/* Stop acquisition if we acquired enough samples. */
	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	return TRUE;
}
