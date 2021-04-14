/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Kevin Matocha <kmatocha@icloud.com>
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

#ifndef LIBSIGROK_HARDWARE_TINY_LOGIC_FRIEND_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TINY_LOGIC_FRIEND_LA_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

// struct rs_sme0x_info {
// 	struct sr_dev_driver di;
// 	char *vendor;
// 	char *device;
// };

// struct rs_device_model {
// 	const char *model_str;
// 	double freq_max;
// 	double freq_min;
// 	double power_max;
// 	double power_min;
// };

struct dev_context {
	const struct tlf_device_model *model_config;
};

#define LOG_PREFIX "tiny-logic-friend-la"

SR_PRIV int tlf_collect_channels(const struct sr_dev_inst *sdi); // gets channel names from device
SR_PRIV int tiny_logic_friend_la_receive_data(int fd, int revents, void *cb_data);

#endif
