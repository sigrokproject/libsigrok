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

#ifndef LIBSIGROK_HARDWARE_CEM_DT_885X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_CEM_DT_885X_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "cem-dt-885x"

/* When retrieving samples from device memory, group this many
 * together into a sigrok packet. */
#define SAMPLES_PER_PACKET 50

/* Various temporary storage, at least 8 bytes. */
#define BUF_SIZE (SAMPLES_PER_PACKET * 2)

/* When in hold mode, force the last measurement out at this interval.
 * We're using 50ms, which duplicates the non-hold 20Hz update rate. */
#define HOLD_REPEAT_INTERVAL (50 * 1000)

enum {
	TOKEN_WEIGHT_TIME_FAST = 0x02,
	TOKEN_WEIGHT_TIME_SLOW = 0x03,
	TOKEN_HOLD_MAX = 0x04,
	TOKEN_HOLD_MIN = 0x05,
	TOKEN_TIME = 0x06,
	TOKEN_MEAS_RANGE_OVER = 0x07,
	TOKEN_MEAS_RANGE_UNDER = 0x08,
	TOKEN_STORE_FULL = 0x09,
	TOKEN_RECORDING_ON = 0x0a,
	TOKEN_MEAS_WAS_READOUT = 0x0b,
	TOKEN_MEAS_WAS_BARGRAPH = 0x0c,
	TOKEN_MEASUREMENT = 0xd,
	TOKEN_HOLD_NONE = 0x0e,
	TOKEN_BATTERY_LOW = 0x0f,
	TOKEN_MEAS_RANGE_OK = 0x11,
	TOKEN_STORE_OK = 0x19,
	TOKEN_RECORDING_OFF = 0x1a,
	TOKEN_WEIGHT_FREQ_A = 0x1b,
	TOKEN_WEIGHT_FREQ_C = 0x1c,
	TOKEN_BATTERY_OK = 0x1f,
	TOKEN_MEAS_RANGE_30_80 = 0x30,
	TOKEN_MEAS_RANGE_30_130 = 0x40,
	TOKEN_MEAS_RANGE_50_100 = 0x4b,
	TOKEN_MEAS_RANGE_80_130 = 0x4c,
};

enum {
	CMD_TOGGLE_RECORDING = 0x55,
	CMD_TOGGLE_WEIGHT_FREQ = 0x99,
	CMD_TOGGLE_WEIGHT_TIME = 0x77,
	CMD_TOGGLE_HOLD_MAX_MIN = 0x11,
	CMD_TOGGLE_MEAS_RANGE = 0x88,
	CMD_TOGGLE_POWER_OFF = 0x33,
	CMD_TRANSFER_MEMORY = 0xac,
};

enum {
	RECORD_DBA = 0xaa,
	RECORD_DBC = 0xcc,
	RECORD_DATA = 0xac,
	RECORD_END = 0xdd,
};

enum {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

struct dev_context {
	enum sr_mqflag cur_mqflags;
	int recording;
	int cur_meas_range;
	int cur_data_source;

	uint64_t limit_samples;

	int state;
	uint64_t num_samples;
	gboolean enable_data_source_memory;

	unsigned char cmd;
	unsigned char token;
	int buf_len;
	unsigned char buf[BUF_SIZE];
	float last_spl;
	gint64 hold_last_sent;
};

/* Parser state machine. */
enum {
	ST_INIT,
	ST_GET_TOKEN,
	ST_GET_DATA,
	ST_GET_LOG_HEADER,
	ST_GET_LOG_RECORD_META,
	ST_GET_LOG_RECORD_DATA,
};

SR_PRIV int cem_dt_885x_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int cem_dt_885x_recording_set(const struct sr_dev_inst *sdi, gboolean start);
SR_PRIV gboolean cem_dt_885x_recording_get(const struct sr_dev_inst *sdi,
		int *state);
SR_PRIV int cem_dt_885x_weight_freq_get(const struct sr_dev_inst *sdi);
SR_PRIV int cem_dt_885x_weight_freq_set(const struct sr_dev_inst *sdi, int freqw);
SR_PRIV int cem_dt_885x_weight_time_get(const struct sr_dev_inst *sdi);
SR_PRIV int cem_dt_885x_weight_time_set(const struct sr_dev_inst *sdi, int timew);
SR_PRIV int cem_dt_885x_holdmode_get(const struct sr_dev_inst *sdi,
		gboolean *holdmode);
SR_PRIV int cem_dt_885x_holdmode_set(const struct sr_dev_inst *sdi, int holdmode);
SR_PRIV int cem_dt_885x_meas_range_get(const struct sr_dev_inst *sdi,
		uint64_t *low, uint64_t *high);
SR_PRIV int cem_dt_885x_meas_range_set(const struct sr_dev_inst *sdi,
		uint64_t low, uint64_t high);
SR_PRIV int cem_dt_885x_power_off(const struct sr_dev_inst *sdi);

#endif
