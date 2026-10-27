/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
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

#include <glib.h>
#include <libusb.h>
#include <stdint.h>

#include <libsigrok/libsigrok.h>

#include "libsigrok-internal.h"

#include "slogic/slogic.h"

#define LOG_PREFIX "sipeed-slogic-analyzer"

/*
 * The capture engine (transfer ring, stall recovery, trigger, exact sample
 * accounting, throughput report) lives in the shared libslogic core
 * (slogic/). This driver only maps libsigrok's scan/config/session surface
 * onto it.
 */
struct dev_context {
	const slogic_model *model;
	struct sr_channel_group *digital_group;

	uint64_t limit_samplerate;    /* for the current channel mode and link */
	int32_t limit_samplechannel;

	uint64_t cur_limit_samples;
	uint64_t cur_samplerate;
	int32_t cur_samplechannel;
	int64_t cur_pattern_mode_idx;
	uint64_t capture_ratio;       /* pre-trigger, percent of the limit */
	double voltage_threshold[2];

	slogic_link link;
	slogic_dev *sdev;
	slogic_capture *cap;
};

SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi);
SR_PRIV void sipeed_slogic_log(void *user, int level, const char *msg);

#endif
