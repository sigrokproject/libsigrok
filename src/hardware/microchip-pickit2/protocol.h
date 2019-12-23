/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_MICROCHIP_PICKIT2_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MICROCHIP_PICKIT2_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "microchip-pickit2"

#define PICKIT2_CHANNEL_COUNT	3
#define PICKIT2_SAMPLE_COUNT	1024
#define PICKIT2_SAMPLE_RAWLEN	(4 * 128)

enum pickit_state {
	STATE_IDLE,
	STATE_CONF,
	STATE_WAIT,
	STATE_DATA,
};

struct dev_context {
	enum pickit_state state;
	const uint64_t *samplerates;
	size_t num_samplerates;
	size_t curr_samplerate_idx;
	const uint64_t *captureratios;
	size_t num_captureratios;
	size_t curr_captureratio_idx;
	struct sr_sw_limits sw_limits;
	gboolean detached_kernel_driver;
	int32_t triggers[PICKIT2_CHANNEL_COUNT];	/**@< see @ref SR_TRIGGER_ZERO et al */
	size_t trigpos;
	uint8_t samples_raw[PICKIT2_SAMPLE_RAWLEN];
	uint8_t samples_conv[PICKIT2_SAMPLE_COUNT];
};

SR_PRIV int microchip_pickit2_setup_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int microchip_pickit2_receive_data(int fd, int revents, void *cb_data);

#endif
