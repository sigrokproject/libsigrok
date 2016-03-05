/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Andreas Zschunke <andreasz77@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_HANTEK_4032L_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_4032L_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hantek-4032l"

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */

	/* Acquisition settings */

	/* Operational state */

	/* Temporary state across callbacks */

};

SR_PRIV int hantek_4032l_receive_data(int fd, int revents, void *cb_data);

#endif
