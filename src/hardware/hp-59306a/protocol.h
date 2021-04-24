/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Frank Stettner <frank-stettner@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_HP_59306A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HP_59306A_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hp-59306a"

struct dev_context {
	size_t channel_count;
};

struct channel_group_context {
	/** The number of the channel group, as labled on the device. */
	size_t number;
};

SR_PRIV int hp_59306a_switch_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean enabled);

#endif
