/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Felipe Calliari <calliarifelipe@gmail.com>
 *
 * Driver for the "LogicAnalyzer" firmware by Agustín Gimenez Bernad (gusmanb).
 * Firmware: https://github.com/gusmanb/logicanalyzer
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
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

/*
 * Wire protocol (host -> device): binary frames
 *   0x55 0xAA <cmd> <payload...> 0xAA 0x55
 * Bytes 0x55, 0xAA and 0xF0 inside the frame body are escaped as
 *   0xF0 (byte ^ 0xF0).
 *
 * Device -> host: text lines terminated by '\n', except capture data which
 * is binary: uint32 sample count, samples (1/2/4 bytes each, LE), uint8
 * timestamp count and, if that count is > 1, count * uint32 timestamps.
 */

#define OUT_FLUSH_SAMPLES (64 * 1024)
#define OUT_UNITSIZE 3

SR_PRIV int la_send_command(struct sr_serial_dev_inst *serial, uint8_t cmd,
	const uint8_t *payload, size_t len)
{
	uint8_t frame[2 + 2 * (1 + 64) + 2];
	size_t pos, i;
	uint8_t b;
	int ret;

	if (len > 64)
		return SR_ERR_ARG;

	pos = 0;
	frame[pos++] = 0x55;
	frame[pos++] = 0xAA;
	for (i = 0; i <= len; i++) {
		b = (i == 0) ? cmd : payload[i - 1];
		if (b == 0x55 || b == 0xAA || b == 0xF0) {
			frame[pos++] = 0xF0;
			frame[pos++] = b ^ 0xF0;
		} else {
			frame[pos++] = b;
		}
	}
	frame[pos++] = 0xAA;
	frame[pos++] = 0x55;

	ret = serial_write_blocking(serial, frame, pos, 1000);
	if (ret < 0 || (size_t)ret != pos) {
		sr_err("Failed to send command %u.", cmd);
		return SR_ERR_IO;
	}

	return SR_OK;
}

/*
 * Discard incoming data until nothing arrived for idle_ms, or max_ms
 * elapsed (whichever happens first). Never blocks inside a read.
 */
SR_PRIV void la_drain(struct sr_serial_dev_inst *serial, int idle_ms,
	int max_ms)
{
	uint8_t buf[4096];
	gint64 now, last, end;
	int len;

	now = last = g_get_monotonic_time();
	end = now + (gint64)max_ms * 1000;
	while (now < end && now - last < (gint64)idle_ms * 1000) {
		len = serial_read_nonblocking(serial, buf, sizeof(buf));
		now = g_get_monotonic_time();
		if (len > 0)
			last = now;
		else if (len < 0)
			break;
		else
			g_usleep(5000);
	}
}

SR_PRIV int la_read_line(struct sr_serial_dev_inst *serial, char *buf,
	int buflen, int timeout_ms)
{
	int len;
	gint64 end, remaining;

	/*
	 * The firmware's stdio sends "\r\n". serial_readline() stops at the
	 * '\r', so the '\n' shows up as an empty line: skip those.
	 */
	end = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
	do {
		remaining = (end - g_get_monotonic_time()) / 1000;
		if (remaining <= 0)
			return 0;
		len = buflen;
		if (serial_readline(serial, &buf, &len, remaining) != SR_OK)
			return -1;
		while (len > 0 && (buf[len - 1] == '\r' ||
				buf[len - 1] == '\n'))
			buf[--len] = '\0';
		/* Drop leading CR/LF left over from a previous line. */
		while (len > 0 && (buf[0] == '\r' || buf[0] == '\n')) {
			memmove(buf, buf + 1, len);
			len--;
		}
	} while (len == 0);

	return len;
}

static gboolean parse_uint_field(const char *line, const char *prefix,
	uint64_t *value)
{
	size_t plen;
	char *end;
	unsigned long long v;

	plen = strlen(prefix);
	if (strncmp(line, prefix, plen) != 0)
		return FALSE;
	v = strtoull(line + plen, &end, 10);
	if (end == line + plen || *end != '\0')
		return FALSE;
	*value = v;

	return TRUE;
}

SR_PRIV int la_identify(struct sr_serial_dev_inst *serial,
	struct dev_context *devc)
{
	char line[128];
	const char *prefix;
	char *ver;
	uint64_t v;
	int len, tries, i;

	prefix = "LOGIC_ANALYZER_";

	/* Drop anything still pending (e.g. data from an aborted capture). */
	serial_flush(serial);
	la_drain(serial, 50, 500);

	if (la_send_command(serial, LA_CMD_ID, NULL, 0) != SR_OK)
		return SR_ERR_IO;

	/* Find the identification line, skipping any garbage. */
	for (tries = 0; tries < 4; tries++) {
		len = la_read_line(serial, line, sizeof(line), 2000);
		if (len <= 0)
			continue;
		if (strncmp(line, prefix, strlen(prefix)) == 0)
			break;
		sr_dbg("Ignoring line '%s'.", line);
	}
	if (tries == 4) {
		sr_dbg("No LogicAnalyzer identification received.");
		return SR_ERR;
	}

	/* "LOGIC_ANALYZER_<BOARD>_V<major>_<minor>" */
	ver = g_strrstr(line, "_V");
	if (!ver || ver < line + strlen(prefix)) {
		sr_err("Malformed identification '%s'.", line);
		return SR_ERR;
	}
	if (sscanf(ver, "_V%d_%d", &devc->fw_major, &devc->fw_minor) != 2) {
		sr_err("Malformed firmware version in '%s'.", line);
		return SR_ERR;
	}
	g_free(devc->board);
	devc->board = g_strndup(line + strlen(prefix),
		ver - (line + strlen(prefix)));

	if (devc->fw_major < LA_MIN_MAJOR ||
	    (devc->fw_major == LA_MIN_MAJOR && devc->fw_minor < LA_MIN_MINOR)) {
		sr_err("Firmware V%d_%d is too old, at least V%d_%d is needed.",
			devc->fw_major, devc->fw_minor,
			LA_MIN_MAJOR, LA_MIN_MINOR);
		return SR_ERR;
	}

	devc->max_freq = 0;
	devc->blast_freq = 0;
	devc->buffer_size = 0;
	devc->num_channels = 0;

	for (i = 0; i < 4; i++) {
		len = la_read_line(serial, line, sizeof(line), 2000);
		if (len <= 0) {
			sr_err("Timeout reading device capabilities.");
			return SR_ERR;
		}
		if (parse_uint_field(line, "FREQ:", &v))
			devc->max_freq = v;
		else if (parse_uint_field(line, "BLASTFREQ:", &v))
			devc->blast_freq = v;
		else if (parse_uint_field(line, "BUFFER:", &v))
			devc->buffer_size = v;
		else if (parse_uint_field(line, "CHANNELS:", &v))
			devc->num_channels = v;
		else
			sr_warn("Unknown capability line '%s'.", line);
	}

	if (!devc->max_freq || !devc->buffer_size || !devc->num_channels) {
		sr_err("Incomplete capability information from the device.");
		return SR_ERR;
	}
	if (devc->num_channels > LA_MAX_CHANNELS)
		devc->num_channels = LA_MAX_CHANNELS;

	sr_info("Found LogicAnalyzer board %s, firmware V%d_%d, "
		"%" PRIu64 " Hz (blast %" PRIu64 " Hz), buffer %u bytes, "
		"%d channels.", devc->board, devc->fw_major, devc->fw_minor,
		devc->max_freq, devc->blast_freq, devc->buffer_size,
		devc->num_channels);

	return SR_OK;
}

SR_PRIV void la_pack_request(const struct la_capture_request *req,
	uint8_t *out)
{
	/* Layout of the firmware's CAPTURE_REQUEST with natural alignment. */
	memset(out, 0, LA_CAPTURE_REQUEST_SIZE);
	out[0] = req->trigger_type;
	out[1] = req->trigger_channel;
	out[2] = req->inverted_or_count;
	/* out[3]: padding */
	WL16(&out[4], req->trigger_value);
	memcpy(&out[6], req->channels, LA_MAX_CHANNELS);
	out[30] = req->channel_count;
	/* out[31]: padding */
	WL32(&out[32], req->frequency);
	WL32(&out[36], req->pre_samples);
	WL32(&out[40], req->post_samples);
	out[44] = req->loop_count;
	out[45] = req->measure;
	out[46] = req->capture_mode;
	/* out[47]: padding */
}

static void flush_samples(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	devc = sdi->priv;
	if (!devc->out_len)
		return;

	logic.unitsize = OUT_UNITSIZE;
	logic.length = devc->out_len * OUT_UNITSIZE;
	logic.data = devc->out_buf;
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	sr_session_send(sdi, &packet);

	devc->out_len = 0;
}

static void push_sample(const struct sr_dev_inst *sdi, uint32_t raw)
{
	struct dev_context *devc;
	uint32_t val;
	uint8_t *p;
	int i;

	devc = sdi->priv;

	if (devc->samples_rx++ < devc->skip_samples)
		return;

	if (devc->has_trigger && devc->samples_sent == devc->trigger_pos) {
		flush_samples(sdi);
		std_session_send_df_trigger(sdi);
	}

	/* Remap wire bit position -> sigrok channel index. */
	val = 0;
	for (i = 0; i < devc->chan_count; i++) {
		if (raw & (1UL << i))
			val |= 1UL << devc->chan_map[i];
	}

	p = devc->out_buf + devc->out_len * OUT_UNITSIZE;
	p[0] = val & 0xff;
	p[1] = (val >> 8) & 0xff;
	p[2] = (val >> 16) & 0xff;
	devc->out_len++;
	devc->samples_sent++;

	if (devc->out_len >= devc->out_cap)
		flush_samples(sdi);
}

static void process_bytes(const struct sr_dev_inst *sdi, const uint8_t *buf,
	size_t len)
{
	struct dev_context *devc;
	size_t i;
	uint32_t raw;

	devc = sdi->priv;

	for (i = 0; i < len && devc->rx_state != LA_RX_DONE; i++) {
		switch (devc->rx_state) {
		case LA_RX_LENGTH:
			devc->len_buf[devc->len_pos++] = buf[i];
			if (devc->len_pos == 4) {
				devc->total_samples = RL32(devc->len_buf);
				sr_dbg("Device reports %u samples.",
					devc->total_samples);
				if (devc->total_samples != devc->expected_samples)
					sr_warn("Expected %u samples, device "
						"sends %u.", devc->expected_samples,
						devc->total_samples);
				devc->rx_state = devc->total_samples ?
					LA_RX_SAMPLES : LA_RX_STAMP_COUNT;
			}
			break;
		case LA_RX_SAMPLES:
			devc->partial[devc->partial_pos++] = buf[i];
			if (devc->partial_pos < devc->bytes_per_sample)
				break;
			devc->partial_pos = 0;
			switch (devc->bytes_per_sample) {
			case 1:
				raw = devc->partial[0];
				break;
			case 2:
				raw = RL16(devc->partial);
				break;
			default:
				raw = RL32(devc->partial);
				break;
			}
			push_sample(sdi, raw);
			if (devc->samples_rx >= devc->total_samples)
				devc->rx_state = LA_RX_STAMP_COUNT;
			break;
		case LA_RX_STAMP_COUNT:
			/* Timestamps are only sent when there are more than one. */
			devc->stamp_bytes_left = buf[i] > 1 ? buf[i] * 4 : 0;
			devc->rx_state = devc->stamp_bytes_left ?
				LA_RX_STAMPS : LA_RX_DONE;
			break;
		case LA_RX_STAMPS:
			if (--devc->stamp_bytes_left == 0)
				devc->rx_state = LA_RX_DONE;
			break;
		default:
			break;
		}
	}
}

SR_PRIV int la_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t buf[16 * 1024];
	int len;

	(void)fd;

	if (!(sdi = cb_data) || !(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN || revents == 0) {
		while ((len = serial_read_nonblocking(serial, buf,
				sizeof(buf))) > 0) {
			process_bytes(sdi, buf, len);
			if (devc->rx_state == LA_RX_DONE)
				break;
		}
		if (len < 0) {
			sr_err("Serial read error.");
			devc->rx_state = LA_RX_DONE;
		}
	}

	if (devc->rx_state == LA_RX_DONE) {
		flush_samples(sdi);
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
	}

	return TRUE;
}

SR_PRIV void la_abort_capture(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	uint8_t b;

	serial = sdi->conn;

	/*
	 * Any byte received while capturing cancels the capture. The firmware
	 * only polls for it every ~2 s, so wait and discard whatever arrives
	 * (which can also be a capture that finished in the meantime).
	 */
	b = 0xFF;
	serial_write_blocking(serial, &b, 1, 100);

	g_usleep(2500 * 1000);
	la_drain(serial, 200, 5000);
}
