/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 cerebralmisfire <bram@cerebralmisfire.net>
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

#ifndef LIBSIGROK_HARDWARE_HANTEK_2XX2_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_2XX2_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hantek-2xx2"

/*
 * Hantek 2D42 / 2D72 / 2C42 handheld oscilloscopes. All three enumerate
 * as 0483:2d42 with the product string "HT2xxLxx".
 */
#define USB_VID			0x0483
#define USB_PID			0x2d42
#define USB_PRODUCT		"HT2xxLxx"
#define USB_INTERFACE		0
#define EP_OUT			0x02
#define EP_IN			0x81
#define USB_TIMEOUT_MS		3000

#define NUM_CHANNELS		2
#define NUM_VDIV		8

/* Every command is 10 bytes: 00 0a <func lo> <func hi> <cmd> <4 value bytes> 00. */
#define CMD_LEN			10
#define FUNC_SCOPE_SETTING	0x0000
#define FUNC_SCOPE_CAPTURE	0x0100
#define FUNC_SCREEN		0x0003

#define CMD_ENABLE(ch)		((ch) == 0 ? 0x00 : 0x06)
#define CMD_COUPLING(ch)	((ch) == 0 ? 0x01 : 0x07)
#define CMD_PROBE(ch)		((ch) == 0 ? 0x02 : 0x08)
#define CMD_SCALE(ch)		((ch) == 0 ? 0x04 : 0x0a)
#define CMD_OFFSET(ch)		((ch) == 0 ? 0x05 : 0x0b)
#define CMD_TIMEBASE		0x0e
#define CMD_CAPTURE		0x16

#define SCREEN_SCOPE		0x00
#define SCREEN_DMM		0x01

/*
 * Each capture request is answered with one packet of up to 64 bytes. The
 * request carries a size, and changing it between requests makes the
 * device answer with an oversized packet; a few of those reset its
 * firmware. It remembers the size across programs, so every program
 * talking to it must use the same one.
 */
#define PACKET_LEN		64
#define REQUEST_BYTES		2048

/*
 * Data comes in blocks of REQUEST_BYTES. Each block is one continuous
 * capture; between blocks there is a jump in time. The last packet of a
 * block takes ~270 ms while the next block is captured (vs ~53 ms), and
 * slow timebases also send a 12-byte marker packet. Either ends a block.
 */
#define BLOCK_SLOW_FACTOR	3.0
#define BLOCK_SLOW_MIN_MS	100.0

/* Offset 100 centres the trace; one division is 25 raw steps. */
#define OFFSET_CENTRE		100
#define STEPS_PER_DIV		25.0
/*
 * Raw reading for 0 V with the offset centred, measured with a shorted
 * probe. It varies by a few steps with V/div and between power cycles.
 */
#define ZERO_RAW		133.0

/* 100 samples per division, up to the converter's 250 MS/s. */
#define SAMPLES_PER_DIV		100
#define MAX_SAMPLERATE		SR_MHZ(250)

enum acq_state {
	IDLE,
	RUNNING,
	STOPPING,
};

struct dev_context {
	/* Settings, as indices into the tables in api.c. */
	int vdiv[NUM_CHANNELS];
	int coupling[NUM_CHANNELS];
	int probe[NUM_CHANNELS];
	int timebase;
	uint64_t limit_frames;

	/* Acquisition. */
	enum acq_state state;
	gboolean transfer_active;
	struct libusb_transfer *xfer_out;
	struct libusb_transfer *xfer_in;
	uint8_t out_buf[CMD_LEN];
	uint8_t in_buf[PACKET_LEN];
	int64_t request_started;
	double packet_ms;
	uint64_t packets_seen;

	gboolean ch_enabled[NUM_CHANNELS];
	int num_enabled;
	/* Bytes of the current block, interleaved by enabled channel. */
	uint8_t block[REQUEST_BYTES + PACKET_LEN];
	size_t block_len;
	/* Seen a block boundary yet: data before it is a partial block. */
	gboolean in_sync;
	uint64_t frames_sent;
};

SR_PRIV void hantek_2xx2_build_cmd(uint8_t *buf, uint16_t func, uint8_t cmd,
		uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3);
SR_PRIV int hantek_2xx2_send(const struct sr_dev_inst *sdi, uint16_t func,
		uint8_t cmd, uint8_t value);
SR_PRIV int hantek_2xx2_apply_settings(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_2xx2_start_capture(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_2xx2_receive_data(int fd, int revents, void *cb_data);

/* Tables shared with api.c. */
SR_PRIV extern const uint64_t hantek_2xx2_vdivs[][2];
SR_PRIV extern const unsigned int hantek_2xx2_num_vdivs;
SR_PRIV extern const uint64_t hantek_2xx2_timebases[][2];
SR_PRIV extern const unsigned int hantek_2xx2_num_timebases;
SR_PRIV extern const uint64_t hantek_2xx2_probe_factors[];
SR_PRIV extern const unsigned int hantek_2xx2_num_probe_factors;

SR_PRIV uint64_t hantek_2xx2_samplerate(const struct dev_context *devc);

#endif
