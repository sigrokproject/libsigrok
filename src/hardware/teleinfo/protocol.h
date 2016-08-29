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

#ifndef LIBSIGROK_HARDWARE_TELEINFO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TELEINFO_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "teleinfo"

enum optarif {
	OPTARIF_NONE,
	OPTARIF_BASE,
	OPTARIF_HC,
	OPTARIF_EJP,
	OPTARIF_BBR,
};

#define TELEINFO_BUF_SIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Acquisition settings */
	struct sr_sw_limits sw_limits;

	/* Operational state */
	enum optarif optarif; /**< The device mode (which measures are reported) */

	/* Temporary state across callbacks */
	uint8_t buf[TELEINFO_BUF_SIZE];
	int buf_len;
};

SR_PRIV gboolean teleinfo_packet_valid(const uint8_t *buf);
SR_PRIV int teleinfo_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int teleinfo_get_optarif(const uint8_t *buf);

#endif
