/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024-2026 PXView Developers <913461865@qq.com>
 * Copyright (C) 2026 dogtopus <dogtopus@users.noreply.github.com>
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

#ifndef LIBSIGROK_HARDWARE_PX_LOGIC_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PX_LOGIC_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "px-logic"

#define VREF_MIN 0.1
#define VREF_MAX 6.0
#define VREF_STEP 0.1
#define VREF_DEFAULT 2.0

#define NUM_PWM_CHANNELS 2

enum device_variant {
	VARIANT_UNKNOWN = -1,
	VARIANT_32,
	VARIANT_16_PRO,
	VARIANT_16_PLUS,
	VARIANT_16_BASE,
	VARIANT_MAX,
};

enum clk_config {
	CLK_1GHZ,
	CLK_500MHZ,
	CLK_250MHZ,
	CLK_125MHZ,
	CLK_800MHZ,
	CLK_400MHZ,
	CLK_200MHZ,
	CLK_100MHZ,
	CLK_NUM_SUPPORTED,
};

enum ext_trigger_slope_config {
	EXT_TRIG_OFF,
	EXT_TRIG_RISING,
	EXT_TRIG_HIGH,
	EXT_TRIG_FALLING,
	EXT_TRIG_LOW,
	EXT_TRIG_EDGE,
	EXT_TRIG_MAX,
};

/**
 * Capture context state.
 */
enum cap_state {
	/**
	 * Capture context is initializing. All the resources owned by the
	 * capture context may or may not already been initialized.
	 */
	CAP_STATE_INIT,
	/**
	 * Capture context is fully initialized and is waiting for trigger.
	 */
	CAP_STATE_WAIT_TRIGGER,
	/**
	 * Capture context is receiving samples from the device.
	 */
	CAP_STATE_SAMPLE_XFER,
	/**
	 * Capture context is being signaled to terminate. Cancelling all
	 * transfers.
	 */
	CAP_STATE_HALT,
	/**
	 * Capture context is terminated, all transfers have been cancelled and
	 * the resoources owned by the capture context must now be freed.
	 */
	CAP_STATE_CLEANUP,
};

struct dev_config {
	uint16_t vid;
	uint16_t pid;
	enum libusb_speed speed;

	enum device_variant variant;
	uint64_t max_buffer_depth;
	/** Number of total channels the device supports (16 or 32). */
	uint8_t channels;
};

struct pwm_config {
	gboolean enabled;
	double freq;
	double duty;
};

struct channel_config {
	/**
	 * Total number of enabled channels.
	 */
	uint32_t n;
	/**
	 * Channel enablement mask (1=enabled, 0=disabled).
	 */
	uint32_t mask;
};

/**
 * Cached trigger configuration derived from sigrok device context.
 */
struct trigger_config {
	uint32_t high_mask;
	uint32_t low_mask;
	uint32_t rising_mask;
	uint32_t falling_mask;
};

struct capture_ctx {
	/**
	 * State.
	 */
	enum cap_state state;
	/**
	 * Where the trigger line should be drawn, in samples.
	 */
	uint32_t trigger_point_real;
	/**
	 * Sample width of the sigrok logic packets for this session.
	 */
	uint8_t sample_width;
	/**
	 * Whether to skip drawing the trigger line.
	 */
	gboolean skip_trigger;

	/**
	 * Number of currently active transfers ((re-)submitted and waiting for
	 * data).
	 */
	uint8_t n_active_data_xfers;
	/**
	 * Dynamically allocated array of libusb transfers.
	 */
	struct libusb_transfer **data_xfers;
	/**
	 * Total number of received samples from the device.
	 */
	uint64_t samples_received;
	/**
	 * Total number of sample points processed by sigrok.
	 */
	uint64_t samples_sent;
	/**
	 * Device to driver monotonic sequence number.
	 */
	uint64_t recv_seq;
	/**
	 * Driver to sigrok monotonic sequence number.
	 */
	uint64_t send_seq;

	/**
	 * Data transpose buffer pool. Shall be allocated to fit all transposed
	 * sample points from all transfers.
	 */
	uint8_t *tr_buffer;
	/**
	 * Size of each individual transpose buffer.
	 */
	size_t tr_buffer_size;
	/**
	 * Thread pool running sample transpose tasks.
	 */
	GThreadPool *tr_workers;
	/**
	 * Output queue for libusb transfers containing finished sample points.
	 */
	GAsyncQueue *tr_out_queue;
};

struct dev_context {
	/* === Hardware configuration. === */

	struct dev_config config;

	/* === Capture properties. === */

	gboolean streaming;
	gboolean filter;
	gboolean invert_clock;
	double voltage_threshold;
	uint64_t samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;

	enum ext_trigger_slope_config ext_slope;
	struct trigger_config trigger;
	struct pwm_config pwm[NUM_PWM_CHANNELS];

	/* === Values derived from properties. === */

	/**
	 * Buffer size of USB transfers.
	 */
	uint32_t buf_size;
	struct channel_config channels;

	struct sr_channel_group *cg_logic;

	struct capture_ctx cap;
};

SR_PRIV enum device_variant
px_logic_probe_variant(struct libusb_device_handle *devhdl);

SR_PRIV int px_logic_probe_mcu(struct sr_context *sr_ctx,
			       struct libusb_device_handle *devhdl);

SR_PRIV int px_logic_probe_fpga(struct sr_context *sr_ctx,
				struct libusb_device_handle *devhdl,
				gboolean reprogram);

SR_PRIV int px_logic_dev_open(const struct sr_dev_inst *sdi);

SR_PRIV int px_logic_receive_config(const struct sr_dev_inst *sdi);

SR_PRIV int px_logic_send_config(const struct sr_dev_inst *sdi);

SR_PRIV int px_logic_send_config_pwm(const struct sr_dev_inst *sdi,
				     uint8_t channel);

SR_PRIV int px_logic_acquisition_start(const struct sr_dev_inst *sdi);

SR_PRIV int px_logic_acquisition_stop(const struct sr_dev_inst *sdi);

#endif
