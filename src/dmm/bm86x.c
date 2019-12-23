/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
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

/**
 * @file
 *
 * Brymen BM86x serial protocol parser. The USB protocol (for the cable)
 * and the packet description (for the meter) were retrieved from:
 * http://brymen.com/product-html/Download2.html
 * http://brymen.com/product-html/PD02BM860s_protocolDL.html
 * http://brymen.com/product-html/images/DownloadList/ProtocolList/BM860-BM860s_List/BM860-BM860s-500000-count-dual-display-DMMs-protocol.pdf
 */

#include <config.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <string.h>

#define LOG_PREFIX "brymen-bm86x"

#ifdef HAVE_SERIAL_COMM
SR_PRIV int sr_brymen_bm86x_packet_request(struct sr_serial_dev_inst *serial)
{
	static const uint8_t request[] = { 0x00, 0x00, 0x86, 0x66, };

	serial_write_nonblocking(serial, request, sizeof(request));

	return SR_OK;
}
#endif

SR_PRIV gboolean sr_brymen_bm86x_packet_valid(const uint8_t *buf)
{
	/*
	 * "Model ID3" (3rd HID report, byte 3) is the only documented
	 * fixed value, and must be 0x86. All other positions either depend
	 * on the meter's function, or the measurement's value, or are not
	 * documented by the vendor (are marked as "don't care", no fixed
	 * values are listed). There is nothing else we can check reliably.
	 */
	if (buf[19] != 0x86)
		return FALSE;

	return TRUE;
}

/*
 * Data bytes in the DMM packet encode LCD segments in an unusual order
 * (bgcdafe) and in an unusual position (bits 7:1 within the byte). The
 * decimal point (bit 0) for one digit resides in the _next_ digit's byte.
 *
 * These routines convert LCD segments to characters, and a section of the
 * DMM packet (which corresponds to the primary or secondary display) to
 * the text representation of the measurement's value, before regular text
 * to number conversion is applied. The first byte of the passed in block
 * contains indicators, the value's digits start at the second byte.
 */

static char brymen_bm86x_parse_digit(uint8_t b)
{
	switch (b >> 1) {
	/* Sign. */
	case 0x20: return '-';
	/* Decimal digits. */
	case 0x5f: return '0';
	case 0x50: return '1';
	case 0x6d: return '2';
	case 0x7c: return '3';
	case 0x72: return '4';
	case 0x3e: return '5';
	case 0x3f: return '6';
	case 0x54: return '7';
	case 0x7f: return '8';
	case 0x7e: return '9';
	/* Temperature units. */
	case 0x0f: return 'C';
	case 0x27: return 'F';
	/* OL condition, and diode mode. */
	case 0x0b: return 'L';
	case 0x79: return 'd';
	case 0x10: return 'i';
	case 0x39: return 'o';
	/* Blank digit. */
	case 0x00: return '\0';
	/* Invalid or unknown segment combination. */
	default:
		sr_warn("Unknown encoding for digit: 0x%02x.", b);
		return '\0';
	}
}

static int brymen_bm86x_parse_digits(const uint8_t *pkt, size_t pktlen,
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
		if (pos && pos < 5 && (byte & 0x01)) {
			*txtptr++ = '.';
			if (digits)
				*digits = 0;
		}
		txtchar = brymen_bm86x_parse_digit(byte);
		if (pos == 5 && (txtchar == 'C' || txtchar == 'F')) {
			if (temp_unit)
				*temp_unit = txtchar;
		} else if (txtchar) {
			*txtptr++ = txtchar;
			if (digits)
				(*digits)++;
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
static void brymen_bm86x_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, size_t ch_idx)
{
	char txtbuf[16], temp_unit;
	int ret, digits, is_diode, over_limit, scale;
	uint8_t ind1, ind15;

	temp_unit = '\0';
	if (ch_idx == 0) {
		/*
		 * Main display. Note that _some_ of the second display's
		 * indicators are involved in the inspection of the _first_
		 * display's measurement value. So we have to get the
		 * second display's text buffer here, too.
		 */
		(void)brymen_bm86x_parse_digits(&buf[9], 4, txtbuf,
			NULL, NULL, NULL, 0);
		is_diode = strcmp(txtbuf, "diod") == 0;
		ret = brymen_bm86x_parse_digits(&buf[2], 6, txtbuf,
			floatval, &temp_unit, &digits, 0x80);
		over_limit = strstr(txtbuf, "0L") || strstr(txtbuf, "0.L");
		if (ret != SR_OK && !over_limit)
			return;

		/* SI unit. */
		if (buf[8] & 0x01) {
			analog->meaning->mq = SR_MQ_VOLTAGE;
			analog->meaning->unit = SR_UNIT_VOLT;
			if (is_diode) {
				analog->meaning->mqflags |= SR_MQFLAG_DIODE;
				analog->meaning->mqflags |= SR_MQFLAG_DC;
			}
		} else if (buf[14] & 0x80) {
			analog->meaning->mq = SR_MQ_CURRENT;
			analog->meaning->unit = SR_UNIT_AMPERE;
		} else if (buf[14] & 0x20) {
			analog->meaning->mq = SR_MQ_CAPACITANCE;
			analog->meaning->unit = SR_UNIT_FARAD;
		} else if (buf[14] & 0x10) {
			analog->meaning->mq = SR_MQ_CONDUCTANCE;
			analog->meaning->unit = SR_UNIT_SIEMENS;
		} else if (buf[15] & 0x01) {
			analog->meaning->mq = SR_MQ_FREQUENCY;
			analog->meaning->unit = SR_UNIT_HERTZ;
		} else if (buf[10] & 0x01) {
			analog->meaning->mq = SR_MQ_CONTINUITY;
			analog->meaning->unit = SR_UNIT_OHM;
		} else if (buf[15] & 0x10) {
			analog->meaning->mq = SR_MQ_RESISTANCE;
			analog->meaning->unit = SR_UNIT_OHM;
		} else if (buf[15] & 0x02) {
			analog->meaning->mq = SR_MQ_POWER;
			analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		} else if (buf[15] & 0x80) {
			analog->meaning->mq = SR_MQ_DUTY_CYCLE;
			analog->meaning->unit = SR_UNIT_PERCENTAGE;
		} else if ((buf[2] & 0x0a) && temp_unit) {
			analog->meaning->mq = SR_MQ_TEMPERATURE;
			if (temp_unit == 'F')
				analog->meaning->unit = SR_UNIT_FAHRENHEIT;
			else
				analog->meaning->unit = SR_UNIT_CELSIUS;
		}

		/*
		 * Remove the MIN/MAX/AVG indicators when all of them
		 * are shown at the same time.
		 */
		ind1 = buf[1];
		if ((ind1 & 0xe0) == 0xe0)
			ind1 &= ~0xe0;

		/* AC/DC/Auto flags. */
		if (buf[1] & 0x10)
			analog->meaning->mqflags |= SR_MQFLAG_DC;
		if (buf[2] & 0x01)
			analog->meaning->mqflags |= SR_MQFLAG_AC;
		if (buf[1] & 0x01)
			analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
		if (buf[1] & 0x08)
			analog->meaning->mqflags |= SR_MQFLAG_HOLD;
		if (ind1 & 0x20)
			analog->meaning->mqflags |= SR_MQFLAG_MAX;
		if (ind1 & 0x40)
			analog->meaning->mqflags |= SR_MQFLAG_MIN;
		if (ind1 & 0x80)
			analog->meaning->mqflags |= SR_MQFLAG_AVG;
		if (buf[3] & 0x01)
			analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;

		/*
		 * Remove the "dBm" indication's "m" indicator before the
		 * SI unit's prefixes get inspected. To avoid an interaction
		 * with the "milli" prefix.
		 */
		ind15 = buf[15];
		if (ind15 & 0x02)
			ind15 &= ~0x04;

		/* SI prefix. */
		scale = 0;
		if (buf[14] & 0x40) /* n */
			scale = -9;
		if (buf[15] & 0x08) /* u */
			scale = -6;
		if (ind15 & 0x04) /* m */
			scale = -3;
		if (buf[15] & 0x40) /* k */
			scale = +3;
		if (buf[15] & 0x20) /* M */
			scale = +6;
		if (scale) {
			*floatval *= pow(10, scale);
			digits += -scale;
		}

		if (over_limit)
			*floatval = INFINITY;

		analog->encoding->digits  = digits;
		analog->spec->spec_digits = digits;
	} else if (ch_idx == 1) {
		/*
		 * Secondary display. Also inspect _some_ primary display
		 * data, to determine the secondary display's validity.
		 */
		(void)brymen_bm86x_parse_digits(&buf[2], 6, txtbuf,
			NULL, &temp_unit, NULL, 0x80);
		ret = brymen_bm86x_parse_digits(&buf[9], 4, txtbuf,
			floatval, NULL, &digits, 0x10);

		/* SI unit. */
		if (buf[14] & 0x08) {
			analog->meaning->mq = SR_MQ_VOLTAGE;
			analog->meaning->unit = SR_UNIT_VOLT;
		} else if (buf[9] & 0x04) {
			analog->meaning->mq = SR_MQ_CURRENT;
			analog->meaning->unit = SR_UNIT_AMPERE;
		} else if (buf[9] & 0x08) {
			analog->meaning->mq = SR_MQ_CURRENT;
			analog->meaning->unit = SR_UNIT_PERCENTAGE;
		} else if (buf[14] & 0x04) {
			analog->meaning->mq = SR_MQ_FREQUENCY;
			analog->meaning->unit = SR_UNIT_HERTZ;
		} else if ((buf[9] & 0x40) && temp_unit) {
			analog->meaning->mq = SR_MQ_TEMPERATURE;
			if (temp_unit == 'F')
				analog->meaning->unit = SR_UNIT_FAHRENHEIT;
			else
				analog->meaning->unit = SR_UNIT_CELSIUS;
		}

		/* AC flag. */
		if (buf[9] & 0x20)
			analog->meaning->mqflags |= SR_MQFLAG_AC;

		/* SI prefix. */
		scale = 0;
		if (buf[ 9] & 0x01) /* u */
			scale = -6;
		if (buf[ 9] & 0x02) /* m */
			scale = -3;
		if (buf[14] & 0x02) /* k */
			scale = +3;
		if (buf[14] & 0x01) /* M */
			scale = +6;
		if (scale) {
			*floatval *= pow(10, scale);
			digits += -scale;
		}

		analog->encoding->digits  = digits;
		analog->spec->spec_digits = digits;
	}

	if (buf[9] & 0x80)
		sr_warn("Battery is low.");
}

SR_PRIV int sr_brymen_bm86x_parse(const uint8_t *buf, float *val,
	struct sr_datafeed_analog *analog, void *info)
{
	struct brymen_bm86x_info *info_local;
	size_t ch_idx;

	/*
	 * Scan a portion of the received DMM packet which corresponds
	 * to the caller's specified display. Then prepare to scan a
	 * different portion of the packet for another display. This
	 * routine gets called multiple times for one received packet.
	 */
	info_local = info;
	ch_idx = info_local->ch_idx;
	brymen_bm86x_parse(buf, val, analog, ch_idx);
	info_local->ch_idx = ch_idx + 1;

	return SR_OK;
}
