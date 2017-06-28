/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Jan Luebbe <jluebbe@lasnet.de>
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

#ifndef LIBSIGROK_HARDWARE_SALEAE_LOGIC_PRO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SALEAE_LOGIC_PRO_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "saleae-logic-pro"

/* 16 channels * 32 samples */
#define CONV_BATCH_SIZE (2 * 32)

/*
 * One packet + one partial conversion: Worst case is only one active
 * channel converted to 2 bytes per sample, with 8 * 16384 samples per packet.
 */
#define CONV_BUFFER_SIZE (2 * 8 * 16384 + CONV_BATCH_SIZE)

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Acquisition settings */
	unsigned int dig_channel_cnt;
	uint16_t dig_channel_mask;
	uint16_t dig_channel_masks[16];
	uint64_t dig_samplerate;

	/* Operational state */
	uint32_t lfsr;

	/* Temporary state across callbacks */
	unsigned int num_transfers;
	unsigned int submitted_transfers;
	struct libusb_transfer **transfers;

	/* Conversion buffer */
	uint8_t *conv_buffer;
	unsigned int conv_size;
	unsigned int batch_index;
};

SR_PRIV int saleae_logic_pro_init(const struct sr_dev_inst *sdi);
SR_PRIV int saleae_logic_pro_prepare(const struct sr_dev_inst *sdi);
SR_PRIV int saleae_logic_pro_start(const struct sr_dev_inst *sdi);
SR_PRIV int saleae_logic_pro_stop(const struct sr_dev_inst *sdi);
SR_PRIV void LIBUSB_CALL saleae_logic_pro_receive_data(struct libusb_transfer *transfer);

#endif
