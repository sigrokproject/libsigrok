/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Felipe Calliari <calliarifelipe@gmail.com>
 *
 * Driver for the "LogicAnalyzer" firmware by Agustín Gimenez Bernad (gusmanb)
 * running on Raspberry Pi Pico / Pico W / Pico 2 / RP2040-Zero boards.
 * Firmware: https://github.com/gusmanb/logicanalyzer
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

#ifndef LIBSIGROK_HARDWARE_GUSMANB_LOGICANALYZER_PROTOCOL_H
#define LIBSIGROK_HARDWARE_GUSMANB_LOGICANALYZER_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "gusmanb-la"

/* USB IDs used by the firmware (pid.codes). */
#define LA_USB_VID 0x1209
#define LA_USB_PID 0x3020

#define LA_SERIALCOMM "115200/8n1/dtr=1/rts=1"

/* Minimum firmware version understood by this driver. */
#define LA_MIN_MAJOR 6
#define LA_MIN_MINOR 0

#define LA_MAX_CHANNELS 24
/* Channel index used by the firmware for the external trigger input. */
#define LA_EXT_TRIGGER_CHANNEL 24

/* Commands. */
#define LA_CMD_ID 0
#define LA_CMD_CAPTURE 1
#define LA_CMD_BOOTLOADER 4

/* Trigger types understood by the firmware. */
enum la_trigger_type {
	LA_TRIG_EDGE = 0,
	LA_TRIG_COMPLEX = 1,
	LA_TRIG_FAST = 2,
	LA_TRIG_BLAST = 3,
};

/* Capture modes (sample width on the wire). */
enum la_capture_mode {
	LA_MODE_8CH = 0,
	LA_MODE_16CH = 1,
	LA_MODE_24CH = 2,
};

/* Size of the packed CAPTURE_REQUEST struct of the firmware (ARM ABI). */
#define LA_CAPTURE_REQUEST_SIZE 48

/* Trigger delay compensation, in max-frequency clock cycles (from the
 * official host software). */
#define LA_COMPLEX_TRIGGER_DELAY 5.0
#define LA_FAST_TRIGGER_DELAY 3.0

enum la_rx_state {
	LA_RX_IDLE = 0,
	LA_RX_LENGTH,
	LA_RX_SAMPLES,
	LA_RX_STAMP_COUNT,
	LA_RX_STAMPS,
	LA_RX_DONE,
};

struct la_capture_request {
	uint8_t trigger_type;
	uint8_t trigger_channel;
	uint8_t inverted_or_count;
	uint16_t trigger_value;
	uint8_t channels[LA_MAX_CHANNELS];
	uint8_t channel_count;
	uint32_t frequency;
	uint32_t pre_samples;
	uint32_t post_samples;
	uint8_t loop_count;
	uint8_t measure;
	uint8_t capture_mode;
};

struct dev_context {
	/* Device information (from the ID response). */
	char *board;
	int fw_major;
	int fw_minor;
	uint64_t max_freq;
	uint64_t blast_freq;
	uint32_t buffer_size;
	int num_channels;

	/* Configuration. */
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;

	/* Acquisition state. */
	enum la_rx_state rx_state;
	uint8_t chan_map[LA_MAX_CHANNELS]; /* wire bit -> sigrok channel */
	int chan_count;
	int bytes_per_sample;      /* on the wire: 1, 2 or 4 */
	uint32_t expected_samples; /* pre + post as requested */
	uint32_t skip_samples;     /* samples to drop at the start */
	uint32_t trigger_pos;      /* sample (after skip) where trigger is */
	gboolean has_trigger;

	uint8_t len_buf[4];
	int len_pos;
	uint32_t total_samples;    /* as reported by the device */
	uint32_t samples_rx;       /* samples received so far */
	uint32_t samples_sent;     /* samples pushed to the session */
	uint8_t partial[4];        /* partially received sample */
	int partial_pos;
	uint32_t stamp_bytes_left;

	uint8_t *out_buf;          /* converted samples (unitsize 3) */
	size_t out_len;
	size_t out_cap;
};

SR_PRIV int la_send_command(struct sr_serial_dev_inst *serial, uint8_t cmd,
	const uint8_t *payload, size_t len);
SR_PRIV void la_drain(struct sr_serial_dev_inst *serial, int idle_ms,
	int max_ms);
SR_PRIV int la_read_line(struct sr_serial_dev_inst *serial, char *buf,
	int buflen, int timeout_ms);
SR_PRIV int la_identify(struct sr_serial_dev_inst *serial,
	struct dev_context *devc);
SR_PRIV void la_pack_request(const struct la_capture_request *req,
	uint8_t *out);
SR_PRIV int la_receive_data(int fd, int revents, void *cb_data);
SR_PRIV void la_abort_capture(const struct sr_dev_inst *sdi);

#endif
