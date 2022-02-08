/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_ZEROPLUS_LOGIC_CUBE_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ZEROPLUS_LOGIC_CUBE_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "zeroplus-logic-cube"

typedef enum {
	LAPC_CLOCK_EDGE_RISING,
	LAPC_CLOCK_EDGE_FALLING,
} ext_clock_edge_t;

struct zp_model;
struct dev_context {
	uint64_t cur_samplerate;
	uint64_t max_samplerate;
	uint64_t limit_samples;
	size_t num_channels;
	size_t memory_size;
	size_t max_sample_depth;
	int trigger;
	uint64_t capture_ratio;
	double cur_threshold;
	const struct zp_model *prof;
	gboolean use_ext_clock;
	ext_clock_edge_t ext_clock_edge;
};

SR_PRIV size_t get_memory_size(int type);
SR_PRIV int zp_set_samplerate(struct dev_context *devc, uint64_t samplerate);
SR_PRIV int set_limit_samples(struct dev_context *devc, uint64_t samples);
SR_PRIV int set_voltage_threshold(struct dev_context *devc, double thresh);
SR_PRIV void set_triggerbar(struct dev_context *devc);

#endif
