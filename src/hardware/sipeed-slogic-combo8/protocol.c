/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 taorye <taorye@outlook.com>
 * Copyright (C) 2026 Lindolfo Meira <meiraa@gmail.com>
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
#include <string.h>
#include "protocol.h"

static int handle_events(int fd, int revents, void *cb_data);
static void finish_acquisition(struct sr_dev_inst *sdi);

/* Precomputed lookup tables for expanding 2ch and 4ch packed samples. */
static uint32_t lut_2ch[256];
static uint16_t lut_4ch[256];
static gboolean lut_initialized = FALSE;

static void init_unpack_luts(void)
{
	unsigned int b, j;

	if (lut_initialized)
		return;

	for (b = 0; b < 256; b++) {
		/* 2-channel mode: 4 samples per byte, 2 bits each. */
		uint32_t v32 = 0;
		for (j = 0; j < 4; j++)
			v32 |= (uint32_t)((b >> (j * 2)) & 0x03) << (j * 8);
		lut_2ch[b] = GUINT32_TO_LE(v32);

		/* 4-channel mode: 2 samples per byte, 4 bits each. */
		uint16_t v16 = 0;
		for (j = 0; j < 2; j++)
			v16 |= (uint16_t)((b >> (j * 4)) & 0x0f) << (j * 8);
		lut_4ch[b] = GUINT16_TO_LE(v16);
	}

	lut_initialized = TRUE;
}

static void unpack_samples(struct dev_context *devc, const uint8_t *src,
			   uint8_t *dst, size_t raw_len)
{
	size_t i;

	if (devc->cur_samplechannel == 2) {
		uint32_t *dst32 = (uint32_t *)dst;
		for (i = 0; i < raw_len; i++)
			dst32[i] = lut_2ch[src[i]];
	} else if (devc->cur_samplechannel == 4) {
		uint16_t *dst16 = (uint16_t *)dst;
		for (i = 0; i < raw_len; i++)
			dst16[i] = lut_4ch[src[i]];
	}
}

/*
 * Bytes produced per millisecond at the current samplerate and channel count.
 * cur_samplechannel bits per sample, 8 bits per byte, 1000 ms per second.
 */
static inline size_t to_bytes_per_ms(struct dev_context *devc)
{
	return (devc->cur_samplerate * devc->cur_samplechannel) / 8 / 1000;
}

/*
 * Per-transfer buffer size: fixed at 210 x 512 = 107,520 bytes (~2.7 ms at
 * 40 MB/s).  This must be a multiple of SIZE_MAX_EP_HS for USB bulk
 * alignment.
 *
 * We deliberately keep this fixed rather than scaling it with samplerate.
 * Each libusb bulk transfer maps to a kernel DMA allocation; on Linux the
 * default usbfs memory limit is 16 MB (/sys/module/usbcore/parameters/
 * usbfs_memory_mb).  With NUM_CONCURRENT_TRANSFERS = 16 transfers in flight,
 * total DMA usage is 16 x 107,520 ~ 1.7 MB -- well within the limit at all
 * samplerates.  Scaling the buffer size with samplerate would push this over
 * the limit at 80-160 MHz (e.g., 16 x 400 KB = 6.4 MB) and cause
 * LIBUSB_ERROR_NO_MEM at submission time.
 */
static inline size_t get_buffer_size(struct dev_context *devc)
{
	(void)devc;
	return 210 * SIZE_MAX_EP_HS; /* 107,520 bytes */
}

static inline size_t get_number_of_transfers(struct dev_context *devc)
{
	(void)devc;
	return NUM_CONCURRENT_TRANSFERS;
}

/*
 * Transfer timeout in milliseconds: time to fill the entire in-flight queue
 * (all concurrent transfers) at the current data rate, plus 25 % headroom.
 * A floor of 1000 ms ensures tolerance for OS scheduling latency, frontend
 * (PulseView) activity, and prevents premature USB transfer aborts.
 */
static inline size_t get_timeout(struct dev_context *devc)
{
	size_t bytes_per_ms = to_bytes_per_ms(devc);
	size_t total_size =
		get_buffer_size(devc) * get_number_of_transfers(devc);
	size_t timeout;

	if (bytes_per_ms == 0)
		bytes_per_ms = 1;

	timeout = total_size /
		  bytes_per_ms; /* ms to fill all in-flight transfers */
	timeout = timeout * 5 / 4; /* +25 % headroom */
	if (timeout < 1000)
		timeout = 1000; /* floor: 1000 ms */
	return timeout;
}

/*
 * Drain any stale data from the bulk IN endpoint until the device goes quiet
 * or the wall-clock deadline is reached.
 *
 * Strategy: use a short per-read timeout (CLEAR_EP_READ_TIMEOUT_MS) so that
 * the first empty read -- which signals the device has actually stopped
 * streaming -- is detected within ~10 ms rather than the original 100 ms.
 * A total wall-clock deadline (CLEAR_EP_TIMEOUT_MS) caps the worst case.
 *
 * Returns  0 if the endpoint was drained cleanly (a read returned 0 bytes),
 *         -1 if an error occurred or the deadline was reached before quiet.
 */
static int clear_ep(uint8_t ep, libusb_device_handle *usbh)
{
	uint8_t tmp[SIZE_MAX_EP_HS];
	int actual_length;
	int ret;
	gint64 deadline;

	sr_dbg("Clearing EP: %u", ep);

	deadline = g_get_monotonic_time() + CLEAR_EP_TIMEOUT_MS * 1000;

	do {
		actual_length = 0;
		ret = libusb_bulk_transfer(usbh, ep | LIBUSB_ENDPOINT_IN, tmp,
					   sizeof(tmp), &actual_length,
					   CLEAR_EP_READ_TIMEOUT_MS);
		if (ret < 0 && ret != LIBUSB_ERROR_TIMEOUT) {
			sr_warn("clear_ep: bulk transfer failed: %s",
				libusb_error_name(ret));
			return -1;
		}
	} while (actual_length > 0 && g_get_monotonic_time() < deadline);

	if (actual_length > 0) {
		sr_warn("clear_ep: device still streaming after %d ms deadline",
			CLEAR_EP_TIMEOUT_MS);
		return -1;
	}

	sr_dbg("Cleared EP: %u", ep);
	return 0;
}

static void submit_data(void *data, size_t len, struct sr_dev_inst *sdi)
{
	struct sr_datafeed_logic logic = {
		.length = len,
		.unitsize = 1,
		.data = data,
	};

	struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic,
	};

	sr_session_send(sdi, &packet);
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t i;

	if (!transfer)
		return;

	sdi = transfer->user_data;
	devc = sdi->priv;

	for (i = 0; i < NUM_CONCURRENT_TRANSFERS; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	if (devc->transfers_used > 0)
		devc->transfers_used--;
}

/*
 * Tear down a completed or aborted acquisition:
 *  - send SR_DF_END to the session
 *  - remove the USB event source from the main loop
 *  - free the bit-expansion buffer and software trigger logic
 */
static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_dev_driver *di;
	struct drv_context *drvc;

	if (!sdi)
		return;

	devc = sdi->priv;
	if (!devc || !devc->acq_running)
		return;

	devc->acq_running = FALSE;
	di = sdi->driver;
	drvc = di->context;

	sr_dbg("finish_acquisition: sending DF_END and removing USB source");

	usb_source_remove(sdi->session, drvc->sr_ctx);
	std_session_send_df_end(sdi);

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
	devc->trigger_fired = TRUE;

	if (devc->expand_buf) {
		g_free(devc->expand_buf);
		devc->expand_buf = NULL;
		devc->expand_buf_size = 0;
	}
}

static void pre_trigger_append_local(struct soft_trigger_logic *stl,
				     const uint8_t *buf, int len)
{
	if (!stl || stl->pre_trigger_size <= 0)
		return;

	if (len > stl->pre_trigger_size) {
		buf += len - stl->pre_trigger_size;
		len = stl->pre_trigger_size;
	}

	stl->pre_trigger_fill =
		MIN(stl->pre_trigger_fill + len, stl->pre_trigger_size);

	while (len > 0) {
		size_t size = MIN((size_t)(stl->pre_trigger_buffer +
					   stl->pre_trigger_size -
					   stl->pre_trigger_head),
				  (size_t)len);
		memcpy(stl->pre_trigger_head, buf, size);
		stl->pre_trigger_head += size;
		if (stl->pre_trigger_head >=
		    stl->pre_trigger_buffer + stl->pre_trigger_size)
			stl->pre_trigger_head = stl->pre_trigger_buffer;
		buf += size;
		len -= size;
	}
}

/*
 * Check if the current raw USB transfer cannot possibly match the trigger.
 * Fast-paths the common single-stage, single-channel trigger case by scanning
 * the packed data in 64-bit words without unpacking.
 */
static gboolean trigger_cannot_match(struct dev_context *devc,
				     const uint8_t *raw_buf, size_t raw_len)
{
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	uint64_t mask64, expected64, w_val;
	size_t n_words, k, i;
	int ch, prev_bit, match_type;
	uint8_t mask, expected_pattern;

	if (!devc->stl || !devc->stl->trigger || !devc->stl->trigger->stages)
		return FALSE;

	/* Only handle single-stage, single-match triggers. */
	if (devc->stl->trigger->stages->next)
		return FALSE;

	stage = devc->stl->trigger->stages->data;
	if (!stage || !stage->matches || stage->matches->next)
		return FALSE;

	match = stage->matches->data;
	if (!match || !match->channel || !match->channel->enabled)
		return FALSE;

	ch = match->channel->index;
	if (ch >= (int)devc->cur_samplechannel)
		return TRUE; /* Channel is disabled in this mode, cannot match. */

	if (devc->cur_samplechannel == 2) {
		mask = (ch == 0) ? 0x55 : 0xaa;
	} else if (devc->cur_samplechannel == 4) {
		mask = (1 << ch) | (1 << (ch + 4));
	} else if (devc->cur_samplechannel == 8) {
		mask = (1 << ch);
	} else {
		return FALSE;
	}

	prev_bit = (*devc->stl->prev_sample >> ch) & 1;
	match_type = match->match;

	if (match_type == SR_TRIGGER_FALLING) {
		if (devc->stl->count == 0 || prev_bit == 1)
			expected_pattern = mask; /* All 1s -> no 0 to fall to */
		else
			expected_pattern = 0; /* All 0s -> no 1 to fall from */
	} else if (match_type == SR_TRIGGER_RISING) {
		if (devc->stl->count == 0 || prev_bit == 0)
			expected_pattern = 0; /* All 0s -> no 1 to rise to */
		else
			expected_pattern =
				mask; /* All 1s -> no 0 to rise from */
	} else if (match_type == SR_TRIGGER_ZERO) {
		expected_pattern = mask; /* All 1s -> no 0 */
	} else if (match_type == SR_TRIGGER_ONE) {
		expected_pattern = 0; /* All 0s -> no 1 */
	} else if (match_type == SR_TRIGGER_EDGE) {
		if (devc->stl->count == 0)
			return FALSE; /* Need at least 1 sample to establish baseline */
		expected_pattern = prev_bit ? mask : 0;
	} else {
		return FALSE;
	}

	mask64 = 0x0101010101010101ULL * mask;
	expected64 = 0x0101010101010101ULL * expected_pattern;

	n_words = raw_len / 8;
	for (k = 0; k < n_words; k++) {
		memcpy(&w_val, raw_buf + k * 8, sizeof(w_val));
		if ((w_val & mask64) != expected64)
			return FALSE;
	}

	for (i = n_words * 8; i < raw_len; i++) {
		if ((raw_buf[i] & mask) != expected_pattern)
			return FALSE;
	}

	return TRUE;
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint8_t *ptr;
	size_t len;
	size_t step;
	size_t expanded_len;
	size_t bytes_to_transfer;
	int trigger_offset;
	int pre_trigger_samples;

	sdi = transfer->user_data;
	devc = sdi->priv;

	sr_dbg("USB status: %d, actual_length: %d", transfer->status,
	       transfer->actual_length);

	if (devc->acq_aborted) {
		free_transfer(transfer);
		if (devc->transfers_used == 0)
			finish_acquisition(sdi);
		return;
	}

	switch (transfer->status) {
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT:
		/*
		 * A completed or timed-out transfer that carried no data is
		 * evidence the device may have stalled.  Track consecutive
		 * empty results and abort once the threshold is reached so we
		 * don't loop forever; any data-carrying result means the
		 * stream is healthy and resets.
		 */
		if (transfer->actual_length == 0) {
			devc->empty_transfer_count++;
			sr_dbg("Empty transfer (%d/%d)",
			       devc->empty_transfer_count, MAX_EMPTY_TRANSFERS);
			if (devc->empty_transfer_count >= MAX_EMPTY_TRANSFERS) {
				sr_warn("Too many consecutive empty transfers, "
					"aborting acquisition");
				free_transfer(transfer);
				sipeed_slogic_combo8_acquisition_stop(sdi);
				if (devc->transfers_used == 0)
					finish_acquisition(sdi);
				return;
			}
		} else {
			devc->empty_transfer_count = 0;
		}
		break;

	case LIBUSB_TRANSFER_CANCELLED:
		free_transfer(transfer);
		if (devc->transfers_used == 0)
			finish_acquisition(sdi);
		return;

	case LIBUSB_TRANSFER_NO_DEVICE:
	default:
		sr_err("Transfer failed with status: %s",
		       libusb_error_name(transfer->status));
		free_transfer(transfer);
		sipeed_slogic_combo8_acquisition_stop(sdi);
		if (devc->transfers_used == 0)
			finish_acquisition(sdi);
		return;
	}

	sr_dbg("Transferring: %zu, transferred: %zu, sum: %zu/%zu",
	       devc->bytes_transferring, devc->bytes_transferred,
	       (devc->bytes_transferred + devc->bytes_transferring),
	       devc->bytes_need_transfer);

	devc->bytes_transferred += transfer->actual_length;
	if (devc->bytes_transferring >= (size_t)transfer->length)
		devc->bytes_transferring -= transfer->length;
	else
		devc->bytes_transferring = 0;

	if (transfer->actual_length > 0) {
		step = (devc->cur_samplechannel > 0) ?
			       (8 / devc->cur_samplechannel) :
			       1;
		expanded_len = (size_t)transfer->actual_length * step;

		/*
		 * Fast path: if the software trigger is armed, check whether the
		 * trigger channel is completely static in this transfer and
		 * cannot possibly match the trigger condition.  When the
		 * channel is idle, we bypass expansion and trigger checks.
		 */
		if (devc->stl && !devc->trigger_fired &&
		    trigger_cannot_match(devc, transfer->buffer,
					 (size_t)transfer->actual_length)) {
			devc->stl->count += expanded_len;

			/* Update prev_sample with the last sample of the transfer. */
			uint8_t last_byte =
				transfer->buffer[transfer->actual_length - 1];
			uint8_t last_sample =
				(last_byte >>
				 ((step - 1) * devc->cur_samplechannel)) &
				(0xff >> (8 - devc->cur_samplechannel));
			*devc->stl->prev_sample = last_sample;

			/* Maintain the circular pre-trigger buffer if enabled. */
			if (devc->stl->pre_trigger_size > 0) {
				size_t tail_samples = MIN(
					expanded_len,
					(size_t)devc->stl->pre_trigger_size);
				if (step == 1) {
					/* 8-channel mode: 1 sample per byte, zero-copy */
					pre_trigger_append_local(
						devc->stl,
						transfer->buffer +
							(transfer->actual_length -
							 tail_samples),
						(int)tail_samples);
				} else {
					/* Packed mode (2ch/4ch): expand into expand_buf */
					size_t tail_raw =
						(tail_samples + step - 1) / step;
					size_t start_raw =
						(size_t)transfer->actual_length -
						tail_raw;
					unpack_samples(
						devc,
						transfer->buffer + start_raw,
						devc->expand_buf, tail_raw);
					pre_trigger_append_local(
						devc->stl,
						devc->expand_buf +
							(tail_raw * step -
							 tail_samples),
						(int)tail_samples);
				}
			}
			goto resubmit;
		}

		ptr = transfer->buffer;
		len = (size_t)transfer->actual_length;

		if (devc->cur_samplechannel != 8 &&
		    devc->cur_samplechannel > 0) {
			unpack_samples(devc, transfer->buffer, devc->expand_buf,
				       len);
			ptr = devc->expand_buf;
			len = expanded_len;
		}

		/*
		 * Software trigger handling.  Before the trigger fires, all
		 * data is consumed by the soft-trigger logic, which keeps a
		 * pre-trigger ring buffer and itself sends the pre-trigger
		 * data plus SR_DF_TRIGGER once the pattern matches.  After it
		 * fires, deliver the remaining data up to the sample budget.
		 */
		if (devc->stl && !devc->trigger_fired) {
			trigger_offset = soft_trigger_logic_check(
				devc->stl, ptr, (int)len,
				&pre_trigger_samples);
			if (trigger_offset >= 0) {
				devc->trigger_fired = TRUE;
				devc->samples_sent =
					(uint64_t)pre_trigger_samples;
				ptr += trigger_offset;
				len -= (size_t)trigger_offset;
			} else {
				len = 0; /* Consumed by pre-trigger buffer */
			}
		}

		/*
		 * Deliver samples up to the configured limit for both
		 * triggered and non-triggered acquisitions.
		 */
		if (devc->trigger_fired && len > 0) {
			if (devc->samples_need > 0 &&
			    len > devc->samples_need - devc->samples_sent)
				len = devc->samples_need - devc->samples_sent;

			if (len > 0) {
				submit_data(ptr, len, sdi);
				devc->samples_sent += len;
			}
		}
	}

resubmit:
	/* Stop acquisition as soon as the sample budget is fulfilled. */
	if (devc->trigger_fired && devc->samples_need > 0 &&
	    devc->samples_sent >= devc->samples_need)
		sipeed_slogic_combo8_acquisition_stop(sdi);

	/* Determine how many bytes to request in the next submission. */
	bytes_to_transfer = 0;
	if (devc->bytes_need_transfer > 0) {
		if (devc->bytes_transferred + devc->bytes_transferring <
		    devc->bytes_need_transfer) {
			bytes_to_transfer = devc->bytes_need_transfer -
					    (devc->bytes_transferred +
					     devc->bytes_transferring);
		}
	} else {
		/* Continuous mode: keep the pipeline full. */
		bytes_to_transfer = devc->transfers_buffer_size;
	}

	if (bytes_to_transfer > devc->transfers_buffer_size)
		bytes_to_transfer = devc->transfers_buffer_size;

	if (bytes_to_transfer > 0 && !devc->acq_aborted) {
		transfer->length = bytes_to_transfer;
		transfer->actual_length = 0;
		transfer->timeout = devc->timeout;
		ret = libusb_submit_transfer(transfer);
		if (ret != LIBUSB_SUCCESS) {
			sr_warn("Failed to resubmit transfer: %s",
				libusb_error_name(ret));
			free_transfer(transfer);
		} else {
			devc->bytes_transferring += bytes_to_transfer;
		}
	} else {
		free_transfer(transfer);
	}

	if (devc->transfers_used == 0) {
		if (devc->acq_aborted) {
			finish_acquisition(sdi);
		} else {
			sr_dbg("All transfers completed normally, stopping "
			       "acquisition");
			sipeed_slogic_combo8_acquisition_stop(sdi);
			if (devc->transfers_used == 0)
				finish_acquisition(sdi);
		}
	}
}

static int handle_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct timeval tv;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	drvc = ((struct sr_dev_driver *)sdi->driver)->context;

	/*
	 * Non-blocking poll for libusb events.  GLib's main context already
	 * monitors the libusb file descriptors; blocking here would stall
	 * the frontend UI thread.
	 */
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
					       NULL);

	return TRUE;
}

SR_PRIV int
sipeed_slogic_combo8_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	struct sr_trigger *trigger;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct sr_datafeed_packet packet;
	uint8_t *dev_buf;
	uint8_t cmd[4];
	uint16_t rate;
	size_t num_transfers;
	size_t bytes_to_transfer;
	size_t step;
	size_t rem;
	uint64_t samples_in_bytes;
	uint64_t pts;
	int pre_trigger_samples;
	int ret;

	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;
	usb = sdi->conn;

	init_unpack_luts();

	sr_dbg("Samplerate: %" PRIu64 "MHz@%" PRIu64 "ch, samples: %" PRIu64,
	       devc->cur_samplerate / SR_MHZ(1), devc->cur_samplechannel,
	       devc->limit_samples);

	/*
	 * Drain any stale data the device may still be streaming from a
	 * previous acquisition before starting a new one.
	 */
	if (clear_ep(EP_IN, usb->devhdl) < 0) {
		sr_err("Device endpoint not idle after drain attempt; "
		       "try unplugging and replugging the device.");
		return SR_ERR_IO;
	}

	devc->acq_running = FALSE;
	devc->acq_aborted = FALSE;
	devc->bytes_need_transfer = 0;
	devc->bytes_transferring = 0;
	devc->bytes_transferred = 0;
	devc->transfers_used = 0;
	devc->empty_transfer_count = 0;
	memset(devc->transfers, 0, sizeof(devc->transfers));

	devc->transfers_buffer_size = get_buffer_size(devc);
	num_transfers = get_number_of_transfers(devc);
	devc->timeout = get_timeout(devc);

	sr_dbg("transfers_buffer_size: %zu, num_transfers: %zu, timeout: %" PRIu64
	       "ms",
	       devc->transfers_buffer_size, num_transfers, devc->timeout);

	/* Compute the total number of samples to deliver this capture. */
	devc->samples_need = devc->limit_samples;
	if (devc->limit_msec > 0) {
		devc->samples_need =
			devc->limit_msec * devc->cur_samplerate / 1000;
		sr_info("Capture limited to %" PRIu64 " samples "
			"(%" PRIu64 " ms)",
			devc->samples_need, devc->limit_msec);
	}
	devc->samples_sent = 0;

	/*
	 * Pre-allocate the bit-expansion buffer for packed 2ch/4ch modes.
	 * Allocated here so incoming transfers always have a valid target buffer.
	 */
	devc->expand_buf = NULL;
	devc->expand_buf_size = 0;
	if (devc->cur_samplechannel != 8 && devc->cur_samplechannel > 0) {
		step = 8 / devc->cur_samplechannel;
		devc->expand_buf_size = devc->transfers_buffer_size * step;
		devc->expand_buf = g_try_malloc(devc->expand_buf_size);
		if (!devc->expand_buf) {
			sr_err("Failed to allocate bit-expansion buffer (%zu bytes)",
			       devc->expand_buf_size);
			return SR_ERR_MALLOC;
		}
	}

	/*
	 * Software trigger: while armed we can't predict when it will fire,
	 * so the bulk pipeline runs continuously and the sample budget stops
	 * it once the post-trigger data has been delivered.
	 */
	devc->stl = NULL;
	devc->trigger_fired = TRUE;
	trigger = sr_session_trigger_get(sdi->session);
	if (trigger) {
		devc->trigger_fired = FALSE;
		pts = 0;
		if (devc->samples_need > 0) {
			pts = (devc->capture_ratio * devc->samples_need) / 100;
		} else {
			pts = ((devc->cur_samplerate / 10) *
			       devc->capture_ratio) /
			      100;
		}
		if (pts > MAX_PRE_TRIGGER_SAMPLES)
			pts = MAX_PRE_TRIGGER_SAMPLES;
		pre_trigger_samples = (int)pts;
		devc->stl = soft_trigger_logic_new(sdi, trigger,
						   pre_trigger_samples);
		if (!devc->stl) {
			sr_err("Failed to allocate software trigger logic");
			g_free(devc->expand_buf);
			devc->expand_buf = NULL;
			return SR_ERR_MALLOC;
		}
		sr_info("Software trigger armed, %d pre-trigger samples",
			pre_trigger_samples);
	}

	/* Compute total bytes to transfer from the sample limit if no trigger. */
	if (devc->trigger_fired && devc->samples_need > 0) {
		samples_in_bytes =
			devc->samples_need * devc->cur_samplechannel / 8;
		devc->bytes_need_transfer =
			samples_in_bytes / devc->transfers_buffer_size;
		devc->bytes_need_transfer +=
			!!(samples_in_bytes % devc->transfers_buffer_size);
		devc->bytes_need_transfer *= devc->transfers_buffer_size;
	} else {
		devc->bytes_need_transfer = 0; /* Continuous or triggered mode */
	}

	ret = usb_source_add(sdi->session, drvc->sr_ctx, 10, handle_events,
			     (void *)sdi);
	if (ret != SR_OK) {
		sr_err("Failed to add USB event source");
		if (devc->stl) {
			soft_trigger_logic_free(devc->stl);
			devc->stl = NULL;
		}
		g_free(devc->expand_buf);
		devc->expand_buf = NULL;
		return ret;
	}

	devc->acq_running = TRUE;

	/*
	 * Submit exactly num_transfers concurrent bulk IN transfers, up to
	 * however many are needed to cover bytes_need_transfer.
	 */
	while (devc->transfers_used < num_transfers) {
		if (devc->bytes_need_transfer > 0 &&
		    (devc->bytes_transferred + devc->bytes_transferring >=
		     devc->bytes_need_transfer))
			break;

		dev_buf = g_try_malloc(devc->transfers_buffer_size);
		if (!dev_buf) {
			sr_err("Failed to allocate memory for USB buffer");
			break;
		}

		transfer = libusb_alloc_transfer(0);
		if (!transfer) {
			g_free(dev_buf);
			sr_err("Failed to allocate libusb transfer");
			break;
		}

		bytes_to_transfer = devc->transfers_buffer_size;
		if (devc->bytes_need_transfer > 0) {
			rem = devc->bytes_need_transfer -
			      (devc->bytes_transferred +
			       devc->bytes_transferring);
			if (bytes_to_transfer > rem)
				bytes_to_transfer = rem;
		}

		libusb_fill_bulk_transfer(transfer, usb->devhdl,
					  EP_IN | LIBUSB_ENDPOINT_IN, dev_buf,
					  bytes_to_transfer, receive_transfer,
					  (void *)sdi, devc->timeout);
		transfer->actual_length = 0;

		ret = libusb_submit_transfer(transfer);
		if (ret != LIBUSB_SUCCESS) {
			sr_warn("Failed to submit transfer: %s",
				libusb_error_name(ret));
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			break;
		}

		devc->transfers[devc->transfers_used] = transfer;
		devc->bytes_transferring += bytes_to_transfer;
		devc->transfers_used++;
	}

	if (devc->transfers_used == 0) {
		sr_err("Failed to submit any USB transfers");
		finish_acquisition((struct sr_dev_inst *)sdi);
		return SR_ERR_IO;
	}

	sr_dbg("Submitted %zu transfers", devc->transfers_used);

	std_session_send_df_header(sdi);

	/*
	 * Report the actual samplerate through a META packet.
	 */
	src = sr_config_new(SR_CONF_SAMPLERATE,
			    g_variant_new_uint64(devc->cur_samplerate));
	meta.config = g_slist_append(NULL, src);

	packet.type = SR_DF_META;
	packet.payload = &meta;
	sr_session_send(sdi, &packet);

	g_slist_free(meta.config);
	sr_config_free(src);

	/*
	 * Send CMD_START.
	 */
	rate = (uint16_t)(devc->cur_samplerate / SR_MHZ(1));
	cmd[0] = rate & 0xff;
	cmd[1] = rate >> 8;
	cmd[2] = (uint8_t)devc->cur_samplechannel;
	cmd[3] = 0;

	ret = libusb_control_transfer(
		usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
		CMD_START, 0x0000, 0x0000, cmd, sizeof(cmd), 500);
	if (ret < 0) {
		sr_err("Unable to send start command: %s",
		       libusb_error_name(ret));
		sipeed_slogic_combo8_acquisition_stop(
			(struct sr_dev_inst *)sdi);
		return SR_ERR_IO;
	}

	sr_dbg("CMD_START sent successfully");
	return SR_OK;
}

SR_PRIV int sipeed_slogic_combo8_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	size_t i;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	devc->acq_aborted = TRUE;

	/*
	 * Do not send CMD_STOP here: it is unstable on the Combo8 and can
	 * wedge the control endpoint.  Cancelling the in-flight bulk transfers
	 * is sufficient to stop the acquisition; any remaining streamed data
	 * is drained at the start of the next acquisition.
	 */
	for (i = 0; i < NUM_CONCURRENT_TRANSFERS; i++) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}

	return SR_OK;
}
