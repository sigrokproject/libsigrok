/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * KERN scale protocol parser.
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "kern"

static int get_buflen(const uint8_t *buf)
{
	/* Find out whether it's a 14-byte or 15-byte packet. */
	if (buf[12] == '\r' && buf[13] == '\n')
		return 14;
	else if (buf[13] == '\r' && buf[14] == '\n')
		return 15;
	else
		return -1;
}

static int parse_value(const uint8_t *buf, float *result,
		int *digits, const struct kern_info *info)
{
	char *strval, *ptrdot;
	float floatval;
	int s2, len;

	s2 = (info->buflen == 14) ? 11 : 12;
	len = (info->buflen == 14) ? 8 : 9;

	if (buf[s2] == 'E') {
		/* Display: "o-Err" or "u-Err", but protocol only has 'E'. */
		sr_spew("Over/under limit.");
		*result = INFINITY;
		return SR_OK;
	}

	strval = g_strndup((const char *)buf, len);
	floatval = g_ascii_strtod(strval, NULL);
	ptrdot = strchr(strval, '.');
	if (ptrdot)
		*digits = len - (ptrdot - strval + 1);
	g_free(strval);
	*result = floatval;

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct kern_info *info)
{
	int u1, u2, s2;

	u1 = (info->buflen == 14) ? 8 : 9;
	u2 = (info->buflen == 14) ? 9 : 10;
	s2 = (info->buflen == 14) ? 11 : 12;

	/* Bytes U1, U2: Unit */
	info->is_gram         = (buf[u1] == ' ' && buf[u2] == 'G');
	info->is_carat        = (buf[u1] == 'C' && buf[u2] == 'T');
	info->is_ounce        = (buf[u1] == 'O' && buf[u2] == 'Z');
	info->is_pound        = (buf[u1] == 'L' && buf[u2] == 'B');
	info->is_troy_ounce   = (buf[u1] == 'O' && buf[u2] == 'T');
	info->is_pennyweight  = (buf[u1] == 'D' && buf[u2] == 'W');
	info->is_grain        = (buf[u1] == 'G' && buf[u2] == 'R');
	info->is_tael         = (buf[u1] == 'T' && buf[u2] == 'L');
	info->is_momme        = (buf[u1] == 'M' && buf[u2] == 'O');
	info->is_tola         = (buf[u1] == 't' && buf[u2] == 'o');
	info->is_percentage   = (buf[u1] == ' ' && buf[u2] == '%');
	info->is_piece        = (buf[u1] == 'P' && buf[u2] == 'C');

	/*
	 * Note: The display can show 3 different variants for Tael:
	 * "Hong Kong", "Singapore, Malaysia", and "Taiwan". However,
	 * in the protocol only one Tael value ('T', 'L') is used, thus
	 * we cannot distinguish between them.
	 */

	/* Byte S1: Result / data type (currently unused) */

	/* Byte S2: Status of the data */
	info->is_unstable     = (buf[s2] == 'U');
	info->is_stable       = (buf[s2] == 'S');
	info->is_error        = (buf[s2] == 'E');
	/* Space: no special status. */

	/* Byte CR: Always '\r' (carriage return, 0x0d, 13) */

	/* Byte LF: Always '\n' (newline, 0x0a, 10) */
}

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
			 const struct kern_info *info)
{
	(void)floatval;

	/* Measured quantity: mass. */
	analog->meaning->mq = SR_MQ_MASS;

	/* Unit */
	if (info->is_gram)
		analog->meaning->unit = SR_UNIT_GRAM;
	if (info->is_carat)
		analog->meaning->unit = SR_UNIT_CARAT;
	if (info->is_ounce)
		analog->meaning->unit = SR_UNIT_OUNCE;
	if (info->is_pound)
		analog->meaning->unit = SR_UNIT_POUND;
	if (info->is_troy_ounce)
		analog->meaning->unit = SR_UNIT_TROY_OUNCE;
	if (info->is_pennyweight)
		analog->meaning->unit = SR_UNIT_PENNYWEIGHT;
	if (info->is_grain)
		analog->meaning->unit = SR_UNIT_GRAIN;
	if (info->is_tael)
		analog->meaning->unit = SR_UNIT_TAEL;
	if (info->is_momme)
		analog->meaning->unit = SR_UNIT_MOMME;
	if (info->is_tola)
		analog->meaning->unit = SR_UNIT_TOLA;
	if (info->is_percentage)
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	if (info->is_piece)
		analog->meaning->unit = SR_UNIT_PIECE;

	/* Measurement related flags */
	if (info->is_unstable)
		analog->meaning->mqflags |= SR_MQFLAG_UNSTABLE;
}

SR_PRIV gboolean sr_kern_packet_valid(const uint8_t *buf)
{
	int buflen, s1, s2, cr, lf;

	if ((buflen = get_buflen(buf)) < 0)
		return FALSE;

	s1 = (buflen == 14) ? 10 : 11;
	s2 = (buflen == 14) ? 11 : 12;
	cr = (buflen == 14) ? 12 : 13;
	lf = (buflen == 14) ? 13 : 14;

	/* Byte 0: Sign (must be '+' or '-' or ' '). */
	if (buf[0] != '+' && buf[0] != '-' && buf[0] != ' ')
		return FALSE;

	/* Byte S1: Must be 'L' or 'G' or 'H' or ' '. */
	if (buf[s1] != 'L' && buf[s1] != 'G' && buf[s1] != 'H' && buf[s1] != ' ')
		return FALSE;

	/* Byte S2: Must be 'U' or 'S' or 'E' or ' '. */
	if (buf[s2] != 'U' && buf[s2] != 'S' && buf[s2] != 'E' && buf[s2] != ' ')
		return FALSE;

	/* Byte CR: Always '\r' (carriage return, 0x0d, 13) */
	/* Byte LF: Always '\n' (newline, 0x0a, 10) */
	if (buf[cr] != '\r' || buf[lf] != '\n')
		return FALSE;

	return TRUE;
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will contain the
 *                 result value upon parsing success. Must not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct kern_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_kern_parse(const uint8_t *buf, float *floatval,
			  struct sr_datafeed_analog *analog, void *info)
{
	int ret, digits = 0;
	struct kern_info *info_local;

	info_local = (struct kern_info *)info;

	info_local->buflen = get_buflen(buf);

	if ((ret = parse_value(buf, floatval, &digits, info_local)) != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	analog->encoding->digits = digits;
	analog->spec->spec_digits = digits;

	parse_flags(buf, info_local);
	handle_flags(analog, floatval, info_local);

	return SR_OK;
}
