/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Karikay Sharma <sharma.kartik2107@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_PSLAB_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PSLAB_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "pslab"
#define NUM_ANALOG_CHANNELS 8

struct dev_context {
};

struct analog_channel {
	const char *name;

	int chosa;
};

struct channel_priv {

	int samples_in_buffer;

	int buffer_idx;

	int chosa;

	int gain;

	int programmable_gain_amplifier;

	int resolution;
};

SR_PRIV int pslab_receive_data(int fd, int revents, void *cb_data);

SR_PRIV struct dev_context *pslab_dev_new();

#endif
