/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 taorye <taorye@outlook.com>
 * Copyright (C) 2026 Lindolfo Meira <meiraa@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_SIPEED_SLOGIC_COMBO8_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SIPEED_SLOGIC_COMBO8_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "sipeed-slogic-combo8"

#define USB_CONN "359f.0300"

#define EP_IN 0x01
#define SIZE_MAX_EP_HS 512

/*
 * Number of simultaneous in-flight USB transfers.
 *
 * The original driver used 1, which meant the host had to ACK each 107 KB
 * chunk before the device could send more.  At >20 MHz the device's TX buffer
 * overflows during that gap, causing the device to stall -- which is the root
 * cause of the acquisition hang.
 *
 * 16 concurrent transfers keeps ~1.7 MB of transfers pipelined at all times,
 * eliminating the stall window without stressing the kernel DMA allocator.
 * The value is intentionally fixed (not samplerate-scaled) for simplicity and
 * to keep total DMA usage predictable and bounded.
 */
#define NUM_CONCURRENT_TRANSFERS 16

/*
 * Maximum number of consecutive empty (zero-length or timed-out) transfers
 * before we consider the device stalled and abort acquisition.
 */
#define MAX_EMPTY_TRANSFERS 32

/*
 * Protocol command used to begin acquisition.
 *
 * CMD_STOP (0xb3) is deliberately not used: on the Combo8 it is unstable and
 * can wedge the device's control endpoint.  Stopping relies on cancelling the
 * in-flight bulk transfers, and the start path only drains the endpoint.
 */
#define CMD_START 0xb1

/*
 * Wall-clock deadline (ms) for draining the endpoint at the start of an
 * acquisition.  The drain loop exits as soon as a read returns zero bytes
 * (device quiet) or this total elapsed time is exceeded.
 */
#define CLEAR_EP_TIMEOUT_MS 500

/*
 * Per-read timeout (ms) used inside the drain loop.  Short so we detect
 * "device went quiet" quickly rather than waiting 100 ms per empty read.
 */
#define CLEAR_EP_READ_TIMEOUT_MS 10

#define DEFAULT_SAMPLE_CHANNEL 8
#define DEFAULT_SAMPLERATE SR_MHZ(40)

/* Default pre-trigger ratio in percent; the SR_CONF_CAPTURE_RATIO config key
 * overrides this at runtime. */
#define DEFAULT_CAPTURE_RATIO 20

/*
 * Maximum pre-trigger ring buffer depth in samples (4 MSamples ~ 4 MB RAM)
 * to prevent excessive memory allocation on very large sample limits.
 */
#define MAX_PRE_TRIGGER_SAMPLES (4 * 1024 * 1024)

struct dev_context {
	uint64_t limit_samples;
	uint64_t limit_msec;
	uint64_t cur_samplerate;
	uint64_t limit_samplerate;
	uint64_t cur_samplechannel;

	struct libusb_transfer *transfers[NUM_CONCURRENT_TRANSFERS];

	gboolean acq_running;
	gboolean acq_aborted;

	uint64_t timeout;

	size_t transfers_buffer_size;

	size_t bytes_need_transfer;
	size_t bytes_transferring;
	size_t bytes_transferred;
	size_t transfers_used;

	/* Consecutive empty (zero-length or timed-out) transfers; triggers
	 * abort when too high. */
	int empty_transfer_count;

	/* Total samples to deliver this acquisition, derived from
	 * limit_samples or limit_msec. */
	uint64_t samples_need;
	/* Samples already delivered to the session (used with triggers). */
	uint64_t samples_sent;

	/* Software trigger state. */
	struct soft_trigger_logic *stl;
	gboolean trigger_fired;
	uint64_t capture_ratio;

	/*
	 * Guard against re-entrant config_channel_set() calls while
	 * update_channel_enables() bulk-enables/disables channels.
	 */
	gboolean updating_channels;

	/*
	 * Pre-allocated buffer for expanding packed samples (2ch/4ch modes) to
	 * one sample per byte.  Sized for transfers_buffer_size * max_step
	 * (step=4 for 2ch).  Allocated once in acquisition_start, freed in
	 * finish_acquisition.
	 */
	uint8_t *expand_buf;
	size_t expand_buf_size;
};

SR_PRIV int
sipeed_slogic_combo8_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int sipeed_slogic_combo8_acquisition_stop(struct sr_dev_inst *sdi);

#endif
