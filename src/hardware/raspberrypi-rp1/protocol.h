/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Thomas Griffiths <thomas@tgriffiths.dev>
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

#ifndef LIBSIGROK_HARDWARE_RASPBERRYPI_RP1_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RASPBERRYPI_RP1_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <sys/eventfd.h>

#define LOG_PREFIX "raspberrypi-rp1"
#define PIO_DEV_NODE "/dev/pio0"

#define MAX_GPIOS 28
#define MAX_SMS 4

#define PIO_CLK_HZ 200000000ULL
#define PIO_TICK_PER_CLK 256
#define PIO_TICK_HZ (PIO_CLK_HZ * PIO_TICK_PER_CLK)
#define PIO_STEP_MIN PIO_TICK_PER_CLK
#define PIO_STEP_MAX (65536u * PIO_TICK_PER_CLK)

#define NO_TICK (~(uint64_t)0)

#define PIO_DMA_HEAVY 50000000u
#define PIO_DMA_LIGHT (PIO_DMA_HEAVY >> 1)
#define PIO_DMA_HEAVY_COUNT 2

#define BUFFER_FILL_TIME_MS 10
#define PER_BUFFER_FILL_TIME(s) ((s)*BUFFER_FILL_TIME_MS / 1000)

enum pin_layout_id {
	PIN_LAYOUT_12_5MHZ = 0,
	PIN_LAYOUT_25MHZ,
	PIN_LAYOUT_50MHZ,
	PIN_LAYOUT_100MHZ,
	PIN_LAYOUT_200MHZ,
	NUM_PIN_LAYOUTS
};

struct pin_layout {
	int8_t pins[MAX_SMS];
	int8_t total_width;
	uint32_t max_freq;
};

static const struct pin_layout sm_pin_layouts[] = {
	[PIN_LAYOUT_12_5MHZ] = { { 0, -1, -1, -1 }, 28, SR_KHZ(12500) },
	[PIN_LAYOUT_25MHZ] = { { 0, 16, -1, -1 }, 28, SR_MHZ(25) },
	[PIN_LAYOUT_50MHZ] = { { 0, 8, 16, 20 }, 24, SR_MHZ(50) },
	[PIN_LAYOUT_100MHZ] = { { 0, 4, 8, 10 }, 12, SR_MHZ(100) },
	[PIN_LAYOUT_200MHZ] = { { 0, 2, 4, 5 }, 6, SR_MHZ(200) },
};

struct la_sm {
	unsigned base;
	unsigned n;
	int claimed;

	uint32_t *buffer;
	size_t words;
	unsigned samples_per_word;
};

struct dev_context {
	int fd;
	uint64_t cur_samplerate;
	unsigned tick_step;
	uint64_t limit_samples;
	uint64_t capture_ratio;

	unsigned num_sms;
	struct la_sm sm[MAX_SMS];
	size_t samples_per_round;

	GMutex sm_lock;
	GCond sm_cond;
	gboolean buffers_full;

	uint64_t samples_sent;
	gint running;
	GThread *worker_thread;

	uint8_t *outbuf;
	size_t outbuf_cap;
	unsigned unitsize;

	struct soft_trigger_logic *stl;
	gboolean trigger_fired;
	gboolean ended;

	enum pin_layout_id selected_layout;

	int event_fd;
};

SR_PRIV unsigned pio_step_for_hz(double hz);
SR_PRIV double pio_step_hz(unsigned step);

SR_PRIV int la_build_sms(const struct sr_dev_inst *sdi);
SR_PRIV int la_start(const struct sr_dev_inst *sdi);
SR_PRIV void la_stop(struct dev_context *devc);
SR_PRIV int la_receive_data(int fd, int revents, void *cb_data);

#endif