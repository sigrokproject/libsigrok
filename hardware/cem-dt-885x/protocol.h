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
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "cem-dt-885x: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

#define BUF_SIZE 32
/* When in hold mode, force the last measurement out at this interval.
 * We're using 50ms, which duplicates the non-hold 20Hz update rate. */
#define HOLD_REPEAT_INTERVAL 50 * 1000

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
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Device state */
	uint64_t cur_mqflags;
	int recording;

	/* Acquisition settings */
	uint64_t limit_samples;

	/* Operational state */
	int state;
	uint64_t num_samples;

	/* Temporary state across callbacks */
	void *cb_data;
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
	ST_GET_LOG,
};

SR_PRIV int cem_dt_885x_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int cem_dt_885x_recording_set(const struct sr_dev_inst *sdi, gboolean start);
SR_PRIV gboolean cem_dt_885x_recording_get(const struct sr_dev_inst *sdi);

#endif
