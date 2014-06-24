/*
 * This file is part of the libsigrok project.
 *
 * Copyright 2014 Google, Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#ifndef LIBSIGROK_HARDWARE_CHROMIUM_TWINKIE_PROTOCOL_H
#define LIBSIGROK_HARDWARE_CHROMIUM_TWINKIE_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "twinkie"

/** Private, per-CC logical channel context. */
struct cc_context {
	int idx;
	int rollbacks;
	uint8_t prev_src;
	uint8_t level;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/** Maximum number of samples to capture, if nonzero. */
	uint64_t limit_samples;

	int64_t sent_samples;
	int submitted_transfers;
	uint8_t *convbuffer;
	size_t convbuffer_size;

	unsigned int num_transfers;
	struct libusb_transfer **transfers;
	struct sr_context *ctx;

	struct cc_context cc[2];
};

SR_PRIV int twinkie_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int twinkie_init_device(const struct sr_dev_inst *sdi);
SR_PRIV void twinkie_receive_transfer(struct libusb_transfer *transfer);

#endif
