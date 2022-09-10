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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_USS_SCALE_PROTOCOL_H
#define LIBSIGROK_HARDWARE_USS_SCALE_PROTOCOL_H

#define LOG_PREFIX "uss-scale"

#define SCALE_BUFSIZE 4096

struct dev_context {
	struct sr_sw_limits limits;

	uint8_t buf[SCALE_BUFSIZE];
	int buflen;
};

struct scale_info {
	struct sr_dev_driver di;
	const char *vendor;
	const char *device;
	/* I've seen some documentation describing models with 16-byte packets,
	 * so the packet_size is parameterised. */
	int packet_size;
	gboolean (*packet_valid)(const uint8_t *);
	int (*packet_parse)(const uint8_t *,
			    struct sr_datafeed_analog *, double *);
};


SR_PRIV int uss_scale_receive_data(int fd, int revents, void *cb_data);

#endif
