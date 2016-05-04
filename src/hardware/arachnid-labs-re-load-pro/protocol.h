/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015-2016 Uwe Hermann <uwe@hermann-uwe.de>
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

#ifndef LIBSIGROK_HARDWARE_ARACHNID_LABS_RE_LOAD_PRO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ARACHNID_LABS_RE_LOAD_PRO_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "re-load-pro"

#define RELOADPRO_BUFSIZE 100

/** Private, per-device-instance driver context. */
struct dev_context {
	struct sr_sw_limits limits;
	uint8_t buf[RELOADPRO_BUFSIZE];
	int buflen;
	gboolean otp_active;
	gboolean uvc_active;
};

SR_PRIV int reloadpro_set_current_limit(const struct sr_dev_inst *sdi,
		float current);
SR_PRIV int reloadpro_set_on_off(const struct sr_dev_inst *sdi, gboolean on);
SR_PRIV int reloadpro_get_current_limit(const struct sr_dev_inst *sdi,
		float *current);
SR_PRIV int reloadpro_get_voltage_current(const struct sr_dev_inst *sdi,
		float *voltage, float *current);
SR_PRIV int reloadpro_receive_data(int fd, int revents, void *cb_data);

#endif
