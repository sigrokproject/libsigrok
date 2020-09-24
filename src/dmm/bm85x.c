/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2019-2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/**
 * @file
 *
 * Protocol parser for Brymen BM850s DMM packets. The USB protocol (for the
 * cable) and the packet description (for the meter) were retrieved from:
 * http://brymen.com/product-html/Download2.html
 * http://brymen.com/product-html/PD02BM850s_protocolDL.html
 * http://brymen.com/product-html/images/DownloadList/ProtocolList/BM850-BM850a-BM850s_List/BM850-BM850a-BM850s-500000-count-DMM-protocol-BC85X-BC85Xa.zip
 *
 * Implementor's notes on the protocol:
 * - The BM85x devices require a low RTS pulse after COM port open and
 *   before communication of requests and responses. The vendor doc
 *   recommends 100ms pulse width including delays around it. Without
 *   that RTS pulse the meter won't respond to requests.
 * - The request has a three byte header (DLE, STX, command code), two
 *   bytes command arguments, and three bytes tail (checksum, DLE, ETX).
 *   The checksum spans the area (including) the command code and args.
 *   The checksum value is the XOR across all payload bytes. Exclusively
 *   command 0x00 is used (initiate next measurement response) which does
 *   not need arguments (passes all-zero values).
 * - The response has a four byte header (DLE, STX, command code, payload
 *   size), the respective number of payload data bytes, and a three byte
 *   tail (checksum, DLE, ETX). The checksum spans the range after the
 *   length field and before the checksum field. Command 0 response data
 *   payload consists of a four byte flags field and a text field for
 *   measurement values (floating point with exponent in ASCII).
 * - Special cases of response data:
 *   - The text field which carries the measurement value also contains
 *     whitespace which may break simple text to number conversion. Like
 *     10 02 00 0f 07 00 00 00 20 30 2e 30 30 33 32 20 45 2b 30 46 10 03
 *     which translates to: 07 00 00 00 " 0.0032 E+0". Text for overload
 *     conditions can be shorter which results in variable packet length.
 *     Some meter functions provide unexpected text for their values.
 *   - The reference impedance for decibel measurements looks wrong and
 *     requires special treatment to isolate the 4..1200R value:
 *     bfunc 80 20 00 00, text " 0. 800 E+1" (reference, 800R)
 *     The decibel measurement values use an unexpected scale.
 *     bfunc 00 20 00 00, text "-0.3702 E-1" (measurement, -37.02dBm)
 *     The reference value gets sent (sometimes) in a DMM response when
 *     the meter's function is entered, or the reference value changes.
 *     The 'bfunc' flags combination allows telling packet types apart.
 *   - Temperature measurements put the C/F unit between the mantissa
 *     and the exponent, which needs to get removed: " 0.0217CE+3"
 *   - Diode measurements appear to exclusively provide the 'Volt' flag
 *     but no 'Diode' flag. The display shows ".diod" for a moment but
 *     this information is no longer available when voltage measurements
 *     are seen.
 */

#include <config.h>
#include <ctype.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <string.h>

#define LOG_PREFIX "brymen-bm85x"

#define STX 0x02
#define ETX 0x03
#define DLE 0x10

#define CMD_GET_READING	0

#define PKT_HEAD_LEN	4
#define PKT_DATA_MAX	15
#define PKT_TAIL_LEN	3
#define PKT_BFUNC_LEN	4

static uint8_t bm85x_crc(const uint8_t *buf, size_t len)
{
	uint8_t crc;

	crc = 0;
	while (len--)
		crc ^= *buf++;

	return crc;
}

#ifdef HAVE_SERIAL_COMM
/** Meter's specific activity after port open and before data exchange. */
SR_PRIV int brymen_bm85x_after_open(struct sr_serial_dev_inst *serial)
{
	int rts_toggle_delay_us;

	/*
	 * The device requires an RTS *pulse* before communication.
	 * The vendor's documentation recommends the following sequence:
	 * Open the COM port, wait for 100ms, set RTS=1, wait for 100ms,
	 * set RTS=0, wait for 100ms, set RTS=1, configure bitrate and
	 * frame format, transmit request data, receive response data.
	 */
	rts_toggle_delay_us = 100 * 1000; /* 100ms */
	g_usleep(rts_toggle_delay_us);
	serial_set_handshake(serial, 1, -1);
	g_usleep(rts_toggle_delay_us);
	serial_set_handshake(serial, 0, -1);
	g_usleep(rts_toggle_delay_us);
	serial_set_handshake(serial, 1, -1);
	g_usleep(rts_toggle_delay_us);

	return SR_OK;
}

static int bm85x_send_command(struct sr_serial_dev_inst *serial,
	uint8_t cmd, uint8_t arg1, uint8_t arg2)
{
	uint8_t buf[8];
	uint8_t crc, *wrptr, *crcptr;
	size_t wrlen;
	int ret;

	wrptr = &buf[0];
	write_u8_inc(&wrptr, DLE);
	write_u8_inc(&wrptr, STX);
	crcptr = wrptr;
	write_u8_inc(&wrptr, cmd);
	write_u8_inc(&wrptr, arg1);
	write_u8_inc(&wrptr, arg2);
	crc = bm85x_crc(crcptr, wrptr - crcptr);
	write_u8_inc(&wrptr, crc);
	write_u8_inc(&wrptr, DLE);
	write_u8_inc(&wrptr, ETX);

	wrlen = wrptr - &buf[0];
	ret = serial_write_nonblocking(serial, &buf[0], wrlen);
	if (ret < 0)
		return ret;
	if ((size_t)ret != wrlen)
		return SR_ERR_IO;

	return SR_OK;
}

/** Initiate reception of another meter's reading. */
SR_PRIV int brymen_bm85x_packet_request(struct sr_serial_dev_inst *serial)
{
	return bm85x_send_command(serial, CMD_GET_READING, 0, 0);
}
#endif

/**
 * Check Brymen BM85x DMM packet for validity.
 *
 * @param[in] st The DMM driver's internal state.
 * @param[in] buf The data bytes received so far.
 * @param[in] len The received data's length (byte count).
 * @param[out] pkt_len The packet's calculated total size (when valid).
 *
 * The BM850s protocol uses packets of variable length. A minimum amount
 * of RX data provides the packet header, which communicates the payload
 * size, which allows to determine the packet's total size. Callers of
 * this validity checker can learn how much data will get consumed when
 * a valid packet got received and processed. The packet size is not
 * known in advance.
 *
 * @returns SR_OK when the packet is valid.
 * @returns SR_ERR* (below zero) when the packet is invalid.
 * @returns Greater 0 when packet is incomplete, more data is needed.
 */
SR_PRIV int brymen_bm85x_packet_valid(void *st,
	const uint8_t *buf, size_t len, size_t *pkt_len)
{
	size_t plen;
	uint8_t cmd, crc;

	(void)st;

	/* Four header bytes: DLE, STX, command, payload length. */
	if (len < PKT_HEAD_LEN)
		return SR_PACKET_NEED_RX;
	if (read_u8_inc(&buf) != DLE)
		return SR_PACKET_INVALID;
	if (read_u8_inc(&buf) != STX)
		return SR_PACKET_INVALID;
	cmd = read_u8_inc(&buf);
	/* Non-fatal, happens with OL pending during connect. */
	if (cmd == 0x01)
		cmd = 0x00;
	if (cmd != CMD_GET_READING)
		return SR_PACKET_INVALID;
	plen = read_u8_inc(&buf);
	if (plen > PKT_DATA_MAX)
		return SR_PACKET_INVALID;
	len -= PKT_HEAD_LEN;

	/* Checksum spans bfunc and value text. Length according to header. */
	if (len < plen + PKT_TAIL_LEN)
		return SR_PACKET_NEED_RX;
	crc = bm85x_crc(buf, plen);
	buf += plen;
	len -= plen;

	/* Three tail bytes: checksum, DLE, ETX. */
	if (len < PKT_TAIL_LEN)
		return SR_PACKET_NEED_RX;
	if (read_u8_inc(&buf) != crc)
		return SR_PACKET_INVALID;
	if (read_u8_inc(&buf) != DLE)
		return SR_PACKET_INVALID;
	if (read_u8_inc(&buf) != ETX)
		return SR_PACKET_INVALID;

	/*
	 * Only return the total packet length when the receive buffer
	 * was found to be valid. For invalid packets it's preferred to
	 * have the caller keep trying to sync to the packet stream.
	 */
	if (pkt_len)
		*pkt_len = PKT_HEAD_LEN + plen + PKT_TAIL_LEN;
	return SR_PACKET_VALID;
}

struct bm85x_flags {
	gboolean is_batt, is_db, is_perc, is_hz, is_amp, is_beep;
	gboolean is_ohm, is_temp_f, is_temp_c, is_diode, is_cap;
	gboolean is_volt, is_dc, is_ac;
};

static int bm85x_parse_flags(const uint8_t *bfunc, struct bm85x_flags *flags)
{
	if (!bfunc || !flags)
		return SR_ERR_ARG;
	memset(flags, 0, sizeof(*flags));

	flags->is_batt = bfunc[3] & (1u << 7);
	if ((bfunc[3] & 0x7f) != 0)
		return SR_ERR_ARG;

	if ((bfunc[2] & 0xff) != 0)
		return SR_ERR_ARG;

	if ((bfunc[1] & 0xc0) != 0)
		return SR_ERR_ARG;
	flags->is_db = bfunc[1] & (1u << 5);
	if ((bfunc[1] & 0x10) != 0)
		return SR_ERR_ARG;
	flags->is_perc = bfunc[1] & (1u << 3);
	flags->is_hz = bfunc[1] & (1u << 2);
	flags->is_amp = bfunc[1] & (1u << 1);
	flags->is_beep = bfunc[1] & (1u << 0);

	flags->is_ohm = bfunc[0] & (1u << 7);
	flags->is_temp_f = bfunc[0] & (1u << 6);
	flags->is_temp_c = bfunc[0] & (1u << 5);
	flags->is_diode = bfunc[0] & (1u << 4);
	flags->is_cap = bfunc[0] & (1u << 3);
	flags->is_volt = bfunc[0] & (1u << 2);
	flags->is_dc = bfunc[0] & (1u << 1);
	flags->is_ac = bfunc[0] & (1u << 0);

	return SR_OK;
}

static int bm85x_parse_value(char *txt, double *val, int *digits)
{
	char *src, *dst, c;
	int ret;

	/*
	 * See above comment on whitespace in response's number text.
	 * The caller provides a NUL terminated writable text copy.
	 * Go for low hanging fruit first (OL condition). Eliminate
	 * whitespace then and do the number conversion.
	 */
	if (strstr(txt, "+OL")) {
		*val = +INFINITY;
		return SR_OK;
	}
	if (strstr(txt, "-OL")) {
		*val = -INFINITY;
		return SR_OK;
	}
	if (strstr(txt, "OL")) {
		*val = INFINITY;
		return SR_OK;
	}

	src = txt;
	dst = txt;
	while (*src) {
		c = *src++;
		if (c == ' ')
			continue;
		*dst++ = c;
	}
	*dst = '\0';

	ret = sr_atod_ascii_digits(txt, val, digits);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int bm85x_parse_payload(const uint8_t *p, size_t l,
	double *val, struct sr_datafeed_analog *analog)
{
	const uint8_t *bfunc;
	char text_buf[PKT_DATA_MAX], *text;
	size_t text_len;
	int ret;
	struct bm85x_flags flags;
	int digits;
	char *parse;

	/* Get a bfunc bits reference, and a writable value text. */
	bfunc = &p[0];
	text_len = l - PKT_BFUNC_LEN;
	memcpy(text_buf, &p[PKT_BFUNC_LEN], text_len);
	text_buf[text_len] = '\0';
	text = &text_buf[0];
	sr_dbg("DMM bfunc %02x %02x %02x %02x, text \"%s\"",
		bfunc[0], bfunc[1], bfunc[2], bfunc[3], text);

	/* Check 'bfunc' bitfield first, text interpretation depends on it. */
	ret = bm85x_parse_flags(bfunc, &flags);
	if (ret != SR_OK)
		return ret;

	/* Parse the text after potential normalization/transformation. */
	if (flags.is_db && flags.is_ohm) {
		static const char *prefix = " 0.";
		static const char *suffix = " E";
		/* See above comment on dBm reference value text. */
		if (strncmp(text, prefix, strlen(prefix)) != 0)
			return SR_ERR_DATA;
		text += strlen(prefix);
		text_len -= strlen(prefix);
		parse = strstr(text, suffix);
		if (!parse)
			return SR_ERR_DATA;
		*parse = '\0';
	}
	if (flags.is_temp_f || flags.is_temp_c) {
		/* See above comment on temperature value text. */
		parse = strchr(text, flags.is_temp_f ? 'F' : 'C');
		if (!parse)
			return SR_ERR_DATA;
		*parse = ' ';
	}
	digits = 0;
	ret = bm85x_parse_value(text, val, &digits);
	if (ret != SR_OK)
		return ret;

	/* Fill in MQ and flags result details. */
	analog->meaning->mqflags = 0;
	if (flags.is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (flags.is_amp) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (flags.is_ohm) {
		if (flags.is_db)
			analog->meaning->mq = SR_MQ_RESISTANCE;
		else if (flags.is_beep)
			analog->meaning->mq = SR_MQ_CONTINUITY;
		else
			analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (flags.is_hz) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (flags.is_perc) {
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	}
	if (flags.is_cap) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (flags.is_temp_f) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_FAHRENHEIT;
	}
	if (flags.is_temp_c) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		analog->meaning->unit = SR_UNIT_CELSIUS;
	}
	if (flags.is_db && !flags.is_ohm) {
		/* See above comment on dBm measurements scale. */
		analog->meaning->mq = SR_MQ_POWER;
		analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		*val *= 1000;
		digits -= 3;
	}

	if (flags.is_diode) {
		/* See above comment on diode measurement responses. */
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
		analog->meaning->mqflags |= SR_MQFLAG_DIODE;
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	}
	if (flags.is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (flags.is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;

	analog->encoding->digits = digits;
	analog->spec->spec_digits = digits;

	if (flags.is_batt)
		sr_warn("Low battery!");

	return SR_OK;
}

SR_PRIV int brymen_bm85x_parse(void *st, const uint8_t *buf, size_t len,
	double *val, struct sr_datafeed_analog *analog, void *info)
{
	const uint8_t *pl_ptr;
	size_t pl_len;

	(void)st;
	(void)info;

	if (!buf || !len)
		return SR_ERR_DATA;
	if (!val || !analog)
		return SR_ERR_DATA;

	if (brymen_bm85x_packet_valid(NULL, buf, len, NULL) != SR_PACKET_VALID)
		return SR_ERR_DATA;
	pl_ptr = &buf[PKT_HEAD_LEN];
	pl_len = len - PKT_HEAD_LEN - PKT_TAIL_LEN;

	return bm85x_parse_payload(pl_ptr, pl_len, val, analog);
}
