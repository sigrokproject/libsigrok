/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Vitaliy Vorobyov
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

#ifndef LIBSIGROK_HARDWARE_SYSCLK_SLA5032_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SYSCLK_SLA5032_PROTOCOL_H

#include <stdint.h>
#include <libusb.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>

#define LOG_PREFIX "sysclk-sla5032"

/* Maximum configurable sample count limit. */
#define MAX_LIMIT_SAMPLES	(64 * 1024 * 1024)
#define MIN_LIMIT_SAMPLES	512

/* USB vendor and product IDs. */
enum {
	USB_VID_SYSCLK   = 0x2961,
	USB_PID_SLA5032  = 0x66B0,
};

/* USB device characteristics. */
enum {
	USB_CONFIG		= 1,
	USB_INTERFACE		= 0,
	USB_CMD_TIMEOUT_MS	= 5000,
	USB_REPLY_TIMEOUT_MS	= 500000,
	USB_DATA_TIMEOUT_MS	= 2000,
};

/* USB device end points. */
enum usb_endpoint {
	EP_COMMAND = 4 | LIBUSB_ENDPOINT_OUT,
	EP_REPLY   = 8 | LIBUSB_ENDPOINT_IN,
	EP_DATA    = 6 | LIBUSB_ENDPOINT_IN,
};


/* Common indicator for no or unknown FPGA config. */
enum {
	FPGA_NOCONF = -1,
	FPGA_CONF,
};

/** Acquisition protocol states. */
enum protocol_state {
	/* idle states */
	STATE_IDLE = 0,
	STATE_STATUS_WAIT,
	/* device command states */
	STATE_START_CAPTURE,
	STATE_STOP_CAPTURE,
	STATE_READ_PREPARE,
	STATE_READ_FINISH,
	/* command followed by response */
	STATE_EXPECT_RESPONSE = 1 << 3,
	STATE_STATUS_REQUEST = STATE_EXPECT_RESPONSE,
	STATE_LENGTH_REQUEST,
	STATE_READ_REQUEST,
};

/** SLA5032 protocol command ID codes. */
enum command_id {
	CMD_INIT_FW_UPLOAD = 1,
	CMD_UPLOAD_FW_CHUNK = 2,
	CMD_READ_REG = 3,
	CMD_WRITE_REG = 4,
	CMD_READ_MEM = 5,
	CMD_READ_DATA = 7,
};

struct dev_context {
	uint64_t samplerate;		/* requested samplerate */
	uint64_t limit_samples;		/* requested capture length (samples) */
	uint64_t capture_ratio;

	uint64_t channel_mask;		/* bit mask of enabled channels */
	uint64_t trigger_mask;		/* trigger enable mask */
	uint64_t trigger_edge_mask;	/* trigger type mask */
	uint64_t trigger_values;	/* trigger level/slope bits */

	struct soft_trigger_logic *stl;
	gboolean trigger_fired;

	int active_fpga_config;		/* FPGA configuration index */

	enum protocol_state state;	/* async protocol state */
};

SR_PRIV int sla5032_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int sla5032_apply_fpga_config(const struct sr_dev_inst *sdi);

#endif
