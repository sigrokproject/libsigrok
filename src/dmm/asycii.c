/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012-2013 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2016 Gerhard Sittig <gerhard.sittig@gmx.net>
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
 * Parser for the ASYC-II 16-bytes ASCII protocol (PRINT).
 *
 * This should work for various multimeters which use this kind of protocol,
 * even though there is some variation in which modes each DMM supports.
 *
 * This implementation was developed for and tested with a Metrix MX56C,
 * which is identical to the BK Precision 5390.
 * See the metex14.c implementation for the 14-byte protocol used by many
 * other models.
 */

#include <config.h>
#include <ctype.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "asycii"

/**
 * Parse sign and value from text buffer, byte 0-6.
 *
 * The first character always is the sign (' ' or '-'). Subsequent
 * positions contain digits, dots, or spaces. Overflow / open inputs
 * are signalled with several magic literals that cannot get interpreted
 * as a number, either with 'X' characters in them, or with several
 * forms of "OL".
 *
 * @param[in]	buf The text buffer received from the DMM.
 * @param[out]	result A floating point number value.
 * @param[out]	exponent Augments the number value.
 */
static int parse_value(const char *buf, struct asycii_info *info,
			float *result, int *exponent)
{
	char valstr[7 + 1];
	const char *valp;
	int i, cnt, is_ol;
	const char *dot_pos;

	/*
	 * Strip all spaces from bytes 0-6. By copying all
	 * non-space characters into a buffer.
	 */
	cnt = 0;
	for (i = 0; i < 7; i++) {
		if (buf[i] != ' ')
			valstr[cnt++] = buf[i];
	}
	valstr[cnt] = '\0';
	valp = &valstr[0];
	sr_spew("%s(), number buffer [%s]", __func__, valp);

	/*
	 * Check for "over limit" conditions. Depending on the meter's
	 * selected mode, the textual representation might differ. Test
	 * all known variations.
	 */
	is_ol = 0;
	is_ol += (g_ascii_strcasecmp(valp, ".OL") == 0) ? 1 : 0;
	is_ol += (g_ascii_strcasecmp(valp, "O.L") == 0) ? 1 : 0;
	is_ol += (g_ascii_strcasecmp(valp, "-.OL") == 0) ? 1 : 0;
	is_ol += (g_ascii_strcasecmp(valp, "-O.L") == 0) ? 1 : 0;
	is_ol += (g_ascii_strncasecmp(valp, "X", 1) == 0) ? 1 : 0;
	is_ol += (g_ascii_strncasecmp(valp, "-X", 2) == 0) ? 1 : 0;
	if (is_ol) {
		sr_spew("%s(), over limit", __func__);
		*result = INFINITY;
		return SR_OK;
	}

	/*
	 * Convert the textual number representation to a float, and
	 * an exponent.
	 */
	if (sr_atof_ascii(valp, result) != SR_OK) {
		info->is_invalid = TRUE;
		sr_spew("%s(), cannot convert number", __func__);
		return SR_ERR_DATA;
	}
	dot_pos = g_strstr_len(valstr, -1, ".");
	if (dot_pos)
		*exponent = -(valstr + strlen(valstr) - dot_pos - 1);
	else
		*exponent = 0;
	sr_spew("%s(), display value is %f, exponent %d",
		__func__, *result, *exponent);
	return SR_OK;
}

/**
 * Parse unit and flags from text buffer, bytes 7-14.
 *
 * The unit and flags optionally follow the number value for the
 * measurement. Either can be present or absent. The scale factor
 * is always at index 7. The unit starts at index 8, and is of
 * variable length. Flags immediately follow the unit. The remainder
 * of the text buffer is SPACE padded, and terminated with CR.
 *
 * Notice the implementation detail of case @b sensitive comparison.
 * Since the measurement unit and flags are directly adjacent and are
 * not separated from each other, case insensitive comparison would
 * yield wrong results. It's essential that e.g. "Vac" gets split into
 * the "V" unit and the "ac" flag, not into "VA" and the unknown "c"
 * flag!
 *
 * Notice, too, that order of comparison matters in the absence of
 * separators or fixed positions and with ambiguous text (note that we do
 * partial comparison). It's essential to e.g. correctly tell "VA" from "V".
 *
 * @param[in]	buf The text buffer received from the DMM.
 * @param[out]	info Broken down measurement details.
 */
static void parse_flags(const char *buf, struct asycii_info *info)
{
	int i, cnt;
	char unit[8 + 1];
	const char *u;

	/* Bytes 0-6: Number value, see parse_value(). */

	/* Strip spaces from bytes 7-14. */
	cnt = 0;
	for (i = 7; i < 15; i++) {
		if (buf[i] != ' ')
			unit[cnt++] = buf[i];
	}
	unit[cnt] = '\0';
	u = &unit[0];
	sr_spew("%s(): unit/flag buffer [%s]", __func__, u);

	/* Scan for the scale factor. */
	sr_spew("%s(): scanning factor, buffer [%s]", __func__, u);
	if (*u == 'p') {
		u++;
		info->is_pico = TRUE;
	} else if (*u == 'n') {
		u++;
		info->is_nano = TRUE;
	} else if (*u == 'u') {
		u++;
		info->is_micro = TRUE;
	} else if (*u == 'm') {
		u++;
		info->is_milli = TRUE;
	} else if (*u == ' ') {
		u++;
	} else if (*u == 'k') {
		u++;
		info->is_kilo = TRUE;
	} else if (*u == 'M') {
		u++;
		info->is_mega = TRUE;
	} else {
		/* Absence of a scale modifier is perfectly fine. */
	}

	/* Scan for the measurement unit. */
	sr_spew("%s(): scanning unit, buffer [%s]", __func__, u);
	if (g_str_has_prefix(u, "A")) {
		u += strlen("A");
		info->is_ampere = TRUE;
	} else if (g_str_has_prefix(u, "VA")) {
		u += strlen("VA");
		info->is_volt_ampere = TRUE;
	} else if (g_str_has_prefix(u, "V")) {
		u += strlen("V");
		info->is_volt = TRUE;
	} else if (g_str_has_prefix(u, "ohm")) {
		u += strlen("ohm");
		info->is_resistance = TRUE;
		info->is_ohm = TRUE;
	} else if (g_str_has_prefix(u, "F")) {
		u += strlen("F");
		info->is_capacitance = TRUE;
		info->is_farad = TRUE;
	} else if (g_str_has_prefix(u, "dB")) {
		u += strlen("dB");
		info->is_gain = TRUE;
		info->is_decibel = TRUE;
	} else if (g_str_has_prefix(u, "Hz")) {
		u += strlen("Hz");
		info->is_frequency = TRUE;
		info->is_hertz = TRUE;
	} else if (g_str_has_prefix(u, "%")) {
		u += strlen("%");
		info->is_duty_cycle = TRUE;
		if (*u == '+') {
			u++;
			info->is_duty_pos = TRUE;
		} else if (*u == '-') {
			u++;
			info->is_duty_neg = TRUE;
		} else {
			info->is_invalid = TRUE;
		}
	} else if (g_str_has_prefix(u, "Cnt")) {
		u += strlen("Cnt");
		info->is_pulse_count = TRUE;
		info->is_unitless = TRUE;
		if (*u == '+') {
			u++;
			info->is_count_pos = TRUE;
		} else if (*u == '-') {
			u++;
			info->is_count_neg = TRUE;
		} else {
			info->is_invalid = TRUE;
		}
	} else if (g_str_has_prefix(u, "s")) {
		u += strlen("s");
		info->is_pulse_width = TRUE;
		info->is_seconds = TRUE;
		if (*u == '+') {
			u++;
			info->is_period_pos = TRUE;
		} else if (*u == '-') {
			u++;
			info->is_period_neg = TRUE;
		} else {
			info->is_invalid = TRUE;
		}
	} else {
		/* Not strictly illegal, but unknown/unsupported. */
		sr_spew("%s(): measurement: unsupported", __func__);
		info->is_invalid = TRUE;
	}

	/* Scan for additional flags. */
	sr_spew("%s(): scanning flags, buffer [%s]", __func__, u);
	if (g_str_has_prefix(u, "ac+dc")) {
		u += strlen("ac+dc");
		info->is_ac_and_dc = TRUE;
	} else if (g_str_has_prefix(u, "ac")) {
		u += strlen("ac");
		info->is_ac = TRUE;
	} else if (g_str_has_prefix(u, "dc")) {
		u += strlen("dc");
		info->is_dc = TRUE;
	} else if (g_str_has_prefix(u, "d")) {
		u += strlen("d");
		info->is_diode = TRUE;
	} else if (g_str_has_prefix(u, "Pk")) {
		u += strlen("Pk");
		if (*u == '+') {
			u++;
			info->is_peak_max = TRUE;
		} else if (*u == '-') {
			u++;
			info->is_peak_min = TRUE;
		} else {
			info->is_invalid = TRUE;
		}
	} else if (*u == '\0') {
		/* Absence of any flags is acceptable. */
	} else {
		/* Presence of unknown flags is not. */
		sr_dbg("%s(): flag: unknown", __func__);
		info->is_invalid = TRUE;
	}

	/* Was all of the received data consumed? */
	if (*u != '\0')
		info->is_invalid = TRUE;

	/*
	 * Note:
	 * - The protocol does not distinguish between "resistance"
	 *   and "continuity".
	 * - Relative measurement and hold cannot get recognized.
	 */
}

/**
 * Fill in a datafeed from previously parsed measurement details.
 *
 * @param[out]	analog The datafeed which gets filled in.
 * @param[in]	floatval The number value of the measurement.
 * @param[in]	exponent Augments the number value.
 * @param[in]	info Scale and unit and other attributes.
 */
static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
			 int *exponent, const struct asycii_info *info)
{
	int factor = 0;

	/* Factors */
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
	*exponent += factor;

	/* Measurement modes */
	if (info->is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_volt_ampere) {
		analog->meaning->mq = SR_MQ_POWER;
		analog->meaning->unit = SR_UNIT_VOLT_AMPERE;
	}
	if (info->is_ampere) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (info->is_frequency) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (info->is_duty_cycle) {
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	}
	if (info->is_pulse_width) {
		analog->meaning->mq = SR_MQ_PULSE_WIDTH;
		analog->meaning->unit = SR_UNIT_SECOND;
	}
	if (info->is_pulse_count) {
		analog->meaning->mq = SR_MQ_COUNT;
		analog->meaning->unit = SR_UNIT_UNITLESS;
	}
	if (info->is_resistance) {
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (info->is_capacitance) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (info->is_diode) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_gain) {
		analog->meaning->mq = SR_MQ_GAIN;
		analog->meaning->unit = SR_UNIT_DECIBEL_VOLT;
	}

	/* Measurement related flags */
	if (info->is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (info->is_ac_and_dc)
		analog->meaning->mqflags |= SR_MQFLAG_AC | SR_MQFLAG_DC;
	if (info->is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	if (info->is_diode)
		analog->meaning->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
	if (info->is_peak_max)
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
	if (info->is_peak_min)
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
}

/**
 * Check measurement details for consistency and validity.
 *
 * @param[in]	info The previously parsed details.
 *
 * @return	TRUE on success, FALSE otherwise.
 */
static gboolean flags_valid(const struct asycii_info *info)
{
	int count;

	/* Have previous checks raised the "invalid" flag? */
	if (info->is_invalid) {
		sr_dbg("Previous parse raised \"invalid\" flag for packet.");
		return FALSE;
	}

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
	count += (info->is_volt || info->is_diode) ? 1 : 0;
	count += (info->is_volt_ampere) ? 1 : 0;
	count += (info->is_ampere) ? 1 : 0;
	count += (info->is_gain) ? 1 : 0;
	count += (info->is_resistance) ? 1 : 0;
	count += (info->is_capacitance) ? 1 : 0;
	count += (info->is_frequency) ? 1 : 0;
	count += (info->is_duty_cycle) ? 1 : 0;
	count += (info->is_pulse_width) ? 1 : 0;
	count += (info->is_pulse_count) ? 1 : 0;
	if (count > 1) {
		sr_dbg("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Are conflicting AC and DC flags set? */
	count = 0;
	count += (info->is_ac) ? 1 : 0;
	count += (info->is_ac_and_dc) ? 1 : 0;
	count += (info->is_dc) ? 1 : 0;
	if (count > 1) {
		sr_dbg("Conflicting AC and DC flags detected in packet.");
		return FALSE;
	}

	return TRUE;
}

#ifdef HAVE_LIBSERIALPORT
/**
 * Arrange for the reception of another measurement from the DMM.
 *
 * This routine is unused in the currently implemented PRINT mode,
 * where the meter sends measurements to the PC in pre-set intervals,
 * without the PC's intervention.
 *
 * @param[in]	serial The serial connection.
 *
 * @private
 */
SR_PRIV int sr_asycii_packet_request(struct sr_serial_dev_inst *serial)
{
	/*
	 * The current implementation assumes that the user pressed
	 * the PRINT button. It has no support to query/trigger packet
	 * reception from the meter.
	 */
	(void)serial;
	sr_spew("NOT requesting DMM packet.");
	return SR_OK;
}
#endif

/**
 * Check whether a received frame is valid.
 *
 * @param[in]	buf The text buffer with received data.
 *
 * @return	TRUE upon success, FALSE otherwise.
 */
SR_PRIV gboolean sr_asycii_packet_valid(const uint8_t *buf)
{
	struct asycii_info info;

	/* First check whether we are in sync with the packet stream. */
	if (buf[15] != '\r')
		return FALSE;

	/* Have the received packet content parsed. */
	memset(&info, 0x00, sizeof(info));
	parse_flags((const char *)buf, &info);
	if (!flags_valid(&info))
		return FALSE;

	return TRUE;
}

/**
 * Parse a protocol packet.
 *
 * @param[in]	buf Buffer containing the protocol packet. Must not be NULL.
 * @param[out]	floatval Pointer to a float variable. That variable will
 *		be modified in-place depending on the protocol packet.
 *		Must not be NULL.
 * @param[out]	analog Pointer to a struct sr_datafeed_analog. The struct
 *		will be filled with data according to the protocol packet.
 *		Must not be NULL.
 * @param[out]	info Pointer to a struct asycii_info. The struct will be
 * 		filled with data according to the protocol packet. Must
 *		not be NULL.
 *
 * @return	SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *		'analog' variable contents are undefined and should not
 *		be used.
 */
SR_PRIV int sr_asycii_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info)
{
	int ret, exponent;
	struct asycii_info *info_local;

	info_local = (struct asycii_info *)info;

	/* Don't print byte 15. That one contains the carriage return. */
	sr_dbg("DMM packet: \"%.15s\"", buf);

	memset(info_local, 0x00, sizeof(*info_local));

	exponent = 0;
	ret = parse_value((const char *)buf, info_local, floatval, &exponent);
	if (ret != SR_OK) {
		sr_dbg("Error parsing value: %d.", ret);
		return ret;
	}

	parse_flags((const char *)buf, info_local);
	handle_flags(analog, floatval, &exponent, info_local);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}
