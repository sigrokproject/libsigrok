/*
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

#ifndef LIBSIGROK_HARDWARE_VIRTUAL_PROTOCOL_H
#define LIBSIGROK_HARDWARE_VIRTUAL_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "virtual"

struct dev_context {
	/** FIFO filename */
	int fd;
	// TODO: add configurations
	// uint64_t cur_samplerate; // SR_CONF_SAMPLERATE
	// int32_t num_logic_channels; // SR_CONF_NUM_LOGIC_CHANNELS
	// int32_t num_analog_channels; // SR_CONF_NUM_ANALOG_CHANNELS
	// size_t enabled_logic_channels; // SR_CONF_ENABLED?
	// size_t enabled_analog_channels; // SR_CONF_ENABLED?
};

SR_PRIV int virtual_receive_data(int fd, int revents, void *cb_data);

#endif
