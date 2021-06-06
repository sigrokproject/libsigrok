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
#include <libusb.h>
#include "protocol.h"

/* Timeout (in ms) of non-data USB transfers. Data transfers use a timeout
 * dynamically calculated from transfer size and sample rate. */
#define USB_TIMEOUT 100

/* Target duration (in ms) of samples to fetch in a single USB transfer. */
#define MS_PER_TRANSFER 100

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

struct clock_config {
	uint64_t rate_millihz;
	uint32_t encoded_divisor;
};

static void stop_acquisition(const struct sr_dev_inst *sdi);

static struct clock_config get_closest_config(uint32_t requested_rate,
		const struct ftdi_chip_desc *chip)
{
	const uint8_t fraction_codes[8] = {0, 3, 2, 4, 1, 5, 6, 7};

	struct clock_config res;

	uint32_t bb_clock; /* bitbang clock */
	uint32_t twothirds_clock;
	uint32_t half_clock;

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

SR_PRIV int ftdi_la_set_samplerate(const struct sr_dev_inst *sdi, uint64_t requested_rate)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	struct clock_config config;
	uint64_t requested_rate_millihz = requested_rate * 1000ull;
	uint16_t index_val;
	int ret;

	config = get_closest_config(requested_rate, devc->desc);

	devc->cur_samplerate = DIV_ROUND_CLOSEST(config.rate_millihz, 1000);

	if (requested_rate_millihz != config.rate_millihz) {
		sr_warn("Chip does not support sample rate %" PRIu64
			"; adjusted to %" PRIu64 ".%03" PRIu64 ".",
			requested_rate, config.rate_millihz / 1000,
			config.rate_millihz % 1000);
	} else {
		sr_info("Configured exact sample rate %" PRIu64 ".", requested_rate);
	}

	if (devc->desc->multi_iface)
		index_val = ((config.encoded_divisor >> 16) << 8) | devc->ftdi_iface_idx;
	else
		index_val = config.encoded_divisor >> 16;

	ret = libusb_control_transfer(usb->devhdl, VENDOR_OUT, REQ_SET_BAUD_RATE,
			config.encoded_divisor & 0xffff, index_val, NULL, 0, USB_TIMEOUT);
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

SR_PRIV int ftdi_la_receive_data(int fd, int revents, void *cb_data)
{
	(void)fd;

	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;

	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	unsigned int timeout;
	int bytes_read;
	int ret;

	sr_spew("recv_data called");

	timeout = (devc->data_buf_size * 1000ull) / devc->cur_samplerate;
	timeout += timeout / 4; /* 25% safety margin */

	ret = libusb_bulk_transfer(usb->devhdl,
			 devc->in_ep_addr, devc->data_buf, devc->data_buf_size,
			 &bytes_read, timeout);
	if (ret < 0) {
		sr_err("Failed to read FTDI data: %s.", libusb_error_name(ret));
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}

	send_samples(sdi, devc->data_buf, bytes_read);

	return TRUE;
}

SR_PRIV int ftdi_la_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	unsigned int packets_per_xfer, bytes_per_xfer;
	int ret;

	/* Reset the chip */
	ret = libusb_control_transfer(usb->devhdl, VENDOR_OUT, REQ_RESET,
			RESET_SIO, devc->ftdi_iface_idx, NULL, 0, USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Failed to reset FTDI chip: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

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

	/* The numerator here is samples per second multiplied by seconds per
	 * transfer, which simplifies to samples per transfer. Divide that by
	 * samples per packet to get packets per transfer. */
	packets_per_xfer = DIV_ROUND_UP((devc->cur_samplerate * MS_PER_TRANSFER) / 1000,
			devc->in_ep_pkt_size - NUM_STATUS_BYTES);
	bytes_per_xfer = packets_per_xfer * devc->in_ep_pkt_size;

	devc->data_buf_size = bytes_per_xfer;
	devc->data_buf = g_malloc0(bytes_per_xfer);

	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK)
		return ret;

	/* Hook up a dummy handler to receive data from the device. */
	ret = sr_session_source_add(sdi->session, -1, G_IO_IN, 0,
			ftdi_la_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static void stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	sr_info("Stopping acquisition.");

	sr_session_source_remove(sdi->session, -1);

	std_session_send_df_end(sdi);

	g_free(devc->data_buf);
}

SR_PRIV int ftdi_la_stop_acquisition(struct sr_dev_inst *sdi)
{
	stop_acquisition(sdi);
	return SR_OK;
}
