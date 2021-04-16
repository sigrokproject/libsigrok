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

SR_PRIV int tlf_collect_channels(struct sr_dev_inst *sdi); // gets channel names from device
SR_PRIV int tlf_collect_samplerates(struct sr_dev_inst *sdi); // gets sample rates
SR_PRIV int tlf_set_samplerate(const struct sr_dev_inst *sdi, uint64_t sample_rate); // set sample rate
SR_PRIV int tlf_channel_state_set(const struct sr_dev_inst *sdi, int32_t channel_index, gboolean enabled); // set channel status
SR_PRIV int tiny_logic_friend_la_receive_data(int fd, int revents, void *cb_data);

extern uint64_t samplerates[3];


// extern uint64_t *samplerates; // sample rate storage: min, max, step size (all in Hz)
// extern char *channel_names[];  // channel names, start with default
// extern const int channel_count_max; // maximum number of channels
// extern int32_t channel_count; // initialize to 0

#endif
