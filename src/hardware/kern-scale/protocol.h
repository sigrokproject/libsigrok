/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
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

#ifndef LIBSIGROK_HARDWARE_KERN_SCALE_PROTOCOL_H
#define LIBSIGROK_HARDWARE_KERN_SCALE_PROTOCOL_H

#define LOG_PREFIX "kern-scale"

struct scale_info {
	/** libsigrok driver info struct. */
	struct sr_dev_driver di;
	/** Manufacturer/brand. */
	const char *vendor;
	/** Model. */
	const char *device;
	/** serialconn string. */
	const char *conn;
	/** Baud rate. */
	uint32_t baudrate;
	/** Packet size in bytes. */
	int packet_size;
	/** Packet validation function. */
	gboolean (*packet_valid)(const uint8_t *);
	/** Packet parsing function. */
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog *, void *);
	/** Size of chipset info struct. */
	gsize info_size;
};

#define SCALE_BUFSIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	struct sr_sw_limits limits;

	uint8_t buf[SCALE_BUFSIZE];
	int bufoffset;
	int buflen;
};

SR_PRIV int kern_scale_receive_data(int fd, int revents, void *cb_data);

#endif
