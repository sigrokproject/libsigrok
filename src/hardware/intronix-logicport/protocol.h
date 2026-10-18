/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Miguel Angel Ajo Pelayo <miguelangel@ajo.es>
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

#ifndef LIBSIGROK_HARDWARE_INTRONIX_LOGICPORT_PROTOCOL_H
#define LIBSIGROK_HARDWARE_INTRONIX_LOGICPORT_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <ftdi.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "intronix-logicport"

#define LOGICPORT_USB_VENDOR		0x0403
#define LOGICPORT_USB_PRODUCT		0xdc48
#define LOGICPORT_USB_DESC		"LogicPort Logic Analyzer"

/* D0..D31, CLK1, CLK2. Bit k of a sample is channel k. */
#define LOGICPORT_NUM_CHANNELS		34
#define LOGICPORT_ALL_CHANNELS		((UINT64_C(1) << LOGICPORT_NUM_CHANNELS) - 1)
#define LOGICPORT_UNITSIZE		5
#define LOGICPORT_MEM_DEPTH		2048

/* Channel indices of the two clock inputs. */
#define LOGICPORT_CH_CLK1		32
#define LOGICPORT_CH_CLK2		33

/* The threshold DAC covers -6 V .. +6 V in 50 mV steps. */
#define LOGICPORT_THRESHOLD_MIN		-6.0
#define LOGICPORT_THRESHOLD_MAX		6.0
#define LOGICPORT_THRESHOLD_STEP	0.05

/* Compression (RLE) is only available up to this samplerate. */
#define LOGICPORT_RLE_MAX_RATE		SR_MHZ(200)

/* Upper bound for RLE expansion when no sample limit was set. */
#define LOGICPORT_RLE_DEFAULT_LIMIT	(16 * 1024 * 1024)

/*
 * FPGA configuration images, one per sampling mode. The loaded image's
 * index can be read back from the FPGA. In state mode, the image fixes
 * where data is sampled relative to the external clock edge (2.5ns
 * before, on, or after it); the "before" images also fix clock and edge.
 */
enum logicport_image {
	IMAGE_STATE_AFTER = 0,
	IMAGE_STATE_BEFORE_CLK1_RISE = 1,
	IMAGE_STATE_BEFORE_CLK2_RISE = 2,
	IMAGE_STATE_BEFORE_CLK1_FALL = 3,
	IMAGE_STATE_BEFORE_CLK2_FALL = 4,
	IMAGE_STATE_ON = 5,
	IMAGE_TIMING_DDR = 6,
	IMAGE_TIMING_SDR = 7,
	IMAGE_COUNT,
};

/* Samplerate table entry: prescaler (c) and 1-2-5 step (d) registers. */
struct logicport_rate {
	uint64_t samplerate;
	gboolean ddr;
	uint8_t prescaler;
	uint8_t step;
};

/*
 * One of the two hardware trigger levels (A, B). Masks have one bit per
 * channel. Only patterns and edges are used, sigrok triggers have no
 * occurrence counts, durations or value ranges.
 */
struct logicport_trigger_level {
	uint64_t pattern_mask;		/* Channels taking part in the pattern. */
	uint64_t pattern_value;		/* Their required levels. */
	uint64_t rising_mask;
	uint64_t falling_mask;
	uint8_t condition;		/* Pattern true / false / unused. */
	uint8_t counter_source;		/* Event counted, e.g. edges. */
	uint8_t counter_mode;		/* How the count is evaluated. */
};

struct dev_context {
	struct ftdi_context *ftdic;
	uint8_t usb_bus;
	uint8_t usb_address;

	/* Register protocol state. */
	uint16_t seq;			/* Expected sequence number of the next ack. */
	int segment;			/* Selected address segment, -1: unknown. */
	gboolean io_error;		/* Link out of step, cleared by a resync. */
	int fpga_image;			/* Loaded image, -1: unknown or none. */
	int threshold_cal;		/* DAC offset, from the EEPROM. */
	int threshold_dac;		/* Last DAC value written, -1: unknown. */

	/* User settings. */
	const struct logicport_rate *rate;
	uint64_t limit_samples;
	uint64_t capture_ratio;
	double threshold;
	gboolean rle;
	gboolean external_clock;
	int clock_source;
	int clock_edge;

	/* Acquisition state. */
	gboolean acq_running;
	/* RLE only: time limits for the pre-/post-trigger fill phases. */
	gint64 acq_start;
	gint64 trigger_seen;
	gint64 prefill_us;
	gint64 postfill_us;
	gboolean prefill_ended;
	gboolean postfill_ended;
	/* Trigger levels A and B, and how they combine. */
	gboolean trigger_used;
	struct logicport_trigger_level trigger[2];
	uint8_t trigger_combine;
	/* Sample memory readout: lanes D0-7 .. D24-31, then the flags lane. */
	uint8_t raw[5][LOGICPORT_MEM_DEPTH];
};

extern const struct logicport_rate logicport_rates[];
extern const size_t logicport_rates_count;

SR_PRIV int logicport_open(struct dev_context *devc);
SR_PRIV void logicport_close(struct dev_context *devc);
SR_PRIV int logicport_resync(struct dev_context *devc, gboolean reopen,
	int attempts);
SR_PRIV int logicport_probe_image(struct dev_context *devc);
SR_PRIV int logicport_read_calibration(struct dev_context *devc);
SR_PRIV int logicport_configure_fpga(struct sr_context *ctx,
	struct dev_context *devc, int image);
SR_PRIV int logicport_select_image(const struct dev_context *devc);
SR_PRIV int logicport_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int logicport_program(struct dev_context *devc);
SR_PRIV int logicport_start(struct dev_context *devc);
SR_PRIV int logicport_halt(struct dev_context *devc);
SR_PRIV int logicport_receive_data(int fd, int revents, void *cb_data);

#endif
