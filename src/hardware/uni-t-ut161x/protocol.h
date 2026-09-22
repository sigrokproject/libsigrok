/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Jochen Rueter <jo.rueter@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_UNI_T_UT161X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_UT161X_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-ut161x"

/*
 * Frame layout, same in both directions:
 *   @0, length 2, magic 0xab 0xcd
 *   @2, length 1, number of bytes that follow (payload plus checksum)
 *   @3, length N, payload
 *   @3+N, length 2, checksum, big endian sum over all preceeding bytes
 */
#define FRAME_MAGIC_0		0xab
#define FRAME_MAGIC_1		0xcd
#define FRAME_HEAD_LEN		3
#define FRAME_CSUM_LEN		2
#define FRAME_MAX_LEN		64

/* Requests from the host to the meter. */
enum ut161x_cmd {
	CMD_MIN_MAX = 0x41,
	CMD_EXIT_MIN_MAX = 0x42,
	CMD_RANGE = 0x46,
	CMD_AUTO_RANGE = 0x47,
	CMD_REL = 0x48,
	CMD_SELECT2 = 0x49,
	CMD_HOLD = 0x4a,
	CMD_LAMP = 0x4b,
	CMD_SELECT1 = 0x4c,
	CMD_PEAK_MIN_MAX = 0x4d,
	CMD_EXIT_PEAK = 0x4e,
	CMD_GET_MEASUREMENT = 0x5e,
	CMD_GET_NAME = 0x5f,
};

/* Measurement payload, offsets within the payload. */
#define MEAS_PAYLOAD_LEN	14
#define MEAS_OFF_MODE		0
#define MEAS_OFF_RANGE		1
#define MEAS_OFF_DISPLAY	2
#define MEAS_DISPLAY_LEN	7
#define MEAS_OFF_FLAGS1		11	/* MAX, MIN, HOLD, REL */
#define MEAS_OFF_FLAGS2		12	/* !AUTO, battery, HV warning */
#define MEAS_OFF_FLAGS3		13	/* DC, peak max, peak min, bar polarity */

#define FLAG1_MAX		(1 << 3)
#define FLAG1_MIN		(1 << 2)
#define FLAG1_HOLD		(1 << 1)
#define FLAG1_REL		(1 << 0)
#define FLAG2_MANUAL		(1 << 2)
#define FLAG2_LOW_BATT		(1 << 1)
#define FLAG2_HV_WARN		(1 << 0)
#define FLAG3_DC		(1 << 3)
#define FLAG3_PEAK_MAX		(1 << 2)
#define FLAG3_PEAK_MIN		(1 << 1)

/* Poll timing. */
#define REQ_TIMEOUT_MS		1000
#define POLL_INTERVAL_MS	10

struct dev_context {
	struct sr_sw_limits limits;
	uint8_t buf[FRAME_MAX_LEN * 2];
	size_t buf_len;
	gboolean req_pending;
	int64_t req_sent_us;
};

SR_PRIV int ut161x_send_cmd(struct sr_serial_dev_inst *serial, uint8_t cmd);
SR_PRIV gboolean ut161x_get_frame(struct dev_context *devc,
	uint8_t *payload, size_t *payload_len);
SR_PRIV int ut161x_read_input(struct sr_serial_dev_inst *serial,
	struct dev_context *devc);
SR_PRIV int uni_t_ut161x_receive_data(int fd, int revents, void *cb_data);

#endif
