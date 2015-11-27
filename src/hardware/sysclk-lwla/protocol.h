/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_SYSCLK_LWLA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SYSCLK_LWLA_PROTOCOL_H

#define LOG_PREFIX "sysclk-lwla"

#include <stdint.h>
#include <libusb.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>

#define VENDOR_NAME	"SysClk"

/* Maximum configurable sample count limit.
 * Due to compression, there is no meaningful hardware limit the driver
 * could report. So this value is less than 2^64-1 for no reason other
 * than to safeguard against integer overflows.
 */
#define MAX_LIMIT_SAMPLES	(UINT64_C(1000) * 1000 * 1000 * 1000)

/* Maximum configurable acquisition time limit.
 * Due to compression, there is no hardware limit that would be meaningful
 * in practice. However, the LWLA1016 reports the elapsed time as a 32-bit
 * value, so keep this below 2^32.
 */
#define MAX_LIMIT_MSEC		(1000 * 1000 * 1000)

struct acquisition_state;

/* USB vendor and product IDs.
 */
enum {
	USB_VID_SYSCLK   = 0x2961,
	USB_PID_LWLA1016 = 0x6688,
	USB_PID_LWLA1034 = 0x6689,
};

/* USB device characteristics.
 */
enum {
	USB_CONFIG	= 1,
	USB_INTERFACE	= 0,
	USB_TIMEOUT_MS	= 3000,
};

/** USB device end points.
 */
enum usb_endpoint {
	EP_COMMAND = 2,
	EP_CONFIG  = 4,
	EP_REPLY   = 6 | LIBUSB_ENDPOINT_IN
};

/** LWLA1034 clock sources.
 */
enum clock_source {
	CLOCK_INTERNAL = 0,
	CLOCK_EXT_CLK,
};

/** LWLA1034 trigger sources.
 */
enum trigger_source {
	TRIGGER_CHANNELS = 0,
	TRIGGER_EXT_TRG,
};

/** Edge choices for the LWLA1034 external clock and trigger inputs.
 */
enum signal_edge {
	EDGE_POSITIVE = 0,
	EDGE_NEGATIVE,
};

/* Common indicator for no or unknown FPGA config. */
enum {
	FPGA_NOCONF = -1,
};

/** Acquisition protocol states.
 */
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

/** Private, per-device-instance driver context.
 */
struct dev_context {
	uint64_t samplerate;	/* requested samplerate */

	uint64_t limit_msec;	/* requested capture duration in ms */
	uint64_t limit_samples;	/* requested capture length in samples */

	uint64_t channel_mask;	/* bit mask of enabled channels */

	uint64_t trigger_mask;		/* trigger enable mask */
	uint64_t trigger_edge_mask;	/* trigger type mask */
	uint64_t trigger_values;	/* trigger level/slope bits */

	const struct model_info *model;		/* device model descriptor */
	struct acquisition_state *acquisition;	/* running capture state */
	int active_fpga_config;			/* FPGA configuration index */

	enum protocol_state state;	/* async protocol state */
	gboolean cancel_requested;	/* stop after current transfer */
	gboolean transfer_error;	/* error during device communication */

	gboolean cfg_rle;			/* RLE compression setting */
	enum clock_source cfg_clock_source;	/* clock source setting */
	enum signal_edge cfg_clock_edge;	/* ext clock edge setting */
	enum trigger_source cfg_trigger_source;	/* trigger source setting */
	enum signal_edge cfg_trigger_slope;	/* ext trigger slope setting */

};

/** LWLA model descriptor.
 */
struct model_info {
	char name[12];
	int num_channels;

	unsigned int num_devopts;
	uint32_t devopts[8];

	unsigned int num_samplerates;
	uint64_t samplerates[20];

	int (*apply_fpga_config)(const struct sr_dev_inst *sdi);
	int (*device_init_check)(const struct sr_dev_inst *sdi);
	int (*setup_acquisition)(const struct sr_dev_inst *sdi);

	int (*prepare_request)(const struct sr_dev_inst *sdi);
	int (*handle_response)(const struct sr_dev_inst *sdi);
};

SR_PRIV const struct model_info lwla1016_info;
SR_PRIV const struct model_info lwla1034_info;

SR_PRIV int lwla_start_acquisition(const struct sr_dev_inst *sdi);

#endif
