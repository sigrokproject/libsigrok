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

enum vbus_group_index {
	VBUS_V = 0,
	VBUS_A = 1,

	VBUS_GRP_COUNT = 2
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
	int vbus_channels;
	char vbus_data[64];
	uint64_t vbus_t0;
	uint64_t vbus_delta;
        struct sr_datafeed_analog vbus_packet[VBUS_GRP_COUNT];
        struct sr_analog_meaning vbus_meaning[VBUS_GRP_COUNT];
        struct sr_analog_encoding vbus_encoding;
        struct sr_analog_spec vbus_spec;
};

SR_PRIV int twinkie_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int twinkie_init_device(const struct sr_dev_inst *sdi);
SR_PRIV void LIBUSB_CALL twinkie_receive_transfer(struct libusb_transfer *transfer);
SR_PRIV void LIBUSB_CALL twinkie_vbus_sent(struct libusb_transfer *transfer);
SR_PRIV void LIBUSB_CALL twinkie_vbus_recv(struct libusb_transfer *transfer);

#endif
