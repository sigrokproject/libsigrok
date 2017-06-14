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

#ifndef LIBSIGROK_HARDWARE_DSLOGIC_PROTOCOL_H
#define LIBSIGROK_HARDWARE_DSLOGIC_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "dslogic"

#define USB_INTERFACE		0
#define USB_CONFIGURATION	1

#define MAX_RENUM_DELAY_MS	3000
#define NUM_SIMUL_TRANSFERS	32
#define MAX_EMPTY_TRANSFERS	(NUM_SIMUL_TRANSFERS * 2)

#define NUM_CHANNELS		16
#define NUM_TRIGGER_STAGES	16

#define DSLOGIC_REQUIRED_VERSION_MAJOR	1

/* 6 delay states of up to 256 clock ticks */
#define MAX_SAMPLE_DELAY	(6 * 256)

#define DSLOGIC_FPGA_FIRMWARE_5V "dreamsourcelab-dslogic-fpga-5v.fw"
#define DSLOGIC_FPGA_FIRMWARE_3V3 "dreamsourcelab-dslogic-fpga-3v3.fw"
#define DSCOPE_FPGA_FIRMWARE "dreamsourcelab-dscope-fpga.fw"
#define DSLOGIC_PRO_FPGA_FIRMWARE "dreamsourcelab-dslogic-pro-fpga.fw"
#define DSLOGIC_PLUS_FPGA_FIRMWARE "dreamsourcelab-dslogic-plus-fpga.fw"
#define DSLOGIC_BASIC_FPGA_FIRMWARE "dreamsourcelab-dslogic-basic-fpga.fw"

struct dslogic_profile {
	uint16_t vid;
	uint16_t pid;

	const char *vendor;
	const char *model;
	const char *model_version;

	const char *firmware;

	uint32_t dev_caps;

	const char *usb_manufacturer;
	const char *usb_product;

	/* Memory depth in bits. */
	uint64_t mem_depth;
};

struct dev_context {
	const struct dslogic_profile *profile;
	/*
	 * Since we can't keep track of an dslogic device after upgrading
	 * the firmware (it renumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;

	/* Supported samplerates */
	const uint64_t *samplerates;
	int num_samplerates;

	/* Device/capture settings */
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;

	/* Operational settings */
	gboolean trigger_fired;
	gboolean acq_aborted;

	unsigned int sent_samples;
	int submitted_transfers;
	int empty_transfer_count;

	unsigned int num_transfers;
	struct libusb_transfer **transfers;
	struct sr_context *ctx;

	uint16_t mode;
	uint32_t trigger_pos;
	gboolean external_clock;
	gboolean continuous_mode;
	int clock_edge;
	double cur_threshold;
};

SR_PRIV int dslogic_set_voltage_threshold(const struct sr_dev_inst *sdi, double threshold);
SR_PRIV int dslogic_command_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di);
SR_PRIV struct dev_context *dslogic_dev_new(void);
SR_PRIV void dslogic_abort_acquisition(struct dev_context *devc);
SR_PRIV void LIBUSB_CALL dslogic_receive_transfer(struct libusb_transfer *transfer);
SR_PRIV size_t dslogic_get_buffer_size(struct dev_context *devc);
SR_PRIV unsigned int dslogic_get_timeout(struct dev_context *devc);
SR_PRIV void dslogic_send_data(struct sr_dev_inst *sdi, uint8_t *data,
		size_t length, size_t sample_width);

#endif
