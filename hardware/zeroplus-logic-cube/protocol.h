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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_HARDWARE_ZEROPLUS_LOGIC_CUBE_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ZEROPLUS_LOGIC_CUBE_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "analyzer.h"

#define LOG_PREFIX "zeroplus"

/* Private, per-device-instance driver context. */
struct dev_context {
	uint64_t cur_samplerate;
	uint64_t max_samplerate;
	uint64_t limit_samples;
	int num_channels;
	int memory_size;
	unsigned int max_sample_depth;
	//uint8_t probe_mask;
	//uint8_t trigger_mask[NUM_TRIGGER_STAGES];
	//uint8_t trigger_value[NUM_TRIGGER_STAGES];
	// uint8_t trigger_buffer[NUM_TRIGGER_STAGES];
	int trigger;
	unsigned int capture_ratio;
	const struct zp_model *prof;
};

SR_PRIV unsigned int get_memory_size(int type);
SR_PRIV int zp_set_samplerate(struct dev_context *devc, uint64_t samplerate);
SR_PRIV int set_limit_samples(struct dev_context *devc, uint64_t samples);
SR_PRIV int set_capture_ratio(struct dev_context *devc, uint64_t ratio);
SR_PRIV void set_triggerbar(struct dev_context *devc);

#endif
