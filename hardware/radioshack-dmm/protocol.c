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


static void handle_packet(const uint8_t *rs_packet,
			  struct dev_context *devc)
{
	float rawval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;

	/* TODO: Check malloc return value. */
	analog = g_try_malloc0(sizeof(struct sr_datafeed_analog));
	/* TODO: Check malloc return value. */

	analog->num_samples = 1;
	analog->mq = -1;

	sr_rs9lcd_parse(rs_packet, &rawval, analog, NULL);
	analog->data = &rawval;

	if (analog->mq != -1) {
		/* Got a measurement. */
		sr_spew("Value: %f.", rawval);
		packet.type = SR_DF_ANALOG;
		packet.payload = analog;
		sr_session_send(devc->cb_data, &packet);
		devc->num_samples++;
	}
	g_free(analog);
}

static void handle_new_data(struct dev_context *devc)
{
	int len;
	size_t i, offset = 0;
	uint8_t *rs_packet;

	/* Try to get as much data as the buffer can hold. */
	len = RS_DMM_BUFSIZE - devc->buflen;
	len = serial_read(devc->serial, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error.");
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= RS_22_812_PACKET_SIZE) {
		rs_packet = (void *)(devc->buf + offset);
		if (sr_rs9lcd_packet_valid(rs_packet)) {
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

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		handle_new_data(devc);
	}

	if (devc->num_samples >= devc->limit_samples) {
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	return TRUE;
}
