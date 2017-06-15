/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_PIPISTRELLO_OLS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PIPISTRELLO_OLS_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <ftdi.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "pipistrello-ols"

#define USB_VENDOR_ID		0x0403
#define USB_DEVICE_ID		0x6010
#define USB_IPRODUCT		"Pipistrello LX45"

#define FTDI_BUF_SIZE          (16 * 1024)

#define NUM_CHANNELS           32
#define NUM_TRIGGER_STAGES     4
#define CLOCK_RATE             SR_MHZ(100)
#define MIN_NUM_SAMPLES        4
#define DEFAULT_SAMPLERATE     SR_MHZ(100)

/* Command opcodes */
#define CMD_RESET                  0x00
#define CMD_RUN                    0x01
#define CMD_TESTMODE               0x03
#define CMD_ID                     0x02
#define CMD_METADATA               0x04
#define CMD_SET_DIVIDER            0x80
#define CMD_SET_FLAGS              0x82
#define CMD_CAPTURE_COUNT          0x83
#define CMD_CAPTURE_DELAY          0x84
#define CMD_SET_TRIGGER_MASK       0xc0
#define CMD_SET_TRIGGER_VALUE      0xc1
#define CMD_SET_TRIGGER_CONFIG     0xc2
#define CMD_SET_TRIGGER_EDGE       0xc3

/* Trigger config */
#define TRIGGER_START              (1 << 3)

/* Bitmasks for CMD_FLAGS */
/* 12-13 unused, 14-15 RLE mode (we hardcode mode 0). */
#define FLAG_INTERNAL_TEST_MODE    (1 << 11)
#define FLAG_EXTERNAL_TEST_MODE    (1 << 10)
#define FLAG_SWAP_CHANNELS         (1 << 9)
#define FLAG_RLE                   (1 << 8)
#define FLAG_SLOPE_FALLING         (1 << 7)
#define FLAG_CLOCK_EXTERNAL        (1 << 6)
#define FLAG_CHANNELGROUP_4        (1 << 5)
#define FLAG_CHANNELGROUP_3        (1 << 4)
#define FLAG_CHANNELGROUP_2        (1 << 3)
#define FLAG_CHANNELGROUP_1        (1 << 2)
#define FLAG_FILTER                (1 << 1)
#define FLAG_DEMUX                 (1 << 0)

struct dev_context {
	struct ftdi_context *ftdic;
	uint8_t *ftdi_buf;

	int max_channels;
	uint32_t max_samplebytes;
	uint32_t max_samplerate;
	uint32_t protocol_version;

	uint64_t cur_samplerate;
	uint32_t cur_samplerate_divider;
	uint32_t max_samples;
	uint64_t limit_samples;
	uint64_t capture_ratio;
	int trigger_at;
	uint32_t channel_mask;
	uint32_t trigger_mask[NUM_TRIGGER_STAGES];
	uint32_t trigger_value[NUM_TRIGGER_STAGES];
	uint32_t trigger_edge[NUM_TRIGGER_STAGES];
	int num_stages;
	uint16_t flag_reg;

	unsigned int num_transfers;
	unsigned int num_samples;
	int num_bytes;
	int cnt_bytes;
	unsigned int cnt_samples;
	int cnt_samples_rle;

	unsigned int rle_count;
	unsigned char sample[4];
	unsigned char tmp_sample[4];
	unsigned char tmp_sample2[4];
	unsigned char *raw_sample_buf;
};

SR_PRIV extern const char *p_ols_channel_names[];
SR_PRIV int write_shortcommand(struct dev_context *devc, uint8_t command);
SR_PRIV int write_longcommand(struct dev_context *devc, uint8_t command, uint8_t *data);
SR_PRIV int p_ols_open(struct dev_context *devc);
SR_PRIV int p_ols_close(struct dev_context *devc);
SR_PRIV void pols_channel_mask(const struct sr_dev_inst *sdi);
SR_PRIV int pols_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV struct sr_dev_inst *p_ols_get_metadata(uint8_t *buf, int bytes_read, struct dev_context *devc);
SR_PRIV int p_ols_set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate);
SR_PRIV int p_ols_receive_data(int fd, int revents, void *cb_data);

#endif
