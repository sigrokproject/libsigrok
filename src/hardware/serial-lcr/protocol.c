/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>
#include "protocol.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void send_frame_start(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct lcr_parse_info *info;
	uint64_t freq;
	const char *model;

	devc = sdi->priv;
	info = &devc->parse_info;

	/* Communicate changes of frequency or model before data values. */
	freq = info->output_freq;
	if (freq != devc->output_freq) {
		devc->output_freq = freq;
		sr_session_send_meta(sdi, SR_CONF_OUTPUT_FREQUENCY,
			g_variant_new_double(freq));
	}
	model = info->circuit_model;
	if (model && model != devc->circuit_model) {
		devc->circuit_model = model;
		sr_session_send_meta(sdi, SR_CONF_EQUIV_CIRCUIT_MODEL,
			g_variant_new_string(model));
	}

	/* Data is about to get sent. Start a new frame. */
	std_session_send_df_frame_begin(sdi);
}

static int handle_packet(struct sr_dev_inst *sdi, const uint8_t *pkt)
{
	struct dev_context *devc;
	struct lcr_parse_info *info;
	const struct lcr_info *lcr;
	size_t ch_idx;
	int rc;
	float value;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	gboolean frame;
	struct sr_channel *channel;

	devc = sdi->priv;
	info = &devc->parse_info;
	lcr = devc->lcr_info;

	/* Note: digits/spec_digits will be overridden later. */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	analog.num_samples = 1;
	analog.data = &value;

	frame = FALSE;
	for (ch_idx = 0; ch_idx < lcr->channel_count; ch_idx++) {
		channel = g_slist_nth_data(sdi->channels, ch_idx);
		analog.meaning->channels = g_slist_append(NULL, channel);
		info->ch_idx = ch_idx;
		rc = lcr->packet_parse(pkt, &value, &analog, info);
		if (sdi->session && rc == SR_OK && analog.meaning->mq && channel->enabled) {
			if (!frame) {
				send_frame_start(sdi);
				frame = TRUE;
			}
			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			sr_session_send(sdi, &packet);
		}
		g_slist_free(analog.meaning->channels);
	}
	if (frame) {
		std_session_send_df_frame_end(sdi);
		sr_sw_limits_update_frames_read(&devc->limits, 1);
	}

	return SR_OK;
}

static int handle_new_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	ssize_t rdsize;
	const struct lcr_info *lcr;
	uint8_t *pkt;
	size_t copy_len;

	devc = sdi->priv;
	serial = sdi->conn;

	/* Read another chunk of data into the buffer. */
	rdsize = sizeof(devc->buf) - devc->buf_rxpos;
	rdsize = serial_read_nonblocking(serial, &devc->buf[devc->buf_rxpos], rdsize);
	if (rdsize < 0)
		return SR_ERR_IO;
	devc->buf_rxpos += rdsize;

	/*
	 * Process as many packets as the buffer might contain. Assume
	 * that the stream is synchronized in the typical case. Re-sync
	 * in case of mismatch (skip individual bytes until data matches
	 * the expected packet layout again).
	 */
	lcr = devc->lcr_info;
	while (devc->buf_rxpos >= lcr->packet_size) {
		pkt = &devc->buf[0];
		if (!lcr->packet_valid(pkt)) {
			copy_len = devc->buf_rxpos - 1;
			memmove(&devc->buf[0], &devc->buf[1], copy_len);
			devc->buf_rxpos--;
			continue;
		}
		(void)handle_packet(sdi, pkt);
		copy_len = devc->buf_rxpos - lcr->packet_size;
		memmove(&devc->buf[0], &devc->buf[lcr->packet_size], copy_len);
		devc->buf_rxpos -= lcr->packet_size;
	}

	return SR_OK;
}

static int handle_timeout(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct lcr_info *lcr;
	struct sr_serial_dev_inst *serial;
	int64_t now;
	int ret;

	devc = sdi->priv;
	lcr = devc->lcr_info;

	if (!lcr->packet_request)
		return SR_OK;

	now = g_get_monotonic_time();
	if (devc->req_next_at && now < devc->req_next_at)
		return SR_OK;

	serial = sdi->conn;
	ret = lcr->packet_request(serial);
	if (ret < 0) {
		sr_err("Failed to request packet: %d.", ret);
		return ret;
	}

	if (lcr->req_timeout_ms)
		devc->req_next_at = now + lcr->req_timeout_ms * 1000;

	return SR_OK;
}

SR_PRIV int lcr_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int ret;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;
	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN)
		ret = handle_new_data(sdi);
	else
		ret = handle_timeout(sdi);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
	if (ret != SR_OK)
		return FALSE;

	return TRUE;
}
