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

#include <inttypes.h>

#include "protocol.h"

static int submit_bulk_ring(const struct sr_dev_inst *sdi);
static int restart_acquisition_after_stall(const struct sr_dev_inst *sdi);

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

	double expected_rate = devc->expected_rate_MBps;
	gboolean resubmitted = FALSE;
	/* Ring transfers in flight besides this one (it is still counted). */
	gint others_in_flight = g_atomic_int_get(&devc->num_transfers_used) - 1;

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
		if (devc->transfers_reached_nbytes_latest > 0 &&
		    devc->transfers_reached_time_first_data == 0)
			devc->transfers_reached_time_first_data =
				transfers_reached_time_now;

		/* Per-model hardware quirk: drop the partial leading word the
		 * firmware emits before whole-word gating (e.g. 16U3/Combo8). A
		 * no-op for models with first_drop_bytes == 0 (e.g. 32U3) and once
		 * drop_left is exhausted. */
		transfer->actual_length = slogic_apply_first_drop(
			&devc->stream, transfer->buffer, transfer->actual_length);

		/* Untriggered, the ring fetches exactly the requested bytes: trim the
		 * last transfer. Triggered, the trigger position is unknown here, so the
		 * ring keeps streaming and the consumer (handle_events) trims and ends
		 * the capture once the post-trigger samples are delivered. */
		if (!devc->has_trigger && transfer->actual_length >
		    devc->samples_need_nbytes - devc->samples_got_nbytes)
			transfer->actual_length = devc->samples_need_nbytes -
						  devc->samples_got_nbytes;
		devc->samples_got_nbytes += transfer->actual_length;

		sr_dbg("[%u] Got %u/%u(%.2f%%) => speed: %.2fMBps, %.2fMBps(avg), %.0fMBps(exp) => "
		       "+%.3f=%.3fms.",
		       devc->num_transfers_completed, devc->samples_got_nbytes,
		       devc->samples_need_nbytes,
		       100.f * devc->samples_got_nbytes /
			       devc->samples_need_nbytes,
		       (double)devc->transfers_reached_nbytes_latest /
			       transfers_reached_duration,
		       slogic_avg_mbps(devc->transfers_reached_nbytes,
				       devc->transfers_reached_time_first_data,
				       transfers_reached_time_now),
			   expected_rate,
		       (double)transfers_reached_duration / SR_KHZ(1),
		       (double)transfers_all_duration / SR_KHZ(1));

		/* Swap in a fresh buffer and resubmit the transfer BEFORE handing the
		 * filled buffer to the consumer thread, so the bulk-IN pipe does not
		 * idle during post-processing — keeps transfers continuous. The filled
		 * buffer is enqueued (and freed by the consumer) after the resubmit. */
		uint8_t *filled = NULL;
		size_t filled_len = 0;
		if (devc->raw_data_queue) {
			uint8_t *ptr = malloc(devc->per_transfer_nbytes);
			if (!ptr) {
				sr_err("Failed to allocate memory: %u bytes!",
				       devc->per_transfer_nbytes);
				g_atomic_int_set(&devc->acq_aborted, 1);
				break;
			}
			filled = transfer->buffer;
			filled_len = transfer->actual_length;
			transfer->buffer = ptr; /* fresh buffer for the resubmit */
		}

		/* Never resubmit once the capture is aborted (stop, error, consumer
		 * done): the ring must drain so handle_events can tear down. */
		if (!g_atomic_int_get(&devc->acq_aborted) &&
		    !devc->restart_pending &&
		    (devc->has_trigger ||
		     devc->samples_got_nbytes +
			     (uint64_t)others_in_flight *
				     devc->per_transfer_nbytes <
		     devc->samples_need_nbytes)) {
			transfer->actual_length = 0;
			transfer->timeout = (TRANSFERS_DURATION_TOLERANCE + 1) *
					    devc->per_transfer_duration *
					    (others_in_flight + 2);
			ret = libusb_submit_transfer(transfer);
			if (ret) {
				sr_dbg("Failed to submit transfer: %s",
				       libusb_error_name(ret));
			} else {
				sr_spew("Resubmit transfer: %p", transfer);
				resubmitted = TRUE;
			}
		}

		if (filled) {
			GByteArray *array = g_byte_array_new_take(filled, filled_len);
			g_async_queue_push(devc->raw_data_queue, array);
		}
	} break;

	case LIBUSB_TRANSFER_CANCELLED:
		/* Cancellation is expected while the ring drains for a stall
		 * re-arm; only an unsolicited cancel is a real abort. */
		if (!devc->restart_pending)
			g_atomic_int_set(&devc->acq_aborted, 1);
		break;

	case LIBUSB_TRANSFER_OVERFLOW:
	case LIBUSB_TRANSFER_STALL:
	case LIBUSB_TRANSFER_NO_DEVICE:
	default:
		g_atomic_int_set(&devc->acq_aborted, 1);
		break;
	}

	/*
	 * Stall watchdog + zero-progress RUN re-arm — shared slogic core policy
	 * (all-logic canonical). Fold this completion's RAW received length into
	 * the watcher (before the trigger trim). A RETRY_RUN verdict defers an
	 * in-place re-arm to the session thread; ABORT ends the capture. Skip while
	 * a re-arm is already pending — the ring is draining.
	 */
	if ((transfer->status == LIBUSB_TRANSFER_COMPLETED ||
	     transfer->status == LIBUSB_TRANSFER_TIMED_OUT) &&
	    !devc->restart_pending) {
		slogic_verdict verdict = slogic_stream_watch(&devc->stream,
			devc->transfers_reached_nbytes_latest,
			transfers_reached_time_now, transfers_reached_duration);
		switch (verdict) {
		case SLOGIC_STREAM_WARN_SLOW:
			sr_warn("USB link slower than the analyzer; "
				"draining at the host's pace.");
			break;
		case SLOGIC_STREAM_RETRY_RUN:
			sr_warn("USB stream did not start; re-arming hardware.");
			devc->restart_pending = 1;
			break;
		case SLOGIC_STREAM_ABORT:
			sr_err("Acquisition stall: aborting capture.");
			g_atomic_int_set(&devc->acq_aborted, 1);
			break;
		case SLOGIC_STREAM_OK:
		case SLOGIC_STREAM_DONE:
		default:
			break;
		}
	}

	/* Last touch of this transfer: only now drop it from the in-flight count.
	 * Doing it earlier let handle_events see 0 and free the ring while this
	 * callback was still about to resubmit or read it (use-after-free). */
	if (!resubmitted &&
	    g_atomic_int_dec_and_test(&devc->num_transfers_used) &&
	    !devc->restart_pending)
		g_atomic_int_set(&devc->acq_aborted, 1);
};

/*
 * Consumer side (session thread). Deliver one queued USB buffer to the session,
 * trimmed so the capture never delivers more than cur_limit_samples in total
 * (pre-trigger + post-trigger with a trigger). Returns TRUE once the limit is
 * reached.
 */
static gboolean deliver_samples(const struct sr_dev_inst *sdi,
				const uint8_t *data, size_t len)
{
	struct dev_context *devc = sdi->priv;
	uint64_t nch = devc->cur_samplechannel;
	uint64_t limit = devc->cur_limit_samples;
	/* slogic_submit_raw_data unpacks <8 ch in groups of nch bytes, >=8 ch
	 * in whole samples of nch/8 bytes: keep lengths a multiple of that. */
	size_t grp = nch < 8 ? nch : nch / 8;
	size_t keep;

	if (!nch || !limit || devc->samples_sent >= limit)
		return TRUE;

	keep = (size_t)(((limit - devc->samples_sent) * nch + 7) / 8);
	keep = (keep + grp - 1) / grp * grp;
	if (keep > len / grp * grp)
		keep = len / grp * grp;
	if (keep) {
		slogic_submit_raw_data((void *)data, keep, sdi);
		devc->samples_sent += (uint64_t)keep * 8 / nch;
	}
	return devc->samples_sent >= limit;
}

/* Hand one queued buffer to the consumer path; frees it. */
static void consume_buffer(const struct sr_dev_inst *sdi, GByteArray *array)
{
	struct dev_context *devc = sdi->priv;
	gboolean done = FALSE;

	if (devc->trigger_fired) {
		done = deliver_samples(sdi, array->data, array->len);
	} else if (devc->stl &&
		   !g_atomic_int_get(&devc->acq_aborted)) {
		/* Only scan for the trigger while the capture is live: after a
		 * stop the backlog is dropped, not trigger-checked (slow) and
		 * pushed to the frontend. */
		extern int slogic_soft_trigger_raw_data(void *data, size_t len, const struct sr_dev_inst *sdi);
		if (slogic_soft_trigger_raw_data(array->data, array->len, sdi)) {
			devc->trigger_fired = TRUE;
			done = devc->samples_sent >= devc->cur_limit_samples;
		}
	}
	g_byte_array_unref(array);

	if (done && !g_atomic_int_get(&devc->acq_aborted)) {
		sr_dbg("Delivered %" PRIu64 " samples; capture complete.",
		       devc->samples_sent);
		g_atomic_int_set(&devc->acq_aborted, 1);
	}
}

static void cancel_ring(struct dev_context *devc)
{
	for (size_t i = 0; i < NUM_MAX_TRANSFERS; ++i) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

static int free_ring(struct dev_context *devc)
{
	int freed = 0;

	for (size_t i = 0; i < NUM_MAX_TRANSFERS; ++i) {
		if (devc->transfers[i]) {
			libusb_free_transfer(devc->transfers[i]);
			devc->transfers[i] = NULL;
			freed++;
		}
	}
	return freed;
}

/* Drop everything still queued and release the per-capture state. Only call
 * once the ring is idle (no transfer in flight). */
static void release_capture(struct dev_context *devc)
{
	GByteArray *array;

	if (devc->raw_data_queue) {
		while ((array = g_async_queue_try_pop(devc->raw_data_queue)))
			g_byte_array_unref(array);
		g_async_queue_unref(devc->raw_data_queue);
		devc->raw_data_queue = NULL;
	}
	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
	devc->trigger_fired = FALSE;
}

static int handle_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;
	GByteArray *array;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	/*
	 * Zero-progress stall re-arm (session thread). Cancel any in-flight ring;
	 * once it has fully drained, re-issue the device start sequence in place.
	 * A control transfer must not be issued from inside a bulk callback (U3
	 * firmware answers BUSY), so the re-arm happens here, not in
	 * receive_transfer.
	 */
	if (devc->restart_pending && !g_atomic_int_get(&devc->acq_aborted)) {
		if (g_atomic_int_get(&devc->num_transfers_used)) {
			cancel_ring(devc);
		} else {
			free_ring(devc);
			if (restart_acquisition_after_stall(sdi) != SR_OK) {
				sr_err("Acquisition stall recovery failed; "
				       "aborting capture.");
				g_atomic_int_set(&devc->acq_aborted, 1);
			}
			devc->restart_pending = 0;
		}
		return TRUE;
	}

	if (!g_atomic_int_get(&devc->acq_aborted)) {
		/* Live capture: consume one buffer per tick. */
		if ((array = g_async_queue_try_pop(devc->raw_data_queue)))
			consume_buffer(sdi, array);
		return TRUE;
	}

	/*
	 * Teardown (stop, error, ring done or sample limit reached). The ring is
	 * no longer resubmitted; cancel what is still in flight and keep
	 * consuming meanwhile — data the capture still wants (untriggered, or
	 * after the trigger fired) is delivered up to the sample limit, the rest
	 * is dropped without trigger checks.
	 */
	if (g_atomic_int_get(&devc->num_transfers_used)) {
		cancel_ring(devc);
		while ((array = g_async_queue_try_pop(devc->raw_data_queue)))
			consume_buffer(sdi, array);
		return TRUE;
	}

	/* Ring idle: no callback can touch it any more. */
	if (free_ring(devc))
		sr_dbg("Freed transfers.");
	if (!devc->dev_stopped) {
		if (slogic_dev_stop(sdi) < 0)
			sr_err("Unhandled `CMD_STOP`");
		devc->dev_stopped = TRUE;
	}
	while ((array = g_async_queue_try_pop(devc->raw_data_queue)))
		consume_buffer(sdi, array);
	release_capture(devc);

	sr_info("Bulk in %" PRIu64 " bytes, delivered %" PRIu64 "/%" PRIu64
		" samples with %zu transfers.",
		devc->samples_got_nbytes, devc->samples_sent,
		devc->cur_limit_samples, devc->num_transfers_completed);
	std_session_send_df_end(sdi);
	sr_session_source_remove(sdi->session,
				 -1 * (size_t)drvc->sr_ctx->libusb_ctx);

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

	devc->expected_rate_MBps = BpMs/SR_KHZ(1);

	uint64_t cur_transfer_duration = 250 /* ms */;
	uint64_t try_transfer_nbytes = cur_transfer_duration * BpMs /* bytes */;

	const uint64_t ALIGN_SIZE = 32 * 1024; /* 32kiB */

#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x0100010C)
	/*
	 * Windows throughput fix. libusb's WinUSB backend keeps only ONE bulk-IN
	 * transfer in flight unless RAW_IO is enabled, no matter how many transfers
	 * we queue (libusb issue #490), so the pipe idles between transfers and
	 * throughput roughly halves (~420 vs ~800 MB/s; Linux usbfs is unaffected).
	 * Enable RAW_IO on the capture endpoint and cap the per-transfer size to the
	 * backend's RAW_IO maximum (RAW_IO reads must be <= that and a multiple of the
	 * endpoint max packet size; our sizes stay 32 KiB-aligned). No-op where the
	 * backend does not support it (returns an error), e.g. Linux. Needs libusb
	 * >= 1.0.30 (API 0x0100010C), which introduced these calls.
	 */
	{
		int rio = libusb_endpoint_set_raw_io(dev_handle, devc->model->ep_in, 1);
		if (rio == LIBUSB_SUCCESS) {
			int maxsz = libusb_get_max_raw_io_transfer_size(
				dev_handle, devc->model->ep_in);
			sr_info("RAW_IO enabled on ep 0x%02x, max_raw_io=%d",
				devc->model->ep_in, maxsz);
			if (maxsz > 0 && (uint64_t)maxsz < try_transfer_nbytes)
				try_transfer_nbytes = (uint64_t)maxsz & ~(ALIGN_SIZE - 1);
		} else {
			sr_dbg("RAW_IO not enabled (%s)", libusb_error_name(rio));
		}
	}
#endif

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
			2; // At least 4 transfets can be pending.
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

/* Fill and submit the bulk-IN transfer ring (used by acquisition start and the
 * stall re-arm). Assumes num_transfers_used == 0 and the per-transfer sizing is
 * already computed. */
static int submit_bulk_ring(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;
	gint n = 0;

	/* Runs on the session thread before any transfer of this ring is in
	 * flight, so the slot index and the in-flight count advance together. */
	while (n < NUM_MAX_TRANSFERS &&
	       (devc->has_trigger ||
		devc->samples_got_nbytes + (uint64_t)n * devc->per_transfer_nbytes <
			devc->samples_need_nbytes)) {
		uint8_t *dev_buf = malloc(devc->per_transfer_nbytes);
		if (!dev_buf) {
			sr_dbg("Failed to allocate memory[%d]", n);
			break;
		}

		struct libusb_transfer *transfer = libusb_alloc_transfer(0);
		if (!transfer) {
			sr_dbg("Failed to allocate transfer[%d]", n);
			free(dev_buf);
			break;
		}

		libusb_fill_bulk_transfer(
			transfer, usb->devhdl, devc->model->ep_in, dev_buf,
			devc->per_transfer_nbytes, receive_transfer,
			(void *)sdi,
			(TRANSFERS_DURATION_TOLERANCE + 1) *
				devc->per_transfer_duration * (n + 2));
		transfer->actual_length = 0;

		transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
		/* Count it before submitting: its callback may run at once. */
		devc->transfers[n] = transfer;
		g_atomic_int_inc(&devc->num_transfers_used);
		ret = libusb_submit_transfer(transfer);
		if (ret) {
			sr_dbg("Failed to submit transfer[%d]: %s.", n,
			       libusb_error_name(ret));
			devc->transfers[n] = NULL;
			g_atomic_int_add(&devc->num_transfers_used, -1);
			libusb_free_transfer(transfer);
			break;
		}
		n++;
	}
	sr_dbg("Submitted %d transfers", n);

	return n ? SR_OK : SR_ERR_IO;
}

/*
 * Re-issue the device start sequence after a zero-progress stall. Runs on the
 * session thread with no transfers in flight and nothing delivered to the
 * frontend, so the capture is recycled in place: stop/flush, reset the stream's
 * progress/timing for the fresh attempt (keeping run_retried so a second stall
 * aborts), resubmit the ring, then re-issue RUN.
 */
static int restart_acquisition_after_stall(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	if ((ret = slogic_dev_stop(sdi)) < 0) {
		sr_err("Stall re-arm: CMD_STOP failed");
		return ret;
	}

	devc->samples_got_nbytes = 0;
	devc->num_transfers_completed = 0;
	devc->transfers_reached_nbytes = 0;
	devc->transfers_reached_nbytes_latest = 0;
	devc->transfers_reached_time_start = g_get_monotonic_time();
	devc->transfers_reached_time_latest = devc->transfers_reached_time_start;
	devc->transfers_reached_time_first_data = 0; /* re-captured at the first byte */
	/* Keep stream.run_retried (one re-arm total); clear progress + timing. */
	devc->stream.received_bytes = 0;
	devc->stream.time_start_us = 0;
	devc->stream.time_last_data_us = 0;
	devc->stream.slow_count = 0;
	devc->stream.drop_left = devc->model->first_drop_bytes; /* re-arm re-opens the gate mid-word */

	if (submit_bulk_ring(sdi) != SR_OK) {
		sr_err("Stall re-arm: failed to resubmit transfers");
		return SR_ERR_IO;
	}

	if ((ret = slogic_dev_start(sdi)) < 0) {
		sr_err("Stall re-arm: CMD_RUN failed");
		return ret;
	}

	sr_info("Acquisition stall recovery: restarted capture.");
	return SR_OK;
}

/*
 * Tear a capture down synchronously from the session thread when it never got
 * going (start failed after the ring was submitted). The libusb event thread
 * delivers the cancellations; wait until the ring is idle, then free it.
 */
static void abort_ring_sync(struct dev_context *devc)
{
	int64_t deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;

	g_atomic_int_set(&devc->acq_aborted, 1);
	while (g_atomic_int_get(&devc->num_transfers_used) &&
	       g_get_monotonic_time() < deadline) {
		cancel_ring(devc);
		g_usleep(5 * 1000);
	}
	if (g_atomic_int_get(&devc->num_transfers_used)) {
		/* Freeing a transfer still owned by libusb would be a
		 * use-after-free; leak it instead. */
		sr_err("Transfers did not drain; leaking them.");
		memset(devc->transfers, 0, sizeof(devc->transfers));
	} else {
		free_ring(devc);
	}
	release_capture(devc);
}

SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_trigger *trigger;

	int ret;

	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;
	usb = sdi->conn;

	if ((ret = slogic_dev_stop(sdi)) < 0) {
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

	g_atomic_int_set(&devc->acq_aborted, 0);
	g_atomic_int_set(&devc->num_transfers_used, 0);
	devc->dev_stopped = FALSE;
	devc->num_transfers_completed = 0;
	memset(devc->transfers, 0, sizeof(devc->transfers));
	devc->transfers_reached_nbytes = 0;
	devc->samples_sent = 0;
	devc->restart_pending = 0;

	/*
	 * Trigger state is read by the transfer callback, so it must be settled
	 * before the ring is submitted. The generic soft trigger sizes its
	 * pre-trigger buffer with a unit size counted over ALL logic channels
	 * (4 bytes for 32 ch), but samples are checked at the current channel
	 * mode's unit size (1 byte below 8 ch): fix both so the pre-trigger depth
	 * really is capture_ratio % of the limit.
	 */
	devc->capture_ratio = 10;
	devc->has_trigger = FALSE;
	devc->trigger_fired = TRUE;
	devc->stl = NULL;
	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int unitsize = (devc->cur_samplechannel + 7) / 8;
		uint64_t pre = devc->capture_ratio * devc->cur_limit_samples / 100;
		if (pre * unitsize > G_MAXINT / 2)
			pre = G_MAXINT / 2 / unitsize;
		devc->stl = soft_trigger_logic_new(sdi, trigger, (int)pre);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->stl->unitsize = unitsize;
		devc->stl->pre_trigger_size = unitsize * (int)pre;
		devc->has_trigger = TRUE;
		devc->trigger_fired = FALSE;
	}

	devc->raw_data_queue = g_async_queue_new();
	if (!devc->raw_data_queue) {
		sr_err("New g_async_queue failed, can't handle data anymore!");
		release_capture(devc);
		return SR_ERR_MALLOC;
	}

	if (submit_bulk_ring(sdi) != SR_OK) {
		abort_ring_sync(devc);
		return SR_ERR_IO;
	}

	/* Arm the shared stall watchdog with this capture's transfer plan. */
	slogic_transfer_plan plan = {
		.size_bytes = (uint32_t)devc->per_transfer_nbytes,
		.ring_count = (int)g_atomic_int_get(&devc->num_transfers_used),
		.expected_rate_bytes =
			(uint64_t)devc->expected_rate_MBps * SLOGIC_MHZ(1),
		.timeout_ms = 0,
	};
	slogic_stream_init(&devc->stream, &plan, devc->samples_need_nbytes);
	devc->stream.drop_left = devc->model->first_drop_bytes; /* per-model HW quirk */
	devc->restart_pending = 0;

	if ((ret = slogic_dev_start(sdi)) < 0) {
		sr_err("Unhandled `CMD_RUN`");
		/* Nothing was sent to the session yet: undo the submitted ring
		 * here, so no callback or queue outlives this failed start. */
		abort_ring_sync(devc);
		slogic_dev_stop(sdi);
		return ret;
	}

	devc->transfers_reached_time_start = g_get_monotonic_time();
	devc->transfers_reached_time_latest =
		devc->transfers_reached_time_start;
	devc->transfers_reached_time_first_data = 0; /* captured at the first byte */

	std_session_send_df_header(sdi);
	std_session_send_df_frame_begin(sdi);

	sr_session_source_add(sdi->session,
			      -1 * (size_t)drvc->sr_ctx->libusb_ctx, 0,
			      (devc->per_transfer_duration / 2) ?: 1,
			      handle_events, (void *)sdi);

	return SR_OK;
}

SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	/* Stop resubmitting and cancel what is in flight right away; the device
	 * keeps streaming otherwise. handle_events finishes the teardown (drops
	 * the untriggered backlog, sends CMD_STOP once, then SR_DF_END). */
	g_atomic_int_set(&devc->acq_aborted, 1);
	cancel_ring(devc);

	return SR_OK;
}
