/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
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

#ifndef LIBSIGROK_HARDWARE_ADALM_2000_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ADALM_2000_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "m2k_wrapper.h"

#define LOG_PREFIX "adalm-2000"

/* Maximum possible input channels */
#define NUM_CHANNELS			16
/* Samples limit */
#define MIN_SAMPLES 16
#define MAX_SAMPLES 5000000

enum m2k_trigger_digital {
	RISING_EDGE_DIGITAL = 0,
	FALLING_EDGE_DIGITAL = 1,
	LOW_LEVEL_DIGITAL = 2,
	HIGH_LEVEL_DIGITAL = 3,
	ANY_EDGE_DIGITAL = 4,
	NO_TRIGGER_DIGITAL = 5,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* m2k wrapper to deal with libm2k */
	m2k_wrapper_t *m2k;

	/* Acquisition settings */
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint32_t triggerflags;
	uint64_t capture_ratio;

	/* channels */
	uint16_t chan_en;

	uint64_t bytes_read;
	uint64_t sent_samples;
	uint16_t *sample_buf;	/* mmap'd kernel buffer here */
};

SR_PRIV int adalm_2000_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int adalm_2000_receive_data(int fd, int revents, void *cb_data);

#endif
