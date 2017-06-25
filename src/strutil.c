/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "strutil"
/** @endcond */

/**
 * @file
 *
 * Helper functions for handling or converting libsigrok-related strings.
 */

/**
 * @defgroup grp_strutil String utilities
 *
 * Helper functions for handling or converting libsigrok-related strings.
 *
 * @{
 */

/**
 * @private
 *
 * Convert a string representation of a numeric value (base 10) to a long integer. The
 * conversion is strict and will fail if the complete string does not represent
 * a valid long integer. The function sets errno according to the details of the
 * failure.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to long where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 */
SR_PRIV int sr_atol(const char *str, long *ret)
{
	long tmp;
	char *endptr = NULL;

	errno = 0;
	tmp = strtol(str, &endptr, 10);

	if (!endptr || *endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}

	*ret = tmp;
	return SR_OK;
}

/**
 * @private
 *
 * Convert a string representation of a numeric value (base 10) to an integer. The
 * conversion is strict and will fail if the complete string does not represent
 * a valid integer. The function sets errno according to the details of the
 * failure.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to int where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 */
SR_PRIV int sr_atoi(const char *str, int *ret)
{
	long tmp;

	if (sr_atol(str, &tmp) != SR_OK)
		return SR_ERR;

	if ((int) tmp != tmp) {
		errno = ERANGE;
		return SR_ERR;
	}

	*ret = (int) tmp;
	return SR_OK;
}

/**
 * @private
 *
 * Convert a string representation of a numeric value to a double. The
 * conversion is strict and will fail if the complete string does not represent
 * a valid double. The function sets errno according to the details of the
 * failure.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to double where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 */
SR_PRIV int sr_atod(const char *str, double *ret)
{
	double tmp;
	char *endptr = NULL;

	errno = 0;
	tmp = strtof(str, &endptr);

	if (!endptr || *endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}

	*ret = tmp;
	return SR_OK;
}

/**
 * @private
 *
 * Convert a string representation of a numeric value to a float. The
 * conversion is strict and will fail if the complete string does not represent
 * a valid float. The function sets errno according to the details of the
 * failure.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to float where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 */
SR_PRIV int sr_atof(const char *str, float *ret)
{
	double tmp;

	if (sr_atod(str, &tmp) != SR_OK)
		return SR_ERR;

	if ((float) tmp != tmp) {
		errno = ERANGE;
		return SR_ERR;
	}

	*ret = (float) tmp;
	return SR_OK;
}

/**
 * @private
 *
 * Convert a string representation of a numeric value to a float. The
 * conversion is strict and will fail if the complete string does not represent
 * a valid float. The function sets errno according to the details of the
 * failure. This version ignores the locale.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to float where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 */
SR_PRIV int sr_atof_ascii(const char *str, float *ret)
{
	double tmp;
	char *endptr = NULL;

	errno = 0;
	tmp = g_ascii_strtod(str, &endptr);

	if (!endptr || *endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}

	/* FIXME This fails unexpectedly. Some other method to safel downcast
	 * needs to be found. Checking against FLT_MAX doesn't work as well. */
	/*
	if ((float) tmp != tmp) {
		errno = ERANGE;
		sr_dbg("ERANGEEEE %e != %e", (float) tmp, tmp);
		return SR_ERR;
	}
	*/

	*ret = (float) tmp;
	return SR_OK;
}

/**
 * Convert a string representation of a numeric value to a sr_rational.
 *
 * The conversion is strict and will fail if the complete string does not
 * represent a valid number. The function sets errno according to the details
 * of the failure. This version ignores the locale.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to sr_rational where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 *
 * @since 0.5.0
 */
SR_API int sr_parse_rational(const char *str, struct sr_rational *ret)
{
	char *endptr = NULL;
	int64_t integral;
	int64_t fractional = 0;
	int64_t denominator = 1;
	int32_t fractional_len = 0;
	int32_t exponent = 0;

	errno = 0;
	integral = g_ascii_strtoll(str, &endptr, 10);

	if (errno)
		return SR_ERR;

	if (*endptr == '.') {
		const char* start = endptr + 1;
		fractional = g_ascii_strtoll(start, &endptr, 10);
		if (errno)
			return SR_ERR;
		fractional_len = endptr - start;
	}

	if ((*endptr == 'E') || (*endptr == 'e')) {
		exponent = g_ascii_strtoll(endptr + 1, &endptr, 10);
		if (errno)
			return SR_ERR;
	}

	if (*endptr != '\0')
		return SR_ERR;

	for (int i = 0; i < fractional_len; i++)
		integral *= 10;
	exponent -= fractional_len;

	if (integral >= 0)
		integral += fractional;
	else
		integral -= fractional;

	while (exponent > 0) {
		integral *= 10;
		exponent--;
	}

	while (exponent < 0) {
		denominator *= 10;
		exponent++;
	}

	ret->p = integral;
	ret->q = denominator;

	return SR_OK;
}

/**
 * Convert a numeric value value to its "natural" string representation
 * in SI units.
 *
 * E.g. a value of 3000000, with units set to "W", would be converted
 * to "3 MW", 20000 to "20 kW", 31500 would become "31.5 kW".
 *
 * @param x The value to convert.
 * @param unit The unit to append to the string, or NULL if the string
 *             has no units.
 *
 * @return A newly allocated string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 *
 * @since 0.2.0
 */
SR_API char *sr_si_string_u64(uint64_t x, const char *unit)
{
	uint8_t i;
	uint64_t quot, divisor[] = {
		SR_HZ(1), SR_KHZ(1), SR_MHZ(1), SR_GHZ(1),
		SR_GHZ(1000), SR_GHZ(1000 * 1000), SR_GHZ(1000 * 1000 * 1000),
	};
	const char *p, prefix[] = "\0kMGTPE";
	char fmt[16], fract[20] = "", *f;

	if (!unit)
		unit = "";

	for (i = 0; (quot = x / divisor[i]) >= 1000; i++);

	if (i) {
		sprintf(fmt, ".%%0%d"PRIu64, i * 3);
		f = fract + sprintf(fract, fmt, x % divisor[i]) - 1;

		while (f >= fract && strchr("0.", *f))
			*f-- = 0;
	}

	p = prefix + i;

	return g_strdup_printf("%" PRIu64 "%s %.1s%s", quot, fract, p, unit);
}

/**
 * Convert a numeric samplerate value to its "natural" string representation.
 *
 * E.g. a value of 3000000 would be converted to "3 MHz", 20000 to "20 kHz",
 * 31500 would become "31.5 kHz".
 *
 * @param samplerate The samplerate in Hz.
 *
 * @return A newly allocated string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 *
 * @since 0.1.0
 */
SR_API char *sr_samplerate_string(uint64_t samplerate)
{
	return sr_si_string_u64(samplerate, "Hz");
}

/**
 * Convert a numeric period value to the "natural" string representation
 * of its period value.
 *
 * The period is specified as a rational number's numerator and denominator.
 *
 * E.g. a pair of (1, 5) would be converted to "200 ms", (10, 100) to "100 ms".
 *
 * @param v_p The period numerator.
 * @param v_q The period denominator.
 *
 * @return A newly allocated string representation of the period value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 *
 * @since 0.5.0
 */
SR_API char *sr_period_string(uint64_t v_p, uint64_t v_q)
{
	double freq, v;
	char *o;
	int prec, r;

	freq = 1 / ((double)v_p / v_q);

	o = g_malloc0(30 + 1);

	if (freq > SR_GHZ(1)) {
		v = (double)v_p / v_q * 1000000000000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		r = snprintf(o, 30, "%.*f ps", prec, v);
	} else if (freq > SR_MHZ(1)) {
		v = (double)v_p / v_q * 1000000000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		r = snprintf(o, 30, "%.*f ns", prec, v);
	} else if (freq > SR_KHZ(1)) {
		v = (double)v_p / v_q * 1000000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		r = snprintf(o, 30, "%.*f us", prec, v);
	} else if (freq > 1) {
		v = (double)v_p / v_q * 1000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		r = snprintf(o, 30, "%.*f ms", prec, v);
	} else {
		v = (double)v_p / v_q;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		r = snprintf(o, 30, "%.*f s", prec, v);
	}

	if (r < 0) {
		/* Something went wrong... */
		g_free(o);
		return NULL;
	}

	return o;
}

/**
 * Convert a numeric voltage value to the "natural" string representation
 * of its voltage value. The voltage is specified as a rational number's
 * numerator and denominator.
 *
 * E.g. a value of 300000 would be converted to "300mV", 2 to "2V".
 *
 * @param v_p The voltage numerator.
 * @param v_q The voltage denominator.
 *
 * @return A newly allocated string representation of the voltage value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 *
 * @since 0.2.0
 */
SR_API char *sr_voltage_string(uint64_t v_p, uint64_t v_q)
{
	int r;
	char *o;

	o = g_malloc0(30 + 1);

	if (v_q == 1000)
		r = snprintf(o, 30, "%" PRIu64 "mV", v_p);
	else if (v_q == 1)
		r = snprintf(o, 30, "%" PRIu64 "V", v_p);
	else
		r = snprintf(o, 30, "%gV", (float)v_p / (float)v_q);

	if (r < 0) {
		/* Something went wrong... */
		g_free(o);
		return NULL;
	}

	return o;
}

/**
 * Convert a "natural" string representation of a size value to uint64_t.
 *
 * E.g. a value of "3k" or "3 K" would be converted to 3000, a value
 * of "15M" would be converted to 15000000.
 *
 * Value representations other than decimal (such as hex or octal) are not
 * supported. Only 'k' (kilo), 'm' (mega), 'g' (giga) suffixes are supported.
 * Spaces (but not other whitespace) between value and suffix are allowed.
 *
 * @param sizestring A string containing a (decimal) size value.
 * @param size Pointer to uint64_t which will contain the string's size value.
 *
 * @return SR_OK upon success, SR_ERR upon errors.
 *
 * @since 0.1.0
 */
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size)
{
	int multiplier, done;
	double frac_part;
	char *s;

	*size = strtoull(sizestring, &s, 10);
	multiplier = 0;
	frac_part = 0;
	done = FALSE;
	while (s && *s && multiplier == 0 && !done) {
		switch (*s) {
		case ' ':
			break;
		case '.':
			frac_part = g_ascii_strtod(s, &s);
			break;
		case 'k':
		case 'K':
			multiplier = SR_KHZ(1);
			break;
		case 'm':
		case 'M':
			multiplier = SR_MHZ(1);
			break;
		case 'g':
		case 'G':
			multiplier = SR_GHZ(1);
			break;
		default:
			done = TRUE;
			s--;
		}
		s++;
	}
	if (multiplier > 0) {
		*size *= multiplier;
		*size += frac_part * multiplier;
	} else
		*size += frac_part;

	if (s && *s && g_ascii_strcasecmp(s, "Hz"))
		return SR_ERR;

	return SR_OK;
}

/**
 * Convert a "natural" string representation of a time value to an
 * uint64_t value in milliseconds.
 *
 * E.g. a value of "3s" or "3 s" would be converted to 3000, a value
 * of "15ms" would be converted to 15.
 *
 * Value representations other than decimal (such as hex or octal) are not
 * supported. Only lower-case "s" and "ms" time suffixes are supported.
 * Spaces (but not other whitespace) between value and suffix are allowed.
 *
 * @param timestring A string containing a (decimal) time value.
 * @return The string's time value as uint64_t, in milliseconds.
 *
 * @todo Add support for "m" (minutes) and others.
 * @todo Add support for picoseconds?
 * @todo Allow both lower-case and upper-case? If no, document it.
 *
 * @since 0.1.0
 */
SR_API uint64_t sr_parse_timestring(const char *timestring)
{
	uint64_t time_msec;
	char *s;

	/* TODO: Error handling, logging. */

	time_msec = strtoull(timestring, &s, 10);
	if (time_msec == 0 && s == timestring)
		return 0;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcmp(s, "s"))
			time_msec *= 1000;
		else if (!strcmp(s, "ms"))
			; /* redundant */
		else
			return 0;
	}

	return time_msec;
}

/** @since 0.1.0 */
SR_API gboolean sr_parse_boolstring(const char *boolstr)
{
	/*
	 * Complete absence of an input spec is assumed to mean TRUE,
	 * as in command line option strings like this:
	 *   ...:samplerate=100k:header:numchannels=4:...
	 */
	if (!boolstr || !*boolstr)
		return TRUE;

	if (!g_ascii_strncasecmp(boolstr, "true", 4) ||
	    !g_ascii_strncasecmp(boolstr, "yes", 3) ||
	    !g_ascii_strncasecmp(boolstr, "on", 2) ||
	    !g_ascii_strncasecmp(boolstr, "1", 1))
		return TRUE;

	return FALSE;
}

/** @since 0.2.0 */
SR_API int sr_parse_period(const char *periodstr, uint64_t *p, uint64_t *q)
{
	char *s;

	*p = strtoull(periodstr, &s, 10);
	if (*p == 0 && s == periodstr)
		/* No digits found. */
		return SR_ERR_ARG;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcmp(s, "fs"))
			*q = 1000000000000000ULL;
		else if (!strcmp(s, "ps"))
			*q = 1000000000000ULL;
		else if (!strcmp(s, "ns"))
			*q = 1000000000ULL;
		else if (!strcmp(s, "us"))
			*q = 1000000;
		else if (!strcmp(s, "ms"))
			*q = 1000;
		else if (!strcmp(s, "s"))
			*q = 1;
		else
			/* Must have a time suffix. */
			return SR_ERR_ARG;
	}

	return SR_OK;
}

/** @since 0.2.0 */
SR_API int sr_parse_voltage(const char *voltstr, uint64_t *p, uint64_t *q)
{
	char *s;

	*p = strtoull(voltstr, &s, 10);
	if (*p == 0 && s == voltstr)
		/* No digits found. */
		return SR_ERR_ARG;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!g_ascii_strcasecmp(s, "mv"))
			*q = 1000L;
		else if (!g_ascii_strcasecmp(s, "v"))
			*q = 1;
		else
			/* Must have a base suffix. */
			return SR_ERR_ARG;
	}

	return SR_OK;
}

/** @} */
