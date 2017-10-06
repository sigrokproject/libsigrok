/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_UNI_T_UT32X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_UT32X_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-ut32x"

#define DEFAULT_DATA_SOURCE DATA_SOURCE_LIVE

#define PACKET_SIZE	19

enum ut32x_data_source {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

enum ut32x_cmd_code {
	CMD_GET_LIVE = 1,
	CMD_STOP = 2,
	CMD_GET_STORED = 7,
};

struct dev_context {
	struct sr_sw_limits limits;
	enum ut32x_data_source data_source;
	uint8_t packet[PACKET_SIZE];
	size_t packet_len;
};

SR_PRIV int ut32x_handle_events(int fd, int revents, void *cb_data);

#endif
