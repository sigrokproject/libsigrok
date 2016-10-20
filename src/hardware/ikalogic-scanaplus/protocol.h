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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_IKALOGIC_SCANAPLUS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_IKALOGIC_SCANAPLUS_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <ftdi.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ikalogic-scanaplus"

#define COMPRESSED_BUF_SIZE (64 * 1024)

/* Private, per-device-instance driver context. */
struct dev_context {
	/** FTDI device context (used by libftdi). */
	struct ftdi_context *ftdic;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	uint8_t *compressed_buf;
	uint64_t compressed_bytes_ignored;
	uint8_t *sample_buf;
	uint64_t bytes_received;
	uint64_t samples_sent;

	/** ScanaPLUS unique device ID (3 bytes). */
	uint8_t devid[3];
};

SR_PRIV int scanaplus_close(struct dev_context *devc);
SR_PRIV int scanaplus_get_device_id(struct dev_context *devc);
SR_PRIV int scanaplus_init(struct dev_context *devc);
SR_PRIV int scanaplus_start_acquisition(struct dev_context *devc);
SR_PRIV int scanaplus_receive_data(int fd, int revents, void *cb_data);

#endif
