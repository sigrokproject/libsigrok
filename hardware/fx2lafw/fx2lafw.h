/*
 * This file is part of the sigrok project.
 *
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

#ifndef LIBSIGROK_HARDWARE_FX2LAFW
#define LIBSIGROK_HARDWARE_FX2LAFW

#define USB_INTERFACE		0
#define USB_CONFIGURATION	1
#define TRIGGER_TYPES		"01rf"
#define FIRMWARE		FIRMWARE_DIR "/fx2lafw-cwav-usbeeax.fw"

#define FIRMWARE_VID		0x0925
#define FIRMWARE_PID		0x3881

#define MAX_RENUM_DELAY		3000 /* ms */

struct fx2lafw_profile {
	uint16_t vid;
	uint16_t pid;

	char *vendor;
	char *model;
	char *model_version;

	int num_probes;
};

struct fx2lafw_device {
	struct fx2lafw_profile *profile;

	/*
	 * Since we can't keep track of an fx2lafw device after upgrading
	 * the firmware (it re-enumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	GTimeVal fw_updated;

	/* Device/Capture Settings */
	uint64_t limit_samples;

	void *session_data;

	struct sr_usb_dev_inst *usb;
};

#endif
