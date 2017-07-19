/*
 * This file is part of the libsigrok project.
 *
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

#ifndef LIBSIGROK_HARDWARE_FX2LAFW_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FX2LAFW_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "fx2lafw"

#define USB_INTERFACE		0
#define USB_CONFIGURATION	1
#define NUM_TRIGGER_STAGES	4

#define MAX_RENUM_DELAY_MS	3000
#define NUM_SIMUL_TRANSFERS	32
#define MAX_EMPTY_TRANSFERS	(NUM_SIMUL_TRANSFERS * 2)

#define NUM_CHANNELS		16

#define FX2LAFW_REQUIRED_VERSION_MAJOR	1

#define MAX_8BIT_SAMPLE_RATE	SR_MHZ(24)
#define MAX_16BIT_SAMPLE_RATE	SR_MHZ(12)

/* 6 delay states of up to 256 clock ticks */
#define MAX_SAMPLE_DELAY	(6 * 256)

#define DEV_CAPS_16BIT_POS	0
#define DEV_CAPS_AX_ANALOG_POS	1

#define DEV_CAPS_16BIT		(1 << DEV_CAPS_16BIT_POS)
#define DEV_CAPS_AX_ANALOG	(1 << DEV_CAPS_AX_ANALOG_POS)

/* Protocol commands */
#define CMD_GET_FW_VERSION		0xb0
#define CMD_START			0xb1
#define CMD_GET_REVID_VERSION		0xb2

#define CMD_START_FLAGS_CLK_CTL2_POS	4
#define CMD_START_FLAGS_WIDE_POS	5
#define CMD_START_FLAGS_CLK_SRC_POS	6

#define CMD_START_FLAGS_CLK_CTL2	(1 << CMD_START_FLAGS_CLK_CTL2_POS)
#define CMD_START_FLAGS_SAMPLE_8BIT	(0 << CMD_START_FLAGS_WIDE_POS)
#define CMD_START_FLAGS_SAMPLE_16BIT	(1 << CMD_START_FLAGS_WIDE_POS)

#define CMD_START_FLAGS_CLK_30MHZ	(0 << CMD_START_FLAGS_CLK_SRC_POS)
#define CMD_START_FLAGS_CLK_48MHZ	(1 << CMD_START_FLAGS_CLK_SRC_POS)

struct fx2lafw_profile {
	uint16_t vid;
	uint16_t pid;

	const char *vendor;
	const char *model;
	const char *model_version;

	const char *firmware;

	uint32_t dev_caps;

	const char *usb_manufacturer;
	const char *usb_product;
};

struct dev_context {
	const struct fx2lafw_profile *profile;
	GSList *enabled_analog_channels;
	/*
	 * Since we can't keep track of an fx2lafw device after upgrading
	 * the firmware (it renumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;

	const uint64_t *samplerates;
	int num_samplerates;

	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;

	gboolean trigger_fired;
	gboolean acq_aborted;
	gboolean sample_wide;
	struct soft_trigger_logic *stl;

	unsigned int sent_samples;
	int submitted_transfers;
	int empty_transfer_count;

	unsigned int num_transfers;
	struct libusb_transfer **transfers;
	struct sr_context *ctx;
	void (*send_data_proc)(struct sr_dev_inst *sdi,
		uint8_t *data, size_t length, size_t sample_width);
	uint8_t *logic_buffer;
	float *analog_buffer;
};

SR_PRIV int fx2lafw_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di);
SR_PRIV struct dev_context *fx2lafw_dev_new(void);
SR_PRIV int fx2lafw_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV void fx2lafw_abort_acquisition(struct dev_context *devc);

#endif
