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
    uint64_t cur_samplerate;
	uint64_t sent_samples;
	uint64_t sent_frame_samples; /* Number of samples that were sent for current frame. */
	int64_t start_us;
	int64_t spent_us;
	uint64_t step;
	/* Logic */
	int32_t num_logic_channels;
	size_t logic_unitsize;
	uint64_t all_logic_channels_mask;
	/* Analog */
	int32_t num_analog_channels;
	size_t enabled_logic_channels;
	size_t enabled_analog_channels;
	size_t first_partial_logic_index;
	uint8_t first_partial_logic_mask;
};

SR_PRIV int virtual_receive_data(int fd, int revents, void *cb_data);

#endif
