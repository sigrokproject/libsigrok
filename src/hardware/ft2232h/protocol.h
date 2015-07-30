/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Sergey Alirzaev <zl29ah@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_FT2232H_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FT2232H_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ft2232h"

#define DATA_BUF_SIZE (16 * 1024)

struct ftdi_chip_desc {
	uint16_t vendor;
	uint16_t product;
	int samplerate_div;
	char *channel_names[];
};

/** Private, per-device-instance driver context. */
struct dev_context {
	struct ftdi_context *ftdic;
	const struct ftdi_chip_desc *desc;
	struct libusb_device *usbdev;

	uint64_t limit_samples;
	uint32_t cur_samplerate;

	unsigned char *data_buf;
	uint64_t samples_sent;
	uint64_t bytes_received;
	void *cb_data;
};

SR_PRIV int ft2232h_receive_data(int fd, int revents, void *cb_data);

#endif
