/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Florian Schmidt <schmidt_florian@gmx.de>
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#ifndef LIBSIGROK_HARDWARE_KINGST_LA2016_PROTOCOL_H
#define LIBSIGROK_HARDWARE_KINGST_LA2016_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "kingst-la2016"

/* device is little endian */

#define LA2016_VID		0x77a1
#define LA2016_PID		0x01a2
#define USB_INTERFACE		0

#define LA2016_BULK_MAX         8388608

#define MAX_RENUM_DELAY_MS	3000
#define DEFAULT_TIMEOUT_MS      200

#define LA2016_THR_VOLTAGE_MIN  0.40
#define LA2016_THR_VOLTAGE_MAX  4.00

#define LA2016_NUM_SAMPLES_MIN  256
#define LA2016_NUM_SAMPLES_MAX  (10UL * 1000 * 1000 * 1000)

typedef struct pwm_setting_dev {
	uint32_t period;
	uint32_t duty;
} __attribute__((__packed__)) pwm_setting_dev_t;

typedef struct trigger_cfg {
	uint32_t channels;
	uint32_t enabled;
	uint32_t level;
	uint32_t high_or_falling;
} __attribute__((__packed__)) trigger_cfg_t;

typedef struct sample_config {
	uint32_t sample_depth;
	uint32_t psa;
	uint16_t u1;
	uint32_t u2;
	uint16_t clock_divisor;
} __attribute__((__packed__)) sample_config_t;

typedef struct capture_info {
	uint32_t n_rep_packets;
	uint32_t n_rep_packets_before_trigger;
	uint32_t write_pos;
} __attribute__((__packed__)) capture_info_t;

typedef struct acq_packet {
	uint16_t state;
	uint8_t repetitions;
} __attribute__((__packed__)) acq_packet_t;

typedef struct transfer_packet {
	acq_packet_t packet[5];
	uint8_t seq;
} __attribute__((__packed__)) transfer_packet_t;

typedef struct pwm_setting {
	uint8_t enabled;
	float freq;
	float duty;
	pwm_setting_dev_t dev;
} pwm_setting_t;

struct dev_context {
	struct sr_context *ctx;

	int64_t fw_updated;
	pwm_setting_t pwm_setting[2];
	unsigned int threshold_voltage_idx;
	float threshold_voltage;
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;
	uint16_t cur_channels;
	int num_channels;

	uint32_t bitstream_size;

	/* derived stuff */
	uint64_t pre_trigger_size;

	/* state after sampling */
	int had_triggers_configured;
	int have_trigger;
	int transfer_finished;
	capture_info_t info;
	unsigned int n_transfer_packets_to_read; /* each with 5 acq packets */
	unsigned int n_bytes_to_read;
	unsigned int n_reps_until_trigger;
	unsigned int reading_behind_trigger;
	uint64_t total_samples;
	uint32_t read_pos;

	unsigned int convbuffer_size;
	uint8_t *convbuffer;
	struct libusb_transfer *transfer;
};

SR_PRIV int la2016_upload_firmware(struct sr_context *sr_ctx, libusb_device *dev, uint16_t product_id);
SR_PRIV int la2016_setup_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_stop_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_abort_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_has_triggered(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_start_retrieval(const struct sr_dev_inst *sdi, libusb_transfer_cb_fn cb);
SR_PRIV int la2016_init_device(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_deinit_device(const struct sr_dev_inst *sdi);

#ifndef WORDS_BIGENDIAN
/* this host is big-endian, need to swap from/to device inplace */
#define inplace_WL32(obj) do { uint32_t tmp = obj; WL32(&(obj), tmp); } while (0)
#define inplace_RL32(obj) obj = RL32(&(obj))
#define inplace_WL16(obj) do { uint16_t tmp = obj; WL16(&(obj), tmp); } while (0)
#define inplace_RL16(obj) obj = RL16(&(obj))

#define pwm_setting_dev_le(obj) do {		\
		inplace_WL32((obj).period);	\
		inplace_WL32((obj).duty);	\
	} while (0)
#define trigger_cfg_le(obj) do {			\
		inplace_WL32((obj).channels);		\
		inplace_WL32((obj).enabled);		\
		inplace_WL32((obj).level);		\
		inplace_WL32((obj).high_or_falling);	\
	} while (0)
#define sample_config_le(obj) do {			\
		inplace_WL32((obj).sample_depth);	\
		inplace_WL32((obj).psa);		\
		inplace_WL16((obj).u1);			\
		inplace_WL32((obj).u2);			\
		inplace_WL16((obj).clock_divisor);	\
	} while (0)

#define capture_info_host(obj) do {					\
		inplace_RL32((obj).n_rep_packets);			\
		inplace_RL32((obj).n_rep_packets_before_trigger);	\
		inplace_RL32((obj).write_pos);				\
	} while (0)
#define acq_packet_host(obj)			\
	inplace_RL16((obj).state)
#define transfer_packet_host(obj) do {			\
		acq_packet_host((obj).packet[0]);	\
		acq_packet_host((obj).packet[1]);	\
		acq_packet_host((obj).packet[2]);	\
		acq_packet_host((obj).packet[3]);	\
		acq_packet_host((obj).packet[4]);	\
	} while (0)

#else
/* this host is little-endian, same as device */
#define pwm_setting_dev_le(obj)   (void)obj
#define trigger_cfg_le(obj)       (void)obj
#define sample_config_le(obj)     (void)obj

#define capture_info_host(obj)    (void)obj
#define acq_packet_host(obj)      (void)obj
#define transfer_packet_host(obj) (void)obj
#endif

#endif
