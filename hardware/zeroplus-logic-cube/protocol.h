/*
 * This file is part of the sigrok project.
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

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "zeroplus: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

/* Private, per-device-instance driver context. */
struct dev_context {
	uint64_t cur_samplerate;
	uint64_t max_samplerate;
	uint64_t limit_samples;
	int num_channels;
	int memory_size;
	unsigned int max_memory_size;
	//uint8_t probe_mask;
	//uint8_t trigger_mask[NUM_TRIGGER_STAGES];
	//uint8_t trigger_value[NUM_TRIGGER_STAGES];
	// uint8_t trigger_buffer[NUM_TRIGGER_STAGES];
	int trigger;
	unsigned int capture_ratio;
	struct sr_usb_dev_inst *usb;
	const struct zp_model *prof;
};

extern const uint64_t zp_supported_samplerates_200[];

SR_PRIV unsigned int get_memory_size(int type);
SR_PRIV int zp_set_samplerate(struct dev_context *devc, uint64_t samplerate);
SR_PRIV int set_limit_samples(struct dev_context *devc, uint64_t samples);
SR_PRIV int set_capture_ratio(struct dev_context *devc, uint64_t ratio);
SR_PRIV void set_triggerbar(struct dev_context *devc);

#endif
