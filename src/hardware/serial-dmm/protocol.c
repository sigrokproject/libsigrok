/*
 * This file is part of the libsigrok project.
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

#include <config.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static void log_dmm_packet(const uint8_t *buf)
{
	sr_dbg("DMM packet: %02x %02x %02x %02x %02x %02x %02x "
	       "%02x %02x %02x %02x %02x %02x %02x %02x %02x "
	       "%02x %02x %02x %02x %02x %02x %02x",
	       buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
	       buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13],
	       buf[14], buf[15], buf[16], buf[17], buf[18], buf[19], buf[20],
	       buf[21], buf[22]);
}

static void handle_packet(const uint8_t *buf, struct sr_dev_inst *sdi,
			  void *info)
{
	struct dmm_info *dmm;
	float floatval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;
	gboolean sent_sample;
	struct sr_channel *channel;
	size_t ch_idx;

	dmm = (struct dmm_info *)sdi->driver;

	log_dmm_packet(buf);
	devc = sdi->priv;

	sent_sample = FALSE;
	memset(info, 0, dmm->info_size);
	for (ch_idx = 0; ch_idx < dmm->channel_count; ch_idx++) {
		/* Note: digits/spec_digits will be overridden by the DMM parsers. */
		sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

		channel = g_slist_nth_data(sdi->channels, ch_idx);
		analog.meaning->channels = g_slist_append(NULL, channel);
		analog.num_samples = 1;
		analog.meaning->mq = 0;

		dmm->packet_parse(buf, &floatval, &analog, info);
		analog.data = &floatval;

		/* If this DMM needs additional handling, call the resp. function. */
		if (dmm->dmm_details)
			dmm->dmm_details(&analog, info);

		if (analog.meaning->mq != 0 && channel->enabled) {
			/* Got a measurement. */
			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			sr_session_send(sdi, &packet);
			sent_sample = TRUE;
		}
	}

	if (sent_sample) {
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}
}

/** Request packet, if required. */
SR_PRIV int req_packet(struct sr_dev_inst *sdi)
{
	struct dmm_info *dmm;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;

	dmm = (struct dmm_info *)sdi->driver;

	if (!dmm->packet_request)
		return SR_OK;

	devc = sdi->priv;
	serial = sdi->conn;

	if (devc->req_next_at && (devc->req_next_at > g_get_monotonic_time())) {
		sr_spew("Not requesting new packet yet, %" PRIi64 " ms left.",
			((devc->req_next_at - g_get_monotonic_time()) / 1000));
		return SR_OK;
	}

	ret = dmm->packet_request(serial);
	if (ret < 0) {
		sr_err("Failed to request packet: %d.", ret);
		return ret;
	}

	if (dmm->req_timeout_ms)
		devc->req_next_at = g_get_monotonic_time() + (dmm->req_timeout_ms * 1000);

	return SR_OK;
}

static void handle_new_data(struct sr_dev_inst *sdi, void *info)
{
	struct dmm_info *dmm;
	struct dev_context *devc;
	int len, offset;
	struct sr_serial_dev_inst *serial;

	dmm = (struct dmm_info *)sdi->driver;

	devc = sdi->priv;
	serial = sdi->conn;

	/* Try to get as much data as the buffer can hold. */
	len = DMM_BUFSIZE - devc->buflen;
	len = serial_read_nonblocking(serial, devc->buf + devc->buflen, len);
	if (len == 0)
		return; /* No new bytes, nothing to do. */
	if (len < 0) {
		sr_err("Serial port read error: %d.", len);
		return;
	}
	devc->buflen += len;

	/* Now look for packets in that data. */
	offset = 0;
	while ((devc->buflen - offset) >= dmm->packet_size) {
		if (dmm->packet_valid(devc->buf + offset)) {
			handle_packet(devc->buf + offset, sdi, info);
			offset += dmm->packet_size;

			/* Request next packet, if required. */
			if (!dmm->packet_request)
				break;
			if (dmm->req_timeout_ms || dmm->req_delay_ms)
				devc->req_next_at = g_get_monotonic_time() +
					dmm->req_delay_ms * 1000;
			req_packet(sdi);
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	if (devc->buflen > offset)
		memmove(devc->buf, devc->buf + offset, devc->buflen - offset);
	devc->buflen -= offset;
}

int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct dmm_info *dmm;
	void *info;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	dmm = (struct dmm_info *)sdi->driver;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		info = g_malloc(dmm->info_size);
		handle_new_data(sdi, info);
		g_free(info);
	} else {
		/* Timeout; send another packet request if DMM needs it. */
		if (dmm->packet_request && (req_packet(sdi) < 0))
			return FALSE;
	}

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
