/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Tilman Sauerbeck <tilman@code-monkey.de>
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

#ifndef LIBSIGROK_HARDWARE_LECROY_LOGICSTUDIO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_LECROY_LOGICSTUDIO_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "lecroy-logicstudio"

#define SAMPLE_BUF_SIZE 40960u
#define CONV_8TO16_BUF_SIZE 8192
#define INTR_BUF_SIZE 32

struct samplerate_info;

/** Private, per-device-instance driver context. */
struct dev_context {
	struct libusb_transfer *intr_xfer;
	struct libusb_transfer *bulk_xfer;

	const struct samplerate_info *samplerate_info;

	/**
	 * When the device is opened, this will point at a buffer
	 * of SAMPLE_BUF_SIZE bytes.
	 */
	uint8_t *fetched_samples;

	/**
	 * Used to convert 8 bit samples (8 channels) to 16 bit samples
	 * (16 channels), thus only used in 8 channel mode.
	 * Holds CONV_8TO16_BUF_SIZE bytes.
	 */
	uint16_t *conv8to16;

	int64_t fw_updated; /* Time of last FX2 firmware upload. */

	/** The pre-trigger capture ratio in percent. */
	uint64_t capture_ratio;

	uint64_t earliest_sample;
	uint64_t trigger_sample;

	/** The number of eight-channel groups enabled (either 1 or 2). */
	uint32_t num_enabled_channel_groups;

	/**
	 * The number of samples to acquire (in thousands).
	 * This is not customizable, but depending on the number
	 * of enabled channel groups.
	 */
	uint32_t num_thousand_samples;

	uint32_t total_received_sample_bytes;

	/** Mask of enabled channels. */
	uint16_t channel_mask;

	uint16_t acquisition_id;

	gboolean want_trigger;
	gboolean abort_acquisition;

	/**
	 * These two magic values are required in order to fix a sample
	 * buffer corruption. Before the first acquisition is run, they
	 * need to be set to 0.
	 */
	uint8_t magic_arm_trigger;
	uint8_t magic_fetch_samples;

	/**
	 * Buffer for interrupt transfers (acquisition state notifications).
	 */
	uint8_t intr_buf[INTR_BUF_SIZE];
};

SR_PRIV void lls_update_channel_mask(const struct sr_dev_inst *sdi);

SR_PRIV int lls_set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate);
SR_PRIV uint64_t lls_get_samplerate(const struct sr_dev_inst *sdi);

SR_PRIV int lls_setup_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int lls_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int lls_stop_acquisition(const struct sr_dev_inst *sdi);

#endif
