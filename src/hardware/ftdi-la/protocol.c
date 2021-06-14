/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Sergey Alirzaev <zl29ah@gmail.com>
 * Copyright (C) 2021 Thomas Hebb <tommyhebb@gmail.com>
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

/* Timeout (in ms) of non-data USB transfers. Data transfers use a timeout
 * dynamically calculated from transfer size and sample rate. */
#define USB_TIMEOUT 100

/* Target duration (in ms) of samples to fetch in a single USB transfer. */
#define MS_PER_TRANSFER 10

/* Target size (in ms) of the entire ring buffer of transfers. Represents
 * maximum expected userspace scheduling latency. */
#define BUFFER_SIZE_MS 250

#define MIN_TRANSFER_BUFFERS 2
#define MAX_TRANSFER_BUFFERS 32

/* Definitions taken from libftdi and Linux's ftdi_sio.h. */
#define VENDOR_OUT (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT)
#define VENDOR_IN (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN)

#define REQ_RESET		0x00
#define RESET_SIO		0

#define REQ_SET_BAUD_RATE	0x03

#define REQ_SET_LATENCY_TIMER	0x09

#define REQ_SET_BITMODE		0x0b
#define SET_BITMODE_BITBANG	1
/* TODO: Support MPSSE mode? */

#define NUM_STATUS_BYTES	2

#define DIV_ROUND_UP(x, d) (((x) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))
#define AVG(a, b) (((a) + (b)) / 2)

static void stop_acquisition(const struct sr_dev_inst *sdi);

static struct clock_config get_closest_config(uint32_t requested_rate,
		const struct ftdi_chip_desc *chip, int iface_idx)
{
	const uint8_t fraction_codes[8] = {0, 3, 2, 4, 1, 5, 6, 7};

	struct clock_config res;

	uint32_t bb_clock; /* bitbang clock */
	uint32_t twothirds_clock;
	uint32_t half_clock;

	/* Low divisor values for bitbang mode don't work on most of the chips
	 * I've tested and instead seem to alias higher values. For example,
	 * setting a divisor of 1 (register value 0) on the FT2232H ought to
	 * produce a 60MHz clock, but instead we only seem to sample at 15MHz
	 * (equivalent to a divisor of 4) on channel A and 12MHz (equivalent
	 * to a divisor of 5) on channel B. Look up the highest clock rate
	 * known to work correctly and clamp to it. */
	if (requested_rate > chip->max_sample_rates[iface_idx])
		requested_rate = chip->max_sample_rates[iface_idx];

	/* Increments of 0.125: divisor_eighths = divisor * 8 */
	uint32_t divisor_eighths;

	bb_clock = chip->base_clock / chip->bitbang_divisor;

	twothirds_clock = (bb_clock * 2) / 3;
	half_clock = bb_clock / 2;

	if (requested_rate > AVG(bb_clock, twothirds_clock)) {
		/* Special integral divisor 0 means no division */
		res.rate_millihz = bb_clock * 1000ull;
		res.encoded_divisor = 0;
	} else if (requested_rate > AVG(twothirds_clock, half_clock)) {
		/* Special integral divisor 1 means multiply by 2/3 */
		res.rate_millihz = twothirds_clock * 1000ull;
		res.encoded_divisor = 1;
	} else if (requested_rate > half_clock) {
		/* Set integral divisor 2, which is not special-cased. */
		res.rate_millihz = half_clock * 1000ull;
		res.encoded_divisor = 2;
	} else {
		/* Calculate fractional divisor. */
		divisor_eighths = DIV_ROUND_CLOSEST(bb_clock * 8, requested_rate);

		/* Clamp if too large for register. */
		divisor_eighths = MIN(divisor_eighths, 0x1ffff);

		res.rate_millihz = (bb_clock * 8000ull) / divisor_eighths;
		res.encoded_divisor =
			(fraction_codes[divisor_eighths & 0x7] << 14) |
			(divisor_eighths >> 3);
	}

	/* H-series chips need bit 17 of encoded_divisor to be set in order to
	 * set UART rates higher than the FT232R-compatible max rate of 3Mbaud.
	 * However, this bit does not appear to have any effect for bitbang mode
	 * in my tests, so I've skipped setting it to simplify the code. */

	return res;
}

SR_PRIV unsigned int ftdi_la_cur_samplerate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	return DIV_ROUND_CLOSEST(devc->cur_clk.rate_millihz, 1000);
}

SR_PRIV void ftdi_la_store_samplerate(const struct sr_dev_inst *sdi, uint64_t requested_rate)
{
	struct dev_context *devc = sdi->priv;

	uint64_t requested_rate_millihz = requested_rate * 1000ull;

	devc->cur_clk = get_closest_config(requested_rate, devc->desc, devc->usb_iface_idx);

	if (requested_rate_millihz != devc->cur_clk.rate_millihz) {
		sr_warn("Chip does not support sample rate %" PRIu64
			"; adjusted to %" PRIu64 ".%03" PRIu64 ".",
			requested_rate, devc->cur_clk.rate_millihz / 1000,
			devc->cur_clk.rate_millihz % 1000);
	} else {
		sr_info("Configured exact sample rate %" PRIu64 ".", requested_rate);
	}
}

static int write_samplerate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	uint16_t index_val;
	int ret;

	if (devc->desc->multi_iface)
		index_val = ((devc->cur_clk.encoded_divisor >> 16) << 8) | devc->ftdi_iface_idx;
	else
		index_val = devc->cur_clk.encoded_divisor >> 16;

	ret = libusb_control_transfer(usb->devhdl, VENDOR_OUT, REQ_SET_BAUD_RATE,
			devc->cur_clk.encoded_divisor & 0xffff, index_val, NULL, 0, USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to set sample rate: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static void send_samples(const struct sr_dev_inst *sdi, unsigned char *buf, size_t len)
{
	struct dev_context *devc = sdi->priv;
	size_t pkt_len, data_len, remaining = 0;
	unsigned char *pkt_buf;

	if (devc->limit_samples)
		remaining = devc->limit_samples - devc->samples_sent;

	for (size_t i = 0; i < len; i += devc->in_ep_pkt_size) {
		pkt_buf = buf + i;
		pkt_len = MIN(len - i, devc->in_ep_pkt_size);
		if (pkt_len < NUM_STATUS_BYTES) {
			sr_warn("Received data packet with no modem status prefix!");
			continue;
		}

		/* Ignore the modem status bytes. The only flag they contain
		 * that's relevant to us is "Receive Overflow Error", but that
		 * flag appears never to be set during bitbang operation and
		 * additionally is sometimes set on the very first read after
		 * transitioning into bitbang mode, even when we've just purged
		 * the buffers, so all it does it cause false alarms. */

		data_len = pkt_len - NUM_STATUS_BYTES;
		if (data_len == 0) {
			sr_info("Received empty data packet");
			continue;
		}

		if (devc->limit_samples && data_len > remaining)
			data_len = remaining;

		struct sr_datafeed_logic logic = {
			.length = data_len,
			.unitsize = 1,
			.data = pkt_buf + NUM_STATUS_BYTES,
		};

		struct sr_datafeed_packet packet = {
			.type = SR_DF_LOGIC,
			.payload = &logic,
		};

		sr_spew("Sending %zu samples.", data_len);
		sr_session_send(sdi, &packet);

		devc->samples_sent += data_len;
		if (devc->limit_samples) {
			remaining -= data_len;
			if (remaining == 0) {
				sr_info("Requested number of samples reached.");
				stop_acquisition(sdi);
				break;
			}
		}
	}
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	int ret;

	sr_spew("receive_transfer called");

	if (devc->acq_aborted || transfer->status == LIBUSB_TRANSFER_CANCELLED)
		goto cleanup_transfer;

	if (transfer->status == LIBUSB_TRANSFER_ERROR ||
			transfer->status == LIBUSB_TRANSFER_NO_DEVICE ||
			transfer->status == LIBUSB_TRANSFER_STALL) {
		sr_err("USB transfer failed: %s.", libusb_error_name(transfer->status));
		stop_acquisition(sdi);
		goto cleanup_transfer;
	}

	sr_spew("Processing completed transfer of length %d.", transfer->actual_length);
	send_samples(sdi, transfer->buffer, transfer->actual_length);

	/* Check again, since send_samples() may have aborted acquisition. */
	if (!devc->acq_aborted) {
		/* Resubmit */
		ret = libusb_submit_transfer(transfer);
		if (ret != 0) {
			sr_err("USB transfer submission failed: %s.", libusb_error_name(ret));
			stop_acquisition(sdi);
			goto cleanup_transfer;
		}

		return;
	}

cleanup_transfer:
	g_free(transfer->buffer);
	transfer->buffer = NULL; /* Stop libusb from trying to free it too. */
	libusb_free_transfer(transfer);

	for (size_t i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	if (--devc->active_transfers == 0) {
		devc->num_transfers = 0;
		g_free(devc->transfers);
		sr_info("Freed all transfer allocations.");

		usb_source_remove(sdi->session, sdi->session->ctx);
	}
}

static int alloc_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	size_t num_xfers;
	unsigned int packets_per_xfer, samples_per_xfer, bytes_per_xfer, timeout;
	unsigned char *buf;
	unsigned int cur_samplerate = ftdi_la_cur_samplerate(sdi);

	/* The numerator here is samples per second multiplied by seconds per
	 * transfer, which simplifies to samples per transfer. Divide that by
	 * samples per packet to get packets per transfer. */
	packets_per_xfer = DIV_ROUND_UP((cur_samplerate * MS_PER_TRANSFER) / 1000,
			devc->in_ep_pkt_size - NUM_STATUS_BYTES);
	/* Without status byte overhead. */
	samples_per_xfer = packets_per_xfer * (devc->in_ep_pkt_size - NUM_STATUS_BYTES);
	/* With status byte overhead. */
	bytes_per_xfer = packets_per_xfer * devc->in_ep_pkt_size;

	/* Enough to hold about BUFFER_SIZE_MS ms of samples. */
	num_xfers = cur_samplerate / samples_per_xfer;
	num_xfers = (num_xfers * BUFFER_SIZE_MS) / 1000;
	num_xfers = CLAMP(num_xfers, MIN_TRANSFER_BUFFERS, MAX_TRANSFER_BUFFERS);

	sr_dbg("Using %zu USB transfers of size %u.", num_xfers, bytes_per_xfer);

	timeout = (num_xfers * samples_per_xfer * 1000ull) / cur_samplerate;
	timeout += timeout / 4; /* 25% safety margin */

	devc->transfers = g_try_malloc0_n(num_xfers, sizeof(*devc->transfers));
	if (!devc->transfers) {
		sr_err("Failed to allocate USB transfer pointers.");
		return SR_ERR_MALLOC;
	}
	devc->num_transfers = num_xfers;
	devc->active_transfers = num_xfers;

	for (size_t i = 0; i < num_xfers; i++) {
		buf = g_try_malloc(bytes_per_xfer);
		devc->transfers[i] = libusb_alloc_transfer(0);

		if (!buf || !devc->transfers[i]) {
			sr_err("Ran out of memory while allocating transfers.");
			return SR_ERR_MALLOC;
		}

		libusb_fill_bulk_transfer(devc->transfers[i], usb->devhdl,
				devc->in_ep_addr, buf, bytes_per_xfer,
				receive_transfer, (void *)sdi, timeout);
	}

	return SR_OK;
}

static int handle_event(int fd, int revents, void *cb_data)
{
	(void)fd;
	(void)revents;

	const struct sr_dev_inst *sdi = cb_data;
	int ret;

	struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

	ret = libusb_handle_events_timeout(sdi->session->ctx->libusb_ctx, &tv);
	if (ret != 0) {
		sr_err("libusb event handling failed: %s.", libusb_error_name(ret));
		stop_acquisition(sdi);
		return FALSE;
	}

	return TRUE;
}

SR_PRIV int ftdi_la_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	/* Reset the chip */
	ret = libusb_control_transfer(usb->devhdl, VENDOR_OUT, REQ_RESET,
			RESET_SIO, devc->ftdi_iface_idx, NULL, 0, USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to reset FTDI chip: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	/* Set sample rate */
	ret = write_samplerate(sdi);
	if (ret != SR_OK)
		return ret;

	/* Set bitbang mode, all pins input */
	uint16_t mode = (SET_BITMODE_BITBANG << 8) | 0x00;
	ret = libusb_control_transfer(usb->devhdl, VENDOR_OUT, REQ_SET_BITMODE,
			mode, devc->ftdi_iface_idx, NULL, 0, USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to put FTDI chip into bitbang mode: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Set the latency timer to MS_PER_TRANSFER. This ensures that, at low
	 * sample rates, the chip doesn't buffer samples for so long that the
	 * delay is user-visible and that, at high sample rates, it has time to
	 * completely fill its buffer before the timer expires, meaning our
	 * large bulk transfers won't get aborted early by a short read.
	 *
	 * Note that we'd have to explicitly set the latency timer even if we
	 * wanted the default value of 16ms, as the reset command above does
	 * not reset it. */
	ret = libusb_control_transfer(usb->devhdl, VENDOR_OUT, REQ_SET_LATENCY_TIMER,
			MS_PER_TRANSFER, devc->ftdi_iface_idx, NULL, 0, USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to set FTDI latency timer: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Reset internal variables before every new acquisition. */
	devc->samples_sent = 0;
	devc->acq_aborted = FALSE;

	ret = alloc_transfers(sdi);
	if (ret != SR_OK)
		return ret;

	ret = usb_source_add(sdi->session, sdi->session->ctx, -1, handle_event,
			(void *)sdi);
	if (ret != SR_OK)
		return ret;

	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK)
		return ret;

	ret = 0;
	for (size_t i = 0; i < devc->num_transfers; i++) {
		if (ret == 0) {
			/* If we haven't failed yet, submit the next transfer. */
			ret = libusb_submit_transfer(devc->transfers[i]);

			/* After the first failure, abort and cancel all started
			 * transfers, which will cause them to be torn down in
			 * their callbacks. */
			if (ret != 0) {
				sr_err("USB transfer initial submission failed: %s.",
						libusb_error_name(ret));
				stop_acquisition(sdi);
			}
		}

		if (ret != 0) {
			/* If we failed (on this iteration or a previous one),
			 * manually call the callback, which will notice that
			 * acq_aborted is set and cleanly free the transfer. */
			receive_transfer(devc->transfers[i]);
		}
	}

	return SR_OK;
}

static void stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	sr_info("Stopping acquisition.");

	devc->acq_aborted = TRUE;

	for (ssize_t i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}

	std_session_send_df_end(sdi);
}

SR_PRIV int ftdi_la_stop_acquisition(struct sr_dev_inst *sdi)
{
	stop_acquisition(sdi);
	return SR_OK;
}
