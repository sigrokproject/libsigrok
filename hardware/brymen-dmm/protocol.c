/*
 * This file is part of the libsigrok project.
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

#include "protocol.h"

/* parser.c */
SR_PRIV int sr_brymen_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info);

static void handle_packet(const uint8_t *buf, struct sr_dev_inst *sdi)
{
	float floatval;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;

	devc = sdi->priv;

	analog.num_samples = 1;
	analog.mq = -1;

	sr_brymen_parse(buf, &floatval, &analog, NULL);
	analog.data = &floatval;

	analog.probes = sdi->probes;

	if (analog.mq != -1) {
		/* Got a measurement. */
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(devc->cb_data, &packet);
		devc->num_samples++;
	}
}

static void handle_new_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int len, status, offset = 0;

	devc = sdi->priv;

	/* Try to get as much data as the buffer can hold. */
	len = DMM_BUFSIZE - devc->buflen;
	len = serial_read(devc->serial, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return;
	}
	devc->buflen += len;
	status = PACKET_INVALID_HEADER;

	/* Now look for packets in that data. */
	while (status != PACKET_NEED_MORE_DATA) {
		/* We don't have a header, look for one. */
		if (devc->next_packet_len == 0) {
			len = devc->buflen - offset;
			status = brymen_packet_length(devc->buf + offset, &len);
			if (status == PACKET_HEADER_OK) {
				/* We know how large the packet will be. */
				devc->next_packet_len = len;
			} else if (status == PACKET_NEED_MORE_DATA) {
				/* We didn't yet receive the full header. */
				devc->next_packet_len = 0;
				break;
			} else {
				/* Invalid header. Move on. */
				devc->next_packet_len = 0;
				offset++;
				continue;
			}
		}

		/* We know how the packet size, but did we receive all of it? */
		if (devc->buflen - offset < devc->next_packet_len)
			break;

		/* We should have a full packet here, so we can check it. */
		if (brymen_packet_is_valid(devc->buf + offset)) {
			handle_packet(devc->buf + offset, sdi);
			offset += devc->next_packet_len;
		} else {
			offset++;
		}

		/* We are done with this packet. Look for a new one. */
		devc->next_packet_len = 0;
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	memmove(devc->buf, devc->buf + offset, devc->buflen - offset);
	devc->buflen -= offset;
}

SR_PRIV int brymen_dmm_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int ret;
	int64_t time;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		handle_new_data(sdi);
	} else {
		/* Timeout, send another packet request. */
		if ((ret = brymen_packet_request(devc->serial)) < 0) {
			sr_err("Failed to request packet: %d.", ret);
			return FALSE;
		}
	}

	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached, stopping.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	if (devc->limit_msec) {
		time = (g_get_monotonic_time() - devc->starttime) / 1000;
		if (time > (int64_t)devc->limit_msec) {
			sr_info("Requested time limit reached, stopping.");
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			return TRUE;
		}
	}

	return TRUE;
}

/**
 * Try to find a valid packet in a serial data stream.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer containing the bytes to write.
 * @param buflen Size of the buffer.
 * @param get_packet_size Callback that assesses the size of incoming packets.
 * @param is_valid Callback that assesses whether the packet is valid or not.
 * @param timeout_ms The timeout after which, if no packet is detected, to
 *                   abort scanning.
 * @param baudrate The baudrate of the serial port. This parameter is not
 *                 critical, but it helps fine tune the serial port polling
 *                 delay.
 *
 * @return SR_OK if a valid packet is found within the given timeout,
 *         SR_ERR upon failure.
 */
SR_PRIV int brymen_stream_detect(struct sr_serial_dev_inst *serial,
				uint8_t *buf, size_t *buflen,
				packet_length_t get_packet_size,
				packet_valid_t is_valid,
				uint64_t timeout_ms, int baudrate)
{
	int64_t start, time, byte_delay_us;
	size_t ibuf, i, maxlen;
	int status, len, packet_len, stream_len;

	maxlen = *buflen;

	sr_dbg("Detecting packets on FD %d (timeout = %" PRIu64
	       "ms, baudrate = %d).", serial->fd, timeout_ms, baudrate);

	/* Assume 8n1 transmission. That is 10 bits for every byte. */
	byte_delay_us = 10 * (1000000 / baudrate);
	start = g_get_monotonic_time();

	packet_len = i = ibuf = len = 0;
	while (ibuf < maxlen) {
		len = serial_read(serial, &buf[ibuf], maxlen - ibuf);
		if (len > 0) {
			ibuf += len;
			sr_spew("Read %d bytes.", len);
		}

		time = g_get_monotonic_time() - start;
		time /= 1000;

		stream_len = ibuf - i;
		if (stream_len > 0 && packet_len == 0) {
			/* How large of a packet are we expecting? */
			packet_len = stream_len;
			status = get_packet_size(&buf[i], &packet_len);
			switch(status) {
			case PACKET_HEADER_OK:
				/* We know how much data we need to wait for. */
				break;
			case PACKET_NEED_MORE_DATA:
				/* We did not receive the full header. */
				packet_len = 0;
				break;
			case PACKET_INVALID_HEADER:
			default:
				/*
				 * We had enough data, but here was an error in
				 * parsing the header. Restart parsing from the
				 * next byte.
				 */
				packet_len = 0;
				i++;
				break;
			}
		}

		if ((stream_len >= packet_len) && (packet_len != 0)) {
			/* We have at least a packet's worth of data. */
			if (is_valid(&buf[i])) {
				sr_spew("Found valid %d-byte packet after "
					"%" PRIu64 "ms.", packet_len, time);
				*buflen = ibuf;
				return SR_OK;
			} else {
				sr_spew("Got %d bytes, but not a valid "
					"packet.", packet_len);

			}

			/* Not a valid packet. Continue searching. */
			i++;
			packet_len = 0;
		}

		if (time >= (int64_t)timeout_ms) {
			/* Timeout */
			sr_dbg("Detection timed out after %dms.", time);
			break;
		}
		g_usleep(byte_delay_us);
	}

	*buflen = ibuf;
	sr_err("Didn't find a valid packet (read %d bytes).", ibuf);

	return SR_ERR;
}
