/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include <glib.h>

#ifndef LIBSIGROK_HARDWARE_FX2LAFW_FX2LAFW_H
#define LIBSIGROK_HARDWARE_FX2LAFW_FX2LAFW_H

#define USB_INTERFACE		0
#define USB_CONFIGURATION	1
#define NUM_TRIGGER_STAGES	4
#define TRIGGER_TYPES		"01"

#define MAX_RENUM_DELAY_MS	3000
#define NUM_SIMUL_TRANSFERS	32
#define MAX_EMPTY_TRANSFERS	(NUM_SIMUL_TRANSFERS * 2)

#define FX2LAFW_REQUIRED_VERSION_MAJOR	1

#define MAX_8BIT_SAMPLE_RATE	SR_MHZ(24)
#define MAX_16BIT_SAMPLE_RATE	SR_MHZ(12)

/* 6 delay states of up to 256 clock ticks */
#define MAX_SAMPLE_DELAY	(6 * 256)

/* Software trigger implementation: positive values indicate trigger stage. */
#define TRIGGER_FIRED          -1

#define DEV_CAPS_16BIT_POS	0

#define DEV_CAPS_16BIT		(1 << DEV_CAPS_16BIT_POS)

struct fx2lafw_profile {
	uint16_t vid;
	uint16_t pid;

	const char *vendor;
	const char *model;
	const char *model_version;

	const char *firmware;

	uint32_t dev_caps;
};

struct context {
	const struct fx2lafw_profile *profile;

	/*
	 * Since we can't keep track of an fx2lafw device after upgrading
	 * the firmware (it re-enumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;

	/* Device/capture settings */
	uint64_t cur_samplerate;
	uint64_t limit_samples;

	gboolean sample_wide;

	uint16_t trigger_mask[NUM_TRIGGER_STAGES];
	uint16_t trigger_value[NUM_TRIGGER_STAGES];
	int trigger_stage;
	uint16_t trigger_buffer[NUM_TRIGGER_STAGES];

	int num_samples;
	int submitted_transfers;
	int empty_transfer_count;

	void *session_dev_id;

	struct sr_usb_dev_inst *usb;

	unsigned int num_transfers;
	struct libusb_transfer **transfers;
};

#endif
