/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef LIBSIGROK_HARDWARE_APPA_55II_PROTOCOL_H
#define LIBSIGROK_HARDWARE_APPA_55II_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "appa-55ii"

#define APPA_55II_NUM_CHANNELS  2
#define APPA_55II_BUF_SIZE    (4 + 32 + 1)
#define DEFAULT_DATA_SOURCE   DATA_SOURCE_LIVE

enum {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Acquisition settings */
	uint64_t limit_samples;   /**< The sampling limit (in number of samples). */
	uint64_t limit_msec;      /**< The time limit (in milliseconds). */
	gboolean data_source;     /**< Whether to read live samples or memory */
	void *session_cb_data;    /**< Opaque pointer passed in by the frontend. */

	/* Operational state */
	uint64_t num_samples;     /**< The number of already received samples. */
	int64_t start_time;       /**< The time at which sampling started. */

	/* Temporary state across callbacks */
	uint8_t buf[APPA_55II_BUF_SIZE];
	unsigned int buf_len;
	uint8_t log_buf[64];
	unsigned int log_buf_len;
	unsigned int num_log_records;
};

SR_PRIV gboolean appa_55ii_packet_valid(const uint8_t *buf);
SR_PRIV int appa_55ii_receive_data(int fd, int revents, void *cb_data);

#endif
