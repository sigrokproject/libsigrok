/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "analog"

struct unit_mq_string {
	uint64_t value;
	char *str;
};

static struct unit_mq_string unit_strings[] = {
	{ SR_UNIT_VOLT, "V" },
	{ SR_UNIT_AMPERE, "A" },
	{ SR_UNIT_OHM, "\xe2\x84\xa6" },
	{ SR_UNIT_FARAD, "F" },
	{ SR_UNIT_HENRY, "H" },
	{ SR_UNIT_KELVIN, "K" },
	{ SR_UNIT_CELSIUS, "\xc2\xb0""C" },
	{ SR_UNIT_FAHRENHEIT, "\xc2\xb0""F" },
	{ SR_UNIT_HERTZ, "Hz" },
	{ SR_UNIT_PERCENTAGE, "%" },
	{ SR_UNIT_SECOND, "s" },
	{ SR_UNIT_SIEMENS, "S" },
	{ SR_UNIT_DECIBEL_MW, "dBu" },
	{ SR_UNIT_DECIBEL_VOLT, "dBv" },
	{ SR_UNIT_DECIBEL_SPL, "dB" },
	{ SR_UNIT_CONCENTRATION, "ppm" },
	{ SR_UNIT_REVOLUTIONS_PER_MINUTE, "RPM" },
	{ SR_UNIT_VOLT_AMPERE, "VA" },
	{ SR_UNIT_WATT, "W" },
	{ SR_UNIT_WATT_HOUR, "Wh" },
	{ SR_UNIT_METER_SECOND, "m/s" },
	{ SR_UNIT_HECTOPASCAL, "hPa" },
	{ SR_UNIT_HUMIDITY_293K, "%rF" },
	{ SR_UNIT_DEGREE, "\xc2\xb0" },
	ALL_ZERO
};

static struct unit_mq_string mq_strings[] = {
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_A, "(A)" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_C, "(C)" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_Z, "(Z)" },
	{ SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT, "(SPL)" },
	{ SR_MQFLAG_SPL_TIME_WEIGHT_S, " S" },
	{ SR_MQFLAG_SPL_TIME_WEIGHT_F, " F" },
	{ SR_MQFLAG_SPL_LAT, " LAT" },
	/* Not a standard function for SLMs, so this is a made-up notation. */
	{ SR_MQFLAG_SPL_PCT_OVER_ALARM, "%oA" },
	{ SR_MQFLAG_AC, " AC" },
	{ SR_MQFLAG_DC, " DC" },
	{ SR_MQFLAG_RMS, " RMS" },
	{ SR_MQFLAG_DIODE, " DIODE" },
	{ SR_MQFLAG_HOLD, " HOLD" },
	{ SR_MQFLAG_MAX, " MAX" },
	{ SR_MQFLAG_MIN, " MIN" },
	{ SR_MQFLAG_AUTORANGE, " AUTO" },
	{ SR_MQFLAG_RELATIVE, " REL" },
	{ SR_MQFLAG_AVG, " AVG" },
	{ SR_MQFLAG_REFERENCE, " REF" },
	ALL_ZERO
};

SR_PRIV int sr_analog_init(struct sr_datafeed_analog2 *analog,
		struct sr_analog_encoding *encoding,
		struct sr_analog_meaning *meaning,
		struct sr_analog_spec *spec,
		int digits)
{
	memset(analog, 0, sizeof(*analog));
	memset(encoding, 0, sizeof(*encoding));
	memset(meaning, 0, sizeof(*meaning));
	memset(spec, 0, sizeof(*spec));

	analog->encoding = encoding;
	analog->meaning = meaning;
	analog->spec = spec;

	encoding->unitsize = sizeof(float);
	encoding->is_float = TRUE;
#ifdef WORDS_BIGENDIAN
	encoding->is_bigendian = TRUE;
#else
	encoding->is_bigendian = FALSE;
#endif
	encoding->digits = digits;
	encoding->is_digits_decimal = TRUE;
	encoding->scale.p = 1;
	encoding->scale.q = 1;
	encoding->offset.p = 0;
	encoding->offset.q = 1;

	spec->spec_digits = digits;

	return SR_OK;
}

SR_API int sr_analog_to_float(const struct sr_datafeed_analog2 *analog,
		float *outbuf)
{
	float offset;
	unsigned int b, i;
	gboolean bigendian;

#ifdef WORDS_BIGENDIAN
	bigendian = TRUE;
#else
	bigendian = FALSE;
#endif
	if (!analog->encoding->is_float) {
		/* TODO */
		sr_err("Only floating-point encoding supported so far.");
		return SR_ERR;
	}

	if (analog->encoding->unitsize == sizeof(float)
			&& analog->encoding->is_bigendian == bigendian
			&& (analog->encoding->scale.p == analog->encoding->scale.q)
			&& analog->encoding->offset.p / (float)analog->encoding->offset.q == 0) {
		/* The data is already in the right format. */
		memcpy(outbuf, analog->data, analog->num_samples * sizeof(float));
	} else {
		for (i = 0; i < analog->num_samples; i += analog->encoding->unitsize) {
			for (b = 0; b < analog->encoding->unitsize; b++) {
				if (analog->encoding->is_bigendian == bigendian)
					outbuf[i + b] = ((float *)analog->data)[i * analog->encoding->unitsize + b];
				else
					outbuf[i + (analog->encoding->unitsize - b)] = ((float *)analog->data)[i * analog->encoding->unitsize + b];
			}
			if (analog->encoding->scale.p != analog->encoding->scale.q)
				outbuf[i] = (outbuf[i] * analog->encoding->scale.p) / analog->encoding->scale.q;
			offset = ((float)analog->encoding->offset.p / (float)analog->encoding->offset.q);
			outbuf[i] += offset;
		}
	}

	return SR_OK;
}

/*
 * Convert a floating point value to a string, limited to the given
 * number of decimal digits.
 *
 * @param value The value to convert.
 * @param digits Number of digits after the decimal point to print.
 * @param result Pointer to store result.
 *
 * The string is allocated by the function and must be freed by the caller
 * after use by calling g_free().
 *
 * @retval SR_OK
 *
 * @since 0.4.0
 */
SR_API int sr_analog_float_to_string(float value, int digits, char **result)
{
	int cnt, i;

	/* This produces at least one too many digits */
	*result = g_strdup_printf("%.*f", digits, value);
	for (i = 0, cnt = 0; (*result)[i]; i++) {
		if (isdigit((*result)[i++]))
			cnt++;
		if (cnt == digits) {
			(*result)[i] = 0;
			break;
		}
	}

	return SR_OK;
}

/*
 * Convert the unit/MQ/MQ flags in the analog struct to a string.
 *
 * @param analog Struct containing the unit, MQ and MQ flags.
 * @param result Pointer to store result.
 *
 * The string is allocated by the function and must be freed by the caller
 * after use by calling g_free().
 *
 * @retval SR_OK
 *
 * @since 0.4.0
 */
SR_API int sr_analog_unit_to_string(const struct sr_datafeed_analog2 *analog,
		char **result)
{
	int i;
	GString *buf = g_string_new(NULL);

	for (i = 0; unit_strings[i].value; i++) {
		if (analog->meaning->unit == unit_strings[i].value) {
			g_string_assign(buf, unit_strings[i].str);
			break;
		}
	}

	/* More than one MQ flag may apply. */
	for (i = 0; mq_strings[i].value; i++)
		if (analog->meaning->mqflags & mq_strings[i].value)
			g_string_append(buf, mq_strings[i].str);

	*result = buf->str;
	g_string_free(buf, FALSE);

	return SR_OK;
}

