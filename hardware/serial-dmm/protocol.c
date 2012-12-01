/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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

static void log_dmm_packet(const uint8_t *buf)
{
	sr_dbg("DMM packet: %02x %02x %02x %02x %02x %02x %02x"
	       " %02x %02x %02x %02x %02x %02x %02x",
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
	       buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13]);
}

SR_PRIV void dmm_details_dt4000zc(struct sr_datafeed_analog *analog, void *info)
{
	dmm_details_tp4000zc(analog, info); /* Same as TP4000ZC. */
}

SR_PRIV void dmm_details_tp4000zc(struct sr_datafeed_analog *analog, void *info)
{
	struct fs9721_info *info_local;

	info_local = (struct fs9721_info *)info;

	/* User-defined FS9721_LP3 flag 'c2c1_10' means temperature. */
	if (info_local->is_c2c1_10) {
		analog->mq = SR_MQ_TEMPERATURE;
		/* No Kelvin or Fahrenheit from the device, just Celsius. */
		analog->unit = SR_UNIT_CELSIUS;
	}
}

static void handle_packet(const uint8_t *buf, struct dev_context *devc,
			  int dmm, void *info)
{
	float floatval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;

	log_dmm_packet(buf);

	if (!(analog = g_try_malloc0(sizeof(struct sr_datafeed_analog)))) {
		sr_err("Analog packet malloc failed.");
		return;
	}

	analog->num_samples = 1;
	analog->mq = -1;

	dmms[dmm].packet_parse(buf, &floatval, analog, info);
	analog->data = &floatval;

	dmms[dmm].dmm_details(analog, info);

	if (analog->mq != -1) {
		/* Got a measurement. */
		packet.type = SR_DF_ANALOG;
		packet.payload = analog;
		sr_session_send(devc->cb_data, &packet);
		devc->num_samples++;
	}

	g_free(analog);
}

static void handle_new_data(struct dev_context *devc, int dmm, void *info)
{
	int len, i, offset = 0;

	/* Try to get as much data as the buffer can hold. */
	len = DMM_BUFSIZE - devc->buflen;
	len = serial_read(devc->serial, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= dmms[dmm].packet_size) {
		if (dmms[dmm].packet_valid(devc->buf + offset)) {
			handle_packet(devc->buf + offset, devc, dmm, info);
			offset += dmms[dmm].packet_size;
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	for (i = 0; i < devc->buflen - offset; i++)
		devc->buf[i] = devc->buf[offset + i];
	devc->buflen -= offset;
}

static int receive_data(int fd, int revents, int dmm, void *info, void *cb_data)
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
		handle_new_data(devc, dmm, info);
	}

	if (devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached, stopping.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	return TRUE;
}

SR_PRIV int digitek_dt4000zc_receive_data(int fd, int revents, void *cb_data)
{
	struct fs9721_info info;

	return receive_data(fd, revents, DIGITEK_DT4000ZC, &info, cb_data);
}

SR_PRIV int tekpower_tp4000zc_receive_data(int fd, int revents, void *cb_data)
{
	struct fs9721_info info;

	return receive_data(fd, revents, TEKPOWER_TP4000ZC, &info, cb_data);
}
