/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_OPENBENCH_LOGIC_SNIFFER_OLS_H
#define LIBSIGROK_HARDWARE_OPENBENCH_LOGIC_SNIFFER_OLS_H

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "ols: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define NUM_PROBES             32
#define NUM_TRIGGER_STAGES     4
#define TRIGGER_TYPES          "01"
#define SERIAL_SPEED           B115200
#define CLOCK_RATE             SR_MHZ(100)
#define MIN_NUM_SAMPLES        4

/* Command opcodes */
#define CMD_RESET                  0x00
#define CMD_RUN                    0x01
#define CMD_ID                     0x02
#define CMD_METADATA               0x04
#define CMD_SET_FLAGS              0x82
#define CMD_SET_DIVIDER            0x80
#define CMD_CAPTURE_SIZE           0x81
#define CMD_SET_TRIGGER_MASK_0     0xc0
#define CMD_SET_TRIGGER_MASK_1     0xc4
#define CMD_SET_TRIGGER_MASK_2     0xc8
#define CMD_SET_TRIGGER_MASK_3     0xcc
#define CMD_SET_TRIGGER_VALUE_0    0xc1
#define CMD_SET_TRIGGER_VALUE_1    0xc5
#define CMD_SET_TRIGGER_VALUE_2    0xc9
#define CMD_SET_TRIGGER_VALUE_3    0xcd
#define CMD_SET_TRIGGER_CONFIG_0   0xc2
#define CMD_SET_TRIGGER_CONFIG_1   0xc6
#define CMD_SET_TRIGGER_CONFIG_2   0xca
#define CMD_SET_TRIGGER_CONFIG_3   0xce

/* Bitmasks for CMD_FLAGS */
#define FLAG_DEMUX                 0x01
#define FLAG_FILTER                0x02
#define FLAG_CHANNELGROUP_1        0x04
#define FLAG_CHANNELGROUP_2        0x08
#define FLAG_CHANNELGROUP_3        0x10
#define FLAG_CHANNELGROUP_4        0x20
#define FLAG_CLOCK_EXTERNAL        0x40
#define FLAG_CLOCK_INVERTED        0x80
#define FLAG_RLE                   0x0100

/* Private, per-device-instance driver context. */
struct dev_context {
	uint32_t max_samplerate;
	uint32_t max_samples;
	uint32_t protocol_version;

	uint64_t cur_samplerate;
	uint32_t cur_samplerate_divider;
	uint64_t limit_samples;
	/* Current state of the flag register */
	uint32_t flag_reg;

	/* Pre/post trigger capture ratio, in percentage.
	 * 0 means no pre-trigger data. */
	int capture_ratio;
	int trigger_at;
	uint32_t probe_mask;
	uint32_t trigger_mask[4];
	uint32_t trigger_value[4];
	int num_stages;

	unsigned int num_transfers;
	unsigned int num_samples;
	int rle_count;
	int num_bytes;
	unsigned char sample[4];
	unsigned char tmp_sample[4];
	unsigned char *raw_sample_buf;

	struct sr_serial_dev_inst *serial;
};

#endif
