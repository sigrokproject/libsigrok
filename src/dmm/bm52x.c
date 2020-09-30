/*
 * This file is part of the libsigrok project.
 *
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
 * Brymen BM52x serial protocol parser. The USB protocol (for the cable)
 * and the packet description (for the meter) were retrieved from:
 * http://brymen.com/product-html/Download2.html
 * http://brymen.com/product-html/PD02BM520s_protocolDL.html
 * http://brymen.com/product-html/images/DownloadList/ProtocolList/BM520-BM520s_List/BM520-BM520s-10000-count-professional-dual-display-mobile-logging-DMMs-protocol.zip
 *
 * This parser was initially created for BM520s devices and tested with
 * BM525s. The Brymen BM820s family of devices uses the same protocol,
 * with just 0x82 instead of 0x52 in request packets and in the fixed
 * fields of the responses. Which means that the packet parser can get
 * shared among the BM520s and BM820s devices, but validity check needs
 * to be individual, and the "wrong" packet request will end up without
 * a response. Compared to BM520s the BM820s has dBm (in the protocol)
 * and NCV (not seen in the protocol) and is non-logging (live only).
 * BM820s support was tested with BM829s.
 *
 * The parser implementation was tested with a Brymen BM525s meter. Some
 * of the responses differ from the vendor's documentation:
 * - Recording session total byte counts don't start after the byte count
 *   field, but instead include this field and the model ID (spans _every_
 *   byte in the stream).
 * - Recording session start/end markers are referred to as DLE, STX,
 *   and ETX. Observed traffic instead sends 0xee, 0xa0, and 0xc0.
 */

/*
 * TODO
 * - Some of the meter's functions and indications cannot get expressed
 *   by means of sigrok MQ and flags terms. Some indicator's meaning is
 *   unknown or uncertain, and thus their state is not evaluated.
 *   - MAX-MIN, the span between extreme values, referred to as Vp-p.
 *   - AVG is not available in BM525s and BM521s.
 *   - LoZ, eliminating ghost voltages.
 *   - LPF, low pass filter.
 *   - low battery, emits sr_warn() but isn't seen in the feed.
 *   - @, 4-20mA loop, % (main display, left hand side), Hi/Lo. Some of
 *     these are in the vendor's documentation for the DMM packet but not
 *     supported by the BM525s device which motivated the creation of the
 *     parser's and was used to test its operation.
 *   - It's a guess that the many undocumented bits (44 of them) are
 *     related to the bargraph (40 ticks, overflow, sign, 6/10 scale).
 *   - Should T1-T2 have a delta ("relative") decoration? But the meter's
 *     "relative" feature is flexible, accepts any display value as the
 *     reference, including min/max/diff when displayed upon activation.
 *   - The "beep jack" displays "InEr" in the secondary display. This is
 *     not caught here, no PC side message gets emitted.
 * - Support for recordings is mostly untested. It was written to the
 *   letter of the vendor documentation, but was not verified to work
 *   for all of the many meter's modes including ranges. Inspection of
 *   the full byte stream is necessary on one hand since random access
 *   is not available, and useful on the other hand for consistency
 *   checks.
 */

#include <config.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <math.h>
#include <string.h>
#include <strings.h>

#define LOG_PREFIX "brymen-bm52x"

/*
 * DMM specific device options, and state keeping. All of it is related
 * to recorded information in contrast to live readings. There also are
 * four types of requesting HID reports that need to be sent.
 */

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

struct brymen_bm52x_state {
	size_t sess_idx;
	struct {
		uint8_t buff[2 * 32];
		size_t fill_pos;
		size_t read_pos;
		size_t remain;
	} rsp;
	const struct sr_dev_inst *sdi;
};

enum bm52x_reqtype {
	REQ_LIVE_READ_520,
	REQ_LIVE_READ_820,
	REQ_REC_HEAD,
	REQ_REC_NEXT,
	REQ_REC_CURR,
};

#ifdef HAVE_SERIAL_COMM
static int bm52x_send_req(struct sr_serial_dev_inst *serial, enum bm52x_reqtype t)
{
	static const uint8_t req_live_520[] = { 0x00, 0x00, 0x52, 0x66, };
	static const uint8_t req_live_820[] = { 0x00, 0x00, 0x82, 0x66, };
	static const uint8_t req_head[] = { 0x00, 0x00, 0x52, 0x88, };
	static const uint8_t req_next[] = { 0x00, 0x00, 0x52, 0x89, };
	static const uint8_t req_curr[] = { 0x00, 0x00, 0x52, 0x8a, };
	static const uint8_t *req_bytes[] = {
		[REQ_LIVE_READ_520] = req_live_520,
		[REQ_LIVE_READ_820] = req_live_820,
		[REQ_REC_HEAD] = req_head,
		[REQ_REC_NEXT] = req_next,
		[REQ_REC_CURR] = req_curr,
	};
	static const size_t req_len = ARRAY_SIZE(req_live_520);

	const uint8_t *p;
	size_t l;
	int ret;

	if (t >= ARRAY_SIZE(req_bytes))
		return SR_ERR_ARG;
	p = req_bytes[t];
	l = req_len;
	ret = serial_write_nonblocking(serial, p, l);
	if (ret < 0)
		return ret;
	if ((size_t)ret != l)
		return SR_ERR_IO;

	return SR_OK;
}

SR_PRIV int sr_brymen_bm52x_packet_request(struct sr_serial_dev_inst *serial)
{
	return bm52x_send_req(serial, REQ_LIVE_READ_520);
}

SR_PRIV int sr_brymen_bm82x_packet_request(struct sr_serial_dev_inst *serial)
{
	return bm52x_send_req(serial, REQ_LIVE_READ_820);
}
#endif

/*
 * The following code interprets live readings ("real-time download")
 * which arrive in the "traditional" bitmap for LCD segments. Reading
 * previously recorded measurements ("memory data sets") differs a lot
 * and is handled in other code paths.
 */

SR_PRIV gboolean sr_brymen_bm52x_packet_valid(const uint8_t *buf)
{
	if (buf[16] != 0x52)
		return FALSE;
	if (buf[17] != 0x52)
		return FALSE;
	if (buf[18] != 0x52)
		return FALSE;
	if (buf[19] != 0x52)
		return FALSE;

	return TRUE;
}

SR_PRIV gboolean sr_brymen_bm82x_packet_valid(const uint8_t *buf)
{
	if (buf[16] != 0x82)
		return FALSE;
	if (buf[17] != 0x82)
		return FALSE;
	if (buf[18] != 0x82)
		return FALSE;
	if (buf[19] != 0x82)
		return FALSE;

	return TRUE;
}

/*
 * Data bytes in the DMM packet encode LCD segments in an unusual order
 * (bgcpafed) and in an unusual position (bit 4 being the decimal point
 * for some digits, an additional indicator for others). Fortunately all
 * eight digits encode their segments in identical ways across the bytes.
 *
 * These routines convert LCD segments to characters, and a section of the
 * DMM packet (which corresponds to the primary or secondary display) to
 * the text representation of the measurement's value, before regular text
 * to number conversion is applied, and SI units and their prefixes get
 * derived from more indicators. It's important to keep in mind similar
 * indicators exist for main and secondary displays in different locations.
 */

static char brymen_bm52x_parse_digit(uint8_t b)
{
	switch (b & ~0x10) {
	/* Sign. */
	case 0x40: /* ------g */ return '-';
	/* Decimal digits. */
	case 0xaf: /* abcdef- */ return '0';
	case 0xa0: /* -bc---- */ return '1';
	case 0xcb: /* ab-de-g */ return '2';
	case 0xe9: /* abcd--g */ return '3';
	case 0xe4: /* -bc--fg */ return '4';
	case 0x6d: /* a-cd-fg */ return '5';
	case 0x6f: /* a-cdefg */ return '6';
	case 0xa8: /* abc---- */ return '7';
	case 0xef: /* abcdefg */ return '8';
	case 0xed: /* abcd-fg */ return '9';
	/* Temperature units. */
	case 0x0f: /* a--def- */ return 'C';
	case 0x4e: /* a---efg */ return 'F';
	/* OL condition, and diode and "Auto" modes. */
	case 0x07: /* ---def- */ return 'L';
	case 0xe3: /* -bcde-g */ return 'd';
	case 0x20: /* --c---- */ return 'i';
	case 0x63: /* --cde-g */ return 'o';
	case 0xee: /* abc-efg */ return 'A';
	case 0x23: /* --cde-- */ return 'u';
	case 0x47: /* ---defg */ return 't';
	/* Blank digit. */
	case 0x00: /* ------- */ return '\0';
	/* Invalid or unknown segment combination. */
	default:
		sr_warn("Unknown encoding for digit: 0x%02x.", b);
		return '\0';
	}
}

static int brymen_bm52x_parse_digits(const uint8_t *pkt, size_t pktlen,
	char *txtbuf, float *value, char *temp_unit, int *digits, int signflag)
{
	uint8_t byte;
	char *txtptr, txtchar;
	size_t pos;
	int ret;

	txtptr = txtbuf;
	if (digits)
		*digits = INT_MIN;

	if (pkt[0] & signflag)
		*txtptr++ = '-';
	for (pos = 0; pos < pktlen; pos++) {
		byte = pkt[1 + pos];
		txtchar = brymen_bm52x_parse_digit(byte);
		if (pos == 3 && (txtchar == 'C' || txtchar == 'F')) {
			if (temp_unit)
				*temp_unit = txtchar;
		} else if (txtchar) {
			*txtptr++ = txtchar;
			if (digits)
				(*digits)++;
		}
		if (pos < 3 && (byte & 0x10)) {
			*txtptr++ = '.';
			if (digits)
				*digits = 0;
		}
	}
	*txtptr = '\0';

	if (digits && *digits < 0)
		*digits = 0;

	ret = value ? sr_atof_ascii(txtbuf, value) : SR_OK;
	if (ret != SR_OK) {
		sr_dbg("invalid float string: '%s'", txtbuf);
		return ret;
	}

	return SR_OK;
}

/*
 * Extract the measurement value and its properties for one of the
 * meter's displays from the DMM packet.
 */
static void brymen_bm52x_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, size_t ch_idx)
{
	char txtbuf[16], temp_unit;
	int ret, digits, scale;
	int is_diode, is_auto, is_no_temp, is_ol, is_db, is_main_milli;
	int is_mm_max, is_mm_min, is_mm_avg, is_mm_dash;

	temp_unit = '\0';
	if (ch_idx == 0) {
		/*
		 * Main display. Note that _some_ of the second display's
		 * indicators are involved in the inspection of the _first_
		 * display's measurement value. So we have to get the
		 * second display's text buffer here, too.
		 */
		(void)brymen_bm52x_parse_digits(&buf[7], 4, txtbuf,
			NULL, NULL, NULL, 0);
		is_diode = strcmp(txtbuf, "diod") == 0;
		is_auto = strcmp(txtbuf, "Auto") == 0;
		ret = brymen_bm52x_parse_digits(&buf[2], 4, txtbuf,
			floatval, &temp_unit, &digits, 0x80);
		is_ol = strstr(txtbuf, "0L") || strstr(txtbuf, "0.L");
		is_no_temp = strcmp(txtbuf, "---C") == 0;
		is_no_temp |= strcmp(txtbuf, "---F") == 0;
		if (ret != SR_OK && !is_ol)
			return;

		/* SI unit, derived from meter's current function. */
		is_db = buf[6] & 0x10;
		is_main_milli = buf[14] & 0x40;
		if (buf[14] & 0x20) {
			analog->meaning->mq = SR_MQ_VOLTAGE;
			analog->meaning->unit = SR_UNIT_VOLT;
			if (is_diode) {
				analog->meaning->mqflags |= SR_MQFLAG_DIODE;
				analog->meaning->mqflags |= SR_MQFLAG_DC;
			}
		} else if (buf[14] & 0x10) {
			analog->meaning->mq = SR_MQ_CURRENT;
			analog->meaning->unit = SR_UNIT_AMPERE;
		} else if (buf[14] & 0x01) {
			analog->meaning->mq = SR_MQ_CAPACITANCE;
			analog->meaning->unit = SR_UNIT_FARAD;
		} else if (buf[14] & 0x02) {
			analog->meaning->mq = SR_MQ_CONDUCTANCE;
			analog->meaning->unit = SR_UNIT_SIEMENS;
		} else if (buf[13] & 0x10) {
			analog->meaning->mq = SR_MQ_FREQUENCY;
			analog->meaning->unit = SR_UNIT_HERTZ;
		} else if (buf[7] & 0x01) {
			analog->meaning->mq = SR_MQ_CONTINUITY;
			analog->meaning->unit = SR_UNIT_OHM;
		} else if (buf[13] & 0x20) {
			analog->meaning->mq = SR_MQ_RESISTANCE;
			analog->meaning->unit = SR_UNIT_OHM;
		} else if (is_db && is_main_milli) {
			analog->meaning->mq = SR_MQ_POWER;
			analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		} else if (buf[14] & 0x04) {
			analog->meaning->mq = SR_MQ_DUTY_CYCLE;
			analog->meaning->unit = SR_UNIT_PERCENTAGE;
		} else if ((buf[2] & 0x09) && temp_unit) {
			if (is_no_temp)
				return;
			analog->meaning->mq = SR_MQ_TEMPERATURE;
			if (temp_unit == 'F')
				analog->meaning->unit = SR_UNIT_FAHRENHEIT;
			else
				analog->meaning->unit = SR_UNIT_CELSIUS;
		}

		/*
		 * Remove the MIN/MAX/AVG indicators when all of them
		 * are shown at the same time (indicating that recording
		 * is active, but live readings are shown). This also
		 * removes the MAX-MIN (V p-p) indication which cannot
		 * get represented by SR_MQFLAG_* means.
		 *
		 * Keep the check conditions separate to simplify future
		 * maintenance when Vp-p gets added. Provide the value of
		 * currently unsupported modes just without flags (show
		 * the maximum amount of LCD content on screen that we
		 * can represent in sigrok).
		 */
		is_mm_max = buf[1] & 0x01;
		is_mm_min = buf[1] & 0x08;
		is_mm_avg = buf[1] & 0x02;
		is_mm_dash = buf[1] & 0x04;
		if (is_mm_max && is_mm_min && is_mm_avg)
			is_mm_max = is_mm_min = is_mm_avg = 0;
		if (is_mm_max && is_mm_min && is_mm_dash)
			is_mm_max = is_mm_min = 0;
		if (is_mm_max && is_mm_min && !is_mm_dash)
			is_mm_max = is_mm_min = 0;

		/* AC/DC/Auto flags. Hold/Min/Max/Rel etc flags. */
		if (buf[1] & 0x20)
			analog->meaning->mqflags |= SR_MQFLAG_DC;
		if (buf[1] & 0x10)
			analog->meaning->mqflags |= SR_MQFLAG_AC;
		if (buf[20] & 0x10)
			analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
		if (buf[20] & 0x80)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		if (is_mm_max)
			analog->meaning->mqflags |= SR_MQFLAG_MAX;
		if (is_mm_min)
			analog->meaning->mqflags |= SR_MQFLAG_MIN;
		if (is_mm_avg)
			analog->meaning->mqflags |= SR_MQFLAG_AVG;
		if (buf[2] & 0x40)
			analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;

		/*
		 * Remove the "dBm" indication's "m" indicator before the
		 * SI unit's prefixes get inspected. Avoids an interaction
		 * with the "milli" prefix. Strictly speaking BM525s does
		 * not support dBm, but other models do and we may want
		 * to share the protocol parser.
		 */
		if (is_db)
			is_main_milli = 0;

		/* SI prefix. */
		scale = 0;
		if (buf[14] & 0x08) /* n */
			scale = -9;
		if (buf[14] & 0x80) /* u */
			scale = -6;
		if (is_main_milli) /* m */
			scale = -3;
		if (buf[13] & 0x80) /* k */
			scale = +3;
		if (buf[13] & 0x40) /* M */
			scale = +6;
		if (scale) {
			*floatval *= pow(10, scale);
			digits += -scale;
		}

		if (is_ol)
			*floatval = INFINITY;

		analog->encoding->digits  = digits;
		analog->spec->spec_digits = digits;
	} else if (ch_idx == 1) {
		/*
		 * Secondary display. Also inspect _some_ primary display
		 * data, to determine the secondary display's validity.
		 */
		(void)brymen_bm52x_parse_digits(&buf[2], 4, txtbuf,
			NULL, &temp_unit, NULL, 0x80);
		ret = brymen_bm52x_parse_digits(&buf[7], 4, txtbuf,
			floatval, NULL, &digits, 0x20);
		is_diode = strcmp(txtbuf, "diod") == 0;
		is_auto = strcmp(txtbuf, "Auto") == 0;
		is_no_temp = strcmp(txtbuf, "---C") == 0;
		is_no_temp |= strcmp(txtbuf, "---F") == 0;
		if (is_diode || is_auto)
			return;
		if (is_no_temp)
			return;

		/* SI unit. */
		if (buf[12] & 0x10) {
			analog->meaning->mq = SR_MQ_VOLTAGE;
			analog->meaning->unit = SR_UNIT_VOLT;
		} else if (buf[12] & 0x20) {
			analog->meaning->mq = SR_MQ_CURRENT;
			if (buf[11] & 0x10)
				analog->meaning->unit = SR_UNIT_PERCENTAGE;
			else
				analog->meaning->unit = SR_UNIT_AMPERE;
		} else if (buf[13] & 0x02) {
			analog->meaning->mq = SR_MQ_RESISTANCE;
			analog->meaning->unit = SR_UNIT_OHM;
		} else if (buf[12] & 0x02) {
			analog->meaning->mq = SR_MQ_CONDUCTANCE;
			analog->meaning->unit = SR_UNIT_SIEMENS;
		} else if (buf[12] & 0x01) {
			analog->meaning->mq = SR_MQ_CAPACITANCE;
			analog->meaning->unit = SR_UNIT_FARAD;
		} else if (buf[7] & 0x06) {
			if (strstr(txtbuf, "---"))
				return;
			analog->meaning->mq = SR_MQ_TEMPERATURE;
			if (temp_unit == 'F')
				analog->meaning->unit = SR_UNIT_FAHRENHEIT;
			else
				analog->meaning->unit = SR_UNIT_CELSIUS;
		} else if (buf[13] & 0x01) {
			analog->meaning->mq = SR_MQ_FREQUENCY;
			analog->meaning->unit = SR_UNIT_HERTZ;
		} else if (buf[11] & 0x08) {
			analog->meaning->mq = SR_MQ_DUTY_CYCLE;
			analog->meaning->unit = SR_UNIT_PERCENTAGE;
		}

		/* DC/AC flags. */
		if (buf[7] & 0x80)
			analog->meaning->mqflags |= SR_MQFLAG_DC;
		if (buf[7] & 0x40)
			analog->meaning->mqflags |= SR_MQFLAG_AC;

		/* SI prefix. */
		scale = 0;
		if (buf[12] & 0x04) /* n */
			scale = -9;
		if (buf[12] & 0x40) /* u */
			scale = -6;
		if (buf[12] & 0x80) /* m */
			scale = -3;
		if (buf[13] & 0x04) /* k */
			scale = +3;
		if (buf[13] & 0x08) /* M */
			scale = +6;
		if (scale) {
			*floatval *= pow(10, scale);
			digits += -scale;
		}

		analog->encoding->digits  = digits;
		analog->spec->spec_digits = digits;
	}

	if (buf[7] & 0x08)
		sr_warn("Battery is low.");
}

SR_PRIV int sr_brymen_bm52x_parse(const uint8_t *buf, float *val,
	struct sr_datafeed_analog *analog, void *info)
{
	struct brymen_bm52x_info *info_local;
	size_t ch_idx;

	/*
	 * Scan a portion of the received DMM packet which corresponds
	 * to the caller's specified display. Then prepare to scan a
	 * different portion of the packet for another display. This
	 * routine gets called multiple times for one received packet.
	 */
	info_local = info;
	ch_idx = info_local->ch_idx;
	brymen_bm52x_parse(buf, val, analog, ch_idx);
	info_local->ch_idx = ch_idx + 1;

	return SR_OK;
}

/*
 * The above code paths support live readings ("real-time download").
 * The below code paths support recordings ("memory data sets") which
 * use different requests and responses and measurement representation
 * which feels like "a different meter".
 */

/*
 * Developer notes, example data for recorded sessions.
 *
 * model
 * 01
 *    total bytes
 *    e6 02 00
 *             session count
 *             01 00
 *                   "DLE/STX" marker
 *                   ee a0
 *                         PS/NS addresses
 *                         8a 03 a0 60 03 a0
 *                                           func/sel/stat (DC-V, single display)
 *                                           02 00 00
 *                                                    session page length in bytes (3 * 240)
 *                                                    d0 02 00
 *                                                             main[/secondary] display data
 *                                                             00 00 00 00
 *                                                                          checksums and padding
 *                                                                          7c 05 00 00 00 00 00 00
 * 00 00 80 00 00 80 00 00 80 00 00 80 00 00 00 00 00 80 00 00 80 00 00 80  80 03 00 00 00 00 00 00
 * 00 00 00 00 00 00 00 00 80 00 00 80 00 00 80 00 00 80 00 00 80 00 00 80  00 03 00 00 00 00 00 00
 * ...
 * 00 00 80 00 00 00 00 00 00 00 00 80 00 00 80 00 00 80 00 00 80 00 00 80  00 03 00 00 00 00 00 00
 * 00 00 80 00 00 80 00 00 80 00 00 80 00 00 80 00 00 80 00 00
 *                                                             "DLE/ETX" marker
 *                                                             ee c0
 *                                                                          ae 04 00 00 00 00 00 00
 *
 * - Checksum in bytes[25:24] is the mere sum of bytes[0:23].
 * - Model ID is 0 or 1 -- does this translate to BM521s and BM525s?
 * - Total byte count _includes_ everything starting at model ID.
 * - There is no measurements count for a session page, but its length
 *   in bytes, and a dual display flag, which lets us derive the count.
 * - STX/ETX/DLE markers don't use the expected ASCII codes.
 */

/*
 * See vendor doc table 3.1 "Logging interval". Includes sub-1Hz rates,
 * but also sub-1s intervals. Let's keep both presentations at hand.
 */
static const struct {
	unsigned int ival_secs;
	unsigned int freq_rate;
} bm52x_rec_ivals[] = {
	[ 0] = {   0, 20, },
	[ 1] = {   0, 10, },
	[ 2] = {   0,  2, },
	[ 3] = {   1,  1, },
	[ 4] = {   2,  0, },
	[ 5] = {   3,  0, },
	[ 6] = {   4,  0, },
	[ 7] = {   5,  0, },
	[ 8] = {  10,  0, },
	[ 9] = {  15,  0, },
	[10] = {  30,  0, },
	[11] = {  60,  0, },
	[12] = { 120,  0, },
	[13] = { 180,  0, },
	[14] = { 300,  0, },
	[15] = { 600,  0, },
};

/*
 * See vendor doc table 6 "Range bits". Temperature is not listed there
 * but keeping it here unifies the processing code paths.
 */
static const int bm52x_ranges_volt[16] = { 3, 2, 1, 0, };
static const int bm52x_ranges_millivolt[16] = { 5, 4, };
static const int bm52x_ranges_freq[16] = { 3, 2, 1, 0, -1, -2, -3, };
static const int bm52x_ranges_duty[16] = { 2, 1, };
static const int bm52x_ranges_ohm[16] = { 1, 0, -1, -2, -3, -4, };
static const int bm52x_ranges_cond[16] = { 11, };
static const int bm52x_ranges_cap[16] = { 11, 10, 9, 8, 7, 6, 5, };
static const int bm52x_ranges_diode[16] = { 3, };
static const int bm52x_ranges_temp[16] = { 0, };
static const int bm52x_ranges_amp[16] = { 3, 2, };
static const int bm52x_ranges_milliamp[16] = { 5, 4, };
static const int bm52x_ranges_microamp[16] = { 7, 6, };

/** Calculate checksum of four-HID-report responses (recordings). */
static uint16_t bm52x_rec_checksum(const uint8_t *b, size_t l)
{
	uint16_t cs;

	cs = 0;
	while (l--)
		cs += *b++;

	return cs;
}

/**
 * Retrieve the first/next chunk of recording information.
 * Support for live readings is theoretical, and unused/untested.
 */
static int bm52x_rec_next_rsp(struct sr_serial_dev_inst *serial,
	enum bm52x_reqtype req, struct brymen_bm52x_state *state)
{
	uint8_t *b;
	size_t l;
	int ret;

	/* Seed internal state when sending the HEAD request. */
	if (req == REQ_REC_HEAD || req == REQ_LIVE_READ_520)
		memset(&state->rsp, 0, sizeof(state->rsp));

	/* Move unprocessed content to the front. */
	if (state->rsp.read_pos) {
		b = &state->rsp.buff[0];
		l = state->rsp.fill_pos - state->rsp.read_pos;
		if (l)
			memmove(&b[0], &b[state->rsp.read_pos], l);
		state->rsp.fill_pos -= state->rsp.read_pos;
		state->rsp.read_pos = 0;
	}

	/* Avoid queries for non-existing data. Limit NEXT requests. */
	if (req == REQ_REC_NEXT && !state->rsp.remain)
		return SR_ERR_IO;

	/* Add another response chunk to the read buffer. */
	b = &state->rsp.buff[state->rsp.fill_pos];
	l = req == REQ_LIVE_READ_520 ? 24 : 32;
	if (sizeof(state->rsp.buff) - state->rsp.fill_pos < l)
		return SR_ERR_BUG;
	ret = bm52x_send_req(serial, req);
	if (ret != SR_OK)
		return ret;
	ret = serial_read_blocking(serial, b, l, 1000);
	if (ret < 0)
		return ret;
	if ((size_t)ret != l)
		return SR_ERR_IO;
	state->rsp.fill_pos += l;

	/* Devel support: dump the new receive data. */
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		GString *text;
		const char *req_text;

		req_text = (req == REQ_LIVE_READ_520) ? "LIVE" :
			(req == REQ_REC_HEAD) ? "MEM HEAD" :
			(req == REQ_REC_NEXT) ? "MEM NEXT" :
			(req == REQ_REC_CURR) ? "MEM CURR" :
			"<inv>";
		text = sr_hexdump_new(b, l);
		sr_spew("%s: %s", req_text, text->str);
		sr_hexdump_free(text);
	}

	/* Verify checksum. No CURR repetition is attempted here. */
	if (l > 24) {
		uint16_t calc, rcvd;

		calc = bm52x_rec_checksum(b, 24);
		rcvd = read_u16le(&b[24]);
		if (calc != rcvd)
			return SR_ERR_DATA;
		state->rsp.fill_pos -= 32 - 24;
	}

	/* Seed amount of total available data from HEAD response. */
	if (req == REQ_REC_HEAD) {
		const uint8_t *rdptr;

		rdptr = &state->rsp.buff[0];
		(void)read_u8_inc(&rdptr); /* model ID */
		state->rsp.remain = read_u24le_inc(&rdptr); /* byte count */
	}

	return SR_OK;
}

/** Make sure a minimum amount of response data is available. */
static const uint8_t *bm52x_rec_ensure(struct sr_serial_dev_inst *serial,
	size_t min_count, struct brymen_bm52x_state *state)
{
	size_t got;
	const uint8_t *read_ptr;
	int ret;

	got = state->rsp.fill_pos - state->rsp.read_pos;
	if (got >= min_count) {
		read_ptr = &state->rsp.buff[state->rsp.read_pos];
		return read_ptr;
	}
	ret = bm52x_rec_next_rsp(serial, REQ_REC_NEXT, state);
	if (ret < 0)
		return NULL;
	read_ptr = &state->rsp.buff[state->rsp.read_pos];
	return read_ptr;
}

/** Get a u8 quantity of response data, with auto-fetch and position increment. */
static uint8_t bm52x_rec_get_u8(struct sr_serial_dev_inst *serial,
	struct brymen_bm52x_state *state)
{
	const uint8_t *read_ptr;
	uint8_t value;
	size_t length;

	length = sizeof(value);
	if (length > state->rsp.remain) {
		state->rsp.remain = 0;
		return 0;
	}
	read_ptr = bm52x_rec_ensure(serial, length, state);
	if (!read_ptr)
		return 0;
	value = read_u8(read_ptr);
	state->rsp.read_pos += length;
	state->rsp.remain -= length;
	return value;
}

/** Get a u16 quantity of response data, with auto-fetch and position increment. */
static uint16_t bm52x_rec_get_u16(struct sr_serial_dev_inst *serial,
	struct brymen_bm52x_state *state)
{
	const uint8_t *read_ptr;
	uint16_t value;
	size_t length;

	length = sizeof(value);
	if (length > state->rsp.remain) {
		state->rsp.remain = 0;
		return 0;
	}
	read_ptr = bm52x_rec_ensure(serial, length, state);
	if (!read_ptr)
		return 0;
	value = read_u16le(read_ptr);
	state->rsp.read_pos += length;
	state->rsp.remain -= length;
	return value;
}

/** Get a u24 quantity of response data, with auto-fetch and position increment. */
static uint32_t bm52x_rec_get_u24(struct sr_serial_dev_inst *serial,
	struct brymen_bm52x_state *state)
{
	const uint8_t *read_ptr;
	uint32_t value;
	size_t length;

	length = 24 / sizeof(uint8_t) / 8;
	if (length > state->rsp.remain) {
		state->rsp.remain = 0;
		return 0;
	}
	read_ptr = bm52x_rec_ensure(serial, length, state);
	if (!read_ptr)
		return 0;
	value = read_u24le(read_ptr);
	state->rsp.read_pos += length;
	state->rsp.remain -= length;
	return value;
}

/** Get the HEAD chunk of recording data, determine session page count. */
static int bm52x_rec_get_count(struct brymen_bm52x_state *state,
	struct sr_serial_dev_inst *serial)
{
	int ret;
	size_t byte_count, sess_count;

	memset(&state->rsp, 0, sizeof(state->rsp));
	ret = bm52x_rec_next_rsp(serial, REQ_REC_HEAD, state);
	if (ret != SR_OK)
		return ret;

	(void)bm52x_rec_get_u8(serial, state); /* model ID */
	byte_count = bm52x_rec_get_u24(serial, state); /* total bytes count */
	sess_count = bm52x_rec_get_u16(serial, state); /* session count */
	sr_dbg("bytes %zu, sessions %zu", byte_count, sess_count);

	return sess_count;
}

static double bm52x_rec_get_value(uint32_t raw, const int *ranges, int *digits)
{
	uint16_t val_digs;
	gboolean is_neg, is_ol, low_batt;
	uint8_t range;
	double value;
	int decimals;

	val_digs = raw >> 8;
	is_neg = raw & (1u << 7);
	is_ol = raw & (1u << 6);
	low_batt = raw & (1u << 5);
	range = raw & 0x0f;
	sr_dbg("item: %s%u, %s %s, range %01x",
		is_neg ? "-" : "+", val_digs,
		is_ol ? "OL" : "ol", low_batt ? "BATT" : "batt",
		range);

	/* Convert to number. OL takes precedence. */
	*digits = 0;
	value = val_digs;
	if (ranges && ranges[range]) {
		decimals = ranges[range];
		value /= pow(10, decimals);
		*digits = decimals;
	}
	if (is_ol)
		value = INFINITY;
	if (is_neg)
		value *= -1;

	/*
	 * Implementor's note: "Low battery" conditions are worth a
	 * warning since the reading could be incorrect. Rate limiting
	 * is not needed since the Brymen DMM will stop recording in
	 * that case, so at most the last sample in the session page
	 * could be affected.
	 */
	if (low_batt)
		sr_warn("Recording was taken when battery was low.");

	return value;
}

static int bm52x_rec_prep_feed(uint8_t bfunc, uint8_t bsel, uint8_t bstat,
	struct sr_datafeed_analog *analog1, struct sr_datafeed_analog *analog2,
	double *value1, double *value2, const int **ranges1, const int **ranges2,
	const struct sr_dev_inst *sdi)
{
	struct sr_channel *ch;
	gboolean is_amp, is_deg_f;
	enum sr_mq *mq1, *mq2;
	enum sr_unit *unit1, *unit2;
	enum sr_mqflag *mqf1, *mqf2;
	enum sr_unit unit_c_f;
	const int *r_a_ma;

	/* Prepare general submission on first channel. */
	analog1->data = value1;
	analog1->encoding->unitsize = sizeof(*value1);
	analog1->num_samples = 1;
	ch = g_slist_nth_data(sdi->channels, 0);
	analog1->meaning->channels = g_slist_append(NULL, ch);
	*ranges1 = NULL;
	mq1 = &analog1->meaning->mq;
	mqf1 = &analog1->meaning->mqflags;
	unit1 = &analog1->meaning->unit;

	/* Prepare general submission on second channel. */
	analog2->data = value2;
	analog2->encoding->unitsize = sizeof(*value2);
	analog2->num_samples = 1;
	ch = g_slist_nth_data(sdi->channels, 1);
	analog2->meaning->channels = g_slist_append(NULL, ch);
	*ranges2 = NULL;
	mq2 = &analog2->meaning->mq;
	mqf2 = &analog2->meaning->mqflags;
	unit2 = &analog2->meaning->unit;

	/* Derive main/secondary display functions from bfunc/bsel/bstat. */
	is_amp = bstat & (1u << 5);
	is_deg_f = bstat & (1u << 4);
	switch (bfunc) {
	case 1: /* AC V */
		switch (bsel) {
		case 0: /* AC volt, Hz */
			*ranges1 = bm52x_ranges_volt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_VOLT;
			*ranges2 = bm52x_ranges_freq;
			*mq2 = SR_MQ_FREQUENCY;
			*unit2 = SR_UNIT_HERTZ;
			break;
		case 1: /* Hz, AC volt */
			*ranges1 = bm52x_ranges_freq;
			*mq1 = SR_MQ_FREQUENCY;
			*unit1 = SR_UNIT_HERTZ;
			*ranges2 = bm52x_ranges_volt;
			*mq2 = SR_MQ_VOLTAGE;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_VOLT;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 2: /* DC V */
		switch (bsel) {
		case 0: /* DC V, - */
			*ranges1 = bm52x_ranges_volt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_VOLT;
			break;
		case 1: /* DC V, AC V */
			*ranges1 = bm52x_ranges_volt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_VOLT;
			*ranges2 = bm52x_ranges_volt;
			*mq2 = SR_MQ_VOLTAGE;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_VOLT;
			break;
		case 2: /* DC+AC V, AC V */
			*ranges1 = bm52x_ranges_volt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_VOLT;
			*ranges2 = bm52x_ranges_volt;
			*mq2 = SR_MQ_VOLTAGE;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_VOLT;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 3: /* DC mV */
		switch (bsel) {
		case 0: /* DC mV, - */
			*ranges1 = bm52x_ranges_millivolt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_VOLT;
			break;
		case 1: /* DC mV, AC mV */
			*ranges1 = bm52x_ranges_millivolt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_VOLT;
			*ranges2 = bm52x_ranges_millivolt;
			*mq2 = SR_MQ_VOLTAGE;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_VOLT;
			break;
		case 2: /* DC+AC mV, AC mV */
			*ranges1 = bm52x_ranges_millivolt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_VOLT;
			*ranges2 = bm52x_ranges_millivolt;
			*mq2 = SR_MQ_VOLTAGE;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_VOLT;
			break;
		case 3: /* Hz, - */
			*ranges1 = bm52x_ranges_freq;
			*mq1 = SR_MQ_FREQUENCY;
			*unit1 = SR_UNIT_HERTZ;
			break;
		case 4: /* %, - */
			*ranges1 = bm52x_ranges_duty;
			*mq1 = SR_MQ_DUTY_CYCLE;
			*unit1 = SR_UNIT_PERCENTAGE;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 4: /* AC mV */
		switch (bsel) {
		case 0: /* AC mV, Hz */
			*ranges1 = bm52x_ranges_millivolt;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_VOLT;
			*ranges2 = bm52x_ranges_freq;
			*mq2 = SR_MQ_FREQUENCY;
			*unit2 = SR_UNIT_HERTZ;
			break;
		case 1: /* Hz, AC mV */
			*ranges1 = bm52x_ranges_freq;
			*mq1 = SR_MQ_FREQUENCY;
			*unit1 = SR_UNIT_HERTZ;
			*ranges2 = bm52x_ranges_millivolt;
			*mq2 = SR_MQ_VOLTAGE;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_VOLT;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 5: /* Res/Cond/Cont */
		switch (bsel) {
		case 0: /* Resistance */
			*ranges1 = bm52x_ranges_ohm;
			*mq1 = SR_MQ_RESISTANCE;
			*unit1 = SR_UNIT_OHM;
			break;
		case 1: /* Siemens */
			*ranges1 = bm52x_ranges_cond;
			*mq1 = SR_MQ_CONDUCTANCE;
			*unit1 = SR_UNIT_SIEMENS;
			break;
		case 2: /* Continuity */
			*ranges1 = bm52x_ranges_ohm;
			*mq1 = SR_MQ_CONTINUITY;
			*unit1 = SR_UNIT_OHM;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 6: /* Temperature */
		unit_c_f = is_deg_f ? SR_UNIT_FAHRENHEIT : SR_UNIT_CELSIUS;
		switch (bsel) {
		case 0: /* T1, - */
			*ranges1 = bm52x_ranges_temp;
			*mq1 = SR_MQ_TEMPERATURE;
			*unit1 = unit_c_f;
			break;
		case 1: /* T2, - */
			*ranges1 = bm52x_ranges_temp;
			*mq1 = SR_MQ_TEMPERATURE;
			*unit1 = unit_c_f;
			break;
		case 2: /* T1, T2 */
			*ranges1 = bm52x_ranges_temp;
			*mq1 = SR_MQ_TEMPERATURE;
			*unit1 = unit_c_f;
			*ranges2 = bm52x_ranges_temp;
			*mq2 = SR_MQ_TEMPERATURE;
			*unit2 = unit_c_f;
			break;
		case 3: /* T1-T2, T2 */
			*ranges1 = bm52x_ranges_temp;
			*mq1 = SR_MQ_TEMPERATURE;
			*unit1 = unit_c_f;
			*ranges2 = bm52x_ranges_temp;
			*mq2 = SR_MQ_TEMPERATURE;
			*unit2 = unit_c_f;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 7: /* Cap/Diode */
		switch (bsel) {
		case 0: /* Capacitance, - */
			*ranges1 = bm52x_ranges_cap;
			*mq1 = SR_MQ_CAPACITANCE;
			*unit1 |= SR_UNIT_FARAD;
			break;
		case 1: /* Diode voltage, - */
			*ranges1 = bm52x_ranges_diode;
			*mq1 = SR_MQ_VOLTAGE;
			*mqf1 |= SR_MQFLAG_DC;
			*mqf1 |= SR_MQFLAG_DIODE;
			*unit1 |= SR_UNIT_VOLT;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 8: /* DC A/mA */
		r_a_ma = is_amp ? bm52x_ranges_amp : bm52x_ranges_milliamp;
		switch (bsel) {
		case 0: /* DC A/mA, - */
			*ranges1 = r_a_ma;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_AMPERE;
			break;
		case 1: /* DC A/mA, AC A/mA */
			*ranges1 = r_a_ma;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_AMPERE;
			*ranges2 = r_a_ma;
			*mq2 = SR_MQ_CURRENT;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_AMPERE;
			break;
		case 2: /* DC+AC A/mA, AC A/mA */
			*ranges1 = r_a_ma;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_DC;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_AMPERE;
			*ranges2 = r_a_ma;
			*mq2 = SR_MQ_CURRENT;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_AMPERE;
			break;
		case 3: /* AC A/mA, Hz */
			*ranges1 = r_a_ma;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_AMPERE;
			*ranges2 = bm52x_ranges_freq;
			*mq2 = SR_MQ_FREQUENCY;
			*unit2 = SR_UNIT_HERTZ;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	case 9: /* DC uA */
		switch (bsel) {
		case 0: /* DC uA, - */
			*ranges1 = bm52x_ranges_microamp;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_AMPERE;
			break;
		case 1: /* DC uA, AC uA */
			*ranges1 = bm52x_ranges_microamp;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_DC;
			*unit1 = SR_UNIT_AMPERE;
			*ranges2 = bm52x_ranges_microamp;
			*mq2 = SR_MQ_CURRENT;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_AMPERE;
			break;
		case 2: /* DC+AC uA, AC uA */
			*ranges1 = bm52x_ranges_microamp;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_DC;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_AMPERE;
			*ranges2 = bm52x_ranges_microamp;
			*mq2 = SR_MQ_CURRENT;
			*mqf2 |= SR_MQFLAG_AC;
			*unit2 = SR_UNIT_AMPERE;
			break;
		case 3: /* AC uA, Hz */
			*ranges1 = bm52x_ranges_microamp;
			*mq1 = SR_MQ_CURRENT;
			*mqf1 |= SR_MQFLAG_AC;
			*unit1 = SR_UNIT_AMPERE;
			*ranges2 = bm52x_ranges_freq;
			*mq2 = SR_MQ_FREQUENCY;
			*unit2 = SR_UNIT_HERTZ;
			break;
		default:
			return SR_ERR_DATA;
		}
		break;
	default:
		return SR_ERR_DATA;
	}

	return SR_OK;
}

/** Traverse one recorded session page, optionally feed session bus. */
static int bm52x_rec_read_page_int(const struct sr_dev_inst *sdi,
	struct brymen_bm52x_state *state, struct sr_serial_dev_inst *serial,
	gboolean skip)
{
	uint8_t bfunc, bsel, bstat;
	uint8_t ival;
	gboolean has_sec_disp;
	size_t page_len, meas_len, meas_count;
	uint32_t meas_data;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog1, analog2;
	struct sr_analog_encoding encoding1, encoding2;
	struct sr_analog_meaning meaning1, meaning2;
	struct sr_analog_spec spec1, spec2;
	int digits, ret;
	double values[2];
	const int *ranges1, *ranges2;
	enum sr_configkey key;
	uint64_t num;

	sr_dbg("progress: %s, %s", __func__, skip ? "skip" : "feed");

	/* Get the header information of the session page (raw). */
	if (bm52x_rec_get_u8(serial, state) != 0xee) /* "DLE" */
		return SR_ERR_DATA;
	if (bm52x_rec_get_u8(serial, state) != 0xa0) /* "STX" */
		return SR_ERR_DATA;
	(void)bm52x_rec_get_u24(serial, state); /* prev page addr */
	(void)bm52x_rec_get_u24(serial, state); /* next page addr */
	bfunc = bm52x_rec_get_u8(serial, state); /* meter function */
	bsel = bm52x_rec_get_u8(serial, state); /* fun selection */
	bstat = bm52x_rec_get_u8(serial, state); /* status */
	page_len = bm52x_rec_get_u24(serial, state); /* page length */
	sr_dbg("page head: func/sel/state %02x/%02x/%02x, len %zu",
		bfunc, bsel, bstat, page_len);

	/* Interpret the header information of the session page. */
	ival = bstat & 0x0f;
	has_sec_disp = bstat & (1u << 7);
	meas_len = (has_sec_disp ? 2 : 1) * 3;
	if (page_len % meas_len)
		return SR_ERR_DATA;
	meas_count = page_len / meas_len;
	sr_dbg("page head: ival %u, %s, samples %zu",
		ival, has_sec_disp ? "dual" : "main", meas_count);

	/* Prepare feed to the sigrok session. Send rate/interval. */
	sr_analog_init(&analog1, &encoding1, &meaning1, &spec1, 0);
	sr_analog_init(&analog2, &encoding2, &meaning2, &spec2, 0);
	ret = bm52x_rec_prep_feed(bfunc, bsel, bstat,
		&analog1, &analog2, &values[0], &values[1],
		&ranges1, &ranges2, sdi);
	if (ret != SR_OK)
		return SR_ERR_DATA;
	if (!skip) {
		memset(&packet, 0, sizeof(packet));
		packet.type = SR_DF_ANALOG;

		if (bm52x_rec_ivals[ival].freq_rate) {
			sr_dbg("rate: %u", bm52x_rec_ivals[ival].freq_rate);
			key = SR_CONF_SAMPLERATE;
			num = bm52x_rec_ivals[ival].freq_rate;
			(void)sr_session_send_meta(sdi,
				key, g_variant_new_uint64(num));
		}
		if (bm52x_rec_ivals[ival].ival_secs) {
			sr_dbg("ival: %u", bm52x_rec_ivals[ival].ival_secs);
			key = SR_CONF_SAMPLE_INTERVAL;
			num = bm52x_rec_ivals[ival].ival_secs * 1000; /* in ms */
			(void)sr_session_send_meta(sdi,
				key, g_variant_new_uint64(num));
		}
	}

	/*
	 * Implementor's note:
	 * Software limits require devc access, which is an internal
	 * detail of the serial-dmm driver, which this bm52x parser
	 * is not aware of. So we always provide the complete set of
	 * recorded samples. Should be acceptable. Duplicating limit
	 * support in local config get/set is considered undesirable.
	 */
	while (meas_count--) {
		meas_data = bm52x_rec_get_u24(serial, state);
		values[0] = bm52x_rec_get_value(meas_data, ranges1, &digits);
		if (!skip) {
			analog1.encoding->digits  = digits;
			analog1.spec->spec_digits = digits;
			packet.payload = &analog1;
			ret = sr_session_send(sdi, &packet);
			if (ret != SR_OK)
				return ret;
		}

		if (!has_sec_disp)
			continue;
		meas_data = bm52x_rec_get_u24(serial, state);
		values[1] = bm52x_rec_get_value(meas_data, ranges2, &digits);
		if (!skip) {
			analog2.encoding->digits  = digits;
			analog2.spec->spec_digits = digits;
			packet.payload = &analog2;
			ret = sr_session_send(sdi, &packet);
			if (ret != SR_OK)
				return ret;
		}
	}

	/* Check termination of the session page. */
	if (bm52x_rec_get_u8(serial, state) != 0xee) /* "DLE" */
		return SR_ERR_DATA;
	if (bm52x_rec_get_u8(serial, state) != 0xc0) /* "ETX" */
		return SR_ERR_DATA;

	return SR_OK;
}

/** Skip one recorded session page. */
static int bm52x_rec_skip_page(const struct sr_dev_inst *sdi,
	struct brymen_bm52x_state *state, struct sr_serial_dev_inst *serial)
{
	return bm52x_rec_read_page_int(sdi, state, serial, TRUE);
}

/** Forward one recorded session page. */
static int bm52x_rec_read_page(const struct sr_dev_inst *sdi,
	struct brymen_bm52x_state *state, struct sr_serial_dev_inst *serial)
{
	return bm52x_rec_read_page_int(sdi, state, serial, FALSE);
}

SR_PRIV void *brymen_bm52x_state_init(void)
{
	return g_malloc0(sizeof(struct brymen_bm52x_state));
}

SR_PRIV void brymen_bm52x_state_free(void *state)
{
	g_free(state);
}

SR_PRIV int brymen_bm52x_config_get(void *st, uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct brymen_bm52x_state *state;
	char text[20];

	state = st;

	if (!sdi)
		return SR_ERR_NA;
	(void)cg;

	switch (key) {
	case SR_CONF_DATA_SOURCE:
		if (!state)
			return SR_ERR_ARG;
		if (state->sess_idx == 0)
			snprintf(text, sizeof(text), "Live");
		else
			snprintf(text, sizeof(text), "Rec-%zu", state->sess_idx);
		*data = g_variant_new_string(text);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

SR_PRIV int brymen_bm52x_config_set(void *st, uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct brymen_bm52x_state *state;
	const char *s;
	int ret, nr;

	state = st;

	if (!sdi)
		return SR_ERR_NA;
	(void)cg;

	switch (key) {
	case SR_CONF_DATA_SOURCE:
		s = g_variant_get_string(data, NULL);
		if (!s || !*s)
			return SR_ERR_ARG;
		if (strcasecmp(s, "Live") == 0) {
			state->sess_idx = 0;
			return SR_OK;
		}
		if (strncasecmp(s, "Rec-", strlen("Rec-")) != 0)
			return SR_ERR_ARG;
		s += strlen("Rec-");
		ret = sr_atoi(s, &nr);
		if (ret != SR_OK || nr <= 0 || nr > 999)
			return SR_ERR_ARG;
		state->sess_idx = nr;
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

SR_PRIV int brymen_bm52x_config_list(void *st, uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct brymen_bm52x_state *state;
	struct sr_serial_dev_inst *serial;
	int ret;
	size_t count, idx;
	GVariantBuilder gvb;
	char name[20];

	/*
	 * Have common keys handled by caller's common code.
	 * ERR N/A results in the caller's logic handling the request.
	 * Only handle strictly local properties here in this code path.
	 */
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		/* Scan options. Common property. */
		return SR_ERR_NA;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi)
			/* Driver options. Common property. */
			return SR_ERR_NA;
		if (cg)
			/* Channel group's devopts. Common error path. */
			return SR_ERR_NA;
		/* List meter's local device options. Overrides common data. */
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		return SR_OK;
	case SR_CONF_DATA_SOURCE:
		state = st;
		if (!sdi)
			return SR_ERR_ARG;
		serial = sdi->conn;
		ret = bm52x_rec_get_count(state, serial);
		if (ret < 0)
			return ret;
		count = ret;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (idx = 0; idx <= count; idx++) {
			if (idx == 0)
				snprintf(name, sizeof(name), "Live");
			else
				snprintf(name, sizeof(name), "Rec-%zu", idx);
			g_variant_builder_add(&gvb, "s", name);
		}
		*data = g_variant_builder_end(&gvb);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

/**
 * BM520s specific receive routine for recorded measurements.
 *
 * @param[in] fd File descriptor.
 * @param[in] revents Mask of pending events.
 * @param[in] cb_data Call back data (receive handler state).
 *
 * @return TRUE when the routine needs to get re-invoked, FALSE when the
 *   routine is done and no further invocations are required.
 *
 * @private
 *
 * It's an implementation detail that a single invocation will carry out
 * all the work that is involved in reading back recorded measurements.
 */
static int bm52x_rec_receive_data(int fd, int revents, void *cb_data)
{
	struct brymen_bm52x_state *state;
	const struct sr_dev_inst *sdi;
	struct sr_dev_inst *sdi_rw;
	struct sr_serial_dev_inst *serial;
	int ret;
	size_t count, idx;

	(void)fd;
	(void)revents;
	state = cb_data;

	sdi = state->sdi;
	serial = sdi->conn;

	ret = bm52x_rec_get_count(state, serial);
	if (ret < 0)
		return FALSE;
	count = ret;

	/* Un-const 'sdi' since sr_dev_acquisition_stop() needs that. */
	sdi_rw = (struct sr_dev_inst *)sdi;

	/*
	 * Immediate (silent, zero data) stop for non-existent sessions.
	 * Early exit is an arbitrary implementation detail, in theory
	 * the loop below would transparently handle the situation when
	 * users request non-existing session pages.
	 */
	if (state->sess_idx > count) {
		sr_dev_acquisition_stop(sdi_rw);
		return FALSE;
	}

	/* Iterate all session pages, forward the one of interest. */
	for (idx = 1; idx <= count; idx++) {
		if (idx == state->sess_idx)
			ret = bm52x_rec_read_page(sdi, state, serial);
		else
			ret = bm52x_rec_skip_page(sdi, state, serial);
		if (ret != SR_OK)
			break;
	}

	sr_dev_acquisition_stop(sdi_rw);
	return FALSE;
}

/**
 * BM520s specific acquisition start callback.
 *
 * @param[in] st DMM parser state.
 * @param[in] sdi Device instance.
 * @param[out] cb Receive callback for the acquisition.
 * @param[out] cb_data Callback data for receive callback.
 *
 * @returns SR_OK upon success.
 * @returns SR_ERR* upon failure.
 *
 * @private
 *
 * The BM520s protocol parser uses common logic and the packet parser
 * for live acquisition, but runs a different set of requests and a
 * different response layout interpretation for recorded measurements.
 */
SR_PRIV int brymen_bm52x_acquire_start(void *st, const struct sr_dev_inst *sdi,
	sr_receive_data_callback *cb, void **cb_data)
{
	struct brymen_bm52x_state *state;

	if (!sdi || !st)
		return SR_ERR_ARG;
	state = st;

	/* Read live measurements. No local override required. */
	if (state->sess_idx == 0)
		return SR_OK;

	/* Arrange to read back recorded session. */
	sr_dbg("session page requested: %zu", state->sess_idx);
	state->sdi = sdi;
	*cb = bm52x_rec_receive_data;
	*cb_data = state;
	return SR_OK;
}
