/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef LIBSIGROK_HARDWARE_BRYMEN_BM86X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_BRYMEN_BM86X_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "brymen-bm86x"

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Acquisition settings */
	uint64_t limit_samples;    /**< The sampling limit (in number of samples).*/
	uint64_t limit_msec;       /**< The time limit (in milliseconds). */
	void *session_cb_data;     /**< Opaque pointer passed in by the frontend. */

	/* Operational state */
	int detached_kernel_driver;/**< Whether kernel driver was detached or not */
	uint64_t num_samples;      /**< The number of already received samples. */
	int64_t start_time;        /**< The time at which sampling started. */

	/* Temporary state across callbacks */
	int interrupt_pending;
};

SR_PRIV int brymen_bm86x_receive_data(int fd, int revents, void *cb_data);

#endif
