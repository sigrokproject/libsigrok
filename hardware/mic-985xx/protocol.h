/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_HARDWARE_MIC_985XX_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MIC_985XX_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "mic-985xx: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

/* Note: When adding entries here, don't forget to update MIC_DEV_COUNT. */
enum {
	MIC_98583,
};

#define MIC_DEV_COUNT 1

struct mic_dev_info {
	char *vendor;
	char *device;
	char *conn;
	uint32_t max_sample_points;
	gboolean has_temperature;
	gboolean has_humidity;
	struct sr_dev_driver *di;
	int (*receive_data)(int, int, void *);
};

extern SR_PRIV const struct mic_dev_info mic_devs[MIC_DEV_COUNT];

#define SERIAL_BUFSIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received samples. */
	uint64_t num_samples;

	int64_t starttime;

	struct sr_serial_dev_inst *serial;

	uint8_t buf[SERIAL_BUFSIZE];
	int bufoffset;
	int buflen;
};

SR_PRIV int receive_data_MIC_98583(int fd, int revents, void *cb_data);

SR_PRIV int mic_cmd_get_device_info(struct sr_serial_dev_inst *serial);

#endif
