/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
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

#ifndef LIBSIGROK_HARDWARE_RIGOL_DS1XX2_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RIGOL_DS1XX2_PROTOCOL_H

#include <stdint.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "rigol-ds1xx2: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define WAVEFORM_SIZE 600

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current frame limit */
	uint64_t limit_frames;

	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received frames. */
	uint64_t num_frames;

	/** The current number of already received samples. */
	uint64_t num_samples;

	/** Current scale setting. */
	float scale;

	/** Current offset setting. */
	float offset;

	/** Path to USBTMC character device file. */
	char *device;

	/** USBTMC character device file descriptor. */
	int fd;

	GSList *enabled_probes;
};

SR_PRIV int rigol_ds1xx2_receive_data(int fd, int revents, void *cb_data);

SR_PRIV int rigol_ds1xx2_send_data(int fd, const char *format, ...);

#endif
