/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012-2013 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2018 Matthias Schulz <matthschulz@arcor.de>
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
 * Voltcraft 13-bytes ASCII protocol parser.
 *
 * Bytes 1-3 measuring mode, byte 4 '-' for negative,
 * bytes 5-9 value, bytes 10-11 unit, bytes 12-13 CRLF 0d 0a.
 */

#include <config.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "vc96"

/** Parse value from buf, byte 3-8. */
static int parse_value(const uint8_t *buf, struct vc96_info *info,
			float *result, int *exponent)
{
	int i, is_ol, cnt, dot_pos;
	char valstr[8 + 1];

	(void)info;

	/* Strip all spaces from bytes 3-8. */
	memset(&valstr, 0, 6 + 1);
	for (i = 0, cnt = 0; i < 6; i++) {
		if (buf[3 + i] != ' ')
			valstr[cnt++] = buf[3 + i];
	}

	/* Bytes 5-7: Over limit (various forms) */
	is_ol = 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, ".OL")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "O.L")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "OL.")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "OL")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "-.OL")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "-O.L")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "-OL.")) ? 1 : 0;
	is_ol += (!g_ascii_strcasecmp((const char *)&valstr, "-OL")) ? 1 : 0;
	if (is_ol != 0) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	}

	/* Bytes 3-10: Sign, value (up to 5 digits) and decimal point */
	sr_atof_ascii((const char *)&valstr, result);

	dot_pos = strcspn(valstr, ".");
	if (dot_pos < cnt)
		*exponent = -(cnt - dot_pos - 1);
	else
		*exponent = 0;

	sr_spew("The display value is %f.", *result);

	return SR_OK;
}

static void parse_flags(const char *buf, struct vc96_info *info)
{
	int i, cnt;
	char unit[4 + 1];
	const char *u;

	/* Bytes 0-1: Measurement mode AC, DC */
	info->is_ac = !strncmp(buf, "AC", 2);
	info->is_dc = !strncmp(buf, "DC", 2);

	/* Bytes 0-2: Measurement mode DIO, OHM */
	info->is_ohm = !strncmp(buf, "OHM", 3);
	info->is_diode = !strncmp(buf, "DIO", 3);
	info->is_hfe = !strncmp(buf, "hfe", 3);

	/* Bytes 3-8: See parse_value(). */

	/* Strip all spaces from bytes 9-10. */
	memset(&unit, 0, 2 + 1);
	for (i = 0, cnt = 0; i < 2; i++) {
		if (buf[9 + i] != ' ')
			unit[cnt++] = buf[9 + i];
	}
	sr_spew("Bytes 9..10 without spaces \"%.4s\".", unit);

	/* Bytes 9-10: Unit */
	u = (const char *)&unit;
	if (!g_ascii_strcasecmp(u, "A"))
		info->is_ampere = TRUE;
	else if (!g_ascii_strcasecmp(u, "mA"))
		info->is_milli = info->is_ampere = TRUE;
	else if (!g_ascii_strcasecmp(u, "uA"))
		info->is_micro = info->is_ampere = TRUE;
	else if (!g_ascii_strcasecmp(u, "V"))
		info->is_volt = TRUE;
	else if (!g_ascii_strcasecmp(u, "mV"))
		info->is_milli = info->is_volt = TRUE;
	else if (!g_ascii_strcasecmp(u, "K"))
		info->is_kilo = TRUE;
	else if (!g_ascii_strcasecmp(u, "M"))
		info->is_mega = TRUE;
	else if (!g_ascii_strcasecmp(u, ""))
		info->is_unitless = TRUE;

	/* Bytes 0-2: Measurement mode, except AC/DC */
	info->is_resistance = !strncmp(buf, "OHM", 3) ||
		(!strncmp(buf, "  ", 3) && info->is_ohm);
	info->is_diode = !strncmp(buf, "DIO", 3) ||
		(!strncmp(buf, "  ", 3) && info->is_volt && info->is_milli);
	info->is_hfe = !strncmp(buf, "hfe", 3) ||
		(!strncmp(buf, "  ", 3) && !info->is_ampere && !info->is_volt &&
		!info->is_resistance && !info->is_diode);

	/*
	 * Note:
	 * - Protocol doesn't distinguish "resistance" from "beep" mode.
	 */

	/* Byte 12: Always '\r' (carriage return, 0x0d, 12) */
	/* Byte 13: Always '\n' (carriage return, 0x0a, 13) */
}

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
			 int *exponent, const struct vc96_info *info)
{
	int factor;

	(void)exponent;

	/* Factors */
	factor = 0;
	if (info->is_micro)
		factor -= 6;
	if (info->is_milli)
		factor -= 3;
	if (info->is_kilo)
		factor += 3;
	if (info->is_mega)
		factor += 6;
	*floatval *= powf(10, factor);

	/* Measurement modes */
	if (info->is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_ampere) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (info->is_ohm) {
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (info->is_diode) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_hfe) {
		analog->meaning->mq = SR_MQ_GAIN;
		analog->meaning->unit = SR_UNIT_UNITLESS;
	}

	/* Measurement related flags */
	if (info->is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	if (info->is_diode)
		analog->meaning->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
}

static gboolean flags_valid(const struct vc96_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count = 0;
	count += (info->is_micro) ? 1 : 0;
	count += (info->is_milli) ? 1 : 0;
	count += (info->is_kilo) ? 1 : 0;
	count += (info->is_mega) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count = 0;
	count += (info->is_ac) ? 1 : 0;
	count += (info->is_dc) ? 1 : 0;
	count += (info->is_resistance) ? 1 : 0;
	count += (info->is_diode) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_dbg("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	return TRUE;
}

SR_PRIV gboolean sr_vc96_packet_valid(const uint8_t *buf)
{
	struct vc96_info info;

	memset(&info, 0x00, sizeof(struct vc96_info));
	parse_flags((const char *)buf, &info);

	if (!flags_valid(&info))
		return FALSE;

	if ((buf[11] != '\r') || (buf[12] != '\n'))
		return FALSE;

	return TRUE;
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will be modified
 *                 in-place depending on the protocol packet. Must not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct vc96_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_vc96_parse(const uint8_t *buf, float *floatval,
			struct sr_datafeed_analog *analog, void *info)
{
	int ret, exponent = 0;
	struct vc96_info *info_local;

	info_local = info;

	/* Don't print byte 12 + 13. Those contain the CR LF. */
	sr_dbg("DMM packet: \"%.11s\".", buf);

	memset(info_local, 0x00, sizeof(struct vc96_info));

	if ((ret = parse_value(buf, info_local, floatval, &exponent)) < 0) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	parse_flags((const char *)buf, info_local);
	handle_flags(analog, floatval, &exponent, info_local);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}
