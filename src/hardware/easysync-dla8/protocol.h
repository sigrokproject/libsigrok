/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Magnus Lucchese <magnus.lucchese@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_EASYSYNC_DLA8_PROTOCOL_H
#define LIBSIGROK_HARDWARE_EASYSYNC_DLA8_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libusb.h>
#include <ftdi.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "easysync-dla8"

#define DLA8_USB_VID            0x0403
#define DLA8_USB_PID            0xfac5
#define DLA8_IPRODUCT           "ES-DLA-8"
#define DLA8_NUM_CHANNELS       8
#define DLA8_BASE_CLOCK_HZ      SR_MHZ(60)
#define DLA8_DEPTH_UNIT         (1024 * 1024)   /* 1 Mega-sample per 0x0d unit */
#define DLA8_DEPTH_MAX          0xff            /* Max hardware depth operand: 255 Mi */
#define DLA8_READ_CHUNK         (64 * 1024)     /* 64 KiB read buffer */
#define DLA8_LATENCY_MS         3
#define DLA8_ECHO_TIMEOUT_MS    200
#define DLA8_NUM_CHANNELS_4CH   4
#define DLA8_CONFIG_PACKET_LEN_4CH 18
#define DLA8_CONFIG_PACKET_LEN_8CH 26
#define DLA8_USB_TIMEOUT_MS     20
#define DLA8_DATA_TIMEOUT_US    2000000         /* 2.0 s stream stall timeout */

enum dla8_cmd {
	DLA8_CMD_INIT        = 0x01,
	DLA8_CMD_ENABLE_CH   = 0x02,  /* + channel id (0x00..0x07) */
	DLA8_CMD_SET_DIVIDER = 0x03,  /* + div_lo, div_hi (16-bit LE) */
	DLA8_CMD_ARM         = 0x04,  /* Arm & begin streaming */
	DLA8_CMD_STOP        = 0x05,  /* Disarm & stop streaming */
	DLA8_CMD_MODE_RAW    = 0x06,  /* Linear uncompressed RAW */
	DLA8_CMD_CLK_EXT_POS = 0x08,  /* External clock, rising edge */
	DLA8_CMD_CLK_EXT_NEG = 0x09,  /* External clock, falling edge */
	DLA8_CMD_CLK_INT     = 0x0a,  /* Internal 60 MHz clock */
	DLA8_CMD_DELIMITER   = 0x0c,  /* Fixed delimiter */
	DLA8_CMD_SET_DEPTH   = 0x0d,  /* + N (x 2^20 samples) */
	DLA8_CMD_PING1       = 0xaa,
	DLA8_CMD_PING2       = 0xab,
};

struct dev_context {
	struct ftdi_context *ftdic;
	uint64_t cur_samplerate;       /* Actual rate: 60 MHz / (divider + 1) */
	uint16_t rate_divider;
	gboolean ext_clock;
	gboolean ext_clock_falling;
	uint64_t capture_ratio;
	struct sr_sw_limits sw_limits;
	struct soft_trigger_logic *stl;
	gboolean trigger_fired;
	uint64_t sent_samples;
	uint64_t total_samples_target; /* Hardware sample count target */
	gint64 acq_start_us;
	gint64 last_rx_us;
	gboolean throughput_warned;
	gboolean is_4ch;               /* TRUE if 4-channel mode active (CH0..CH3 only) */
	uint8_t unpack_buf[DLA8_READ_CHUNK * 2]; /* Buffer for unpacked 4-bit samples */
	uint8_t buf[DLA8_READ_CHUNK];
};

SR_PRIV gboolean dla8_is_4ch_mode(const struct sr_dev_inst *sdi);
SR_PRIV int dla8_ftdi_init(struct dev_context *devc);
SR_PRIV int dla8_handshake(struct dev_context *devc);
SR_PRIV int dla8_set_samplerate(struct dev_context *devc, uint64_t rate);
SR_PRIV size_t dla8_build_config(const struct dev_context *devc, uint8_t depth_m, uint8_t *buf, gboolean is_4ch);
SR_PRIV int dla8_start(const struct sr_dev_inst *sdi);
SR_PRIV int dla8_stop(struct dev_context *devc);
SR_PRIV int dla8_receive_data(int fd, int revents, void *cb_data);

extern const uint64_t dla8_samplerates_4ch[];
extern const size_t dla8_num_samplerates_4ch;
extern const uint64_t *dla8_samplerates_8ch;
extern const size_t dla8_num_samplerates_8ch;

#endif
