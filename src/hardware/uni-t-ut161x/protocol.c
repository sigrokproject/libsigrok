/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Jochen Rueter <jo.rueter@gmail.com>
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

/*
 * UNI-T UT161B/D/E (and UT61B+/D+/E+, UT60BT) protocol.
 *
 * The meter only sends data upon request. Each request is a frame with
 * a single byte command, the response is a frame which either carries
 * a measurement, the model name, or a short acknowledge (0xff 0x00).
 * Protocol details were taken from https://github.com/ljakob/unit_ut61eplus
 * and USB captures of the vendor software with a UT161B.
 *
 * Measurement payload (all flag bytes are ASCII digits, i.e. 0x30 | bits):
 *   @0, mode (index into the mode table below)
 *   @1, range, ASCII digit '0'..'7'
 *   @2, 7 characters display text, e.g. "  3.795", "-0.0012", "  OL.  "
 *   @9, 2 bytes bar graph (not interpreted)
 *   @11, flags: MAX, MIN, HOLD, REL
 *   @12, flags: !AUTO, low battery, HV warning
 *   @13, flags: DC, peak max, peak min, bar polarity
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include "protocol.h"

enum acdc_source {
	ACDC_FIXED,	/* Use flags from the mode table. */
	ACDC_FROM_FLAG,	/* Pick AC or DC from the DC flag in the frame. */
};

struct mode_info {
	const char *name;
	enum sr_mq mq;
	enum sr_unit unit;
	enum sr_mqflag mqflags;
	enum acdc_source acdc;
	/* Unit prefix exponent, NULL when not dependent on the range. */
	const int8_t *range_exp;
	int exp;
};

/* Display unit prefix per range, e.g. range 1-3 in Ohms mode are kOhms. */
static const int8_t exp_ohm[8] = { 0, 3, 3, 3, 6, 6, 6, 6, };
static const int8_t exp_hz[8] = { 0, 0, 3, 3, 3, 6, 6, 6, };
static const int8_t exp_cap[8] = { -9, -9, -6, -6, -6, -3, -3, -3, };

#define AC_RMS	(SR_MQFLAG_AC | SR_MQFLAG_RMS)
#define AC_DC	(SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS)

static const struct mode_info modes[] = {
	[0x00] = { "ACV", SR_MQ_VOLTAGE, SR_UNIT_VOLT, AC_RMS, ACDC_FIXED, NULL, 0, },
	[0x01] = { "ACmV", SR_MQ_VOLTAGE, SR_UNIT_VOLT, AC_RMS, ACDC_FIXED, NULL, -3, },
	[0x02] = { "DCV", SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC, ACDC_FIXED, NULL, 0, },
	[0x03] = { "DCmV", SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DC, ACDC_FIXED, NULL, -3, },
	[0x04] = { "Hz", SR_MQ_FREQUENCY, SR_UNIT_HERTZ, 0, ACDC_FIXED, exp_hz, 0, },
	[0x05] = { "Duty", SR_MQ_DUTY_CYCLE, SR_UNIT_PERCENTAGE, 0, ACDC_FIXED, NULL, 0, },
	[0x06] = { "Ohm", SR_MQ_RESISTANCE, SR_UNIT_OHM, 0, ACDC_FIXED, exp_ohm, 0, },
	[0x07] = { "Cont", SR_MQ_RESISTANCE, SR_UNIT_OHM, 0, ACDC_FIXED, NULL, 0, },
	[0x08] = { "Diode", SR_MQ_VOLTAGE, SR_UNIT_VOLT, SR_MQFLAG_DIODE | SR_MQFLAG_DC, ACDC_FIXED, NULL, 0, },
	[0x09] = { "Cap", SR_MQ_CAPACITANCE, SR_UNIT_FARAD, 0, ACDC_FIXED, exp_cap, 0, },
	[0x0a] = { "degC", SR_MQ_TEMPERATURE, SR_UNIT_CELSIUS, 0, ACDC_FIXED, NULL, 0, },
	[0x0b] = { "degF", SR_MQ_TEMPERATURE, SR_UNIT_FAHRENHEIT, 0, ACDC_FIXED, NULL, 0, },
	[0x0c] = { "DCuA", SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC, ACDC_FIXED, NULL, -6, },
	[0x0d] = { "ACuA", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_RMS, ACDC_FIXED, NULL, -6, },
	[0x0e] = { "DCmA", SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC, ACDC_FIXED, NULL, -3, },
	[0x0f] = { "ACmA", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_RMS, ACDC_FIXED, NULL, -3, },
	[0x10] = { "DCA", SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC, ACDC_FIXED, NULL, 0, },
	[0x11] = { "ACA", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_RMS, ACDC_FIXED, NULL, 0, },
	[0x12] = { "hFE", SR_MQ_GAIN, SR_UNIT_UNITLESS, 0, ACDC_FIXED, NULL, 0, },
	/* 0x13 "Live" and 0x14 "NCV" carry no numeric value. */
	[0x15] = { "LoZV", SR_MQ_VOLTAGE, SR_UNIT_VOLT, 0, ACDC_FROM_FLAG, NULL, 0, },
	[0x16] = { "ACA", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_RMS, ACDC_FIXED, NULL, 0, },
	[0x17] = { "DCA", SR_MQ_CURRENT, SR_UNIT_AMPERE, SR_MQFLAG_DC, ACDC_FIXED, NULL, 0, },
	[0x18] = { "LPF", SR_MQ_VOLTAGE, SR_UNIT_VOLT, AC_RMS, ACDC_FIXED, NULL, 0, },
	[0x19] = { "AC/DC", SR_MQ_VOLTAGE, SR_UNIT_VOLT, AC_DC, ACDC_FIXED, NULL, 0, },
	[0x1a] = { "LPF", SR_MQ_VOLTAGE, SR_UNIT_VOLT, AC_RMS, ACDC_FIXED, NULL, 0, },
	[0x1b] = { "AC+DC", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_DC, ACDC_FIXED, NULL, 0, },
	[0x1c] = { "LPF", SR_MQ_VOLTAGE, SR_UNIT_VOLT, AC_RMS, ACDC_FIXED, NULL, 0, },
	[0x1d] = { "AC+DC2", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_DC, ACDC_FIXED, NULL, 0, },
	[0x1e] = { "Inrush", SR_MQ_CURRENT, SR_UNIT_AMPERE, AC_RMS, ACDC_FIXED, NULL, 0, },
};

static uint16_t frame_checksum(const uint8_t *data, size_t len)
{
	uint16_t sum;

	sum = 0;
	while (len--)
		sum += *data++;

	return sum;
}

SR_PRIV int ut161x_send_cmd(struct sr_serial_dev_inst *serial, uint8_t cmd)
{
	uint8_t frame[FRAME_HEAD_LEN + 1 + FRAME_CSUM_LEN];
	size_t len;
	uint16_t sum;
	int ret;

	len = 0;
	frame[len++] = FRAME_MAGIC_0;
	frame[len++] = FRAME_MAGIC_1;
	frame[len++] = 1 + FRAME_CSUM_LEN;
	frame[len++] = cmd;
	sum = frame_checksum(frame, len);
	WB16(&frame[len], sum);
	len += FRAME_CSUM_LEN;

	ret = serial_write_blocking(serial, frame, len, serial_timeout(serial, len));
	if (ret < 0)
		return ret;
	if ((size_t)ret != len)
		return SR_ERR_IO;

	return SR_OK;
}

/* Append received data to the frame buffer. Returns the number of bytes read. */
SR_PRIV int ut161x_read_input(struct sr_serial_dev_inst *serial,
	struct dev_context *devc)
{
	size_t space;
	int ret;

	space = sizeof(devc->buf) - devc->buf_len;
	if (!space) {
		/* Cannot happen with valid frames, drop stale data. */
		sr_dbg("Receive buffer overflow, discarding data.");
		devc->buf_len = 0;
		space = sizeof(devc->buf);
	}
	ret = serial_read_nonblocking(serial, &devc->buf[devc->buf_len], space);
	if (ret < 0)
		return ret;
	devc->buf_len += ret;

	return ret;
}

static void buf_consume(struct dev_context *devc, size_t count)
{
	if (count > devc->buf_len)
		count = devc->buf_len;
	memmove(devc->buf, &devc->buf[count], devc->buf_len - count);
	devc->buf_len -= count;
}

/*
 * Extract the next complete and valid frame from the receive buffer.
 * Copies the payload (without header and checksum) to the caller's
 * buffer, which must hold FRAME_MAX_LEN bytes. Returns TRUE when a
 * frame was found.
 */
SR_PRIV gboolean ut161x_get_frame(struct dev_context *devc,
	uint8_t *payload, size_t *payload_len)
{
	size_t skip, frame_len, data_len;
	uint16_t sum_calc, sum_recv;
	const uint8_t *p;

	while (devc->buf_len >= FRAME_HEAD_LEN) {
		/* Synchronize to the magic header bytes. */
		for (skip = 0; skip + 1 < devc->buf_len; skip++) {
			if (devc->buf[skip] == FRAME_MAGIC_0 &&
					devc->buf[skip + 1] == FRAME_MAGIC_1)
				break;
		}
		if (skip) {
			sr_dbg("Skipping %zu bytes of garbage.", skip);
			buf_consume(devc, skip);
			continue;
		}

		p = devc->buf;
		data_len = p[2];
		frame_len = FRAME_HEAD_LEN + data_len;
		if (data_len < FRAME_CSUM_LEN || frame_len > FRAME_MAX_LEN) {
			sr_dbg("Invalid frame length %zu.", data_len);
			buf_consume(devc, 1);
			continue;
		}
		if (devc->buf_len < frame_len)
			return FALSE;

		sum_calc = frame_checksum(p, frame_len - FRAME_CSUM_LEN);
		sum_recv = RB16(&p[frame_len - FRAME_CSUM_LEN]);
		if (sum_calc != sum_recv) {
			sr_dbg("Checksum mismatch, got 0x%04x, expected 0x%04x.",
				sum_recv, sum_calc);
			buf_consume(devc, 1);
			continue;
		}

		*payload_len = data_len - FRAME_CSUM_LEN;
		memcpy(payload, &p[FRAME_HEAD_LEN], *payload_len);
		buf_consume(devc, frame_len);
		return TRUE;
	}

	return FALSE;
}

static int parse_display(const uint8_t *raw, const struct mode_info *mode,
	double *value, int *digits)
{
	char text[MEAS_DISPLAY_LEN + 1];
	size_t i, len;
	int ret;

	/* Strip spaces, which the meter uses for padding. */
	len = 0;
	for (i = 0; i < MEAS_DISPLAY_LEN; i++) {
		if (raw[i] != ' ')
			text[len++] = raw[i];
	}
	text[len] = '\0';

	/* Overload is shown as "OL" with a decimal point somewhere. */
	if (strstr(text, "OL")) {
		*value = (text[0] == '-') ? -INFINITY : INFINITY;
		*digits = 0;
		return SR_OK;
	}

	ret = sr_atod_ascii_digits(text, value, digits);
	if (ret != SR_OK) {
		sr_dbg("Unhandled display text '%s' in mode %s.",
			text, mode->name);
		return ret;
	}

	return SR_OK;
}

static int handle_measurement(const struct sr_dev_inst *sdi,
	const uint8_t *payload)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	const struct mode_info *mode;
	uint8_t mode_idx, range, flags1, flags2, flags3;
	double dval;
	float value;
	int digits, exp;

	devc = sdi->priv;

	mode_idx = payload[MEAS_OFF_MODE];
	range = payload[MEAS_OFF_RANGE] & 0x0f;
	flags1 = payload[MEAS_OFF_FLAGS1] & 0x0f;
	flags2 = payload[MEAS_OFF_FLAGS2] & 0x0f;
	flags3 = payload[MEAS_OFF_FLAGS3] & 0x0f;

	mode = (mode_idx < ARRAY_SIZE(modes)) ? &modes[mode_idx] : NULL;
	if (!mode || !mode->name) {
		sr_dbg("Unsupported mode 0x%02x, ignoring.", mode_idx);
		return SR_OK;
	}
	if (range >= 8) {
		sr_dbg("Invalid range %u in mode %s.", range, mode->name);
		return SR_OK;
	}
	if (parse_display(&payload[MEAS_OFF_DISPLAY], mode, &dval, &digits) != SR_OK)
		return SR_OK;

	exp = mode->range_exp ? mode->range_exp[range] : mode->exp;
	if (isfinite(dval))
		dval *= pow(10, exp);
	value = dval;
	digits -= exp;

	sr_spew("Mode %s, range %u, value %g, flags %x/%x/%x.",
		mode->name, range, value, flags1, flags2, flags3);
	if (flags2 & FLAG2_LOW_BATT)
		sr_dbg("Low battery.");

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &value;
	analog.meaning->mq = mode->mq;
	analog.meaning->unit = mode->unit;
	analog.meaning->mqflags = mode->mqflags;
	if (mode->acdc == ACDC_FROM_FLAG) {
		if (flags3 & FLAG3_DC)
			analog.meaning->mqflags |= SR_MQFLAG_DC;
		else
			analog.meaning->mqflags |= AC_RMS;
	}
	if (flags1 & FLAG1_HOLD)
		analog.meaning->mqflags |= SR_MQFLAG_HOLD;
	if (flags1 & FLAG1_REL)
		analog.meaning->mqflags |= SR_MQFLAG_RELATIVE;
	if ((flags1 & FLAG1_MAX) || (flags3 & FLAG3_PEAK_MAX))
		analog.meaning->mqflags |= SR_MQFLAG_MAX;
	if ((flags1 & FLAG1_MIN) || (flags3 & FLAG3_PEAK_MIN))
		analog.meaning->mqflags |= SR_MQFLAG_MIN;
	if (!(flags2 & FLAG2_MANUAL))
		analog.meaning->mqflags |= SR_MQFLAG_AUTORANGE;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	sr_sw_limits_update_samples_read(&devc->limits, 1);

	return SR_OK;
}

static void handle_frame(const struct sr_dev_inst *sdi,
	const uint8_t *payload, size_t len)
{
	struct dev_context *devc;
	GString *text;

	devc = sdi->priv;

	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		text = sr_hexdump_new(payload, len);
		sr_spew("Frame payload: %s", text->str);
		sr_hexdump_free(text);
	}

	if (len == MEAS_PAYLOAD_LEN) {
		devc->req_pending = FALSE;
		handle_measurement(sdi, payload);
		return;
	}

	/* Acknowledges and names are not expected during acquisition. */
	sr_dbg("Ignoring frame with %zu bytes payload.", len);
}

SR_PRIV int uni_t_ut161x_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t payload[FRAME_MAX_LEN];
	size_t len;
	int64_t now;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	devc = sdi->priv;
	if (!devc)
		return TRUE;
	serial = sdi->conn;

	/*
	 * Always check for input, the HID transport polls and does not
	 * reliably signal G_IO_IN.
	 */
	if (ut161x_read_input(serial, devc) < 0) {
		sr_err("Failed to read from the device.");
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
		return TRUE;
	}
	while (ut161x_get_frame(devc, payload, &len))
		handle_frame(sdi, payload, len);

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
		return TRUE;
	}

	/* Request the next measurement, or retry when the meter kept silent. */
	now = g_get_monotonic_time();
	if (devc->req_pending && now - devc->req_sent_us < REQ_TIMEOUT_MS * 1000)
		return TRUE;
	if (devc->req_pending)
		sr_dbg("Request timed out, retrying.");
	if (ut161x_send_cmd(serial, CMD_GET_MEASUREMENT) != SR_OK) {
		sr_err("Failed to send measurement request.");
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
		return TRUE;
	}
	devc->req_pending = TRUE;
	devc->req_sent_us = now;

	return TRUE;
}
