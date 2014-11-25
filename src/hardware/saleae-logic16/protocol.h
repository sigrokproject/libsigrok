/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#ifndef LIBSIGROK_HARDWARE_SALEAE_LOGIC16_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SALEAE_LOGIC16_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "saleae-logic16"

enum voltage_range {
	VOLTAGE_RANGE_UNKNOWN,
	VOLTAGE_RANGE_18_33_V,	/* 1.8V and 3.3V logic */
	VOLTAGE_RANGE_5_V,	/* 5V logic */
};

enum fpga_variant {
	FPGA_VARIANT_ORIGINAL,
	FPGA_VARIANT_MCUPRO    /* mcupro clone v4.6 with Actel FPGA */
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/** Distinguishing between original Logic16 and clones */
	enum fpga_variant fpga_variant;

	/*
	 * Since we can't keep track of a Logic16 device after upgrading
	 * the firmware (it renumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;

	/** The currently configured samplerate of the device. */
	uint64_t cur_samplerate;

	/** Maximum number of samples to capture, if nonzero. */
	uint64_t limit_samples;

	/** Percent of the samples that should be captured before the trigger. */
	uint64_t capture_ratio;

	/** The currently configured input voltage of the device. */
	enum voltage_range cur_voltage_range;

	/** The input voltage selected by the user. */
	enum voltage_range selected_voltage_range;

	/** Channels to use. */
	uint16_t cur_channels;

	/* EEPROM data from address 8. */
	uint8_t eeprom_data[8];

	int64_t sent_samples;
	int submitted_transfers;
	int empty_transfer_count;
	int num_channels;
	int cur_channel;
	uint16_t channel_masks[16];
	uint16_t channel_data[16];
	uint8_t *convbuffer;
	size_t convbuffer_size;
	struct soft_trigger_logic *stl;
	gboolean trigger_fired;

	void *cb_data;
	unsigned int num_transfers;
	struct libusb_transfer **transfers;
	struct sr_context *ctx;
};

SR_PRIV int logic16_setup_acquisition(const struct sr_dev_inst *sdi,
			uint64_t samplerate, uint16_t channels);
SR_PRIV int logic16_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int logic16_abort_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int logic16_init_device(const struct sr_dev_inst *sdi);
SR_PRIV void logic16_receive_transfer(struct libusb_transfer *transfer);

#endif
