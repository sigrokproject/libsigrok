/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_HARDWARE_CENTER_3XX_PROTOCOL_H
#define LIBSIGROK_HARDWARE_CENTER_3XX_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "center-3xx"

enum {
	CENTER_309,
	VOLTCRAFT_K204,
};

struct center_dev_info {
	char *vendor;
	char *device;
	char *conn;
	int num_channels;
	uint32_t max_sample_points;
	uint8_t packet_size;
	gboolean (*packet_valid)(const uint8_t *);
	struct sr_dev_driver *di;
	int (*receive_data)(int, int, void *);
};

extern SR_PRIV const struct center_dev_info center_devs[];

#define SERIAL_BUFSIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received samples. */
	uint64_t num_samples;

	int64_t starttime;

	uint8_t buf[SERIAL_BUFSIZE];
	int bufoffset;
	int buflen;
};

SR_PRIV gboolean center_3xx_packet_valid(const uint8_t *buf);

SR_PRIV int receive_data_CENTER_309(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_K204(int fd, int revents, void *cb_data);

#endif
