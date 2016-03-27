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

#ifndef LIBSIGROK_HARDWARE_BRYMEN_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_BRYMEN_DMM_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "brymen-dmm"

#define DMM_BUFSIZE 256

enum packet_len_status {
	PACKET_HEADER_OK,
	PACKET_NEED_MORE_DATA,
	PACKET_INVALID_HEADER,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** The current number of already received samples. */
	uint64_t num_samples;

	/** Start time of acquisition session */
	int64_t starttime;

	uint8_t buf[DMM_BUFSIZE];
	int bufoffset;
	int buflen;
	int next_packet_len;
};

/**
 * Callback that assesses the size and status of the incoming packet.
 *
 * @return PACKET_HEADER_OK - This is a proper packet header.
 *         PACKET_NEED_MORE_DATA The buffer does not contain the entire header.
 *         PACKET_INVALID_HEADER Not a valid start of packet.
 */
typedef int (*packet_length_t)(const uint8_t *buf, int *len);

SR_PRIV int brymen_dmm_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int brymen_packet_request(struct sr_serial_dev_inst *serial);

SR_PRIV int brymen_packet_length(const uint8_t *buf, int *len);
SR_PRIV gboolean brymen_packet_is_valid(const uint8_t *buf);

SR_PRIV int brymen_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog_old *analog, void *info);

SR_PRIV int brymen_stream_detect(struct sr_serial_dev_inst *serial,
				 uint8_t *buf, size_t *buflen,
				 packet_length_t get_packet_size,
				 packet_valid_callback is_valid,
				 uint64_t timeout_ms, int baudrate);

#endif
