/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Magnus Lucchese <magnus.lucchese@gmail.com>
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

/*
 * The 18 standard samplerates supported by the vendor software,
 * derived from the 60 MHz master clock via: f = 60 MHz / (divider + 1).
 *
 * In 4-channel mode (CH0..CH3), all 18 rates including 30 MHz are available.
 * In 8-channel mode (CH0..CH7), 30 MHz is disabled due to FT245 FIFO bandwidth limits.
 */
const uint64_t dla8_samplerates_4ch[] = {
	SR_MHZ(30),
	SR_MHZ(15),
	SR_HZ(7500000),
	SR_MHZ(4),
	SR_MHZ(2),
	SR_MHZ(1),
	SR_KHZ(500),
	346820,
	SR_KHZ(250),
	SR_KHZ(200),
	172910,
	115384,
	SR_KHZ(100),
	57636,
	SR_KHZ(50),
	43227,
	28804,
	SR_KHZ(25),
};

const size_t dla8_num_samplerates_4ch = ARRAY_SIZE(dla8_samplerates_4ch);
const uint64_t *dla8_samplerates_8ch = &dla8_samplerates_4ch[1];
const size_t dla8_num_samplerates_8ch = ARRAY_SIZE(dla8_samplerates_4ch) - 1;

SR_PRIV int dla8_ftdi_init(struct dev_context *devc)
{
	int ret;

	if (!devc || !devc->ftdic)
		return SR_ERR_ARG;

	if ((ret = ftdi_usb_reset(devc->ftdic)) < 0) {
		sr_err("Failed to reset FTDI: %s", ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	if ((ret = ftdi_tcioflush(devc->ftdic)) < 0) {
		sr_err("Failed to flush FTDI FIFOs: %s", ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	if ((ret = ftdi_setflowctrl(devc->ftdic, SIO_RTS_CTS_HS)) < 0) {
		sr_err("Failed to set RTS/CTS flow control: %s", ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	if ((ret = ftdi_setdtr_rts(devc->ftdic, 1, 1)) < 0) {
		sr_err("Failed to assert DTR/RTS: %s", ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	if ((ret = ftdi_set_latency_timer(devc->ftdic, DLA8_LATENCY_MS)) < 0) {
		sr_err("Failed to set latency timer: %s", ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	if ((ret = ftdi_read_data_set_chunksize(devc->ftdic, DLA8_READ_CHUNK)) < 0) {
		sr_err("Failed to set read chunk size: %s", ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	devc->ftdic->usb_read_timeout = DLA8_USB_TIMEOUT_MS;

	/* Send STOP byte to ensure FPGA is disarmed if left running */
	uint8_t stop_cmd = DLA8_CMD_STOP;
	(void)ftdi_write_data(devc->ftdic, &stop_cmd, 1);
	ftdi_tcioflush(devc->ftdic);

	/* Drain any residual bytes in FIFO (up to 50 ms) */
	gint64 drain_deadline = g_get_monotonic_time() + 50000;
	while (g_get_monotonic_time() < drain_deadline) {
		uint8_t trash[512];
		ret = ftdi_read_data(devc->ftdic, trash, sizeof(trash));
		if (ret <= 0)
			break;
	}

	return SR_OK;
}

static int send_and_expect_echo(struct ftdi_context *ftdic, uint8_t byte)
{
	uint8_t rx_buf[16];
	int ret;
	gint64 deadline;

	ret = ftdi_write_data(ftdic, &byte, 1);
	if (ret < 1) {
		sr_err("Failed to write handshake byte 0x%02x: %s",
			byte, ftdi_get_error_string(ftdic));
		return SR_ERR_IO;
	}

	deadline = g_get_monotonic_time() + (DLA8_ECHO_TIMEOUT_MS * 1000);
	while (g_get_monotonic_time() < deadline) {
		ret = ftdi_read_data(ftdic, rx_buf, 1);
		if (ret > 0) {
			if (rx_buf[0] == byte)
				return SR_OK;
			sr_spew("Discarding residual byte 0x%02x while waiting for echo 0x%02x",
				rx_buf[0], byte);
		} else {
			g_usleep(1000);
		}
	}

	sr_err("Handshake timeout waiting for echo 0x%02x", byte);
	return SR_ERR_TIMEOUT;
}

SR_PRIV int dla8_handshake(struct dev_context *devc)
{
	int ret;

	if (!devc || !devc->ftdic)
		return SR_ERR_ARG;

	sr_spew("Sending handshake byte 0xaa...");
	if ((ret = send_and_expect_echo(devc->ftdic, DLA8_CMD_PING1)) != SR_OK)
		return ret;

	sr_spew("Sending handshake byte 0xab...");
	if ((ret = send_and_expect_echo(devc->ftdic, DLA8_CMD_PING2)) != SR_OK)
		return ret;

	sr_info("ES-DLA-8 hardware handshake successful");
	return SR_OK;
}

SR_PRIV int dla8_set_samplerate(struct dev_context *devc, uint64_t rate)
{
	uint32_t divider;

	if (!devc)
		return SR_ERR_ARG;

	if (rate == 0 || rate > DLA8_BASE_CLOCK_HZ) {
		sr_err("Samplerate %" PRIu64 " out of range (1 - 60 MHz)", rate);
		return SR_ERR_ARG;
	}

	divider = (uint32_t)((DLA8_BASE_CLOCK_HZ / rate) - 1);
	if (divider > 0xffff)
		divider = 0xffff;

	devc->rate_divider = (uint16_t)divider;
	devc->cur_samplerate = DLA8_BASE_CLOCK_HZ / (divider + 1);

	sr_dbg("Set samplerate: requested %" PRIu64 " Hz, divider 0x%04x, actual %" PRIu64 " Hz",
		rate, devc->rate_divider, devc->cur_samplerate);

	return SR_OK;
}

SR_PRIV size_t dla8_build_config(const struct dev_context *devc, uint8_t depth_m, uint8_t *buf, gboolean is_4ch)
{
	size_t idx = 0;

	if (!devc || !buf)
		return 0;

	/* 1. Reset / init FPGA configuration */
	buf[idx++] = DLA8_CMD_INIT;

	/* 2. Channel enable: 4 channels (CH0..CH3) or all 8 channels (CH0..CH7) */
	uint8_t num_ch = is_4ch ? DLA8_NUM_CHANNELS_4CH : DLA8_NUM_CHANNELS;
	for (uint8_t ch = 0; ch < num_ch; ch++) {
		buf[idx++] = DLA8_CMD_ENABLE_CH;
		buf[idx++] = ch;
	}

	/* 3. Stream mode: linear uncompressed RAW */
	buf[idx++] = DLA8_CMD_MODE_RAW;

	/* 4. Clock source and edge */
	if (devc->ext_clock)
		buf[idx++] = devc->ext_clock_falling ? DLA8_CMD_CLK_EXT_NEG : DLA8_CMD_CLK_EXT_POS;
	else
		buf[idx++] = DLA8_CMD_CLK_INT;

	/* 5. Fixed delimiter before divider */
	buf[idx++] = DLA8_CMD_DELIMITER;

	/* 6. Clock divider (16-bit little-endian) */
	buf[idx++] = DLA8_CMD_SET_DIVIDER;
	buf[idx++] = (uint8_t)(devc->rate_divider & 0xff);
	buf[idx++] = (uint8_t)((devc->rate_divider >> 8) & 0xff);

	/* 7. Hardware depth in Mega-samples */
	buf[idx++] = DLA8_CMD_SET_DEPTH;
	buf[idx++] = depth_m;

	/* 8. Arm & start acquisition */
	buf[idx++] = DLA8_CMD_ARM;

	return idx;
}

SR_PRIV int dla8_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t cfg_buf[32];
	size_t cfg_len, expected_cfg_len;
	uint8_t depth_m;
	int ret;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	devc->is_4ch = dla8_is_4ch_mode(sdi);

	/* Guard: 30 MHz is only supported when 4 or fewer channels (CH0..CH3) are active */
	if (!devc->is_4ch && devc->cur_samplerate > SR_MHZ(15)) {
		sr_err("30 MHz samplerate is only supported when 4 or fewer channels (CH0-CH3) are enabled");
		return SR_ERR_ARG;
	}

	devc->sent_samples = 0;
	devc->trigger_fired = (devc->stl == NULL);
	devc->throughput_warned = FALSE;
	devc->acq_start_us = g_get_monotonic_time();
	devc->last_rx_us = devc->acq_start_us;

	/*
	 * Determine hardware Mega-sample depth:
	 * If waiting for a trigger or in continuous mode, arm with maximum depth (255 Mi).
	 * If a finite sample limit is configured, request the ceiling Mega-sample count.
	 * In 4-channel mode, 2 samples are packed per byte, so bytes needed is ceil(samples / 2).
	 */
	if (devc->stl != NULL) {
		depth_m = DLA8_DEPTH_MAX;
	} else if (devc->sw_limits.limit_samples > 0) {
		uint64_t m = (devc->sw_limits.limit_samples + DLA8_DEPTH_UNIT - 1) / DLA8_DEPTH_UNIT;
		if (m > DLA8_DEPTH_MAX)
			depth_m = DLA8_DEPTH_MAX;
		else if (m < 1)
			depth_m = 1;
		else
			depth_m = (uint8_t)m;
	} else {
		depth_m = DLA8_DEPTH_MAX;
	}

	devc->total_samples_target = (uint64_t)depth_m * DLA8_DEPTH_UNIT;

	cfg_len = dla8_build_config(devc, depth_m, cfg_buf, devc->is_4ch);
	expected_cfg_len = devc->is_4ch ? DLA8_CONFIG_PACKET_LEN_4CH : DLA8_CONFIG_PACKET_LEN_8CH;
	if (cfg_len != expected_cfg_len) {
		sr_err("Internal error building config packet: length %zu != %zu",
			cfg_len, expected_cfg_len);
		return SR_ERR_BUG;
	}

	sr_dbg("Starting acquisition (%s-channel mode): depth_m=%u (%" PRIu64 " samples target), divider=0x%04x",
		devc->is_4ch ? "4" : "8", depth_m, devc->total_samples_target, devc->rate_divider);

	/* Flush any residual data before arming */
	ftdi_tcioflush(devc->ftdic);

	ret = ftdi_write_data(devc->ftdic, cfg_buf, (int)cfg_len);
	if (ret < (int)cfg_len) {
		sr_err("Failed to write configuration packet: %s",
			ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	return SR_OK;
}

SR_PRIV int dla8_stop(struct dev_context *devc)
{
	uint8_t stop_cmd = DLA8_CMD_STOP;
	int ret;

	if (!devc || !devc->ftdic)
		return SR_ERR_ARG;

	sr_dbg("Sending STOP (0x%02x) command to hardware", stop_cmd);

	ret = ftdi_write_data(devc->ftdic, &stop_cmd, 1);
	if (ret < 1)
		sr_warn("Failed to write stop byte: %s", ftdi_get_error_string(devc->ftdic));

	/* Flush input FIFO */
	ftdi_tciflush(devc->ftdic);

	return SR_OK;
}

static void send_samples(const struct sr_dev_inst *sdi, const uint8_t *samples, size_t count)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (count == 0)
		return;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = count;
	logic.unitsize = 1;
	logic.data = (void *)samples;

	sr_session_send(sdi, &packet);
}

SR_PRIV int dla8_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int bytes_read;
	uint8_t *sample_buf;
	size_t sample_count;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return FALSE;

	while (!sr_sw_limits_check(&devc->sw_limits) &&
	       devc->sent_samples < devc->total_samples_target) {
		bytes_read = ftdi_read_data(devc->ftdic, devc->buf, sizeof(devc->buf));
		if (bytes_read == LIBUSB_ERROR_TIMEOUT || bytes_read == 0)
			break;
		if (bytes_read < 0) {
			sr_err("FTDI read error (%d): %s", bytes_read, ftdi_get_error_string(devc->ftdic));
			sr_dev_acquisition_stop(sdi);
			return FALSE;
		}

		devc->last_rx_us = g_get_monotonic_time();

		if (devc->is_4ch) {
			/*
			 * 4-channel mode: unpack 2 samples per byte.
			 * Low nibble [3:0] is sample 2N (earlier),
			 * High nibble [7:4] is sample 2N+1 (later).
			 */
			for (int i = 0; i < bytes_read; i++) {
				devc->unpack_buf[2 * i]     = devc->buf[i] & 0x0f;
				devc->unpack_buf[2 * i + 1] = (devc->buf[i] >> 4) & 0x0f;
			}
			sample_buf = devc->unpack_buf;
			sample_count = (size_t)bytes_read * 2;
		} else {
			sample_buf = devc->buf;
			sample_count = (size_t)bytes_read;
		}

		/* Process software trigger if active */
		if (devc->stl && !devc->trigger_fired) {
			int trigger_offset, pre_trigger_samples = 0;

			trigger_offset = soft_trigger_logic_check(devc->stl, sample_buf, sample_count,
				&pre_trigger_samples);

			if (trigger_offset > -1) {
				devc->trigger_fired = TRUE;
				sr_info("Trigger matched at sample offset %d (pre-trigger samples: %d)",
					trigger_offset, pre_trigger_samples);

				devc->sent_samples += pre_trigger_samples;
				sr_sw_limits_update_samples_read(&devc->sw_limits, pre_trigger_samples);

				/* Send samples from trigger offset forward */
				const uint8_t *post_trig = sample_buf + trigger_offset;
				size_t post_len = sample_count - (size_t)trigger_offset;

				if (devc->sw_limits.limit_samples > 0) {
					uint64_t remaining = devc->sw_limits.limit_samples - devc->sw_limits.samples_read;
					if (post_len > remaining)
						post_len = (size_t)remaining;
				}

				send_samples(sdi, post_trig, post_len);
				devc->sent_samples += post_len;
				sr_sw_limits_update_samples_read(&devc->sw_limits, post_len);
			}
		} else {
			/* Trigger already fired or no trigger specified */
			size_t to_send = sample_count;

			/* Clamp to limit_samples if configured */
			if (devc->sw_limits.limit_samples > 0) {
				uint64_t remaining = devc->sw_limits.limit_samples - devc->sw_limits.samples_read;
				if (to_send > remaining)
					to_send = (size_t)remaining;
			}

			send_samples(sdi, sample_buf, to_send);
			devc->sent_samples += to_send;
			sr_sw_limits_update_samples_read(&devc->sw_limits, to_send);
		}
	}

	/* Check limits */
	if (sr_sw_limits_check(&devc->sw_limits)) {
		sr_info("Requested sample or time limit reached, stopping acquisition");
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}

	if (devc->sent_samples >= devc->total_samples_target) {
		sr_info("Hardware sample block target (%" PRIu64 " samples) reached",
			devc->total_samples_target);
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}

	/* Inactivity timeout check: if data was flowing and then stopped */
	if (devc->sent_samples > 0 && !devc->ext_clock) {
		gint64 elapsed_since_rx = g_get_monotonic_time() - devc->last_rx_us;
		if (elapsed_since_rx > DLA8_DATA_TIMEOUT_US) {
			sr_warn("Acquisition stream stalled after %" PRIu64 " samples (timeout), stopping",
				devc->sent_samples);
			sr_dev_acquisition_stop(sdi);
			return FALSE;
		}
	}

	return TRUE;
}
