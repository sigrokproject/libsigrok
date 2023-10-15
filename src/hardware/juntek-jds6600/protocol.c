/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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
 * Juntek JDS6600 is a DDS signal generator.
 * Often rebranded, goes by different names, among them Joy-IT JDS6600.
 *
 * This driver was built using Kristoff Bonne's knowledge as seen in his
 * MIT licensed Python code for JDS6600 control. For details see the
 * https://github.com/on1arf/jds6600_python repository.
 *
 * Supported features:
 * - Model detection, which determines the upper output frequency limit
 *   (15..60MHz models exist).
 * - Assumes exactly two channels. Other models were not seen out there.
 * - Per channel configuration of: Waveform, output frequency, amplitude,
 *   offset, duty cycle.
 * - Phase between channels is a global property and affects multiple
 *   channels at the same time (their relation to each other).
 *
 * TODO
 * - Add support for the frequency measurement and/or the counter. This
 *   feature's availability may depend on or interact with the state of
 *   other generator channels. Needs consideration of constraints.
 * - Add support for "modes" (sweep, pulse, burst; modulation if the
 *   device supports it).
 * - Add support for download/upload of arbitrary waveforms. This needs
 *   infrastructure in common libsigrok code as well as in applications.
 *   At the moment "blob transfer" (waveform upload/download) appears to
 *   not be supported.
 * - Re-consider parameter value ranges. Frequency depends on the model.
 *   Amplitude depends on the model and frequencies. Can be -20..+20,
 *   or -10..+10, or -5..+5. Could be affected by offsets and further
 *   get clipped. This implementation caps application's input to the
 *   -20..+20 range, and sends the set request to the device. If any
 *   further transformation happens in the device then applications
 *   need to read back, this library driver doesn't.
 *
 * Implementation details:
 * - Communicates via USB CDC at 115200/8n1 (virtual COM port). The user
 *   perceives a USB attached device (full speed, CDC/ACM class). The
 *   implementation needs to remember that a WCH CH340G forwards data
 *   to a microcontroller. Maximum throughput is in the 10KiB/s range.
 * - Requests are in text format. Start with a ':' colon, followed by a
 *   single letter instruction opcode, followed by a number which either
 *   addresses a parameter (think hardware register) or storage slot for
 *   an arbitrary waveform. Can be followed by an '=' equals sign and a
 *   value. Multiple values are comma separated. The line may end in a
 *   '.' period. Several end-of-line conventions are supported by the
 *   devices' firmware versions, LF and CR/LF are reported to work.
 * - Responses also are in text format. Start with a ':' colon, followed
 *   by an instruction letter, followed by a number (a parameter index,
 *   or a waveform index), followed by '=' equal sign and one or more
 *   values. Optionally ending in a '.' period. And ending in the
 *   firmware's end-of-line. Read responses will have this format.
 *   Responses to write requests might just have the ":ok." literal.
 * - There are four instructions: 'r' to read and 'w' to write parameters
 *   (think "hardware registers", optionaly multi-valued), 'a' to write
 *   and 'b' to read arbitrary waveform data (sequence of sample values).
 * - Am not aware of a vendor's documentation for the protocol. Joy-IT
 *   provides the JT-JDS6600-Communication-protocol.pdf document which
 *   leaves a lot of questions. This sigrok driver implementation used
 *   a lot of https://github.com/on1arf/jds6600_python knowledge for
 *   the initial version (MIT licenced Python code by Kristoff Bonne).
 * - The requests take effect when sent from application code. While
 *   the requests remain uneffective when typed in interactive terminal
 *   sessions. Though there are ":ok" responses, the action would not
 *   happen in the device. It's assumed to be a firmware implementation
 *   constraint that is essential to keep in mind.
 * - The right hand side of write requests or read responses can carry
 *   any number of values, both numbers and text, integers and floats.
 *   Still some of the parameters (voltages, times, frequencies) come in
 *   interesting formats. A floating point "mantissa" and an integer code
 *   for scaling the value. Not an exponent, but some kind of index. In
 *   addition to an open coded "fixed point" style multiplier that is
 *   implied and essential, but doesn't show on the wire. Interpretation
 *   of responses and phrasing of values in requests is arbitrary, this
 *   "black magic" was found by local experimentation (reading back the
 *   values which were configured by local UI interaction).
 * - Communication is more reliable when the host unconditionally sends
 *   "function codes" (register and waveform indices) in two-digit form.
 *   Device firmware might implement rather specific assumptions.
 * - Semantics of the right hand side in :rNN= and :bNN= read requests
 *   is uncertain. Just passing 0 in all situations worked in a local
 *   setup. As did omitting the value during interactive exploration.
 *
 * Example requests and responses.
 * - Get model identification (max output frequency)
 *    TX text: --> :r00=0.
 *    TX bytes: --> 3a 72 30 30 3d 30 2e 0d  0a
 *    RX bytes: <-- 3a 72 30 30 3d 36 30 2e  0d 0a
 *    RX text: <-- :r00=60.
 * - Get all channels' enabled state
 *    TX text: --> :r20=0.
 *    TX bytes: --> 3a 72 32 30 3d 30 2e 0d  0a
 *    RX bytes: <-- 3a 72 32 30 3d 31 2c 31  2e 0d 0a
 *    RX text: <-- :r20=1,1.
 * - Get first channel's waveform selection
 *    TX text: --> :r21=0.
 *    TX bytes: --> 3a 72 32 31 3d 30 2e 0d  0a
 *    RX bytes: <-- 3a 72 32 31 3d 31 30 33  2e 0d 0a
 *    RX text: <-- :r21=103.
 * - Set second channel's output frequency
 *    TX text: --> :w24=1234500,0.
 *    TX bytes: --> 3a 77 32 34 3d 31 32 33  34 35 30 30 2c 30 2e 0d   0a
 *    RX bytes: <-- 3a 6f 6b 0d 0a
 *    RX text: <-- :ok
 * - Read arbitrary waveform number 13
 *    TX text: --> :b13=0.
 *    TX bytes: --> 3a 62 31 33 3d 30 2e 0d  0a
 *    RX bytes: <-- 3a 62 31 33 3d 34 30 39  35 2c 34 30 39 35 2c ... 2c 34 30 39 35 2c   34 30 39 35 2c 0d 0a
 *    RX text: <-- :b13=4095,4095,...,4095,4095,
 */

#include "config.h"

#include <glib.h>
#include <math.h>
#include <string.h>

#include "protocol.h"

#define WITH_SERIAL_RAW_DUMP	0 /* Includes EOL and non-printables. */
#define WITH_ARBWAVE_DOWNLOAD	0 /* Development HACK */

/*
 * The firmware's maximum response length. Seen when an arbitrary
 * waveform gets retrieved. Carries 2048 samples in the 0..4095 range.
 * Plus some decoration around that data.
 *   :b01=4095,4095,...,4095,<CRLF>
 */
#define MAX_RSP_LENGTH	(8 + 2048 * 5)

/*
 * Times are in milliseconds.
 * - Delay after transmission was an option during initial development.
 *   Has become obsolete. Support remains because it doesn't harm.
 * - Delay after flash is essential when writing multiple waveforms to
 *   the device. Not letting more idle time pass after successful write
 *   and reception of the "ok" response, and before the next write, will
 *   result in corrupted waveform storage in the device. The next wave
 *   that is written waveform will start with several hundred samples
 *   of all-one bits.
 * - Timeout per receive attempt at the physical layer can be short.
 *   Experience suggests that 2ms are a good value. Reception ends when
 *   the response termination was seen. Or when no receive data became
 *   available within that per-attemt timeout, and no higher level total
 *   timeout was specified. Allow some slack for USB FS frame intervals.
 * - Timeout for identify attempts at the logical level can be short.
 *   Captures of the microcontroller communication suggest that firmware
 *   responds immediately (within 2ms). So 10ms per identify attempt
 *   are plenty for successful communication, yet quick enough to not
 *   stall on missing peripherals.
 * - Timeout for waveform upload/download needs to be huge. Textual
 *   presentation of 2k samples with 12 significant bits (0..4095 range)
 *   combined with 115200bps UART communication result in a 1s maximum
 *   transfer time per waveform. So 1.2s is a good value.
 */
#define DELAY_AFTER_SEND	0
#define DELAY_AFTER_FLASH	100
#define TIMEOUT_READ_CHUNK	2
#define TIMEOUT_IDENTIFY	10
#define TIMEOUT_WAVEFORM	1200

/* Instruction codes. Read/write parameters/waveforms. */
#define INSN_WRITE_PARA	'w'
#define INSN_READ_PARA	'r'
#define INSN_WRITE_WAVE	'a'
#define INSN_READ_WAVE	'b'

/* Indices for "register access". */
enum param_index {
	IDX_DEVICE_TYPE = 0,
	IDX_SERIAL_NUMBER = 1,
	IDX_CHANNELS_ENABLE = 20,
	IDX_WAVEFORM_CH1 = 21,
	IDX_WAVEFORM_CH2 = 22,
	IDX_FREQUENCY_CH1 = 23,
	IDX_FREQUENCY_CH2 = 24,
	IDX_AMPLITUDE_CH1 = 25,
	IDX_AMPLITUDE_CH2 = 26,
	IDX_OFFSET_CH1 = 27,
	IDX_OFFSET_CH2 = 28,
	IDX_DUTYCYCLE_CH1 = 29,
	IDX_DUTYCYCLE_CH2 = 30,
	IDX_PHASE_CHANNELS = 31,
	IDX_ACTION = 32,
	IDX_MODE = 33,
	IDX_INPUT_COUPLING = 36,
	IDX_MEASURE_GATE = 37,
	IDX_MEASURE_MODE = 38,
	IDX_COUNTER_RESET = 39,
	IDX_SWEEP_STARTFREQ = 40,
	IDX_SWEEP_ENDFREQ = 41,
	IDX_SWEEP_TIME = 42,
	IDX_SWEEP_DIRECTION = 43,
	IDX_SWEEP_MODE = 44,
	IDX_PULSE_WIDTH = 45,
	IDX_PULSE_PERIOD = 46,
	IDX_PULSE_OFFSET = 47,
	IDX_PULSE_AMPLITUDE = 48,
	IDX_BURST_COUNT = 49,
	IDX_BURST_MODE = 50,
	IDX_SYSTEM_SOUND = 51,
	IDX_SYSTEM_BRIGHTNESS = 52,
	IDX_SYSTEM_LANGUAGE = 53,
	IDX_SYSTEM_SYNC = 54, /* "Tracking" channels? */
	IDX_SYSTEM_ARBMAX = 55,
	IDX_PROFILE_SAVE = 70,
	IDX_PROFILE_LOAD = 71,
	IDX_PROFILE_CLEAR = 72,
	IDX_COUNTER_VALUE = 80,
	IDX_MEAS_VALUE_FREQLOW = 81,
	IDX_MEAS_VALUE_FREQHI = 82,
	IDX_MEAS_VALUE_WIDTHHI = 83,
	IDX_MEAS_VALUE_WIDTHLOW = 84,
	IDX_MEAS_VALUE_PERIOD = 85,
	IDX_MEAS_VALUE_DUTYCYCLE = 86,
	IDX_MEAS_VALUE_U1 = 87,
	IDX_MEAS_VALUE_U2 = 88,
	IDX_MEAS_VALUE_U3 = 89,
};

/* Firmware's codes for waveform selection. */
enum waveform_index_t {
	/* 17 pre-defined waveforms. */
	WAVE_SINE = 0,
	WAVE_SQUARE = 1,
	WAVE_PULSE = 2,
	WAVE_TRIANGLE = 3,
	WAVE_PARTIAL_SINE = 4,
	WAVE_CMOS = 5,
	WAVE_DC = 6,
	WAVE_HALF_WAVE = 7,
	WAVE_FULL_WAVE = 8,
	WAVE_POS_LADDER = 9,
	WAVE_NEG_LADDER = 10,
	WAVE_NOISE = 11,
	WAVE_EXP_RISE = 12,
	WAVE_EXP_DECAY = 13,
	WAVE_MULTI_TONE = 14,
	WAVE_SINC = 15,
	WAVE_LORENZ = 16,
	WAVES_COUNT_BUILTIN,
	/* Up to 60 arbitrary waveforms. */
	WAVES_ARB_BASE = 100,
	WAVE_ARB01 = WAVES_ARB_BASE +  1,
	/* ... */
	WAVE_ARB60 = WAVES_ARB_BASE + 60,
	WAVES_PAST_LAST_ARB,
};
#define WAVES_COUNT_ARBITRARY	(WAVES_PAST_LAST_ARB - WAVE_ARB01)

static const char *waveform_names[] = {
	[WAVE_SINE] = "sine",
	[WAVE_SQUARE] = "square",
	[WAVE_PULSE] = "pulse",
	[WAVE_TRIANGLE] = "triangle",
	[WAVE_PARTIAL_SINE] = "partial-sine",
	[WAVE_CMOS] = "cmos",
	[WAVE_DC] = "dc",
	[WAVE_HALF_WAVE] = "half-wave",
	[WAVE_FULL_WAVE] = "full-wave",
	[WAVE_POS_LADDER] = "pos-ladder",
	[WAVE_NEG_LADDER] = "neg-ladder",
	[WAVE_NOISE] = "noise",
	[WAVE_EXP_RISE] = "exp-rise",
	[WAVE_EXP_DECAY] = "exp-decay",
	[WAVE_MULTI_TONE] = "multi-tone",
	[WAVE_SINC] = "sinc",
	[WAVE_LORENZ] = "lorenz",
};
#define WAVEFORM_ARB_NAME_FMT	"arb-%02zu"

static void log_raw_bytes(const char *caption, GString *buff)
{
	GString *text;

	if (!WITH_SERIAL_RAW_DUMP)
		return;
	if (sr_log_loglevel_get() < SR_LOG_SPEW)
		return;

	if (!caption)
		caption = "";
	text = sr_hexdump_new((const uint8_t *)buff->str, buff->len);
	sr_spew("%s%s", caption, text->str);
	sr_hexdump_free(text);
}

/*
 * Writes a text line to the serial port. Normalizes end-of-line
 * including trailing period.
 *
 * Accepts:
 *   ":r01=0.<CR><LF>"
 *   ":r01=0."
 *   ":r01=0<LF>"
 *   ":r01=0"
 * Normalizes to:
 *   ":r01=0.<CR><LF>"
 */
static int serial_send_textline(const struct sr_dev_inst *sdi,
	GString *s, unsigned int delay_ms)
{
	struct sr_serial_dev_inst *conn;
	const char *rdptr;
	size_t padlen, rdlen, wrlen;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	conn = sdi->conn;
	if (!conn)
		return SR_ERR_ARG;
	if (!s)
		return SR_ERR_ARG;

	/*
	 * Trim surrounding whitespace. Normalize to canonical format.
	 * Make sure there is enough room for the period and CR/LF
	 * (and NUL termination). Use a glib API that's easy to adjust
	 * the padded length of. Performance is not a priority here.
	 */
	padlen = 4;
	while (padlen--)
		g_string_append_c(s, '\0');
	rdptr = sr_text_trim_spaces(s->str);
	rdlen = strlen(rdptr);
	if (rdlen && rdptr[rdlen - 1] == '.')
		rdlen--;
	g_string_set_size(s, rdlen);
	g_string_append_c(s, '.');
	sr_spew("serial TX text: --> %s", rdptr);
	g_string_append_c(s, '\r');
	g_string_append_c(s, '\n');
	rdlen = strlen(rdptr);
	log_raw_bytes("serial TX bytes: --> ", s);

	/* Handle chunked writes, check for transmission errors. */
	while (rdlen) {
		ret = serial_write_blocking(conn, rdptr, rdlen, 0);
		if (ret < 0)
			return SR_ERR_IO;
		wrlen = (size_t)ret;
		if (wrlen > rdlen)
			wrlen = rdlen;
		rdptr += wrlen;
		rdlen -= wrlen;
	}

	if (delay_ms)
		g_usleep(delay_ms * 1000);

	return SR_OK;
}

/*
 * Reads a text line from the serial port. Assumes that only a single
 * response text line is in flight (does not handle the case of more
 * receive data following after the first EOL). Transparently deals
 * with trailing period and end-of-line, so callers need not bother.
 *
 * Checks plausibility when the caller specifies conditions to check.
 * Optionally returns references (and lengths) to the response's RHS.
 * That's fine because data resides in a caller provided buffer.
 */
static int serial_recv_textline(const struct sr_dev_inst *sdi,
	GString *s, unsigned int delay_ms, unsigned int timeout_ms,
	gboolean *is_ok, char wants_insn, size_t wants_index,
	char **rhs_start, size_t *rhs_length)
{
	struct sr_serial_dev_inst *ser;
	char *rdptr;
	size_t rdlen, got;
	int ret;
	guint64 now_us, deadline_us;
	gboolean has_timedout;
	char *eol_pos, *endptr;
	char got_insn;
	unsigned long got_index;

	if (is_ok)
		*is_ok = FALSE;
	if (rhs_start)
		*rhs_start = NULL;
	if (rhs_length)
		*rhs_length = 0;

	if (!sdi)
		return SR_ERR_ARG;
	ser = sdi->conn;
	if (!ser)
		return SR_ERR_ARG;
	if (!s)
		return SR_ERR_ARG;

	g_string_set_size(s, MAX_RSP_LENGTH);
	g_string_truncate(s, 0);

	/* Arrange for overall receive timeout when caller specified. */
	now_us = deadline_us = 0;
	if (timeout_ms) {
		now_us = g_get_monotonic_time();
		deadline_us = now_us;
		deadline_us += timeout_ms * 1000;
	}

	rdptr = s->str;
	rdlen = s->allocated_len - 1 - s->len;
	while (rdlen) {
		/* Get another chunk of receive data. Check for EOL. */
		ret = serial_read_blocking(ser, rdptr, rdlen, delay_ms);
		if (ret < 0)
			return SR_ERR_IO;
		got = (size_t)ret;
		if (got > rdlen)
			got = rdlen;
		rdptr[got] = '\0';
		eol_pos = strchr(rdptr, '\n');
		rdptr += got;
		rdlen -= got;
		g_string_set_size(s, s->len + got);
		/* Check timeout expiration upon empty reception. */
		has_timedout = FALSE;
		if (timeout_ms && !got) {
			now_us = g_get_monotonic_time();
			if (now_us >= deadline_us)
				has_timedout = TRUE;
		}
		if (!eol_pos) {
			if (has_timedout)
				break;
			continue;
		}
		log_raw_bytes("serial RX bytes: <-- ", s);

		/* Normalize the received text line. */
		*eol_pos++ = '\0';
		rdptr = s->str;
		(void)sr_text_trim_spaces(rdptr);
		rdlen = strlen(rdptr);
		sr_spew("serial RX text: <-- %s", rdptr);
		if (rdlen && rdptr[rdlen - 1] == '.')
			rdptr[--rdlen] = '\0';

		/* Check conditions as requested by the caller. */
		if (is_ok || wants_insn || rhs_start) {
			if (*rdptr != ':') {
				sr_dbg("serial read, colon missing");
				return SR_ERR_DATA;
			}
			rdptr++;
			rdlen--;
		}
		/*
		 * The check for 'ok' is terminal. Does not combine with
		 * responses which carry payload data on their RHS.
		 */
		if (is_ok) {
			*is_ok = strcmp(rdptr, "ok") == 0;
			sr_dbg("serial read, 'ok' check %d", *is_ok);
			return *is_ok ? SR_OK : SR_ERR_DATA;
		}
		/*
		 * Conditional strict checks for caller's expected fields.
		 * Unconditional weaker checks for general structure.
		 */
		if (wants_insn && *rdptr != wants_insn) {
			sr_dbg("serial read, unexpected insn");
			return SR_ERR_DATA;
		}
		got_insn = *rdptr++;
		switch (got_insn) {
		case INSN_WRITE_PARA:
		case INSN_READ_PARA:
		case INSN_WRITE_WAVE:
		case INSN_READ_WAVE:
			/* EMPTY */
			break;
		default:
			sr_dbg("serial read, unknown insn %c", got_insn);
			return SR_ERR_DATA;
		}
		endptr = NULL;
		ret = sr_atoul_base(rdptr, &got_index, &endptr, 10);
		if (ret != SR_OK || !endptr)
			return SR_ERR_DATA;
		if (wants_index && got_index != wants_index) {
			sr_dbg("serial read, unexpected index %lu", got_index);
			return SR_ERR_DATA;
		}
		rdptr = endptr;
		if (rhs_start || rhs_length) {
			if (*rdptr != '=') {
				sr_dbg("serial read, equals sign missing");
				return SR_ERR_DATA;
			}
		}
		if (*rdptr)
			rdptr++;

		/* Response is considered plausible here. */
		if (rhs_start)
			*rhs_start = rdptr;
		if (rhs_length)
			*rhs_length = strlen(rdptr);
		return SR_OK;
	}
	log_raw_bytes("serial RX bytes: <-- ", s);
	sr_dbg("serial read, unterminated response, discarded");

	return SR_ERR_DATA;
}

/* Formatting helpers for request construction. */

static void append_insn_read_para(GString *s, char insn, size_t idx)
{
	g_string_append_printf(s, ":%c%02zu=0", insn, idx & 0xff);
}

static void append_insn_write_para_va(GString *s, char insn, size_t idx,
	const char *fmt, va_list args) ATTR_FMT_PRINTF(4, 0);
static void append_insn_write_para_va(GString *s, char insn, size_t idx,
	const char *fmt, va_list args)
{
	g_string_append_printf(s, ":%c%02zu=", insn, idx & 0xff);
	g_string_append_vprintf(s, fmt, args);
}

static void append_insn_write_para_dots(GString *s, char insn, size_t idx,
	const char *fmt, ...) ATTR_FMT_PRINTF(4, 5);
static void append_insn_write_para_dots(GString *s, char insn, size_t idx,
	const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	append_insn_write_para_va(s, insn, idx, fmt, args);
	va_end(args);
}

/*
 * Turn comma separators into whitespace. Simplifies the interpretation
 * of multi-value response payloads. Also replaces any trailing period
 * in case callers kept one in the receive buffer.
 */
static void replace_separators(char *s)
{

	while (s && *s) {
		if (s[0] == ',') {
			*s++ = ' ';
			continue;
		}
		if (s[0] == '.' && s[1] == '\0') {
			*s++ = ' ';
			continue;
		}
		s++;
	}
}

/*
 * Convenience to interpret responses' values. Also concentrates the
 * involved magic and simplifies diagnostics. It's essential to apply
 * implicit multipliers, and to properly combine multiple fields into
 * the resulting parameter's value (think scaling and offsetting).
 */

static const double scales_freq[] = {
	1, 1, 1, 1e-3, 1e-6,
};

static int parse_freq_text(char *s, double *value)
{
	char *word;
	int ret;
	double dvalue;
	unsigned long scale;

	replace_separators(s);

	/* First word is a mantissa, in centi-Hertz. :-O */
	word = sr_text_next_word(s, &s);
	ret = sr_atod(word, &dvalue);
	if (ret != SR_OK)
		return ret;

	/* Next word is an encoded scaling factor. */
	word = sr_text_next_word(s, &s);
	ret = sr_atoul_base(word, &scale, NULL, 10);
	if (ret != SR_OK)
		return ret;
	sr_spew("parse freq, mant %f, scale %lu", dvalue, scale);
	if (scale >= ARRAY_SIZE(scales_freq))
		return SR_ERR_DATA;

	/* Do scale the mantissa's value. */
	dvalue /= 100.0;
	dvalue /= scales_freq[scale];
	sr_spew("parse freq, value %f", dvalue);

	if (value)
		*value = dvalue;
	return SR_OK;
}

static int parse_volt_text(char *s, double *value)
{
	int ret;
	double dvalue;

	/* Single value, in units of mV. */
	ret = sr_atod(s, &dvalue);
	if (ret != SR_OK)
		return ret;
	sr_spew("parse volt, mant %f", dvalue);
	dvalue /= 1000.0;
	sr_spew("parse volt, value %f", dvalue);

	if (value)
		*value = dvalue;
	return SR_OK;
}

static int parse_bias_text(char *s, double *value)
{
	int ret;
	double dvalue;

	/*
	 * Single value, in units of 10mV with a 10V offset. Capped to
	 * the +9.99V..-9.99V range. The Joy-IT PDF is a little weird
	 * suggesting that ":w27=9999." translates to 9.99 volts.
	 */
	ret = sr_atod(s, &dvalue);
	if (ret != SR_OK)
		return ret;
	sr_spew("parse bias, mant %f", dvalue);
	dvalue /= 100.0;
	dvalue -= 10.0;
	if (dvalue >= 9.99)
		dvalue = 9.99;
	if (dvalue <= -9.99)
		dvalue = -9.99;
	sr_spew("parse bias, value %f", dvalue);

	if (value)
		*value = dvalue;
	return SR_OK;
}

static int parse_duty_text(char *s, double *value)
{
	int ret;
	double dvalue;

	/*
	 * Single value, in units of 0.1% (permille).
	 * Scale to the 0.0..1.0 range.
	 */
	ret = sr_atod(s, &dvalue);
	if (ret != SR_OK)
		return ret;
	sr_spew("parse duty, mant %f", dvalue);
	dvalue /= 1000.0;
	sr_spew("parse duty, value %f", dvalue);

	if (value)
		*value = dvalue;
	return SR_OK;
}

static int parse_phase_text(char *s, double *value)
{
	int ret;
	double dvalue;

	/* Single value, in units of deci-degrees. */
	ret = sr_atod(s, &dvalue);
	if (ret != SR_OK)
		return ret;
	sr_spew("parse phase, mant %f", dvalue);
	dvalue /= 10.0;
	sr_spew("parse phase, value %f", dvalue);

	if (value)
		*value = dvalue;
	return SR_OK;
}

/*
 * Convenience to generate request presentations. Also concentrates the
 * involved magic and simplifies diagnostics. It's essential to apply
 * implicit multipliers, and to properly create all request fields that
 * communicate a value to the device's firmware (think scale and offset).
 */

static void write_freq_text(GString *s, double freq)
{
	unsigned long scale_idx;
	const char *text_pos;

	sr_spew("write freq, value %f", freq);
	text_pos = &s->str[s->len];

	/*
	 * First word is mantissa in centi-Hertz. Second word is a
	 * scaling factor code. Keep scaling simple, always scale
	 * by a factor of 1.0.
	 */
	scale_idx = 0;
	freq *= scales_freq[scale_idx];
	freq *= 100.0;

	g_string_append_printf(s, "%.0f,%lu", freq, scale_idx);
	sr_spew("write freq, text %s", text_pos);
}

static void write_volt_text(GString *s, double volt)
{
	const char *text_pos;

	sr_spew("write volt, value %f", volt);
	text_pos = &s->str[s->len];

	/*
	 * Single value in units of 1mV.
	 * Limit input values to the 0..+20 range. This writer is only
	 * used by the amplitude setter.
	 */
	if (volt > 20.0)
		volt = 20.0;
	if (volt < 0.0)
		volt = 0.0;
	volt *= 1000.0;
	g_string_append_printf(s, "%.0f", volt);
	sr_spew("write volt, text %s", text_pos);
}

static void write_bias_text(GString *s, double volt)
{
	const char *text_pos;

	sr_spew("write bias, value %f", volt);
	text_pos = &s->str[s->len];

	/*
	 * Single value in units of 10mV with a 10V offset. Capped to
	 * the +9.99..-9.99 range.
	 */
	if (volt > 9.99)
		volt = 9.99;
	if (volt < -9.99)
		volt = -9.99;
	volt += 10.0;
	volt *= 100.0;

	g_string_append_printf(s, "%.0f", volt);
	sr_spew("write bias, text %s", text_pos);
}

static void write_duty_text(GString *s, double duty)
{
	const char *text_pos;

	sr_spew("write duty, value %f", duty);
	text_pos = &s->str[s->len];

	/*
	 * Single value in units of 0.1% (permille). Capped to the
	 * 0.0..1.0 range.
	 */
	if (duty < 0.0)
		duty = 0.0;
	if (duty > 1.0)
		duty = 1.0;
	duty *= 1000.0;

	g_string_append_printf(s, "%.0f", duty);
	sr_spew("write duty, text %s", text_pos);
}

static void write_phase_text(GString *s, double phase)
{
	const char *text_pos;

	sr_spew("write phase, value %f", phase);
	text_pos = &s->str[s->len];

	/*
	 * Single value in units of deci-degrees.
	 * Kept to the 0..360 range by means of a modulo operation.
	 */
	phase = fmod(phase, 360.0);
	phase *= 10.0;

	g_string_append_printf(s, "%.0f", phase);
	sr_spew("write phase, text %s", text_pos);
}

/*
 * Convenience communication wrapper. Re-uses a buffer in devc, which
 * simplifies resource handling in error paths. Sends a parameter-less
 * read-request. Then receives a response which can carry values.
 */
static int quick_send_read_then_recv(const struct sr_dev_inst *sdi,
	char insn, size_t idx,
	unsigned int read_timeout_ms,
	char **rhs_start, size_t *rhs_length)
{
	struct dev_context *devc;
	GString *s;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (!devc->quick_req)
		devc->quick_req = g_string_sized_new(MAX_RSP_LENGTH);
	s = devc->quick_req;

	g_string_truncate(s, 0);
	append_insn_read_para(s, insn, idx);
	ret = serial_send_textline(sdi, s, DELAY_AFTER_SEND);
	if (ret != SR_OK)
		return ret;

	ret = serial_recv_textline(sdi, s,
		TIMEOUT_READ_CHUNK, read_timeout_ms,
		NULL, insn, idx, rhs_start, rhs_length);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * Convenience communication wrapper, re-uses a buffer in devc. Sends a
 * write-request with parameters. Then receives an "ok" style response.
 * Had to put the request details after the response related parameters
 * because of the va_list API.
 */
static int quick_send_write_then_recv_ok(const struct sr_dev_inst *sdi,
	unsigned int read_timeout_ms, gboolean *is_ok,
	char insn, size_t idx, const char *fmt, ...) ATTR_FMT_PRINTF(6, 7);
static int quick_send_write_then_recv_ok(const struct sr_dev_inst *sdi,
	unsigned int read_timeout_ms, gboolean *is_ok,
	char insn, size_t idx, const char *fmt, ...)
{
	struct dev_context *devc;
	GString *s;
	va_list args;
	int ret;
	gboolean ok;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (!devc->quick_req)
		devc->quick_req = g_string_sized_new(MAX_RSP_LENGTH);
	s = devc->quick_req;

	g_string_truncate(s, 0);
	va_start(args, fmt);
	append_insn_write_para_va(s, insn, idx, fmt, args);
	va_end(args);
	ret = serial_send_textline(sdi, s, DELAY_AFTER_SEND);
	if (ret != SR_OK)
		return ret;

	ret = serial_recv_textline(sdi, s,
		TIMEOUT_READ_CHUNK, read_timeout_ms,
		&ok, '\0', 0, NULL, NULL);
	if (is_ok)
		*is_ok = ok;
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * High level getters/setters for device properties.
 * To be used by the api.c config get/set infrastructure.
 */

SR_PRIV int jds6600_get_chans_enable(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	char *rdptr, *word, *endptr;
	struct devc_dev *device;
	struct devc_chan *chans;
	size_t idx;
	unsigned long on;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_CHANNELS_ENABLE,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get enabled, response text: %s", rdptr);

	/* Interpret the response (multiple values, boolean). */
	replace_separators(rdptr);
	device = &devc->device;
	chans = devc->channel_config;
	for (idx = 0; idx < device->channel_count_gen; idx++) {
		word = sr_text_next_word(rdptr, &rdptr);
		if (!word || !*word)
			return SR_ERR_DATA;
		endptr = NULL;
		ret = sr_atoul_base(word, &on, &endptr, 10);
		if (ret != SR_OK || !endptr || *endptr)
			return SR_ERR_DATA;
		chans[idx].enabled = on;
	}

	return SR_OK;
}

SR_PRIV int jds6600_get_waveform(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	int ret;
	char *rdptr, *endptr;
	struct devc_wave *waves;
	struct devc_chan *chan;
	unsigned long code;
	size_t idx;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	waves = &devc->waveforms;
	if (ch_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_WAVEFORM_CH1 + ch_idx,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get waveform, response text: %s", rdptr);

	/*
	 * Interpret the response (integer value, waveform code).
	 * Lookup the firmware's code for that waveform in the
	 * list of user perceivable names for waveforms.
	 */
	endptr = NULL;
	ret = sr_atoul_base(rdptr, &code, &endptr, 10);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	for (idx = 0; idx < waves->names_count; idx++) {
		if (code != waves->fw_codes[idx])
			continue;
		chan->waveform_code = code;
		chan->waveform_index = idx;
		sr_dbg("get waveform, code %lu, idx %zu, name %s",
			code, idx, waves->names[idx]);
		return SR_OK;
	}

	return SR_ERR_DATA;
}

#if WITH_ARBWAVE_DOWNLOAD
/*
 * Development HACK. Get a waveform from the device. Uncertain where to
 * dump it though. Have yet to identify a sigrok API for waveforms.
 */
static int jds6600_get_arb_waveform(const struct sr_dev_inst *sdi, size_t idx)
{
	struct dev_context *devc;
	struct devc_wave *waves;
	int ret;
	char *rdptr, *word, *endptr;
	size_t sample_count;
	unsigned long value;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	waves = &devc->waveforms;

	if (idx >= waves->arbitrary_count)
		return SR_ERR_ARG;

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_WAVE, idx,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get arb wave, response text: %s", rdptr);

	/* Extract the sequence of samples for the waveform. */
	replace_separators(rdptr);
	sample_count = 0;
	while (rdptr && *rdptr) {
		word = sr_text_next_word(rdptr, &rdptr);
		if (!word)
			break;
		endptr = NULL;
		ret = sr_atoul_base(word, &value, &endptr, 10);
		if (ret != SR_OK || !endptr || *endptr) {
			sr_dbg("get arb wave, conv error: %s", word);
			return SR_ERR_DATA;
		}
		sample_count++;
	}
	sr_dbg("get arb wave, samples count: %zu", sample_count);

	return SR_OK;
}
#endif

SR_PRIV int jds6600_get_frequency(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	int ret;
	char *rdptr;
	double freq;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_FREQUENCY_CH1 + ch_idx,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get frequency, response text: %s", rdptr);

	/* Interpret the response (value and scale, frequency). */
	ret = parse_freq_text(rdptr, &freq);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	sr_dbg("get frequency, value %f", freq);
	chan->output_frequency = freq;
	return SR_OK;
}

SR_PRIV int jds6600_get_amplitude(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	int ret;
	char *rdptr;
	double amp;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_AMPLITUDE_CH1 + ch_idx,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get amplitude, response text: %s", rdptr);

	/* Interpret the response (single value, a voltage). */
	ret = parse_volt_text(rdptr, &amp);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	sr_dbg("get amplitude, value %f", amp);
	chan->amplitude = amp;
	return SR_OK;
}

SR_PRIV int jds6600_get_offset(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	int ret;
	char *rdptr;
	double off;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_OFFSET_CH1 + ch_idx,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get offset, response text: %s", rdptr);

	/* Interpret the response (single value, an offset). */
	ret = parse_bias_text(rdptr, &off);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	sr_dbg("get offset, value %f", off);
	chan->offset = off;
	return SR_OK;
}

SR_PRIV int jds6600_get_dutycycle(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	int ret;
	char *rdptr;
	double duty;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= ARRAY_SIZE(devc->channel_config))
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_DUTYCYCLE_CH1 + ch_idx,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get duty cycle, response text: %s", rdptr);

	/* Interpret the response (single value, a percentage). */
	ret = parse_duty_text(rdptr, &duty);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	sr_dbg("get duty cycle, value %f", duty);
	chan->dutycycle = duty;
	return SR_OK;
}

SR_PRIV int jds6600_get_phase_chans(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	char *rdptr;
	double phase;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Transmit the request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_PHASE_CHANNELS,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("get phase, response text: %s", rdptr);

	/* Interpret the response (single value, an angle). */
	ret = parse_phase_text(rdptr, &phase);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	sr_dbg("get phase, value %f", phase);
	devc->channels_phase = phase;
	return SR_OK;
}

SR_PRIV int jds6600_set_chans_enable(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct devc_chan *chans;
	GString *en_text;
	size_t idx;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Transmit the request, receive an "ok" style response. */
	chans = devc->channel_config;
	en_text = g_string_sized_new(20);
	for (idx = 0; idx < devc->device.channel_count_gen; idx++) {
		if (en_text->len)
			g_string_append_c(en_text, ',');
		g_string_append_c(en_text, chans[idx].enabled ? '1' : '0');
	}
	sr_dbg("set enabled, request text: %s", en_text->str);
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_CHANNELS_ENABLE, "%s", en_text->str);
	g_string_free(en_text, 20);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int jds6600_set_waveform(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= devc->device.channel_count_gen)
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive an "ok" style response. */
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_WAVEFORM_CH1 + ch_idx,
		"%" PRIu32, chan->waveform_code);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

#if WITH_ARBWAVE_DOWNLOAD
/*
 * Development HACK. Send a waveform to the device. Uncertain where
 * to get it from though. Just generate some stupid pattern that's
 * seen on the LCD later.
 *
 * Local experiments suggest that writing another waveform after having
 * written one earlier results in the next waveform to become mangled.
 * It appears to start with an all-bits-set pattern for a remarkable
 * number of samples, before the actually written pattern is seen. Some
 * delay after reception of the ":ok" response may be required to avoid
 * this corruption.
 */

/* Stupid creation of one sample value. Gets waveform index and sample count. */
static uint16_t make_sample(size_t wave, size_t curr, size_t total)
{
	uint16_t max_value, high_value, low_value;
	size_t ival, high_width;
	gboolean is_high;

	/* Get the waveform's amplitudes. */
	max_value = 4096;
	high_value = max_value / (wave + 3);
	high_value = max_value - high_value;
	low_value = max_value - high_value;

	/* Get pulses' total interval, high and low half-periods. */
	ival = (total - 10) / wave;
	high_width = ival / 2;

	/* Check location in the current period. */
	curr %= ival;
	is_high = curr <= high_width;
	return is_high ? high_value : low_value;
}

/* Creation and download of the sequence of samples. */
static int jds6600_set_arb_waveform(const struct sr_dev_inst *sdi, size_t idx)
{
	struct dev_context *devc;
	struct devc_wave *waves;
	GString *wave_text;
	size_t samples_total, samples_curr;
	uint16_t value;
	gboolean ok;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	waves = &devc->waveforms;

	if (idx >= waves->arbitrary_count)
		return SR_ERR_ARG;

	/* Construct a pattern that depends on the waveform index. */
	wave_text = g_string_sized_new(MAX_RSP_LENGTH);
	samples_total = 2048;
	samples_curr = 0;
	for (samples_curr = 0; samples_curr < samples_total; samples_curr++) {
		value = make_sample(idx, samples_curr, samples_total);
		if (samples_curr)
			g_string_append_c(wave_text, ',');
		g_string_append_printf(wave_text, "%" PRIu16, value);
	}
	sr_dbg("set arb wave, request text: %s", wave_text->str);

	/* Transmit the request, receive an "ok" style response. */
	ret = quick_send_write_then_recv_ok(sdi, 0, &ok,
		INSN_WRITE_WAVE, idx, "%s", wave_text->str);
	if (ret != SR_OK)
		return ret;
	sr_dbg("set arb wave, response ok: %d", ok);

	if (DELAY_AFTER_FLASH)
		g_usleep(DELAY_AFTER_FLASH * 1000);

	return SR_OK;
}
#endif

SR_PRIV int jds6600_set_frequency(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	double freq;
	GString *freq_text;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= devc->device.channel_count_gen)
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Limit input values to the range supported by the model. */
	freq = chan->output_frequency;
	if (freq < 0.01)
		freq = 0.01;
	if (freq > devc->device.max_output_frequency)
		freq = devc->device.max_output_frequency;

	/* Transmit the request, receive an "ok" style response. */
	freq_text = g_string_sized_new(32);
	write_freq_text(freq_text, freq);
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_FREQUENCY_CH1 + ch_idx,
		"%s", freq_text->str);
	g_string_free(freq_text, TRUE);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int jds6600_set_amplitude(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	GString *volt_text;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= devc->device.channel_count_gen)
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive an "ok" style response. */
	volt_text = g_string_sized_new(32);
	write_volt_text(volt_text, chan->amplitude);
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_AMPLITUDE_CH1 + ch_idx,
		"%s", volt_text->str);
	g_string_free(volt_text, TRUE);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int jds6600_set_offset(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	GString *volt_text;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= devc->device.channel_count_gen)
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive an "ok" style response. */
	volt_text = g_string_sized_new(32);
	write_bias_text(volt_text, chan->offset);
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_OFFSET_CH1 + ch_idx,
		"%s", volt_text->str);
	g_string_free(volt_text, TRUE);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int jds6600_set_dutycycle(const struct sr_dev_inst *sdi, size_t ch_idx)
{
	struct dev_context *devc;
	struct devc_chan *chan;
	GString *duty_text;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (ch_idx >= devc->device.channel_count_gen)
		return SR_ERR_ARG;
	chan = &devc->channel_config[ch_idx];

	/* Transmit the request, receive an "ok" style response. */
	duty_text = g_string_sized_new(32);
	write_duty_text(duty_text, chan->dutycycle);
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_DUTYCYCLE_CH1 + ch_idx,
		"%s", duty_text->str);
	g_string_free(duty_text, TRUE);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int jds6600_set_phase_chans(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GString *phase_text;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Transmit the request, receive an "ok" style response. */
	phase_text = g_string_sized_new(32);
	write_phase_text(phase_text, devc->channels_phase);
	ret = quick_send_write_then_recv_ok(sdi, 0, NULL,
		INSN_WRITE_PARA, IDX_PHASE_CHANNELS,
		"%s", phase_text->str);
	g_string_free(phase_text, TRUE);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * High level helpers for the scan/probe phase. Identify the attached
 * device and synchronize to its current state and its capabilities.
 */

SR_PRIV int jds6600_identify(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	char *rdptr, *endptr;
	unsigned long devtype;

	(void)append_insn_write_para_dots;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Transmit "read device type" request, receive the response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_DEVICE_TYPE,
		TIMEOUT_IDENTIFY, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("identify, device type '%s'", rdptr);

	/* Interpret the response (integer value, max freq). */
	endptr = NULL;
	ret = sr_atoul_base(rdptr, &devtype, &endptr, 10);
	if (ret != SR_OK || !endptr)
		return SR_ERR_DATA;
	devc->device.device_type = devtype;

	/* Transmit "read serial number" request. receive response. */
	ret = quick_send_read_then_recv(sdi,
		INSN_READ_PARA, IDX_SERIAL_NUMBER,
		0, &rdptr, NULL);
	if (ret != SR_OK)
		return ret;
	sr_dbg("identify, serial number '%s'", rdptr);

	/* Keep the response (in string format, some serial number). */
	devc->device.serial_number = g_strdup(rdptr);

	return SR_OK;
}

SR_PRIV int jds6600_setup_devc(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	size_t alloc_count, assign_idx, idx;
	struct devc_dev *device;
	struct devc_wave *waves;
	enum waveform_index_t code;
	char *name;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/*
	 * Derive maximum output frequency from detected device type.
	 * Open coded generator channel count.
	 */
	device = &devc->device;
	if (!device->device_type)
		return SR_ERR_DATA;
	device->max_output_frequency = device->device_type;
	device->max_output_frequency *= SR_MHZ(1);
	device->channel_count_gen = MAX_GEN_CHANNELS;

	/* Construct the list of waveform names and their codes. */
	waves = &devc->waveforms;
	waves->builtin_count = WAVES_COUNT_BUILTIN;
	waves->arbitrary_count = WAVES_COUNT_ARBITRARY;
	alloc_count = waves->builtin_count;
	alloc_count += waves->arbitrary_count;
	waves->names_count = alloc_count;
	waves->fw_codes = g_malloc0(alloc_count * sizeof(waves->fw_codes[0]));
	alloc_count++;
	waves->names = g_malloc0(alloc_count * sizeof(waves->names[0]));
	if (!waves->names || !waves->fw_codes) {
		g_free(waves->names);
		g_free(waves->fw_codes);
		return SR_ERR_MALLOC;
	}
	assign_idx = 0;
	for (idx = 0; idx < waves->builtin_count; idx++) {
		code = idx;
		name = g_strdup(waveform_names[idx]);
		waves->fw_codes[assign_idx] = code;
		waves->names[assign_idx] = name;
		assign_idx++;
	}
	for (idx = 0; idx < waves->arbitrary_count; idx++) {
		code = WAVE_ARB01 + idx;
		name = g_strdup_printf(WAVEFORM_ARB_NAME_FMT, idx + 1);
		waves->fw_codes[assign_idx] = code;
		waves->names[assign_idx] = name;
		assign_idx++;
	}
	waves->names[assign_idx] = NULL;

	/*
	 * Populate internal channel configuration details from the
	 * device's current state. Emit a series of queries which
	 * update internal knowledge.
	 *
	 * Implementation detail: Channel count is low, all parameters
	 * are simple scalars. Communication cycles are few, while we
	 * still are in the scan/probe phase and successfully verified
	 * the device to respond. Disconnects and other exceptional
	 * conditions are extremely unlikely. Not checking every getter
	 * call's return value is acceptable here.
	 */
	ret = SR_OK;
	ret |= jds6600_get_chans_enable(sdi);
	for (idx = 0; idx < device->channel_count_gen; idx++) {
		ret |= jds6600_get_waveform(sdi, idx);
		ret |= jds6600_get_frequency(sdi, idx);
		ret |= jds6600_get_amplitude(sdi, idx);
		ret |= jds6600_get_offset(sdi, idx);
		ret |= jds6600_get_dutycycle(sdi, idx);
		if (ret != SR_OK)
			break;
	}
	ret |= jds6600_get_phase_chans(sdi);
	if (ret != SR_OK)
		return SR_ERR_DATA;

#if WITH_ARBWAVE_DOWNLOAD
	/*
	 * Development HACK, to see how waveform upload works.
	 * How to forward the data to the application? Or the
	 * sigrok session actually? Provide these as acquisition
	 * results?
	 */
	ret |= jds6600_get_arb_waveform(sdi, 13);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	ret |= jds6600_set_arb_waveform(sdi, 12);
	ret |= jds6600_set_arb_waveform(sdi, 13);
	if (ret != SR_OK)
		return SR_ERR_DATA;
#endif

	return SR_OK;
}
