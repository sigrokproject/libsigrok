/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Timo Boettcher <timo@timoboettcher.name>
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

#ifndef LIBSIGROK_HARDWARE_SIGLENT_SDL10X0_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SIGLENT_SDL10X0_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "siglent-sdl10x0"

struct dev_context {
};

SR_PRIV int siglent_sdl10x0_receive_data(int fd, int revents, void *cb_data);

#endif
