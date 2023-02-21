/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 taorye <taorye@outlook.com>
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

#ifndef LIBSIGROK_HARDWARE_SIPEED_SLOGIC_ANALYZER_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SIPEED_SLOGIC_ANALYZER_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "sipeed-slogic-analyzer"

#define NUM_SIMUL_TRANSFERS 32
#define MAX_EMPTY_TRANSFERS (NUM_SIMUL_TRANSFERS * 2)

#define DBG_VAL(expr) do {\
	__typeof((expr)) _expr = (expr);\
	sr_warn("[%u]%s<"#expr"> i:%d\tu:%u\tf:%f\th:%x", __LINE__, __func__, \
		*(long*)(&_expr), \
		*(unsigned long*)(&_expr), \
		*(float*)(&_expr), \
		*(unsigned long*)(&_expr)); \
}while(0)

struct slogic_profile {
	uint16_t vid;
	uint16_t pid;
};

struct dev_context {
    struct slogic_profile *profile;

	uint64_t limit_samples;
	uint64_t limit_frames;

	gboolean acq_aborted;
	gboolean trigger_fired;
	struct soft_trigger_logic *stl;

	uint64_t num_frames;
	uint64_t sent_samples;
	int submitted_transfers;
	int empty_transfer_count;

	uint64_t num_transfers;
	struct libusb_transfer **transfers;

	uint64_t cur_samplerate;
	int logic_pattern;
	double voltage_threshold[2];
	/* Triggers */
	uint64_t capture_ratio;
};

#pragma pack(push, 1)
struct cmd_start_acquisition {
	uint8_t sample_rate_l;
	uint8_t sample_rate_h;
};
#pragma pack(pop)

/* Protocol commands */
#define CMD_START			0xb1

SR_PRIV int sipeed_slogic_analyzer_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi);

static inline size_t to_bytes_per_ms(struct dev_context *devc)
{
	size_t channel_counts = 1 << (devc->logic_pattern);
	return (devc->cur_samplerate * channel_counts)/8/1000;
}

static inline size_t get_buffer_size(struct dev_context *devc)
{
	/**
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of 512.
	 */
	size_t s = 10 * to_bytes_per_ms(devc);
	size_t pack_size = 512;
	return (s + (pack_size-1)) & ~(pack_size-1);
}

static inline size_t get_number_of_transfers(struct dev_context *devc)
{
	/* Total buffer size should be able to hold about 500ms of data. */
	size_t n = (500 * to_bytes_per_ms(devc) / get_buffer_size(devc));
	if (n > NUM_SIMUL_TRANSFERS)
		return NUM_SIMUL_TRANSFERS;
	return n;
}

static inline size_t get_timeout(struct dev_context *devc)
{
	size_t total_size = get_buffer_size(devc) *
			get_number_of_transfers(devc);
	size_t timeout = total_size / to_bytes_per_ms(devc);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
}

#endif
