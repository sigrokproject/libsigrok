/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 VK4TMZ
 *
 * Hantek 1008C support, based on independently captured USB transactions and
 * the public mfg92/hantek1008py initialization sequence.
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

#ifndef LIBSIGROK_HARDWARE_HANTEK_1008C_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_1008C_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hantek-1008c"

#define H1008C_USB_VID          0x0783
#define H1008C_USB_PID          0x5725
#define H1008C_USB_INTERFACE    0
#define H1008C_EP_OUT           0x02
#define H1008C_EP_IN            0x81
#define H1008C_USB_PACKET       64
#define H1008C_USB_TIMEOUT_MS   1000

#define H1008C_INIT_B0_DELAY_US       (700 * 1000)
#define H1008C_CAL_CAPTURE_DELAY_US   12400
#define H1008C_F6_DELAY_US            213200
#define H1008C_TRIGGERED_ARM_DELAY_US     0
#define H1008C_A5_POLL_DELAY_US       2000
#define H1008C_A5_READY_POLLS         100
#define H1008C_AUTO_TRIGGER_TIMEOUT_US (1870 * 1000)

#define H1008C_NUM_HW_CHANNELS  8
#define H1008C_TRIGGER_SOURCE_NONE UINT8_MAX
#define H1008C_DEFAULT_BASE_SAMPLERATE UINT64_C(2400000)
#define H1008C_A3_24MSPS        0x0f
#define H1008C_DEFAULT_RANGE    0x03

enum h1008c_trigger_slope {
	H1008C_TRIGGER_RISING = 0,
	H1008C_TRIGGER_FALLING,
};

enum h1008c_acquisition_mode {
	H1008C_MODE_TRIGGERED = 0,
	H1008C_MODE_ROLL,
	H1008C_MODE_SCAN,
};

struct dev_context {
	struct sr_sw_limits limits;
	uint64_t requested_limit_samples;
	uint64_t samplerate;
	uint64_t base_samplerate;
	uint8_t enabled_mask[H1008C_NUM_HW_CHANNELS];
	unsigned int enabled_count;
	unsigned int acquisition_width;
	gboolean running;
	uint64_t triggered_count;
	gboolean calibration_valid[H1008C_NUM_HW_CHANNELS];
	double calibration_zero_adc[H1008C_NUM_HW_CHANNELS];
	double calibration_volts_per_count[H1008C_NUM_HW_CHANNELS];
	uint8_t a3;
	uint8_t range_id;
	enum h1008c_acquisition_mode acquisition_mode;
	gboolean trigger_enabled;
	uint8_t trigger_source;
	enum h1008c_trigger_slope trigger_slope;
	double trigger_level_volts;
	uint16_t trigger_level_adc;
	gboolean triggered_armed;
	gboolean triggered_forced;
	gint64 triggered_arm_us;
	uint8_t scan_carry[2 * H1008C_NUM_HW_CHANNELS];
	size_t scan_carry_len;
	uint8_t roll_carry[2 * (H1008C_NUM_HW_CHANNELS + 1)];
	size_t roll_carry_len;
};

struct h1008c_rate {
	uint64_t samplerate;
	uint8_t a3;
	enum h1008c_acquisition_mode mode;
};

SR_PRIV unsigned int h1008c_acquisition_width(unsigned int count);
SR_PRIV unsigned int h1008c_rate_divisor(
		enum h1008c_acquisition_mode mode, unsigned int enabled_count);
SR_PRIV const char *h1008c_range_name(uint8_t range_id);
SR_PRIV int h1008c_range_id(const char *name);
SR_PRIV const char *h1008c_trigger_source_name(uint8_t source);
SR_PRIV int h1008c_trigger_source_id(const char *name);
SR_PRIV int h1008c_trigger_level_to_adc(double volts, double zero_adc,
		double volts_per_count, uint16_t *raw_adc);
SR_PRIV size_t h1008c_rate_count(enum h1008c_acquisition_mode mode);
SR_PRIV int h1008c_rate_get(enum h1008c_acquisition_mode mode,
		size_t index, struct h1008c_rate *rate);
SR_PRIV const struct h1008c_rate *h1008c_find_effective_rate(
		uint64_t samplerate, unsigned int divisor,
		enum h1008c_acquisition_mode mode);
SR_PRIV int h1008c_decode_stream_rows(const uint8_t *input, size_t input_len,
		unsigned int enabled_count, unsigned int trailing_words,
		uint8_t *carry, size_t *carry_len, size_t carry_capacity,
		float **samples, size_t *row_count);

SR_PRIV int h1008c_open(struct sr_dev_inst *sdi);
SR_PRIV int h1008c_close(struct sr_dev_inst *sdi);
SR_PRIV int h1008c_reopen(struct sr_dev_inst *sdi);
SR_PRIV int h1008c_startup(const struct sr_dev_inst *sdi, uint8_t selected_a3,
		uint8_t selected_range, unsigned int enabled_count,
		const uint8_t enabled_mask[H1008C_NUM_HW_CHANNELS]);
SR_PRIV int h1008c_acquire_triggered_frame(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count);
SR_PRIV int h1008c_abort_frame(const struct sr_dev_inst *sdi);
SR_PRIV int h1008c_start_roll(const struct sr_dev_inst *sdi, uint8_t a3);
SR_PRIV int h1008c_read_roll(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count);
SR_PRIV int h1008c_start_scan(const struct sr_dev_inst *sdi, uint8_t a3);
SR_PRIV int h1008c_read_scan(const struct sr_dev_inst *sdi,
		float **samples, size_t *sample_count);

#endif
