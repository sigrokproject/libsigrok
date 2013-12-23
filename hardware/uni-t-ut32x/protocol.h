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
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-ut32x"

#define DEFAULT_DATA_SOURCE DATA_SOURCE_LIVE
#define USB_CONN "1a86.e008"
#define VENDOR "UNI-T"
#define MODEL "UT32x"
#define USB_INTERFACE 0
#define USB_CONFIGURATION 1

#define EP_IN 0x80 | 2
#define EP_OUT 2

enum {
    DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

enum {
	CMD_GET_LIVE = 1,
	CMD_STOP = 2,
	CMD_GET_STORED = 7,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Acquisition settings */
	uint64_t limit_samples;
	gboolean data_source;

	/* Operational state */
	uint64_t num_samples;
	unsigned char buf[8];
	struct libusb_transfer *xfer;
	void *cb_data;

	/* Temporary state across callbacks */
	unsigned char packet[32];
	int packet_len;
};

SR_PRIV int uni_t_ut32x_handle_events(int fd, int revents, void *cb_data);
SR_PRIV void uni_t_ut32x_receive_transfer(struct libusb_transfer *transfer);

#endif
