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

/* User-defined FS9721_LP3 flag 'c2c1_10' means temperature on this DMM. */
#define is_temperature info.is_c2c1_10

/* Now see what the value means, and pass that on. */
static void fs9721_serial_handle_packet(const uint8_t *buf,
					struct dev_context *devc)
{
	float floatval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;
	struct fs9721_info info;

	if (!(analog = g_try_malloc0(sizeof(struct sr_datafeed_analog)))) {
		sr_err("Analog packet malloc failed.");
		return;
	}

	if (!(analog->data = g_try_malloc(sizeof(float)))) {
		sr_err("Analog value malloc failed.");
		g_free(analog);
		return;
	}

	analog->num_samples = 1;
	analog->mq = -1;

	sr_fs9721_parse(buf, &floatval, analog, &info);
	*analog->data = floatval;

	if (is_temperature) {
		analog->mq = SR_MQ_TEMPERATURE;
		/* No Kelvin or Fahrenheit from the device, just Celsius. */
		analog->unit = SR_UNIT_CELSIUS;
	}

	if (analog->mq != -1) {
		/* Got a measurement. */
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

	/* Try to get as much data as the buffer can hold. */
	len = DMM_BUFSIZE - devc->buflen;
	len = serial_read(fd, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= FS9721_PACKET_SIZE) {
		if (sr_fs9721_packet_valid(devc->buf + offset)) {
			fs9721_serial_handle_packet(devc->buf + offset, devc);
			offset += FS9721_PACKET_SIZE;
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
