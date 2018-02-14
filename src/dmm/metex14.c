/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012-2013 Uwe Hermann <uwe@hermann-uwe.de>
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

/**
 * @file
 *
 * Metex 14-bytes ASCII protocol parser.
 *
 * @internal
 * This should work for various multimeters which use this kind of protocol,
 * even though there is some variation in which modes each DMM supports.
 *
 * It does _not_ work for all Metex DMMs, some use a quite different protocol.
 */

#include <config.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "metex14"

/** Parse value from buf, byte 2-8. */
static int parse_value(const uint8_t *buf, struct metex14_info *info,
			float *result, int *exponent)
{
	int i, is_ol, cnt, dot_pos;
	char valstr[7 + 1];

	/* Strip all spaces from bytes 2-8. */
	memset(&valstr, 0, 7 + 1);
	for (i = 0, cnt = 0; i < 7; i++) {
		if (buf[2 + i] != ' ')
			valstr[cnt++] = buf[2 + i];
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

	/* Logic functions */
	if (!strcmp((const char *)&valstr, "READY") ||
			!strcmp((const char *)&valstr, "FLOAT")) {
		*result = INFINITY;
		info->is_logic = TRUE;
	} else if (!strcmp((const char *)&valstr, "Hi")) {
		*result = 1.0;
		info->is_logic = TRUE;
	} else if (!strcmp((const char *)&valstr, "Lo")) {
		*result = 0.0;
		info->is_logic = TRUE;
	}
	if (info->is_logic)
		return SR_OK;

	/* Bytes 2-8: Sign, value (up to 5 digits) and decimal point */
	sr_atof_ascii((const char *)&valstr, result);

	dot_pos = strcspn(valstr, ".");
	if (dot_pos < cnt)
		*exponent = -(cnt - dot_pos - 1);
	else
		*exponent = 0;

	sr_spew("The display value is %f.", *result);

	return SR_OK;
}

static void parse_flags(const char *buf, struct metex14_info *info)
{
	int i, cnt;
	char unit[4 + 1];
	const char *u;

	/* Bytes 0-1: Measurement mode AC, DC */
	info->is_ac = !strncmp(buf, "AC", 2);
	info->is_dc = !strncmp(buf, "DC", 2);

	/* Bytes 2-8: See parse_value(). */

	/* Strip all spaces from bytes 9-12. */
	memset(&unit, 0, 4 + 1);
	for (i = 0, cnt = 0; i < 4; i++) {
		if (buf[9 + i] != ' ')
			unit[cnt++] = buf[9 + i];
	}

	/* Bytes 9-12: Unit */
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
	else if (!g_ascii_strcasecmp(u, "Ohm"))
		info->is_ohm = TRUE;
	else if (!g_ascii_strcasecmp(u, "KOhm"))
		info->is_kilo = info->is_ohm = TRUE;
	else if (!g_ascii_strcasecmp(u, "MOhm"))
		info->is_mega = info->is_ohm = TRUE;
	else if (!g_ascii_strcasecmp(u, "pF"))
		info->is_pico = info->is_farad = TRUE;
	else if (!g_ascii_strcasecmp(u, "nF"))
		info->is_nano = info->is_farad = TRUE;
	else if (!g_ascii_strcasecmp(u, "uF"))
		info->is_micro = info->is_farad = TRUE;
	else if (!g_ascii_strcasecmp(u, "KHz"))
		info->is_kilo = info->is_hertz = TRUE;
	else if (!g_ascii_strcasecmp(u, "C"))
		info->is_celsius = TRUE;
	else if (!g_ascii_strcasecmp(u, "F"))
		info->is_fahrenheit = TRUE;
	else if (!g_ascii_strcasecmp(u, "DB"))
		info->is_decibel = TRUE;
	else if (!g_ascii_strcasecmp(u, "dBm"))
		info->is_decibel_mw = TRUE;
	else if (!g_ascii_strcasecmp(u, "W"))
		info->is_watt = TRUE;
	else if (!g_ascii_strcasecmp(u, ""))
		info->is_unitless = TRUE;

	/* Bytes 0-1: Measurement mode, except AC/DC */
	info->is_resistance = !strncmp(buf, "OH", 2) ||
		(!strncmp(buf, "  ", 2) && info->is_ohm);
	info->is_capacity = !strncmp(buf, "CA", 2) ||
		(!strncmp(buf, "  ", 2) && info->is_farad);
	info->is_temperature = !strncmp(buf, "TE", 2) ||
		info->is_celsius || info->is_fahrenheit;
	info->is_diode = !strncmp(buf, "DI", 2) ||
		(!strncmp(buf, "  ", 2) && info->is_volt && info->is_milli);
	info->is_frequency = !strncmp(buf, "FR", 2) ||
		(!strncmp(buf, "  ", 2) && info->is_hertz);
	info->is_gain = !strncmp(buf, "DB", 2) && info->is_decibel;
	info->is_power = (!strncmp(buf, "dB", 2) && info->is_decibel_mw) ||
		((!strncmp(buf, "WT", 2) && info->is_watt));
	info->is_hfe = !strncmp(buf, "HF", 2) ||
		(!strncmp(buf, "  ", 2) && !info->is_volt && !info->is_ohm &&
		 !info->is_logic && !info->is_farad && !info->is_hertz);
	info->is_min = !strncmp(buf, "MN", 2);
	info->is_max = !strncmp(buf, "MX", 2);
	info->is_avg = !strncmp(buf, "AG", 2);

	/*
	 * Note:
	 * - Protocol doesn't distinguish "resistance" from "beep" mode.
	 * - "DB" shows the logarithmic ratio of input voltage to a
	 *   pre-stored (user-changeable) value in the DMM.
	 */

	/* Byte 13: Always '\r' (carriage return, 0x0d, 13) */
}

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
			 int *exponent, const struct metex14_info *info)
{
	int factor;

	(void)exponent;

	/* Factors */
	factor = 0;
	if (info->is_pico)
		factor -= 12;
	if (info->is_nano)
		factor -= 9;
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
	if (info->is_hertz) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (info->is_farad) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (info->is_temperature) {
		analog->meaning->mq = SR_MQ_TEMPERATURE;
		if (info->is_celsius)
			analog->meaning->unit = SR_UNIT_CELSIUS;
		else if (info->is_fahrenheit)
			analog->meaning->unit = SR_UNIT_FAHRENHEIT;
		else
			analog->meaning->unit = SR_UNIT_UNITLESS;
	}
	if (info->is_diode) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_power) {
		analog->meaning->mq = SR_MQ_POWER;
		if (info->is_decibel_mw)
			analog->meaning->unit = SR_UNIT_DECIBEL_MW;
		else if (info->is_watt)
			analog->meaning->unit = SR_UNIT_WATT;
		else
			analog->meaning->unit = SR_UNIT_UNITLESS;
	}
	if (info->is_gain) {
		analog->meaning->mq = SR_MQ_GAIN;
		analog->meaning->unit = SR_UNIT_DECIBEL_VOLT;
	}
	if (info->is_hfe) {
		analog->meaning->mq = SR_MQ_GAIN;
		analog->meaning->unit = SR_UNIT_UNITLESS;
	}
	if (info->is_logic) {
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
	if (info->is_min)
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
	if (info->is_max)
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
	if (info->is_avg)
		analog->meaning->mqflags |= SR_MQFLAG_AVG;
}

static gboolean flags_valid(const struct metex14_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count = 0;
	count += (info->is_pico) ? 1 : 0;
	count += (info->is_nano) ? 1 : 0;
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
	count += (info->is_capacity) ? 1 : 0;
	count += (info->is_temperature) ? 1 : 0;
	count += (info->is_diode) ? 1 : 0;
	count += (info->is_frequency) ? 1 : 0;
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

#ifdef HAVE_LIBSERIALPORT
SR_PRIV int sr_metex14_packet_request(struct sr_serial_dev_inst *serial)
{
	const uint8_t wbuf = 'D';

	sr_spew("Requesting DMM packet.");

	return serial_write_blocking(serial, &wbuf, 1, 0);
}
#endif

SR_PRIV gboolean sr_metex14_packet_valid(const uint8_t *buf)
{
	struct metex14_info info;

	memset(&info, 0x00, sizeof(struct metex14_info));
	parse_flags((const char *)buf, &info);

	if (!flags_valid(&info))
		return FALSE;

	if (buf[13] != '\r')
		return FALSE;

	return TRUE;
}

SR_PRIV gboolean sr_metex14_4packets_valid(const uint8_t *buf)
{
	struct metex14_info info;
	size_t ch_idx;
	const uint8_t *ch_buf;

	ch_buf = buf;
	for (ch_idx = 0; ch_idx < 4; ch_idx++) {
		if (ch_buf[13] != '\r')
			return FALSE;
		memset(&info, 0x00, sizeof(info));
		parse_flags((const char *)ch_buf, &info);
		if (!flags_valid(&info))
			return FALSE;
		ch_buf += METEX14_PACKET_SIZE;
	}
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
 * @param info Pointer to a struct metex14_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_metex14_parse(const uint8_t *buf, float *floatval,
			     struct sr_datafeed_analog *analog, void *info)
{
	int ret, exponent = 0;
	struct metex14_info *info_local;

	info_local = (struct metex14_info *)info;

	/* Don't print byte 13. That one contains the carriage return. */
	sr_dbg("DMM packet: \"%.13s\"", buf);

	memset(info_local, 0x00, sizeof(struct metex14_info));

	if ((ret = parse_value(buf, info_local, floatval, &exponent)) != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	parse_flags((const char *)buf, info_local);
	handle_flags(analog, floatval, &exponent, info_local);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}

/**
 * Parse one out of four values of a four-display Metex14 variant.
 *
 * The caller's 'info' parameter can be used to track the channel index,
 * as long as the information is kept across calls to the 14-byte packet
 * parse routine (which clears the 'info' container).
 *
 * Since analog values have further details in the 'analog' parameter,
 * passing multiple values per parse routine call is problematic. So we
 * prefer the approach of passing one value per call, which is most
 * reliable and shall fit every similar device with multiple displays.
 *
 * The meters which use this parse routine send one 14-byte packet per
 * display. Each packet has the regular Metex14 layout.
 */
SR_PRIV int sr_metex14_4packets_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, void *info)
{
	struct metex14_info *info_local;
	size_t ch_idx;
	const uint8_t *ch_buf;
	int rc;

	info_local = info;
	ch_idx = info_local->ch_idx;
	ch_buf = buf + ch_idx * METEX14_PACKET_SIZE;
	rc = sr_metex14_parse(ch_buf, floatval, analog, info);
	info_local->ch_idx = ch_idx + 1;
	return rc;
}
