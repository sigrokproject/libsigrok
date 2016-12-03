/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 George Hopkins <george-hopkins@null.net>
 * Copyright (C) 2016 Matthieu Guillaumin <matthieu@guillaum.in>
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

#ifndef LIBSIGROK_HARDWARE_PCE_322A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PCE_322A_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "pce-322a"

#define BUFFER_SIZE 13
#define MEM_USAGE_BUFFER_SIZE 9
#define MEM_DATA_BUFFER_SIZE 11

enum {
	CMD_CONNECT = 0xacff,
	CMD_DISCONNECT = 0xcaff,
	CMD_TOGGLE_WEIGHT_FREQ = 0xaaf1,
	CMD_TOGGLE_MEAS_RANGE = 0xaaf2,
	CMD_TOGGLE_HOLD_MAX_MIN = 0xaaf3,
	CMD_TOGGLE_WEIGHT_TIME = 0xaaf4,
	CMD_TOGGLE_HOLD = 0xaaf5,
	CMD_TOGGLE_BACKLIGHT = 0xaaf6,
	CMD_TOGGLE_DATE_TIME = 0xaaf7,
	CMD_LOG_START = 0x7e00,
	CMD_MEMORY_STATUS = 0xadda,
	CMD_MEMORY_TRANSFER = 0xd3da,
	CMD_MEMORY_CLEAR = 0xaac1,
	CMD_POWER_OFF = 0xaaf8,
};

enum {
	MEAS_RANGE_30_130 = 0,
	MEAS_RANGE_30_80 = 1,
	MEAS_RANGE_50_100 = 2,
	MEAS_RANGE_80_130 = 3,
};

enum {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

enum {
	MEM_STATE_REQUEST_MEMORY_USAGE,
	MEM_STATE_GET_MEMORY_USAGE,
	MEM_STATE_REQUEST_MEMORY_BLOCK,
	MEM_STATE_GET_MEMORY_BLOCK,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	uint64_t cur_mqflags;
	uint8_t cur_meas_range;

	/* Acquisition settings */
	uint8_t cur_data_source;
	uint64_t limit_samples;

	/* Operational state */
	uint64_t num_samples;

	/* Memory reading state */
	uint8_t memory_state; /* State for requesting memory usage before memory blocks. */
	uint16_t memory_block_usage; /* Store number of memory blocks used. */
	uint8_t memory_last_block_usage; /* Store size of last memory block. */
	uint16_t memory_block_counter; /* Number of memory blocks retrieved so far. */
	uint8_t memory_block_cursor; /* Number of bytes retrieved in current memory block. */

	/* Temporary state across callbacks. */
	uint8_t buffer[BUFFER_SIZE];
	int buffer_len;
	int buffer_skip; /* Number of bytes to skip in memory mode. */
};

SR_PRIV int pce_322a_connect(const struct sr_dev_inst *sdi);
SR_PRIV int pce_322a_disconnect(const struct sr_dev_inst *sdi);
SR_PRIV int pce_322a_memory_status(const struct sr_dev_inst *sdi);
SR_PRIV int pce_322a_memory_clear(const struct sr_dev_inst *sdi);
SR_PRIV int pce_322a_memory_block(const struct sr_dev_inst *sdi, uint16_t memblk);
SR_PRIV int pce_322a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV uint64_t pce_322a_weight_freq_get(const struct sr_dev_inst *sdi);
SR_PRIV int pce_322a_weight_freq_set(const struct sr_dev_inst *sdi, uint64_t freqw);
SR_PRIV uint64_t pce_322a_weight_time_get(const struct sr_dev_inst *sdi);
SR_PRIV int pce_322a_weight_time_set(const struct sr_dev_inst *sdi, uint64_t timew);
SR_PRIV int pce_322a_meas_range_get(const struct sr_dev_inst *sdi,
		uint64_t *low, uint64_t *high);
SR_PRIV int pce_322a_meas_range_set(const struct sr_dev_inst *sdi,
		uint64_t low, uint64_t high);
SR_PRIV int pce_322a_power_off(const struct sr_dev_inst *sdi);

#endif
