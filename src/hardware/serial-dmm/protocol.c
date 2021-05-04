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

static void log_dmm_packet(const uint8_t *buf, size_t len)
{
	GString *text;

	if (sr_log_loglevel_get() < SR_LOG_DBG)
		return;

	text = sr_hexdump_new(buf, len);
	sr_dbg("DMM packet: %s", text->str);
	sr_hexdump_free(text);
}

static void handle_packet(struct sr_dev_inst *sdi,
	const uint8_t *buf, size_t len, void *info)
{
	struct dmm_info *dmm;
	struct dev_context *devc;
	float floatval;
	double doubleval;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	gboolean sent_sample;
	struct sr_channel *channel;
	size_t ch_idx;

	dmm = (struct dmm_info *)sdi->driver;

	log_dmm_packet(buf, len);
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

		if (dmm->packet_parse) {
			dmm->packet_parse(buf, &floatval, &analog, info);
			analog.data = &floatval;
			analog.encoding->unitsize = sizeof(floatval);
		} else if (dmm->packet_parse_len) {
			dmm->packet_parse_len(dmm->dmm_state, buf, len,
				&doubleval, &analog, info);
			analog.data = &doubleval;
			analog.encoding->unitsize = sizeof(doubleval);
		}

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
	uint64_t now, left, next;
	int ret;

	dmm = (struct dmm_info *)sdi->driver;
	if (!dmm->packet_request)
		return SR_OK;

	devc = sdi->priv;
	serial = sdi->conn;

	now = g_get_monotonic_time();
	if (devc->req_next_at && now < devc->req_next_at) {
		left = (devc->req_next_at - now) / 1000;
		sr_spew("Not re-requesting yet, %" PRIu64 "ms left.", left);
		return SR_OK;
	}

	sr_spew("Requesting next packet.");
	ret = dmm->packet_request(serial);
	if (ret < 0) {
		sr_err("Failed to request packet: %d.", ret);
		return ret;
	}

	if (dmm->req_timeout_ms) {
		next = now + dmm->req_timeout_ms * 1000;
		devc->req_next_at = next;
	}

	return SR_OK;
}

static void handle_new_data(struct sr_dev_inst *sdi, void *info)
{
	struct dmm_info *dmm;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;
	size_t read_len, check_pos, check_len, pkt_size, copy_len;
	uint8_t *check_ptr;
	uint64_t deadline;

	dmm = (struct dmm_info *)sdi->driver;

	devc = sdi->priv;
	serial = sdi->conn;

	/* Add the maximum available RX data we can get to the local buffer. */
	read_len = DMM_BUFSIZE - devc->buflen;
	ret = serial_read_nonblocking(serial, &devc->buf[devc->buflen], read_len);
	if (ret == 0)
		return; /* No new bytes, nothing to do. */
	if (ret < 0) {
		sr_err("Serial port read error: %d.", ret);
		return;
	}
	devc->buflen += ret;

	/*
	 * Process packets when their reception has completed, or keep
	 * trying to synchronize to the stream of input data.
	 */
	check_pos = 0;
	while (check_pos < devc->buflen) {
		/* Got the (minimum) amount of receive data for a packet? */
		check_len = devc->buflen - check_pos;
		if (check_len < dmm->packet_size)
			break;
		sr_dbg("Checking: pos %zu, len %zu.", check_pos, check_len);

		/* Is it a valid packet? */
		check_ptr = &devc->buf[check_pos];
		if (dmm->packet_valid_len) {
			ret = dmm->packet_valid_len(dmm->dmm_state,
				check_ptr, check_len, &pkt_size);
			if (ret == SR_PACKET_NEED_RX) {
				sr_dbg("Need more RX data.");
				break;
			}
			if (ret == SR_PACKET_INVALID) {
				sr_dbg("Not a valid packet, searching.");
				check_pos++;
				continue;
			}
		} else if (dmm->packet_valid) {
			if (!dmm->packet_valid(check_ptr)) {
				sr_dbg("Not a valid packet, searching.");
				check_pos++;
				continue;
			}
			pkt_size = dmm->packet_size;
		}

		/* Process the package. */
		sr_dbg("Valid packet, size %zu, processing", pkt_size);
		handle_packet(sdi, check_ptr, pkt_size, info);
		check_pos += pkt_size;

		/* Arrange for the next packet request if needed. */
		if (!dmm->packet_request)
			continue;
		if (dmm->req_timeout_ms || dmm->req_delay_ms) {
			deadline = g_get_monotonic_time();
			deadline += dmm->req_delay_ms * 1000;
			devc->req_next_at = deadline;
		}
		req_packet(sdi);
		continue;
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	if (devc->buflen > check_pos) {
		copy_len = devc->buflen - check_pos;
		memmove(&devc->buf[0], &devc->buf[check_pos], copy_len);
	}
	devc->buflen -= check_pos;

	/*
	 * If the complete buffer filled up and none of it got processed,
	 * discard the unprocessed buffer, re-sync to the stream in later
	 * calls again.
	 */
	if (devc->buflen == sizeof(devc->buf)) {
		sr_info("Drop unprocessed RX data, try to re-sync to stream.");
		devc->buflen = 0;
	}
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
