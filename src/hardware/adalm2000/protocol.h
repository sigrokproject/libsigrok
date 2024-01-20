/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Analog Devices Inc.
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

#ifndef LIBSIGROK_HARDWARE_ADALM2000_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ADALM2000_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "libm2k.h"

#define LOG_PREFIX "adalm2000"

#define DEFAULT_NUM_LOGIC_CHANNELS               16
#define DEFAULT_NUM_ANALOG_CHANNELS               2
#define MAX_NEG_DELAY                         -8192

struct dev_context {
	struct M2k *m2k;

	uint64_t start_time;
	int64_t spent_us;
	uint64_t limit_msec;
	uint64_t limit_frames;
	uint64_t limit_samples;
	uint64_t sent_samples;
	uint64_t buffersize;
	uint32_t logic_unitsize;
	gboolean avg;
	uint64_t avg_samples;

	struct sr_datafeed_analog packet;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
};

SR_PRIV int adalm2000_nb_enabled_channels(const struct sr_dev_inst *sdi, int type);

SR_PRIV int adalm2000_convert_trigger(const struct sr_dev_inst *sdi);

SR_PRIV int adalm2000_receive_data(int fd, int revents, void *cb_data);

#endif
