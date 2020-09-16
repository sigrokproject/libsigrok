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
 */

/*
 * TODO
 * - This DMM packet parser exclusively supports live readings (vendor
 *   documentation refers to it as "real-time download" aka RTD). A HID
 *   report is sent which results in three HID reports in response which
 *   carry 24 bytes with LCD indicator bitfields (and a few literals to
 *   synchronize to the byte stream). Reading back previous recordings
 *   ("memory data sets" in the vendor documentation) involve different
 *   types of requests, and several of them, and result in a different
 *   number of response reports while their interpretation differs, too,
 *   of course. None of this fits the serial-dmm approach, and needs to
 *   get addressed later.
 *   - Configurable sample rate, range 20/s rate up to 600s period.
 *   - Multiple sessions, one function per session, up to 999 "session
 *     pages" (recordings with their sequence of measurement values).
 *   - Up to 87000 (single display) or 43500 (dual display) measurements
 *     total on BM525s.
 *   - Request 0x00, 0x00, 0x52, 0x88 to request the HEAD of recordings.
 *     Request 0x00, 0x00, 0x52, 0x89 to request the NEXT memory chunk.
 *     Request 0x00, 0x00, 0x52, 0x8a to re-request the CURR memory chunk
 *     (repetition when transmission failed detectably?).
 *   - All these HID report requests result in four HID responses which
 *     carry 32 bytes (24 bytes of payload data, and a checksum) where
 *     application's fields can cross the boundary of HID reports and
 *     even response chunks.
 * - Some of the meter's functions and indications cannot get expressed
 *   by means of sigrok MQ and flags terms. Some indicator's meaning is
 *   unknown or uncertain, and thus their state is not evaluated.
 *   - MAX-MIN, the span between extreme values, referred to as Vp-p.
 *   - AVG is not available in BM525s and BM521s.
 *   - LoZ, eliminating ghost voltages.
 *   - LPF, low pass filter.
 *   - dBm is a BM829s feature only, not available in BM525s.
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
 */

#include <config.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <math.h>
#include <string.h>

#define LOG_PREFIX "brymen-bm52x"

#ifdef HAVE_SERIAL_COMM
SR_PRIV int sr_brymen_bm52x_packet_request(struct sr_serial_dev_inst *serial)
{
	static const uint8_t request[] = { 0x00, 0x00, 0x52, 0x66, };

	serial_write_nonblocking(serial, request, sizeof(request));

	return SR_OK;
}
#endif

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
