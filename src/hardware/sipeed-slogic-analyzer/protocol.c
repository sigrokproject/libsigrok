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

#include <config.h>

#include "protocol.h"

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	int ret;
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	sdi = transfer->user_data;
	if (!sdi)
		return;
	devc = sdi->priv;

	int64_t transfers_reached_time_now = g_get_monotonic_time();
	int64_t transfers_reached_duration =
		transfers_reached_time_now -
		devc->transfers_reached_time_latest;
	int64_t transfers_all_duration =
		transfers_reached_time_now - devc->transfers_reached_time_start;

	devc->num_transfers_used -= 1;
	devc->num_transfers_completed += 1;
	sr_spew("[%d] Transfer #%d status: %d(%s).",
		devc->num_transfers_completed,
		std_u64_idx(g_variant_new_uint64((uint64_t)transfer),
			    (uint64_t *)devc->transfers, NUM_MAX_TRANSFERS),
		transfer->status, libusb_error_name(transfer->status));
	switch (transfer->status) {
	case LIBUSB_TRANSFER_COMPLETED: /* normal case */
	case LIBUSB_TRANSFER_TIMED_OUT: /* may have received some data */
	{
		devc->transfers_reached_time_latest =
			transfers_reached_time_now;

		devc->transfers_reached_nbytes_latest = transfer->actual_length;
		devc->transfers_reached_nbytes +=
			devc->transfers_reached_nbytes_latest;

		if (transfer->actual_length >
		    devc->samples_need_nbytes - devc->samples_got_nbytes)
			transfer->actual_length = devc->samples_need_nbytes -
						  devc->samples_got_nbytes;
		devc->samples_got_nbytes += transfer->actual_length;

		sr_dbg("[%u] Got %u/%u(%.2f%%) => speed: %.2fMBps, %.2fMBps(avg) => "
		       "+%.3f=%.3fms.",
		       devc->num_transfers_completed, devc->samples_got_nbytes,
		       devc->samples_need_nbytes,
		       100.f * devc->samples_got_nbytes /
			       devc->samples_need_nbytes,
		       (double)devc->transfers_reached_nbytes_latest /
			       transfers_reached_duration,
		       (double)devc->transfers_reached_nbytes /
			       transfers_all_duration,
		       (double)transfers_reached_duration / SR_KHZ(1),
		       (double)transfers_all_duration / SR_KHZ(1));

		/* TODO: move out submit to ensure continuous transfers */
		if (devc->raw_data_queue &&
		    devc->cur_pattern_mode_idx != PATTERN_MODE_TEST_MAX_SPEED) {
			uint8_t *d = transfer->buffer;
			size_t len = transfer->actual_length;
			// sr_dbg("HEAD: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x
			// %02x %02x %02x %02x %02x", 	d[0], d[1], d[2], d[3], d[4], d[5], d[6],
			// d[7], d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
			// devc->model->submit_raw_data(d, len, sdi);

			uint8_t *ptr = malloc(devc->per_transfer_nbytes);
			if (!ptr) {
				sr_err("Failed to allocate memory: %u bytes!",
				       devc->per_transfer_nbytes);
				devc->acq_aborted = 1;
				break;
			}
			transfer->buffer = ptr;
			GByteArray *array = g_byte_array_new_take(d, len);
			g_async_queue_push(devc->raw_data_queue, array);
		}

		if (devc->samples_got_nbytes +
			    devc->num_transfers_used *
				    devc->per_transfer_nbytes <
		    devc->samples_need_nbytes) {
			transfer->actual_length = 0;
			transfer->timeout = (TRANSFERS_DURATION_TOLERANCE + 1) *
					    devc->per_transfer_duration *
					    (devc->num_transfers_used + 2);
			ret = libusb_submit_transfer(transfer);
			if (ret) {
				sr_dbg("Failed to submit transfer: %s",
				       libusb_error_name(ret));
			} else {
				sr_spew("Resubmit transfer: %p", transfer);
				devc->num_transfers_used += 1;
			}
		}
	} break;

	case LIBUSB_TRANSFER_OVERFLOW:
	case LIBUSB_TRANSFER_STALL:
	case LIBUSB_TRANSFER_NO_DEVICE:
	default:
		devc->acq_aborted = 1;
		break;
	}

	if (devc->num_transfers_completed &&
	    (double)transfers_reached_duration / SR_KHZ(1) >
		    (TRANSFERS_DURATION_TOLERANCE + 1) *
			    devc->per_transfer_duration) {
		devc->timeout_count += 1;
		if (devc->timeout_count > devc->num_transfers_used) {
			sr_err("Timeout %.3fms!!! Reach duration limit: %.3f(%u+%.1f%%), %.3f > "
			       "%.3f(%u+%.1f%%)(total) except first one.",
			       (double)transfers_reached_duration / SR_KHZ(1),
			       (TRANSFERS_DURATION_TOLERANCE + 1) *
				       devc->per_transfer_duration,
			       devc->per_transfer_duration,
			       TRANSFERS_DURATION_TOLERANCE * 100,
			       (double)transfers_all_duration / SR_KHZ(1),
			       (TRANSFERS_DURATION_TOLERANCE + 1) *
				       devc->per_transfer_duration *
				       devc->num_transfers_completed,
			       devc->per_transfer_duration *
				       (devc->num_transfers_completed + 1),
			       TRANSFERS_DURATION_TOLERANCE * 100);
			devc->acq_aborted = 1;
		}
	} else {
		devc->timeout_count = 0;
	}

	if (devc->num_transfers_used == 0) {
		devc->acq_aborted = 1;
	}
};

static int handle_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	// sr_spew("handle_events enter");

	if (devc->acq_aborted) {
		if (devc->num_transfers_used) {
			for (size_t i = 0; i < NUM_MAX_TRANSFERS; ++i) {
				struct libusb_transfer *transfer =
					devc->transfers[i];
				if (transfer) {
					libusb_cancel_transfer(transfer);
				}
			}
		} else {
			int freed = 0;
			for (size_t i = 0; i < NUM_MAX_TRANSFERS; ++i) {
				struct libusb_transfer *transfer =
					devc->transfers[i];
				if (transfer) {
					freed += 1;
					libusb_free_transfer(transfer);
					devc->transfers[i] = NULL;
				}
			}
			if (freed) {
				sr_dbg("Freed %d transfers.", freed);
			} else {
				if ((devc->model->operation.remote_stop(sdi)) < 0) {
					sr_err("Unhandled `CMD_STOP`");
				}
				if (!g_async_queue_length(
					   devc->raw_data_queue)) {
					sr_dbg("Freed all transfers.");
					g_async_queue_unref(devc->raw_data_queue);
					devc->raw_data_queue = NULL;

					if (devc->stl) {
						soft_trigger_logic_free(devc->stl);
						devc->stl = NULL;
						devc->trigger_fired = FALSE;
					}
				}
			}
		}
	}

	if (!devc->raw_data_queue) {
		sr_info("Bulk in %u/%u bytes with %u transfers.",
			devc->samples_got_nbytes, devc->samples_need_nbytes,
			devc->num_transfers_completed);
		std_session_send_df_end(sdi);
		sr_session_source_remove(sdi->session,
					 -1 * (size_t)drvc->sr_ctx->libusb_ctx);
	} else if (g_async_queue_length(devc->raw_data_queue)) {
		GByteArray *array = g_async_queue_try_pop(devc->raw_data_queue);
		if (array != NULL) {
			if (devc->trigger_fired) {
				devc->model->submit_raw_data(
					array->data, array->len, sdi);
			} else if (devc->stl) {
				devc->samples_got_nbytes = 0;
				extern int slogic_soft_trigger_raw_data(void *data, size_t len, const struct sr_dev_inst *sdi);
				int sent_samples = slogic_soft_trigger_raw_data(array->data, array->len, sdi);
				if (sent_samples) {
					devc->samples_got_nbytes += sent_samples * devc->cur_samplechannel / 8;
					devc->trigger_fired = TRUE;
				}
			}
			g_byte_array_unref(array);
		}
	}

	return TRUE;
}

// to find out the maixmum size of ONE transfer
static int train_bulk_in_transfer(struct dev_context *devc,
				  libusb_device_handle *dev_handle)
{
	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	if (!transfer) {
		sr_err("Failed to allocate libusb transfer!");
		return SR_ERR_IO;
	}

	uint64_t sr = devc->cur_samplerate;
	uint64_t ch = devc->cur_samplechannel;
	uint64_t bps = sr * ch;
	uint64_t Bps = bps / 8;
	uint64_t BpMs = Bps / SR_KHZ(1);

	uint64_t cur_transfer_duration = 125 /* ms */;
	uint64_t try_transfer_nbytes = cur_transfer_duration * BpMs /* bytes */;

	const uint64_t ALIGN_SIZE = 32 * 1024; /* 32kiB */
	do {
		// Align up
		try_transfer_nbytes = (try_transfer_nbytes + (ALIGN_SIZE - 1)) &
				      ~(ALIGN_SIZE - 1);

		uint8_t *transfer_buffer = malloc(try_transfer_nbytes);
		if (!transfer_buffer) {
			sr_dbg("Failed to allocate memory: %u bytes! Half it.",
			       try_transfer_nbytes);
			try_transfer_nbytes >>= 1;
			continue;
		}

		cur_transfer_duration = try_transfer_nbytes / BpMs;
		sr_dbg("Train: receive %u bytes per %ums...",
		       try_transfer_nbytes, cur_transfer_duration);

		libusb_fill_bulk_transfer(transfer, dev_handle,
					  devc->model->ep_in, transfer_buffer,
					  try_transfer_nbytes, NULL, NULL, 0);
		transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
		transfer->flags |= LIBUSB_TRANSFER_FREE_TRANSFER;
		int ret = libusb_submit_transfer(transfer);
		if (ret) {
			sr_dbg("Failed to submit transfer: %s!",
			       libusb_error_name(ret));
			if (ret == LIBUSB_ERROR_NO_MEM) {
				free(transfer->buffer);
				sr_dbg("Half it and try again.");
				try_transfer_nbytes >>= 1;
				continue;
			} else {
				libusb_free_transfer(transfer);
				return SR_ERR_IO;
			}
		}

		ret = libusb_cancel_transfer(transfer);
		if (ret) {
			sr_dbg("Failed to cancel transfer: %s!",
			       libusb_error_name(ret));
		}

		try_transfer_nbytes >>=
			1; // At least 2 transfets can be pending.
		break;
	} while (try_transfer_nbytes >
		 ALIGN_SIZE); // 32kiB > 125ms * 1MHZ * 2ch

	cur_transfer_duration = try_transfer_nbytes / BpMs;
	sr_dbg("Choose: receive %u bytes per %ums :)", try_transfer_nbytes,
	       cur_transfer_duration);

	// Assign
	devc->per_transfer_duration = cur_transfer_duration;
	devc->per_transfer_nbytes = try_transfer_nbytes;

	return SR_OK;
}

SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;

	int ret;

	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;
	usb = sdi->conn;

	if ((ret = devc->model->operation.remote_stop(sdi)) < 0) {
		sr_err("Unhandled `CMD_STOP`");
		return ret;
	}

	devc->samples_got_nbytes = 0;
	devc->samples_need_nbytes =
		devc->cur_limit_samples * devc->cur_samplechannel / 8;
	sr_info("Need %ux %uch@%uMHz in %ums.", devc->cur_limit_samples,
		devc->cur_samplechannel, devc->cur_samplerate / SR_MHZ(1),
		1000 * devc->cur_limit_samples / devc->cur_samplerate);

	if ((ret = train_bulk_in_transfer(devc, usb->devhdl)) != SR_OK) {
		sr_err("Failed to train bulk_in_transfer!`");
		return ret;
	}

	devc->acq_aborted = 0;
	devc->num_transfers_used = 0;
	devc->num_transfers_completed = 0;
	memset(devc->transfers, 0, sizeof(devc->transfers));
	devc->transfers_reached_nbytes = 0;
	devc->timeout_count = 0;
	devc->raw_data_queue = g_async_queue_new();

	if (!devc->raw_data_queue) {
		sr_err("New g_async_queue failed, can't handle data anymore!");
		return SR_ERR_MALLOC;
	}

	while (devc->num_transfers_used < NUM_MAX_TRANSFERS &&
	       devc->samples_got_nbytes + devc->num_transfers_used *
						  devc->per_transfer_nbytes <
		       devc->samples_need_nbytes) {
		uint8_t *dev_buf = malloc(devc->per_transfer_nbytes);
		if (!dev_buf) {
			sr_dbg("Failed to allocate memory[%d]",
			       devc->num_transfers_used);
			break;
		}

		struct libusb_transfer *transfer = libusb_alloc_transfer(0);
		if (!transfer) {
			sr_dbg("Failed to allocate transfer[%d]",
			       devc->num_transfers_used);
			free(dev_buf);
			break;
		}

		libusb_fill_bulk_transfer(
			transfer, usb->devhdl, devc->model->ep_in, dev_buf,
			devc->per_transfer_nbytes, receive_transfer, sdi,
			(TRANSFERS_DURATION_TOLERANCE + 1) *
				devc->per_transfer_duration *
				(devc->num_transfers_used + 2));
		transfer->actual_length = 0;

		transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
		ret = libusb_submit_transfer(transfer);
		if (ret) {
			sr_dbg("Failed to submit transfer[%d]: %s.",
			       devc->num_transfers_used,
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			break;
		}
		devc->transfers[devc->num_transfers_used] = transfer;
		devc->num_transfers_used += 1;
	}
	sr_dbg("Submited %u transfers", devc->num_transfers_used);

	if (!devc->num_transfers_used) {
		return SR_ERR_IO;
	}

	std_session_send_df_header(sdi);
	std_session_send_df_frame_begin(sdi);

	sr_session_source_add(sdi->session,
			      -1 * (size_t)drvc->sr_ctx->libusb_ctx, 0,
			      (devc->per_transfer_duration / 2) ?: 1,
			      handle_events, (void *)sdi);

	devc->trigger_fired = TRUE;

	devc->capture_ratio = 10;
	struct sr_trigger *trigger = NULL;
	/* Setup triggers */
	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int pre_trigger_samples = 0;
		if (devc->cur_limit_samples > 0)
			pre_trigger_samples = (devc->capture_ratio * devc->cur_limit_samples) / 100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	}

	if ((ret = devc->model->operation.remote_run(sdi)) < 0) {
		sr_err("Unhandled `CMD_RUN`");
		sipeed_slogic_acquisition_stop(sdi);
		return ret;
	}

	devc->transfers_reached_time_start = g_get_monotonic_time();
	devc->transfers_reached_time_latest =
		devc->transfers_reached_time_start;

	return SR_OK;
}

SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	devc->acq_aborted = 1;

	return SR_OK;
}
