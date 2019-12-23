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

#ifndef LIBSIGROK_HARDWARE_MASTECH_MS6514_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MASTECH_MS6514_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "mastech-ms6514"

#define MASTECH_MS6514_NUM_CHANNELS	2
#define MASTECH_MS6514_BUF_SIZE		(3 * 18)
#define MASTECH_MS6514_FRAME_SIZE 	18
#define DEFAULT_DATA_SOURCE		DATA_SOURCE_LIVE

enum mastech_ms6614_data_source {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

enum mastech_ms6614_command {
	CMD_GET_STORED = 0xA1
};

struct dev_context {
	struct sr_sw_limits limits;
	enum mastech_ms6614_data_source data_source;
	unsigned int buf_len;
	uint8_t buf[MASTECH_MS6514_BUF_SIZE];
	unsigned int log_buf_len;
};

SR_PRIV int mastech_ms6514_receive_data(int fd, int revents, void *cb_data);
SR_PRIV gboolean mastech_ms6514_packet_valid(const uint8_t *buf);

#endif
