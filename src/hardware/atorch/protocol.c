/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Mathieu Pilato <pilato.mathieu@free.fr>
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

/* Duration of scan. */
#define ATORCH_PROBE_TIMEOUT_MS	10000

/*
 * Message layout:
 * 2 magic header bytes
 * 1 message type byte
 * N payload bytes, determined by message type
 */

/* Position of message type byte in a message. */
#define HEADER_MSGTYPE_IDX	2
#define PAYLOAD_START_IDX	3

/* Length of each message type. */
#define MSGLEN_REPORT	(4 + 32)
#define MSGLEN_REPLY	(4 + 4)
#define MSGLEN_COMMAND	(4 + 6)

/* Minimal length of a valid message. */
#define MSGLEN_MIN	4

static const uint8_t header_magic[] = {
	0xff, 0x55,
};

static const struct atorch_channel_desc atorch_dc_power_meter_channels[] = {
	{ "V", { 4, BVT_BE_UINT24, }, { 100, 1e3, }, 1, SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC, },
	{ "I", { 7, BVT_BE_UINT24, }, { 1, 1e3, }, 3, SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC, },
	{ "C", { 10, BVT_BE_UINT24, }, { 10, 1e3, }, 2, SR_MQ_ENERGY, SR_UNIT_AMPERE_HOUR, 0, },
	{ "E", { 13, BVT_BE_UINT32, }, { 10, 1, }, -2, SR_MQ_ENERGY, SR_UNIT_WATT_HOUR, 0, },
	{ "T", { 24, BVT_BE_UINT16, }, { 1, 1, }, 0, SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS, 0, },
};

static const struct atorch_channel_desc atorch_usb_power_meter_channels[] = {
	{ "V", { 4, BVT_BE_UINT24, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC, },
	{ "I", { 7, BVT_BE_UINT24, }, { 10, 1e3, }, 2, SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC, },
	{ "C", { 10, BVT_BE_UINT24, }, { 1, 1e3, }, 3, SR_MQ_ENERGY, SR_UNIT_AMPERE_HOUR, 0, },
	{ "E", { 13, BVT_BE_UINT32, }, { 10, 1e3, }, 2, SR_MQ_ENERGY, SR_UNIT_WATT_HOUR, 0, },
	{ "D-", { 17, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC, },
	{ "D+", { 19, BVT_BE_UINT16, }, { 10, 1e3, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC, },
	{ "T", { 21, BVT_BE_UINT16, }, { 1, 1, }, 0, SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS, 0, },
};

static const struct atorch_device_profile atorch_profiles[] = {
	{ 0x02, "DC Meter", ARRAY_AND_SIZE(atorch_dc_power_meter_channels), },
	{ 0x03, "USB Meter", ARRAY_AND_SIZE(atorch_usb_power_meter_channels), },
};

static size_t get_length_for_msg_type(uint8_t msg_type)
{
	switch (msg_type) {
	case MSG_REPORT:
		return MSGLEN_REPORT;
	case MSG_REPLY:
		return MSGLEN_REPLY;
	case MSG_COMMAND:
		return MSGLEN_COMMAND;
	default:
		return 0;
	}
}

static void log_atorch_msg(const uint8_t *buf, size_t len)
{
	GString *text;

	if (sr_log_loglevel_get() < SR_LOG_DBG)
		return;

	text = sr_hexdump_new(buf, len);
	sr_dbg("Atorch msg: %s", text->str);
	sr_hexdump_free(text);
}

static const uint8_t *locate_next_valid_msg(struct dev_context *devc)
{
	uint8_t *valid_msg_ptr;
	size_t valid_msg_len;
	uint8_t *msg_ptr;

	/* Enough byte to make a message? */
	while (devc->rd_idx + MSGLEN_MIN <= devc->wr_idx) {
		/* Look for header magic. */
		msg_ptr = devc->buf + devc->rd_idx;
		if (memcmp(msg_ptr, header_magic, sizeof(header_magic)) != 0) {
			devc->rd_idx += 1;
			continue;
		}

		/* Determine msg type and length. */
		valid_msg_len = get_length_for_msg_type(msg_ptr[HEADER_MSGTYPE_IDX]);
		if (!valid_msg_len) {
			devc->rd_idx += 2;
			continue;
		}

		/* Do we have the complete message? */
		if (devc->rd_idx + valid_msg_len <= devc->wr_idx) {
			valid_msg_ptr = msg_ptr;
			devc->rd_idx += valid_msg_len;
			log_atorch_msg(valid_msg_ptr, valid_msg_len);
			return valid_msg_ptr;
		}

		return NULL;
	}
	return NULL;
}

static const uint8_t *receive_msg(struct sr_serial_dev_inst *serial,
	struct dev_context *devc)
{
	size_t len;
	const uint8_t *valid_msg_ptr;

	while (1) {
		/* Remove bytes already processed. */
		if (devc->rd_idx > 0) {
			len = devc->wr_idx - devc->rd_idx;
			memmove(devc->buf, devc->buf + devc->rd_idx, len);
			devc->wr_idx -= devc->rd_idx;
			devc->rd_idx = 0;
		}

		/* Read more bytes to process. */
		len = ATORCH_BUFSIZE - devc->wr_idx;
		len = serial_read_nonblocking(serial, devc->buf + devc->wr_idx, len);
		if (len <= 0)
			return NULL;
		devc->wr_idx += len;

		/* Locate next start of message. */
		valid_msg_ptr = locate_next_valid_msg(devc);
		if (valid_msg_ptr)
			return valid_msg_ptr;
	}
}

static const struct atorch_device_profile *find_profile_for_device_type(uint8_t dev_type)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(atorch_profiles); i++) {
		if (atorch_profiles[i].device_type == dev_type)
			return &atorch_profiles[i];
	}
	return NULL;
}

static void parse_report_msg(struct sr_dev_inst *sdi, const uint8_t *report_ptr)
{
	struct dev_context *devc;
	float val;
	size_t i;

	devc = sdi->priv;

	std_session_send_df_frame_begin(sdi);

	for (i = 0; i < devc->profile->channel_count; i++) {
		bv_get_value(&val, &devc->profile->channels[i].spec, report_ptr);
		feed_queue_analog_submit_one(devc->feeds[i], val, 1);
	}

	std_session_send_df_frame_end(sdi);

	sr_sw_limits_update_frames_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
}

SR_PRIV int atorch_probe(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	int64_t deadline_us;
	const struct atorch_device_profile *p;
	const uint8_t *msg_ptr;

	devc->wr_idx = 0;
	devc->rd_idx = 0;

	deadline_us = g_get_monotonic_time();
	deadline_us += ATORCH_PROBE_TIMEOUT_MS * 1000;
	while (g_get_monotonic_time() <= deadline_us) {
		msg_ptr = receive_msg(serial, devc);
		if (msg_ptr && msg_ptr[HEADER_MSGTYPE_IDX] == MSG_REPORT) {
			p = find_profile_for_device_type(msg_ptr[PAYLOAD_START_IDX]);
			if (p) {
				devc->profile = p;
				return SR_OK;
			}
			sr_err("Unrecognized device type (0x%.4" PRIx8 ").",
			       devc->buf[PAYLOAD_START_IDX]);
			return SR_ERR;
		}
		g_usleep(100 * 1000);
	}
	return SR_ERR;
}

SR_PRIV int atorch_receive_data_callback(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const uint8_t *msg_ptr;

	(void)fd;

	sdi = cb_data;
	devc = sdi->priv;

	if (!sdi || !devc)
		return TRUE;

	if (revents & G_IO_IN) {
		while ((msg_ptr = receive_msg(sdi->conn, devc))) {
			if (msg_ptr[HEADER_MSGTYPE_IDX] == MSG_REPORT)
				parse_report_msg(sdi, msg_ptr);
		}
	}

	return TRUE;
}
