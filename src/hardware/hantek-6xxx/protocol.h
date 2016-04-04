/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Christer Ekholm <christerekholm@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_HANTEK_6XXX_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_6XXX_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hantek-6xxx"

#define MAX_RENUM_DELAY_MS	3000

#define DEFAULT_VOLTAGE		2
#define DEFAULT_COUPLING        COUPLING_DC
#define DEFAULT_SAMPLERATE	SR_MHZ(8)

#define NUM_CHANNELS		2

#define SAMPLERATE_VALUES \
	SR_MHZ(48), SR_MHZ(30), SR_MHZ(24), \
	SR_MHZ(16), SR_MHZ(8), SR_MHZ(4), \
	SR_MHZ(1), SR_KHZ(500), SR_KHZ(200), \
	SR_KHZ(100),

#define SAMPLERATE_REGS \
	48, 30, 24, 16, 8,  4, 1, 50, 20, 10,

#define VDIV_VALUES \
	{ 100, 1000 }, \
	{ 250, 1000 }, \
	{ 500, 1000 }, \
	{ 1, 1 },

#define VDIV_REG \
	10, 5, 2, 1,

#define VDIV_MULTIPLIER		10

/* Weird flushing needed for filtering glitch away. */
#define FLUSH_PACKET_SIZE	2600

#define MIN_PACKET_SIZE		600
#define MAX_PACKET_SIZE		(12 * 1024 * 1024)

#define HANTEK_EP_IN		0x86
#define USB_INTERFACE		0
#define USB_CONFIGURATION	1

enum control_requests {
	VDIV_CH1_REG   = 0xe0,
	VDIV_CH2_REG   = 0xe1,
	SAMPLERATE_REG = 0xe2,
	TRIGGER_REG    = 0xe3,
	CHANNELS_REG   = 0xe4,
	COUPLING_REG   = 0xe5,
};

enum states {
	IDLE,
	FLUSH,
	CAPTURE,
	STOPPING,
};

enum couplings {
	COUPLING_AC = 0,
	COUPLING_DC,
};

struct hantek_6xxx_profile {
	/* VID/PID after cold boot */
	uint16_t orig_vid;
	uint16_t orig_pid;
	/* VID/PID after firmware upload */
	uint16_t fw_vid;
	uint16_t fw_pid;
	const char *vendor;
	const char *model;
	const char *firmware;
};

struct dev_context {
	const struct hantek_6xxx_profile *profile;
	void *cb_data;
	GSList *enabled_channels;
	/*
	 * We can't keep track of an FX2-based device after upgrading
	 * the firmware (it re-enumerates into a different device address
	 * after the upgrade) this is like a global lock. No device will open
	 * until a proper delay after the last device was upgraded.
	 */
	int64_t fw_updated;
	int dev_state;
	uint64_t samp_received;
	uint64_t aq_started;

	uint64_t read_start_ts;
	uint32_t read_data_amount;

	struct libusb_transfer **sample_buf;
	uint32_t sample_buf_write;
	uint32_t sample_buf_size;

	gboolean ch_enabled[NUM_CHANNELS];
	int voltage[NUM_CHANNELS];
	int coupling[NUM_CHANNELS];
	uint64_t samplerate;

	uint64_t limit_msec;
	uint64_t limit_samples;
};

SR_PRIV int hantek_6xxx_open(struct sr_dev_inst *sdi);
SR_PRIV void hantek_6xxx_close(struct sr_dev_inst *sdi);
SR_PRIV int hantek_6xxx_get_channeldata(const struct sr_dev_inst *sdi,
		libusb_transfer_cb_fn cb, uint32_t data_amount);

SR_PRIV int hantek_6xxx_start_data_collecting(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_6xxx_stop_data_collecting(const struct sr_dev_inst *sdi);

SR_PRIV int hantek_6xxx_update_coupling(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_6xxx_update_samplerate(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_6xxx_update_vdiv(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_6xxx_update_channels(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_6xxx_init(const struct sr_dev_inst *sdi);

#endif
