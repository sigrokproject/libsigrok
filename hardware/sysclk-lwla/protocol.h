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

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "sysclk-lwla"

#include "lwla.h"
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include <stdint.h>
#include <glib.h>

/* For now, only the LWLA1034 is supported.
 */
#define VENDOR_NAME	"SysClk"
#define MODEL_NAME	"LWLA1034"

#define USB_VID_PID	"2961.6689"
#define USB_INTERFACE	0
#define USB_TIMEOUT	3000 /* ms */

#define NUM_PROBES	34
#define TRIGGER_TYPES	"01fr"

/** Unit and packet size for the sigrok logic datafeed.
 */
#define UNIT_SIZE	((NUM_PROBES + 7) / 8)
#define PACKET_SIZE	(10000 * UNIT_SIZE)	/* bytes */

/** Size of the acquisition buffer in device memory units.
 */
#define MEMORY_DEPTH	(256 * 1024)	/* 256k x 36 bit */

/** Number of device memory units (36 bit) to read at a time.  Slices of 8
 * consecutive 36-bit words are mapped to 9 32-bit words each, so the chunk
 * length should be a multiple of 8 to ensure alignment to slice boundaries.
 *
 * Experimentation has shown that reading chunks larger than about 1024 bytes
 * is unreliable.  The threshold seems to relate to the buffer size on the FX2
 * USB chip:  The configured endpoint buffer size is 512, and with double or
 * triple buffering enabled a multiple of 512 bytes can be kept in fly.
 *
 * The vendor software limits reads to 120 words (15 slices, 540 bytes) at
 * a time.  So far, it appears safe to increase this to 224 words (28 slices,
 * 1008 bytes), thus making the most of two 512 byte buffers.
 */
#define READ_CHUNK_LEN	(28 * 8)

/** Calculate the required buffer size in 16-bit units for reading a given
 * number of device memory words.  Rounded to a multiple of 8 device words.
 */
#define LWLA1034_MEMBUF_LEN(count) (((count) + 7) / 8 * 18)

/** Maximum number of 16-bit words sent at a time during acquisition.
 * Used for allocating the libusb transfer buffer.
 */
#define MAX_ACQ_SEND_WORDS	8 /* 5 for memory read request plus stuffing */

/** Maximum number of 16-bit words received at a time during acquisition.
 * Round to the next multiple of the endpoint buffer size to avoid nasty
 * transfer overflow conditions on hiccups.
 */
#define MAX_ACQ_RECV_WORDS	((READ_CHUNK_LEN / 4 * 9 + 255) / 256 * 256)

/** Maximum length of a register write sequence.
 */
#define MAX_REG_WRITE_SEQ_LEN   5

/** Default configured samplerate.
 */
#define DEFAULT_SAMPLERATE	SR_MHZ(125)

/** Maximum configurable sample count limit.
 */
#define MAX_LIMIT_SAMPLES	(UINT64_C(1) << 48)

/** Maximum configurable capture duration in milliseconds.
 */
#define MAX_LIMIT_MSEC		(UINT64_C(1) << 32)

/** LWLA clock sources.
 */
enum clock_source {
	CLOCK_SOURCE_NONE,
	CLOCK_SOURCE_INT,
	CLOCK_SOURCE_EXT_RISE,
	CLOCK_SOURCE_EXT_FALL,
};

/** LWLA device states.
 */
enum device_state {
	STATE_IDLE = 0,

	STATE_START_CAPTURE,

	STATE_STATUS_WAIT,
	STATE_STATUS_REQUEST,
	STATE_STATUS_RESPONSE,

	STATE_STOP_CAPTURE,

	STATE_LENGTH_REQUEST,
	STATE_LENGTH_RESPONSE,

	STATE_READ_PREPARE,
	STATE_READ_REQUEST,
	STATE_READ_RESPONSE,
	STATE_READ_END,
};

/** LWLA run-length encoding states.
 */
enum rle_state {
	RLE_STATE_DATA,
	RLE_STATE_LEN
};

/** LWLA sample acquisition and decompression state.
 */
struct acquisition_state {
	uint64_t sample;
	uint64_t run_len;

	/** Maximum number of samples to process. */
	uint64_t samples_max;
	/** Number of samples sent to the session bus. */
	uint64_t samples_done;

	/** Maximum duration of capture, in milliseconds. */
	uint64_t duration_max;
	/** Running capture duration since trigger event. */
	uint64_t duration_now;

	/** Capture memory fill level. */
	size_t mem_addr_fill;

	size_t mem_addr_done;
	size_t mem_addr_next;
	size_t mem_addr_stop;

	size_t out_offset;

	struct libusb_transfer *xfer_in;
	struct libusb_transfer *xfer_out;

	unsigned int capture_flags;

	enum rle_state rle;

	/** Whether to bypass the clock divider. */
	gboolean bypass_clockdiv;

	/* Payload data buffers for outgoing and incoming transfers. */
	uint16_t xfer_buf_out[MAX_ACQ_SEND_WORDS];
	uint16_t xfer_buf_in[MAX_ACQ_RECV_WORDS];

	/* Payload buffer for sigrok logic packets. */
	uint8_t out_packet[PACKET_SIZE];
};

/** Private, per-device-instance driver context.
 */
struct dev_context {
	/** The samplerate selected by the user. */
	uint64_t samplerate;

	/** The maximimum sampling duration, in milliseconds. */
	uint64_t limit_msec;

	/** The maximimum number of samples to acquire. */
	uint64_t limit_samples;

	/** Channels to use. */
	uint64_t channel_mask;

	uint64_t trigger_mask;
	uint64_t trigger_edge_mask;
	uint64_t trigger_values;

	struct acquisition_state *acquisition;

	struct regval_pair reg_write_seq[MAX_REG_WRITE_SEQ_LEN];
	int reg_write_pos;
	int reg_write_len;

	enum device_state state;

	/** The currently configured clock source of the device. */
	enum clock_source cur_clock_source;
	/** The clock source selected by the user. */
	enum clock_source selected_clock_source;

	/* Indicates that stopping the acquisition is currently in progress. */
	gboolean stopping_in_progress;

	/* Indicates whether a transfer failed. */
	gboolean transfer_error;
};

SR_PRIV struct acquisition_state *lwla_alloc_acquisition_state(void);
SR_PRIV void lwla_free_acquisition_state(struct acquisition_state *acq);

SR_PRIV int lwla_init_device(const struct sr_dev_inst *sdi);
SR_PRIV int lwla_set_clock_source(const struct sr_dev_inst *sdi);
SR_PRIV int lwla_setup_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int lwla_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int lwla_abort_acquisition(const struct sr_dev_inst *sdi);

SR_PRIV int lwla_receive_data(int fd, int revents, void *cb_data);

#endif /* !LIBSIGROK_HARDWARE_SYSCLK_LWLA_PROTOCOL_H */
