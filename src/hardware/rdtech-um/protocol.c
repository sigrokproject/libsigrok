/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018-2020 Andreas Sandberg <andreas@sandberg.pp.se>
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

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "libsigrok-internal.h"
#include "protocol.h"

/* Read/write timeouts, poll request intervals. */
#define PROBE_TO_MS 1000
#define WRITE_TO_MS 1
#define POLL_PERIOD_MS 100

/* Expected receive data size for poll responses. */
#define POLL_RECV_LEN 130

/* Command code to request another poll response. */
#define UM_CMD_POLL 0xf0

static const struct rdtech_um_channel_desc default_channels[] = {
	{ "V", { 2, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "I", { 4, BVT_BE_UINT16, }, { 1, 1e3, }, 3, SR_MQ_CURRENT, SR_UNIT_AMPERE },
	{ "D+", { 96, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "D-", { 98, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "T", { 10, BVT_BE_UINT16, }, { 1, 1, }, 0, SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS },
	/* Threshold-based recording (mWh) */
	{ "E", { 106, BVT_BE_UINT32, }, { 1, 1e3, }, 3, SR_MQ_ENERGY, SR_UNIT_WATT_HOUR },
};

static const struct rdtech_um_channel_desc um25c_channels[] = {
	{ "V", { 2, BVT_BE_UINT16, }, { 1, 1e3, }, 3, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "I", { 4, BVT_BE_UINT16, }, { 100, 1e6, }, 4, SR_MQ_CURRENT, SR_UNIT_AMPERE },
	{ "D+", { 96, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "D-", { 98, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "T", { 10, BVT_BE_UINT16, }, { 1, 1, }, 0, SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS },
	/* Threshold-based recording (mWh) */
	{ "E", { 106, BVT_BE_UINT32, }, { 1, 1e3, }, 3, SR_MQ_ENERGY, SR_UNIT_WATT_HOUR },
};

static gboolean csum_ok_fff1(const uint8_t *buf, size_t len)
{
	uint16_t csum_recv;

	if (len != POLL_RECV_LEN)
		return FALSE;

	csum_recv = read_u16be(&buf[len - sizeof(uint16_t)]);
	if (csum_recv != 0xfff1)
		return FALSE;

	return TRUE;
}

static gboolean csum_ok_um34c(const uint8_t *buf, size_t len)
{
	static const int positions[] = {
		1, 3, 7, 9, 15, 17, 19, 23, 31, 39, 41, 45, 49, 53,
		55, 57, 59, 63, 67, 69, 73, 79, 83, 89, 97, 99, 109,
		111, 113, 119, 121, 127,
	};

	size_t i;
	uint8_t csum_calc, csum_recv;

	if (len != POLL_RECV_LEN)
		return FALSE;

	csum_calc = 0;
	for (i = 0; i < ARRAY_SIZE(positions); i++)
		csum_calc ^= buf[positions[i]];
	csum_recv = read_u8(&buf[len - sizeof(uint8_t)]);
	if (csum_recv != csum_calc)
		return FALSE;

	return TRUE;
}

static const struct rdtech_um_profile um_profiles[] = {
	{ "UM24C", RDTECH_UM24C, ARRAY_AND_SIZE(default_channels), csum_ok_fff1, },
	{ "UM25C", RDTECH_UM25C, ARRAY_AND_SIZE(um25c_channels), csum_ok_fff1, },
	{ "UM34C", RDTECH_UM34C, ARRAY_AND_SIZE(default_channels), csum_ok_um34c, },
};

static const struct rdtech_um_profile *find_profile(uint16_t id)
{
	size_t i;
	const struct rdtech_um_profile *profile;

	for (i = 0; i < ARRAY_SIZE(um_profiles); i++) {
		profile = &um_profiles[i];
		if (profile->model_id == id)
			return profile;
	}
	return NULL;
}

SR_PRIV const struct rdtech_um_profile *rdtech_um_probe(struct sr_serial_dev_inst *serial)
{
	const struct rdtech_um_profile *p;
	uint8_t req;
	int ret;
	uint8_t buf[RDTECH_UM_BUFSIZE];
	int rcvd;
	uint16_t model_id;

	req = UM_CMD_POLL;
	ret = serial_write_blocking(serial, &req, sizeof(req), WRITE_TO_MS);
	if (ret < 0) {
		sr_err("Failed to send probe request.");
		return NULL;
	}

	rcvd = serial_read_blocking(serial, buf, POLL_RECV_LEN, PROBE_TO_MS);
	if (rcvd != POLL_RECV_LEN) {
		sr_err("Failed to read probe response.");
		return NULL;
	}

	model_id = read_u16be(&buf[0]);
	p = find_profile(model_id);
	if (!p) {
		sr_err("Unrecognized UM device (0x%.4" PRIx16 ").", model_id);
		return NULL;
	}

	if (!p->csum_ok(buf, rcvd)) {
		sr_err("Probe response fails checksum verification.");
		return NULL;
	}

	return p;
}

SR_PRIV int rdtech_um_poll(const struct sr_dev_inst *sdi, gboolean force)
{
	struct dev_context *devc;
	int64_t now, elapsed;
	struct sr_serial_dev_inst *serial;
	uint8_t req;
	int ret;

	/* Don't send request when receive data is being accumulated. */
	devc = sdi->priv;
	if (!force && devc->buflen)
		return SR_OK;

	/* Check for expired intervals or forced requests. */
	now = g_get_monotonic_time() / 1000;
	elapsed = now - devc->cmd_sent_at;
	if (!force && elapsed < POLL_PERIOD_MS)
		return SR_OK;

	/* Send another poll request. Update interval only on success. */
	serial = sdi->conn;
	req = UM_CMD_POLL;
	ret = serial_write_blocking(serial, &req, sizeof(req), WRITE_TO_MS);
	if (ret < 0) {
		sr_err("Unable to send poll request.");
		return SR_ERR;
	}
	devc->cmd_sent_at = now;

	return SR_OK;
}

static int process_data(struct sr_dev_inst *sdi,
	const uint8_t *data, size_t dlen)
{
	struct dev_context *devc;
	const struct rdtech_um_profile *p;
	size_t ch_idx;
	float v;
	int ret;

	devc = sdi->priv;
	p = devc->profile;

	sr_spew("Received poll packet (len: %zu).", dlen);
	if (dlen < POLL_RECV_LEN) {
		sr_err("Insufficient response data length: %zu", dlen);
		return SR_ERR_DATA;
	}

	if (!p->csum_ok(data, POLL_RECV_LEN)) {
		sr_err("Packet checksum verification failed.");
		return SR_ERR_DATA;
	}

	ret = SR_OK;
	std_session_send_df_frame_begin(sdi);
	for (ch_idx = 0; ch_idx < p->channel_count; ch_idx++) {
		ret = bv_get_value_len(&v, &p->channels[ch_idx].spec, data, dlen);
		if (ret != SR_OK)
			break;
		ret = feed_queue_analog_submit_one(devc->feeds[ch_idx], v, 1);
		if (ret != SR_OK)
			break;
	}
	std_session_send_df_frame_end(sdi);

	sr_sw_limits_update_frames_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return ret;
}

static int accum_data(struct sr_dev_inst *sdi, struct sr_serial_dev_inst *serial)
{
	struct dev_context *devc;
	const struct rdtech_um_profile *p;
	uint8_t *rdptr;
	size_t space, rcvd, rdlen;
	int ret;
	gboolean do_sync_check;
	size_t sync_len, sync_idx;

	/*
	 * Receive data became available. Drain the serial transport.
	 * Grab incoming data in as large a chunk as possible. Also
	 * copes with zero receive data length, as some transports may
	 * trigger periodically without data really being available.
	 */
	devc = sdi->priv;
	p = devc->profile;
	rdptr = &devc->buf[devc->buflen];
	space = sizeof(devc->buf) - devc->buflen;
	do_sync_check = FALSE;
	sync_len = sizeof(uint16_t);
	while (space) {
		ret = serial_read_nonblocking(serial, rdptr, space);
		if (ret < 0)
			return SR_ERR_IO;
		rcvd = (size_t)ret;
		if (rcvd == 0)
			break;
		if (rcvd > space)
			return SR_ERR_BUG;
		if (devc->buflen < sync_len)
			do_sync_check = TRUE;
		devc->buflen += rcvd;
		if (devc->buflen < sync_len)
			do_sync_check = FALSE;
		space -= rcvd;
		rdptr += rcvd;
	}

	/*
	 * Synchronize to the packetized input stream. Check the model
	 * ID at the start of receive data. Which is a weak condition,
	 * but going out of sync should be rare, and repeated attempts
	 * to synchronize should eventually succeed. Try to rate limit
	 * the emission of diagnostics messages. (Re-)run this logic
	 * at the first reception which makes enough data available,
	 * but not during subsequent accumulation of more data.
	 *
	 * Reducing redundancy in the implementation at the same time as
	 * increasing robustness would involve the creation of a checker
	 * routine, which just gets called for every byte position until
	 * it succeeds. Similar to what a previous implementation of the
	 * read loop did, which was expensive on the serial transport.
	 */
	sync_idx = 0;
	if (do_sync_check && read_u16be(&devc->buf[sync_idx]) != p->model_id)
		sr_warn("Unexpected response data, trying to synchronize.");
	while (do_sync_check) {
		if (sync_idx + sync_len >= devc->buflen)
			break;
		if (read_u16be(&devc->buf[sync_idx]) == p->model_id)
			break;
		sync_idx++;
	}
	if (do_sync_check && sync_idx) {
		sr_dbg("Skipping %zu bytes in attempt to sync.", sync_idx);
		sync_len = devc->buflen - sync_idx;
		if (sync_len)
			memmove(&devc->buf[0], &devc->buf[sync_idx], sync_len);
		devc->buflen -= sync_idx;
	}

	/*
	 * Process packets as their reception completes. Periodically
	 * re-transmit poll requests. Discard consumed data after all
	 * processing has completed.
	 */
	rdptr = devc->buf;
	rdlen = devc->buflen;
	ret = SR_OK;
	while (ret == SR_OK && rdlen >= POLL_RECV_LEN) {
		ret = process_data(sdi, rdptr, rdlen);
		if (ret != SR_OK) {
			sr_err("Processing response packet failed.");
			break;
		}
		rdptr += POLL_RECV_LEN;
		rdlen -= POLL_RECV_LEN;

		if (0 && !sr_sw_limits_check(&devc->limits))
			(void)rdtech_um_poll(sdi, FALSE);
	}
	rcvd = rdptr - devc->buf;
	devc->buflen -= rcvd;
	if (devc->buflen)
		memmove(&devc->buf[0], rdptr, devc->buflen);

	return ret;
}

SR_PRIV int rdtech_um_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;
	if (!(devc = sdi->priv))
		return TRUE;

	/*
	 * Drain and process receive data as it becomes available.
	 * Terminate acquisition upon receive or processing error.
	 */
	serial = sdi->conn;
	if (revents == G_IO_IN) {
		ret = accum_data(sdi, serial);
		if (ret != SR_OK) {
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		}
	}

	/* Check configured acquisition limits. */
	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	/* Periodically retransmit measurement requests. */
	(void)rdtech_um_poll(sdi, FALSE);

	return TRUE;
}
