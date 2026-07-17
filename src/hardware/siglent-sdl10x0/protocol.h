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

/*
 * Operating modes.
 */
enum siglent_sdl10x0_modes {
	CC = 0,
	CV = 1,
	CP = 2,
	CR = 3,
	LED = 4,
	SDL10x0_MODES, /* Total count, for internal use. */
};

/*
 * Possible states in an acquisition.
 */
enum acquisition_state {
	ACQ_REQUESTED_VOLTAGE,
	ACQ_REQUESTED_CURRENT,
	ACQ_REQUESTED_POWER,
	ACQ_REQUESTED_RESISTANCE,
};

struct dev_context {
	struct sr_sw_limits limits;
	enum acquisition_state acq_state;
	float voltage;
	float current;
	double maxpower;
};

SR_PRIV const char *siglent_sdl10x0_mode_to_string(enum siglent_sdl10x0_modes mode);
SR_PRIV const char *siglent_sdl10x0_mode_to_longstring(enum siglent_sdl10x0_modes mode);
SR_PRIV int siglent_sdl10x0_string_to_mode(const char *modename, enum siglent_sdl10x0_modes *mode);

SR_PRIV void siglent_sdl10x0_send_value(const struct sr_dev_inst *sdi, float value, enum sr_mq mq, enum sr_mqflag mqflags, enum sr_unit unit, int digits);

SR_PRIV int siglent_sdl10x0_receive_data(struct sr_dev_inst *sdi);
SR_PRIV int siglent_sdl10x0_handle_events(int fd, int revents, void *cb_data);

#endif
