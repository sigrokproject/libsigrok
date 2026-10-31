/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 cerebralmisfire <bram@cerebralmisfire.net>
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
#include <math.h>
#include <string.h>
#include "protocol.h"

/* Index is the device's scale code. */
SR_PRIV const uint64_t hantek_2xx2_vdivs[][2] = {
	{ 10, 1000 }, { 20, 1000 }, { 50, 1000 }, { 100, 1000 }, { 200, 1000 },
	{ 500, 1000 }, { 1, 1 }, { 2, 1 }, { 5, 1 }, { 10, 1 },
};
SR_PRIV const unsigned int hantek_2xx2_num_vdivs = ARRAY_SIZE(hantek_2xx2_vdivs);

/*
 * Index is the device's timebase code. The sample rate was checked against
 * a known signal from 500 ns to 500 ms per division; slower timebases
 * exist but are left out until checked.
 */
SR_PRIV const uint64_t hantek_2xx2_timebases[][2] = {
	{ 5, 1000000000 }, { 10, 1000000000 }, { 20, 1000000000 },
	{ 50, 1000000000 }, { 100, 1000000000 }, { 200, 1000000000 },
	{ 500, 1000000000 },
	{ 1, 1000000 }, { 2, 1000000 }, { 5, 1000000 }, { 10, 1000000 },
	{ 20, 1000000 }, { 50, 1000000 }, { 100, 1000000 }, { 200, 1000000 },
	{ 500, 1000000 },
	{ 1, 1000 }, { 2, 1000 }, { 5, 1000 }, { 10, 1000 }, { 20, 1000 },
	{ 50, 1000 }, { 100, 1000 }, { 200, 1000 }, { 500, 1000 },
};
SR_PRIV const unsigned int hantek_2xx2_num_timebases = ARRAY_SIZE(hantek_2xx2_timebases);

/* Index is the device's probe code. */
SR_PRIV const uint64_t hantek_2xx2_probe_factors[] = { 1, 10, 100, 1000 };
SR_PRIV const unsigned int hantek_2xx2_num_probe_factors = ARRAY_SIZE(hantek_2xx2_probe_factors);

SR_PRIV uint64_t hantek_2xx2_samplerate(const struct dev_context *devc)
{
	const uint64_t *tb = hantek_2xx2_timebases[devc->timebase];
	double rate = SAMPLES_PER_DIV * (double)tb[1] / (double)tb[0];

	return (uint64_t)MIN(rate, (double)MAX_SAMPLERATE);
}

SR_PRIV void hantek_2xx2_build_cmd(uint8_t *buf, uint16_t func, uint8_t cmd,
		uint8_t v0, uint8_t v1, uint8_t v2, uint8_t v3)
{
	buf[0] = 0x00;
	buf[1] = 0x0a;
	buf[2] = func & 0xff;
	buf[3] = func >> 8;
	buf[4] = cmd;
	buf[5] = v0;
	buf[6] = v1;
	buf[7] = v2;
	buf[8] = v3;
	buf[9] = 0x00;
}

SR_PRIV int hantek_2xx2_send(const struct sr_dev_inst *sdi, uint16_t func,
		uint8_t cmd, uint8_t value)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint8_t buf[CMD_LEN];
	int ret, sent;

	hantek_2xx2_build_cmd(buf, func, cmd, value, 0, 0, 0);
	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, buf, CMD_LEN, &sent,
			USB_TIMEOUT_MS);
	if (ret != 0) {
		sr_err("Failed to send command 0x%02x: %s.", cmd,
			libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

/*
 * The device cannot report its settings, so all of them are sent before
 * capturing.
 */
SR_PRIV int hantek_2xx2_apply_settings(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ch;

	if (hantek_2xx2_send(sdi, FUNC_SCREEN, 0, SCREEN_SCOPE) != SR_OK)
		return SR_ERR;

	for (ch = 0; ch < NUM_CHANNELS; ch++) {
		if (hantek_2xx2_send(sdi, FUNC_SCOPE_SETTING, CMD_COUPLING(ch),
				devc->coupling[ch]) != SR_OK
			|| hantek_2xx2_send(sdi, FUNC_SCOPE_SETTING, CMD_PROBE(ch),
				devc->probe[ch]) != SR_OK
			|| hantek_2xx2_send(sdi, FUNC_SCOPE_SETTING, CMD_SCALE(ch),
				devc->vdiv[ch]) != SR_OK
			|| hantek_2xx2_send(sdi, FUNC_SCOPE_SETTING, CMD_OFFSET(ch),
				OFFSET_CENTRE) != SR_OK)
			return SR_ERR;
	}
	if (hantek_2xx2_send(sdi, FUNC_SCOPE_SETTING, CMD_TIMEBASE,
			devc->timebase) != SR_OK)
		return SR_ERR;

	/*
	 * The device applies settings sent over USB but does not show them on
	 * its own screen; switching to the multimeter screen and back makes
	 * it redraw.
	 */
	if (hantek_2xx2_send(sdi, FUNC_SCREEN, 0, SCREEN_DMM) != SR_OK)
		return SR_ERR;
	g_usleep(300 * 1000);
	if (hantek_2xx2_send(sdi, FUNC_SCREEN, 0, SCREEN_SCOPE) != SR_OK)
		return SR_ERR;

	/* The device streams every channel enabled on it, interleaved. */
	for (ch = 0; ch < NUM_CHANNELS; ch++) {
		if (hantek_2xx2_send(sdi, FUNC_SCOPE_SETTING, CMD_ENABLE(ch),
				devc->ch_enabled[ch] ? 1 : 0) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

static void send_frame(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_channel *ch;
	GSList *l;
	float *data;
	size_t num_samples, i;
	int pos, digits;
	double volts_per_step;

	num_samples = devc->block_len / devc->num_enabled;
	if (num_samples == 0)
		return;
	data = g_malloc(num_samples * sizeof(float));

	std_session_send_df_frame_begin(sdi);

	/* Enabled channels appear in the data in channel order. */
	pos = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!devc->ch_enabled[ch->index])
			continue;

		volts_per_step = (double)hantek_2xx2_vdivs[devc->vdiv[ch->index]][0]
			/ hantek_2xx2_vdivs[devc->vdiv[ch->index]][1]
			* hantek_2xx2_probe_factors[devc->probe[ch->index]]
			/ STEPS_PER_DIV;
		for (i = 0; i < num_samples; i++) {
			data[i] = (devc->block[i * devc->num_enabled + pos] - ZERO_RAW)
				* volts_per_step;
		}

		digits = MAX(0, (int)ceil(-log10(volts_per_step)));
		sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
		analog.meaning->channels = g_slist_append(NULL, ch);
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.meaning->mqflags = 0;
		analog.num_samples = num_samples;
		analog.data = data;
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		g_slist_free(analog.meaning->channels);

		pos++;
	}

	std_session_send_df_frame_end(sdi);
	g_free(data);
	devc->frames_sent++;
}

/* The current block is complete; the next byte starts a new one. */
static void block_boundary(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->in_sync && devc->block_len > 0)
		send_frame(sdi);
	devc->block_len = 0;
	devc->in_sync = TRUE;
}

/*
 * Whether a request's round trip ended a block: well above the running
 * average. The first few packets only set the average.
 */
static gboolean slow_packet(struct dev_context *devc, double ms)
{
	gboolean slow;

	devc->packets_seen++;
	if (devc->packets_seen == 1)
		devc->packet_ms = ms;
	slow = devc->packets_seen > 4 && ms > BLOCK_SLOW_FACTOR * devc->packet_ms
		&& ms > BLOCK_SLOW_MIN_MS;
	if (!slow)
		devc->packet_ms += (ms - devc->packet_ms) * 0.1;

	return slow;
}

static void stop_on_error(const struct sr_dev_inst *sdi, const char *what,
		int status)
{
	struct dev_context *devc = sdi->priv;

	sr_err("%s: %s.", what, libusb_error_name(status));
	devc->transfer_active = FALSE;
	devc->state = STOPPING;
}

static int submit_request(const struct sr_dev_inst *sdi);

static void LIBUSB_CALL receive_packet(struct libusb_transfer *transfer)
{
	static const uint8_t marker[] = {
		0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
	};
	const struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	double ms;
	int len;

	if (devc->state != RUNNING) {
		devc->transfer_active = FALSE;
		return;
	}
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		stop_on_error(sdi, "Capture read failed", transfer->status);
		return;
	}

	ms = (g_get_monotonic_time() - devc->request_started) / 1000.0;
	len = transfer->actual_length;

	if (len == sizeof(marker) && memcmp(devc->in_buf, marker, len) == 0) {
		block_boundary(sdi);
	} else {
		if (devc->block_len + len > sizeof(devc->block)) {
			/* Missed a boundary; wait for the next one. */
			sr_dbg("Block overran %zu bytes, resyncing.", devc->block_len);
			devc->block_len = 0;
			devc->in_sync = FALSE;
		}
		if (devc->in_sync) {
			memcpy(devc->block + devc->block_len, devc->in_buf, len);
			devc->block_len += len;
		}
		if (slow_packet(devc, ms))
			block_boundary(sdi);
	}

	if (devc->limit_frames && devc->frames_sent >= devc->limit_frames) {
		sr_info("Requested number of frames reached.");
		devc->transfer_active = FALSE;
		devc->state = STOPPING;
		return;
	}

	if (submit_request(sdi) != SR_OK)
		stop_on_error(sdi, "Capture request failed", LIBUSB_ERROR_IO);
}

static void LIBUSB_CALL request_sent(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	if (devc->state != RUNNING) {
		devc->transfer_active = FALSE;
		return;
	}
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		stop_on_error(sdi, "Capture request failed", transfer->status);
		return;
	}

	/* Always read a whole packet: asking for less fails with an overflow. */
	libusb_fill_bulk_transfer(devc->xfer_in, usb->devhdl, EP_IN,
		devc->in_buf, PACKET_LEN, receive_packet, (void *)sdi,
		USB_TIMEOUT_MS);
	if ((ret = libusb_submit_transfer(devc->xfer_in)) != 0)
		stop_on_error(sdi, "Failed to submit capture read", ret);
}

/* One capture request is answered with one packet. */
static int submit_request(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint16_t half = REQUEST_BYTES / 2;
	int ret;

	hantek_2xx2_build_cmd(devc->out_buf, FUNC_SCOPE_CAPTURE, CMD_CAPTURE,
		half & 0xff, half >> 8, half & 0xff, half >> 8);
	libusb_fill_bulk_transfer(devc->xfer_out, usb->devhdl, EP_OUT,
		devc->out_buf, CMD_LEN, request_sent, (void *)sdi,
		USB_TIMEOUT_MS);
	devc->request_started = g_get_monotonic_time();
	if ((ret = libusb_submit_transfer(devc->xfer_out)) != 0) {
		sr_err("Failed to submit capture request: %s.",
			libusb_error_name(ret));
		return SR_ERR;
	}
	devc->transfer_active = TRUE;

	return SR_OK;
}

SR_PRIV int hantek_2xx2_start_capture(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->state = RUNNING;
	devc->block_len = 0;
	/* Data up to the first boundary is a partial block from before. */
	devc->in_sync = FALSE;
	devc->frames_sent = 0;
	devc->packets_seen = 0;

	return submit_request(sdi);
}

SR_PRIV int hantek_2xx2_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;
	struct timeval tv;

	(void)fd;
	(void)revents;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	if (devc->state == STOPPING && !devc->transfer_active) {
		usb_source_remove(sdi->session, drvc->sr_ctx);
		std_session_send_df_end(sdi);
		devc->state = IDLE;
	}

	return TRUE;
}
