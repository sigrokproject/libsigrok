/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012-2013 Uwe Hermann <uwe@hermann-uwe.de>
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

#ifndef LIBSIGROK_HARDWARE_UNI_T_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_DMM_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-dmm"

struct dmm_info {
	struct sr_dev_driver di;
	char *vendor;
	char *device;
	uint32_t baudrate;
	int packet_size;
	gboolean (*packet_valid)(const uint8_t *);
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog *, void *);
	void (*dmm_details)(struct sr_datafeed_analog *, void *);
	gsize info_size;
};

#define CHUNK_SIZE		8

#define DMM_BUFSIZE		256

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

	gboolean first_run;

	uint8_t protocol_buf[DMM_BUFSIZE];
	uint8_t bufoffset;
	uint8_t buflen;
};

SR_PRIV int uni_t_dmm_receive_data(int fd, int revents, void *cb_data);

#endif
