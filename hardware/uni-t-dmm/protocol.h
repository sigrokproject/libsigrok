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
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-dmm"

enum {
	TECPEL_DMM_8061,
	UNI_T_UT60A,
	UNI_T_UT60E,
	UNI_T_UT60G,
	UNI_T_UT61B,
	UNI_T_UT61C,
	UNI_T_UT61D,
	UNI_T_UT61E,
	VOLTCRAFT_VC820,
	VOLTCRAFT_VC830,
	VOLTCRAFT_VC840,
	TENMA_72_7745,
	TENMA_72_7750,
};

struct dmm_info {
	char *vendor;
	char *device;
	uint32_t baudrate;
	int packet_size;
	gboolean (*packet_valid)(const uint8_t *);
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog *, void *);
	void (*dmm_details)(struct sr_datafeed_analog *, void *);
	struct sr_dev_driver *di;
	int (*receive_data)(int, int, void *);
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

SR_PRIV int receive_data_TECPEL_DMM_8061(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT60A(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT60E(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT60G(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61B(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61C(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61D(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_UNI_T_UT61E(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC820(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC830(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_VOLTCRAFT_VC840(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TENMA_72_7745(int fd, int revents, void *cb_data);
SR_PRIV int receive_data_TENMA_72_7750(int fd, int revents, void *cb_data);

#endif
