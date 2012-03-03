/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_SALEAE_LOGIC_SALEAE_LOGIC_H
#define LIBSIGROK_HARDWARE_SALEAE_LOGIC_SALEAE_LOGIC_H

#define USB_INTERFACE          0
#define USB_CONFIGURATION      1
#define NUM_TRIGGER_STAGES     4
#define TRIGGER_TYPES          "01"
#define FIRMWARE               FIRMWARE_DIR "/saleae-logic.fw"

/* delay in ms */
#define MAX_RENUM_DELAY        3000
#define NUM_SIMUL_TRANSFERS    10
#define MAX_EMPTY_TRANSFERS    (NUM_SIMUL_TRANSFERS * 2)

/* Software trigger implementation: positive values indicate trigger stage. */
#define TRIGGER_FIRED          -1

struct fx2_profile {
	/* VID/PID when first found */
	uint16_t orig_vid;
	uint16_t orig_pid;
	/* VID/PID after firmware upload */
	uint16_t fw_vid;
	uint16_t fw_pid;
	char *vendor;
	char *model;
	char *model_version;
	int num_probes;
};

/* Private, per-device-instance driver context. */
struct context {
	struct fx2_profile *profile;
	/*
	 * Since we can't keep track of a Saleae Logic device after upgrading
	 * the firmware (it re-enumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	GTimeVal fw_updated;
	/* device/capture settings */
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint8_t probe_mask;
	uint8_t trigger_mask[NUM_TRIGGER_STAGES];
	uint8_t trigger_value[NUM_TRIGGER_STAGES];
	int trigger_stage;
	uint8_t trigger_buffer[NUM_TRIGGER_STAGES];
	int num_samples;
	/*
	 * opaque session data passed in by the frontend, will be passed back
	 * on the session bus along with samples.
	 */
	void *session_dev_id;

	struct sr_usb_dev_inst *usb;
};

#endif
