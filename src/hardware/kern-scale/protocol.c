/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <config.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static void handle_packet(const uint8_t *buf, struct sr_dev_inst *sdi,
			  void *info)
{
	struct scale_info *scale;
	float floatval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;

	scale = (struct scale_info *)sdi->driver;

	devc = sdi->priv;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.meaning->mq = 0;

	scale->packet_parse(buf, &floatval, &analog, info);
	analog.data = &floatval;

	if (analog.meaning->mq != 0) {
		/* Got a measurement. */
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}
}

static void handle_new_data(struct sr_dev_inst *sdi, void *info)
{
	struct scale_info *scale;
	struct dev_context *devc;
	int len, i, offset = 0;
	struct sr_serial_dev_inst *serial;

	scale = (struct scale_info *)sdi->driver;

	devc = sdi->priv;
	serial = sdi->conn;

	/* Try to get as much data as the buffer can hold. */
	len = SCALE_BUFSIZE - devc->buflen;
	len = serial_read_nonblocking(serial, devc->buf + devc->buflen, len);
	if (len == 0)
		return; /* No new bytes, nothing to do. */
	if (len < 0) {
		sr_err("Serial port read error: %d.", len);
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= scale->packet_size) {
		if (scale->packet_valid(devc->buf + offset)) {
			handle_packet(devc->buf + offset, sdi, info);
			offset += scale->packet_size;
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	for (i = 0; i < devc->buflen - offset; i++)
		devc->buf[i] = devc->buf[offset + i];
	devc->buflen -= offset;
}

SR_PRIV int kern_scale_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct scale_info *scale;
	void *info;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	scale = (struct scale_info *)sdi->driver;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		info = g_malloc(scale->info_size);
		handle_new_data(sdi, info);
		g_free(info);
	}

	if (sr_sw_limits_check(&devc->limits))
		sdi->driver->dev_acquisition_stop(sdi);

	return TRUE;
}
