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

#define USB_VID_SIPEED UINT16_C(0x359f)
#define NUM_MAX_TRANSFERS 16
#define TRANSFERS_DURATION_TOLERANCE 0.3f

struct dev_context {
	const slogic_model *model;

	struct sr_channel_group *digital_group;

	struct {
		uint64_t limit_samplerate;
		int32_t limit_samplechannel;
	};

	struct {
		uint64_t cur_limit_samples;
		uint64_t cur_samplerate;
		int32_t cur_samplechannel;
		int64_t cur_pattern_mode_idx;
		uint32_t expected_rate_MBps;
	}; // configuration

	struct {
		GThread *libusb_event_thread;
		int libusb_event_thread_run;

		enum libusb_speed speed;

		uint64_t samples_need_nbytes;
		uint64_t samples_got_nbytes;

		uint64_t per_transfer_duration; /* unit: ms */
		uint64_t per_transfer_nbytes;

		size_t num_transfers_completed;
		size_t num_transfers_used;
		struct libusb_transfer *transfers[NUM_MAX_TRANSFERS];

		uint64_t transfers_reached_nbytes; /* real received bytes in all */
		uint64_t transfers_reached_nbytes_latest; /* real received bytes this transfer */
		int64_t transfers_reached_time_start;
		int64_t transfers_reached_time_first_data; /* first byte arrival; excludes cold-start from avg */
		int64_t transfers_reached_time_latest;

		GAsyncQueue *raw_data_queue;
	}; // usb

	int acq_aborted;

	/* Zero-progress stall recovery (shared slogic core policy, all-logic
	 * canonical). The transfer callback folds each completion into `stream`;
	 * a RETRY_RUN verdict sets `restart_pending`, and the re-arm (stop, resubmit
	 * the ring, re-issue RUN) runs on the session thread in handle_events. */
	slogic_stream stream;
	int restart_pending;

	/* Triggers */
	uint64_t capture_ratio;
	gboolean trigger_fired;
	struct soft_trigger_logic *stl;

	double voltage_threshold[2];
};

SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi);
SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi);

/* Adapter entry points bridging the shared libslogic core to libsigrok:
 * defined in api.c, called from protocol.c's transfer engine. */
SR_PRIV int slogic_dev_start(const struct sr_dev_inst *sdi);
SR_PRIV int slogic_dev_stop(const struct sr_dev_inst *sdi);
SR_PRIV void slogic_submit_raw_data(void *data, size_t len,
				    const struct sr_dev_inst *sdi);

#endif
