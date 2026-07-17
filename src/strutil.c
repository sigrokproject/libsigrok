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

/* Needed for POSIX.1-2008 locale functions */
/** @cond PRIVATE */
#define _XOPEN_SOURCE 700
/** @endcond */
#include <config.h>
#include <ctype.h>
#include <locale.h>
#if defined(__FreeBSD__) || defined(__APPLE__)
#include <xlocale.h>
#endif
#if defined(__FreeBSD__)
#include <sys/param.h>
#endif
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
 *
 * @private
 */
SR_PRIV int sr_atol(const char *str, long *ret)
{
	long tmp;
	char *endptr = NULL;

	errno = 0;
	tmp = strtol(str, &endptr, 10);

	while (endptr && isspace(*endptr))
		endptr++;

	if (!endptr || *endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}

	*ret = tmp;
	return SR_OK;
}

/**
 * Convert a text to a number including support for non-decimal bases.
 * Also optionally returns the position after the number, where callers
 * can either error out, or support application specific suffixes.
 *
 * @param[in] str The input text to convert.
 * @param[out] ret The conversion result.
 * @param[out] end The position after the number.
 * @param[in] base The number format's base, can be 0.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Conversion failed.
 *
 * @private
 *
 * This routine is more general than @ref sr_atol(), which strictly
 * expects the input text to contain just a decimal number, and nothing
 * else in addition. The @ref sr_atol_base() routine accepts trailing
 * text after the number, and supports non-decimal numbers (bin, hex),
 * including automatic detection from prefix text.
 */
SR_PRIV int sr_atol_base(const char *str, long *ret, char **end, int base)
{
	long num;
	char *endptr;

	/* Add "0b" prefix support which strtol(3) may be missing. */
	while (str && isspace(*str))
		str++;
	if (!base && strncmp(str, "0b", strlen("0b")) == 0) {
		str += strlen("0b");
		base = 2;
	}

	/* Run the number conversion. Quick bail out if that fails. */
	errno = 0;
	endptr = NULL;
	num = strtol(str, &endptr, base);
	if (!endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}
	*ret = num;

	/* Advance to optional non-space trailing suffix. */
	while (endptr && isspace(*endptr))
		endptr++;
	if (end)
		*end = endptr;

	return SR_OK;
}

/**
 * Convert a text to a number including support for non-decimal bases.
 * Also optionally returns the position after the number, where callers
 * can either error out, or support application specific suffixes.
 *
 * @param[in] str The input text to convert.
 * @param[out] ret The conversion result.
 * @param[out] end The position after the number.
 * @param[in] base The number format's base, can be 0.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Conversion failed.
 *
 * @private
 *
 * This routine is more general than @ref sr_atol(), which strictly
 * expects the input text to contain just a decimal number, and nothing
 * else in addition. The @ref sr_atoul_base() routine accepts trailing
 * text after the number, and supports non-decimal numbers (bin, hex),
 * including automatic detection from prefix text.
 */
SR_PRIV int sr_atoul_base(const char *str, unsigned long *ret, char **end, int base)
{
	unsigned long num;
	char *endptr;

	/* Add "0b" prefix support which strtol(3) may be missing. */
	while (str && isspace(*str))
		str++;
	if ((!base || base == 2) && strncmp(str, "0b", strlen("0b")) == 0) {
		str += strlen("0b");
		base = 2;
	}

	/* Run the number conversion. Quick bail out if that fails. */
	errno = 0;
	endptr = NULL;
	num = strtoul(str, &endptr, base);
	if (!endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}
	*ret = num;

	/* Advance to optional non-space trailing suffix. */
	while (endptr && isspace(*endptr))
		endptr++;
	if (end)
		*end = endptr;

	return SR_OK;
}

/**
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
 *
 * @private
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
 *
 * @private
 */
SR_PRIV int sr_atod(const char *str, double *ret)
{
	double tmp;
	char *endptr = NULL;

	errno = 0;
	tmp = strtof(str, &endptr);

	while (endptr && isspace(*endptr))
		endptr++;

	if (!endptr || *endptr || errno) {
		if (!errno)
			errno = EINVAL;
		return SR_ERR;
	}

	*ret = tmp;
	return SR_OK;
}

/**
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
 *
 * @private
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
 * Convert a string representation of a numeric value to a double. The
 * conversion is strict and will fail if the complete string does not represent
 * a valid double. The function sets errno according to the details of the
 * failure. This version ignores the locale.
 *
 * @param str The string representation to convert.
 * @param ret Pointer to double where the result of the conversion will be stored.
 *
 * @retval SR_OK Conversion successful.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int sr_atod_ascii(const char *str, double *ret)
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

	*ret = tmp;
	return SR_OK;
}

/**
 * Convert text to a floating point value, and get its precision.
 *
 * @param[in] str The input text to convert.
 * @param[out] ret The conversion result, a double precision float number.
 * @param[out] digits The number of significant decimals.
 *
 * @returns SR_OK in case of successful text to number conversion.
 * @returns SR_ERR when conversion fails.
 *
 * @since 0.6.0
 */
SR_PRIV int sr_atod_ascii_digits(const char *str, double *ret, int *digits)
{
	int d;
	double f;

	if (sr_count_digits(str, &d) != SR_OK || sr_atod_ascii(str, &f) != SR_OK)
		return SR_ERR;

	if (ret)
		*ret = f;

	if (digits)
		*digits = d;

	return SR_OK;
}

/**
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
 *
 * @private
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
 * Convert text to a floating point value, and get its precision.
 *
 * @param[in] str The input text to convert.
 * @param[out] ret The conversion result, a double precision float number.
 * @param[out] digits The number of significant decimals.
 *
 * @returns SR_OK in case of successful text to number conversion.
 * @returns SR_ERR when conversion fails.
 */
SR_PRIV int sr_atof_ascii_digits(const char *str, float *ret, int *digits)
{
	int d;
	float f;

	if (sr_count_digits(str, &d) != SR_OK || sr_atof_ascii(str, &f) != SR_OK)
		return SR_ERR;

	if (ret)
		*ret = f;

	if (digits)
		*digits = d;

	return SR_OK;
}

/**
 * Get the precision of a floating point number.
 *
 * @param[in] str The input text to convert.
 * @param[out] digits The number of significant decimals.
 *
 * @returns SR_OK in case of successful.
 * @returns SR_ERR when conversion fails.
 *
 * @private
 */
SR_PRIV int sr_count_digits(const char *str, int *digits)
{
	const char *p = str;
	char *exp_end;
	int d_int = 0;
	int d_dec = 0;
	int exp = 0;

	/* Skip leading spaces */
	while ((*p) && isspace(*p))
		p++;

	/* Skip the integer part */
	if ((*p == '-') || (*p == '+'))
		p++;
	while (isdigit(*p)) {
		d_int++;
		p++;
	}

	/* Count decimal digits. */
	if (*p == '.') {
		p++;
		while (isdigit(*p)) {
			p++;
			d_dec++;
		}
	}

	/* Parse the exponent */
	if (toupper(*p) == 'E') {
		p++;
		errno = 0;
		exp = strtol(p, &exp_end, 10);
		if (errno) {
			sr_spew("Failed to parse exponent: txt \"%s\", e \"%s\"",
				str, p);
			return SR_ERR;
		}
		p = exp_end;
	}

	sr_spew("count digits: txt \"%s\" -> d_int %d, d_dec %d, e %d "
		"-> digits %d\n",
		str, d_int, d_dec, exp, d_dec - exp);

	/* We should have parsed the whole string by now. Return an
	 * error if not. */
	if ((*p) || (d_dec == 0 && d_int == 0)) {
		return SR_ERR;
	}

	*digits = d_dec - exp;
	return SR_OK;
}

/**
 * Compose a string with a format string in the buffer pointed to by buf.
 *
 * It is up to the caller to ensure that the allocated buffer is large enough
 * to hold the formatted result.
 *
 * A terminating NUL character is automatically appended after the content
 * written.
 *
 * After the format parameter, the function expects at least as many additional
 * arguments as needed for format.
 *
 * This version ignores the current locale and uses the locale "C" for Linux,
 * FreeBSD, OSX and Android.
 *
 * @param buf Pointer to a buffer where the resulting C string is stored.
 * @param format C string that contains a format string (see printf).
 * @param ... A sequence of additional arguments, each containing a value to be
 *        used to replace a format specifier in the format string.
 *
 * @return On success, the number of characters that would have been written,
 *         not counting the terminating NUL character.
 *
 * @since 0.6.0
 */
SR_API int sr_sprintf_ascii(char *buf, const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_vsprintf_ascii(buf, format, args);
	va_end(args);

	return ret;
}

/**
 * Compose a string with a format string in the buffer pointed to by buf.
 *
 * It is up to the caller to ensure that the allocated buffer is large enough
 * to hold the formatted result.
 *
 * Internally, the function retrieves arguments from the list identified by
 * args as if va_arg was used on it, and thus the state of args is likely to
 * be altered by the call.
 *
 * In any case, args should have been initialized by va_start at some point
 * before the call, and it is expected to be released by va_end at some point
 * after the call.
 *
 * This version ignores the current locale and uses the locale "C" for Linux,
 * FreeBSD, OSX and Android.
 *
 * @param buf Pointer to a buffer where the resulting C string is stored.
 * @param format C string that contains a format string (see printf).
 * @param args A value identifying a variable arguments list initialized with
 *        va_start.
 *
 * @return On success, the number of characters that would have been written,
 *         not counting the terminating NUL character.
 *
 * @since 0.6.0
 */
SR_API int sr_vsprintf_ascii(char *buf, const char *format, va_list args)
{
#if defined(_WIN32)
	int ret;

#if 0
	/*
	 * TODO: This part compiles with mingw-w64 but doesn't run with Win7.
	 *       Doesn't start because of "Procedure entry point _create_locale
	 *       not found in msvcrt.dll".
	 *       mingw-w64 should link to msvcr100.dll not msvcrt.dll!
	 * See: https://msdn.microsoft.com/en-us/en-en/library/1kt27hek.aspx
	 */
	_locale_t locale;

	locale = _create_locale(LC_NUMERIC, "C");
	ret = _vsprintf_l(buf, format, locale, args);
	_free_locale(locale);
#endif

	/* vsprintf() uses the current locale, may not work correctly for floats. */
	ret = vsprintf(buf, format, args);

	return ret;
#elif defined(__APPLE__)
	/*
	 * See:
	 * https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/printf_l.3.html
	 * https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/xlocale.3.html
	 */
	int ret;
	locale_t locale;

	locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
	ret = vsprintf_l(buf, locale, format, args);
	freelocale(locale);

	return ret;
#elif defined(__FreeBSD__) && __FreeBSD_version >= 901000
	/*
	 * See:
	 * https://www.freebsd.org/cgi/man.cgi?query=printf_l&apropos=0&sektion=3&manpath=FreeBSD+9.1-RELEASE
	 * https://www.freebsd.org/cgi/man.cgi?query=xlocale&apropos=0&sektion=3&manpath=FreeBSD+9.1-RELEASE
	 */
	int ret;
	locale_t locale;

	locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
	ret = vsprintf_l(buf, locale, format, args);
	freelocale(locale);

	return ret;
#elif defined(__ANDROID__)
	/*
	 * The Bionic libc only has two locales ("C" aka "POSIX" and "C.UTF-8"
	 * aka "en_US.UTF-8"). The decimal point is hard coded as "."
	 * See: https://android.googlesource.com/platform/bionic/+/master/libc/bionic/locale.cpp
	 */
	int ret;

	ret = vsprintf(buf, format, args);

	return ret;
#elif defined(__linux__)
	int ret;
	locale_t old_locale, temp_locale;

	/* Switch to C locale for proper float/double conversion. */
	temp_locale = newlocale(LC_NUMERIC, "C", NULL);
	old_locale = uselocale(temp_locale);

	ret = vsprintf(buf, format, args);

	/* Switch back to original locale. */
	uselocale(old_locale);
	freelocale(temp_locale);

	return ret;
#elif defined(__unix__) || defined(__unix)
	/*
	 * This is a fallback for all other BSDs, *nix and FreeBSD <= 9.0, by
	 * using the current locale for snprintf(). This may not work correctly
	 * for floats!
	 */
	int ret;

	ret = vsprintf(buf, format, args);

	return ret;
#else
	/* No implementation for unknown systems! */
	return -1;
#endif
}

/**
 * Composes a string with a format string (like printf) in the buffer pointed
 * by buf (taking buf_size as the maximum buffer capacity to fill).
 * If the resulting string would be longer than n - 1 characters, the remaining
 * characters are discarded and not stored, but counted for the value returned
 * by the function.
 * A terminating NUL character is automatically appended after the content
 * written.
 * After the format parameter, the function expects at least as many additional
 * arguments as needed for format.
 *
 * This version ignores the current locale and uses the locale "C" for Linux,
 * FreeBSD, OSX and Android.
 *
 * @param buf Pointer to a buffer where the resulting C string is stored.
 * @param buf_size Maximum number of bytes to be used in the buffer. The
 *        generated string has a length of at most buf_size - 1, leaving space
 *        for the additional terminating NUL character.
 * @param format C string that contains a format string (see printf).
 * @param ... A sequence of additional arguments, each containing a value to be
 *        used to replace a format specifier in the format string.
 *
 * @return On success, the number of characters that would have been written if
 *         buf_size had been sufficiently large, not counting the terminating
 *         NUL character. On failure, a negative number is returned.
 *         Notice that only when this returned value is non-negative and less
 *         than buf_size, the string has been completely written.
 *
 * @since 0.6.0
 */
SR_API int sr_snprintf_ascii(char *buf, size_t buf_size,
	const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_vsnprintf_ascii(buf, buf_size, format, args);
	va_end(args);

	return ret;
}

/**
 * Composes a string with a format string (like printf) in the buffer pointed
 * by buf (taking buf_size as the maximum buffer capacity to fill).
 * If the resulting string would be longer than n - 1 characters, the remaining
 * characters are discarded and not stored, but counted for the value returned
 * by the function.
 * A terminating NUL character is automatically appended after the content
 * written.
 * Internally, the function retrieves arguments from the list identified by
 * args as if va_arg was used on it, and thus the state of args is likely to
 * be altered by the call.
 * In any case, arg should have been initialized by va_start at some point
 * before the call, and it is expected to be released by va_end at some point
 * after the call.
 *
 * This version ignores the current locale and uses the locale "C" for Linux,
 * FreeBSD, OSX and Android.
 *
 * @param buf Pointer to a buffer where the resulting C string is stored.
 * @param buf_size Maximum number of bytes to be used in the buffer. The
 *        generated string has a length of at most buf_size - 1, leaving space
 *        for the additional terminating NUL character.
 * @param format C string that contains a format string (see printf).
 * @param args A value identifying a variable arguments list initialized with
 *        va_start.
 *
 * @return On success, the number of characters that would have been written if
 *         buf_size had been sufficiently large, not counting the terminating
 *         NUL character. On failure, a negative number is returned.
 *         Notice that only when this returned value is non-negative and less
 *         than buf_size, the string has been completely written.
 *
 * @since 0.6.0
 */
SR_API int sr_vsnprintf_ascii(char *buf, size_t buf_size,
	const char *format, va_list args)
{
#if defined(_WIN32)
	int ret;

#if 0
	/*
	 * TODO: This part compiles with mingw-w64 but doesn't run with Win7.
	 *       Doesn't start because of "Procedure entry point _create_locale
	 *       not found in msvcrt.dll".
	 *       mingw-w64 should link to msvcr100.dll not msvcrt.dll!.
	 * See: https://msdn.microsoft.com/en-us/en-en/library/1kt27hek.aspx
	 */
	_locale_t locale;

	locale = _create_locale(LC_NUMERIC, "C");
	ret = _vsnprintf_l(buf, buf_size, format, locale, args);
	_free_locale(locale);
#endif

	/* vsprintf uses the current locale, may cause issues for floats. */
	ret = vsnprintf(buf, buf_size, format, args);

	return ret;
#elif defined(__APPLE__)
	/*
	 * See:
	 * https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/printf_l.3.html
	 * https://developer.apple.com/legacy/library/documentation/Darwin/Reference/ManPages/man3/xlocale.3.html
	 */
	int ret;
	locale_t locale;

	locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
	ret = vsnprintf_l(buf, buf_size, locale, format, args);
	freelocale(locale);

	return ret;
#elif defined(__FreeBSD__) && __FreeBSD_version >= 901000
	/*
	 * See:
	 * https://www.freebsd.org/cgi/man.cgi?query=printf_l&apropos=0&sektion=3&manpath=FreeBSD+9.1-RELEASE
	 * https://www.freebsd.org/cgi/man.cgi?query=xlocale&apropos=0&sektion=3&manpath=FreeBSD+9.1-RELEASE
	 */
	int ret;
	locale_t locale;

	locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
	ret = vsnprintf_l(buf, buf_size, locale, format, args);
	freelocale(locale);

	return ret;
#elif defined(__ANDROID__)
	/*
	 * The Bionic libc only has two locales ("C" aka "POSIX" and "C.UTF-8"
	 * aka "en_US.UTF-8"). The decimal point is hard coded as ".".
	 * See: https://android.googlesource.com/platform/bionic/+/master/libc/bionic/locale.cpp
	 */
	int ret;

	ret = vsnprintf(buf, buf_size, format, args);

	return ret;
#elif defined(__linux__)
	int ret;
	locale_t old_locale, temp_locale;

	/* Switch to C locale for proper float/double conversion. */
	temp_locale = newlocale(LC_NUMERIC, "C", NULL);
	old_locale = uselocale(temp_locale);

	ret = vsnprintf(buf, buf_size, format, args);

	/* Switch back to original locale. */
	uselocale(old_locale);
	freelocale(temp_locale);

	return ret;
#elif defined(__unix__) || defined(__unix)
	/*
	 * This is a fallback for all other BSDs, *nix and FreeBSD <= 9.0, by
	 * using the current locale for snprintf(). This may not work correctly
	 * for floats!
	 */
	int ret;

	ret = vsnprintf(buf, buf_size, format, args);

	return ret;
#else
	/* No implementation for unknown systems! */
	return -1;
#endif
}

/**
 * Convert a sequence of bytes to its textual representation ("hex dump").
 *
 * Callers should free the allocated GString. See sr_hexdump_free().
 *
 * @param[in] data Pointer to the byte sequence to print.
 * @param[in] len Number of bytes to print.
 *
 * @return NULL upon error, newly allocated GString pointer otherwise.
 *
 * @private
 */
SR_PRIV GString *sr_hexdump_new(const uint8_t *data, const size_t len)
{
	GString *s;
	size_t i;

	i = 3 * len;
	i += len / 8;
	i += len / 16;
	s = g_string_sized_new(i);
	for (i = 0; i < len; i++) {
		if (i)
			g_string_append_c(s, ' ');
		if (i && (i % 8) == 0)
			g_string_append_c(s, ' ');
		if (i && (i % 16) == 0)
			g_string_append_c(s, ' ');
		g_string_append_printf(s, "%02x", data[i]);
	}

	return s;
}

/**
 * Free a hex dump text that was created by sr_hexdump_new().
 *
 * @param[in] s Pointer to the GString to release.
 *
 * @private
 */
SR_PRIV void sr_hexdump_free(GString *s)
{
	if (s)
		g_string_free(s, TRUE);
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
	const char *readptr;
	char *endptr;
	gboolean is_negative, empty_integral, empty_fractional, exp_negative;
	int64_t integral;
	int64_t fractional;
	int64_t denominator;
	uint32_t fractional_len;
	int32_t exponent;

	/*
	 * Implementor's note: This routine tries hard to avoid calling
	 * glib's or the platform's conversion routines with input that
	 * cannot get converted *at all* (see bug #1093). It also takes
	 * care to return with non-zero errno values for any failed
	 * conversion attempt. It's assumed that correctness and robustness
	 * are more important than performance, which is why code paths
	 * are not optimized at all. Maintainability took priority.
	 */

	readptr = str;

	/* Skip leading whitespace. */
	while (isspace(*readptr))
		readptr++;

	/* Determine the sign, default to non-negative. */
	is_negative = FALSE;
	if (*readptr == '-') {
		is_negative = TRUE;
		readptr++;
	} else if (*readptr == '+') {
		is_negative = FALSE;
		readptr++;
	}

	/* Get the (optional) integral part. */
	empty_integral = TRUE;
	integral = 0;
	endptr = (char *)readptr;
	errno = 0;
	if (isdigit(*readptr)) {
		empty_integral = FALSE;
		integral = g_ascii_strtoll(readptr, &endptr, 10);
		if (errno)
			return SR_ERR;
		if (endptr == str) {
			errno = -EINVAL;
			return SR_ERR;
		}
		readptr = endptr;
	}

	/* Get the optional fractional part. */
	empty_fractional = TRUE;
	fractional = 0;
	fractional_len = 0;
	if (*readptr == '.') {
		readptr++;
		endptr++;
		errno = 0;
		if (isdigit(*readptr)) {
			empty_fractional = FALSE;
			fractional = g_ascii_strtoll(readptr, &endptr, 10);
			if (errno)
				return SR_ERR;
			if (endptr == readptr) {
				errno = -EINVAL;
				return SR_ERR;
			}
			fractional_len = endptr - readptr;
			readptr = endptr;
		}
	}

	/* At least one of integral or fractional is required. */
	if (empty_integral && empty_fractional) {
		errno = -EINVAL;
		return SR_ERR;
	}

	/* Get the (optional) exponent. */
	exponent = 0;
	if ((*readptr == 'E') || (*readptr == 'e')) {
		readptr++;
		endptr++;
		exp_negative = FALSE;
		if (*readptr == '+') {
			exp_negative = FALSE;
			readptr++;
			endptr++;
		} else if (*readptr == '-') {
			exp_negative = TRUE;
			readptr++;
			endptr++;
		}
		if (!isdigit(*readptr)) {
			errno = -EINVAL;
			return SR_ERR;
		}
		errno = 0;
		exponent = g_ascii_strtoll(readptr, &endptr, 10);
		if (errno)
			return SR_ERR;
		if (endptr == readptr) {
			errno = -EINVAL;
			return SR_ERR;
		}
		readptr = endptr;
		if (exp_negative)
			exponent = -exponent;
	}

	/* Input must be exhausted. Unconverted remaining input is fatal. */
	if (*endptr != '\0') {
		errno = -EINVAL;
		return SR_ERR;
	}

	/*
	 * Apply the sign to the integral (and fractional) part(s).
	 * Adjust exponent (decimal position) such that the above integral
	 * and fractional parts both fit into the (new) integral part.
	 */
	if (is_negative)
		integral = -integral;
	while (fractional_len-- > 0) {
		integral *= 10;
		exponent--;
	}
	if (!is_negative)
		integral += fractional;
	else
		integral -= fractional;
	while (exponent > 0) {
		integral *= 10;
		exponent--;
	}

	/*
	 * When significant digits remain after the decimal, scale up the
	 * denominator such that we end up with two integer p/q numbers.
	 */
	denominator = 1;
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
	int prec;

	freq = 1 / ((double)v_p / v_q);

	if (freq > SR_GHZ(1)) {
		v = (double)v_p / v_q * 1000000000000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		return g_strdup_printf("%.*f ps", prec, v);
	} else if (freq > SR_MHZ(1)) {
		v = (double)v_p / v_q * 1000000000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		return g_strdup_printf("%.*f ns", prec, v);
	} else if (freq > SR_KHZ(1)) {
		v = (double)v_p / v_q * 1000000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		return g_strdup_printf("%.*f us", prec, v);
	} else if (freq > 1) {
		v = (double)v_p / v_q * 1000.0;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		return g_strdup_printf("%.*f ms", prec, v);
	} else {
		v = (double)v_p / v_q;
		prec = ((v - (uint64_t)v) < FLT_MIN) ? 0 : 3;
		return g_strdup_printf("%.*f s", prec, v);
	}
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
	if (v_q == 1000)
		return g_strdup_printf("%" PRIu64 " mV", v_p);
	else if (v_q == 1)
		return g_strdup_printf("%" PRIu64 " V", v_p);
	else
		return g_strdup_printf("%g V", (float)v_p / (float)v_q);
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
	uint64_t multiplier;
	int done;
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
		case 't':
		case 'T':
			multiplier = SR_GHZ(1000);
			break;
		case 'p':
		case 'P':
			multiplier = SR_GHZ(1000 * 1000);
			break;
		case 'e':
		case 'E':
			multiplier = SR_GHZ(1000 * 1000 * 1000);
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
	} else {
		*size += frac_part;
	}

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
			*q = UINT64_C(1000000000000000);
		else if (!strcmp(s, "ps"))
			*q = UINT64_C(1000000000000);
		else if (!strcmp(s, "ns"))
			*q = UINT64_C(1000000000);
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

/**
 * Append another text item to a NULL terminated string vector.
 *
 * @param[in] table The previous string vector.
 * @param[in,out] sz The previous and the resulting vector size
 *       (item count).
 * @param[in] text The text string to append to the vector.
 *       Can be #NULL.
 *
 * @returns The new vector, its location can differ from 'table'.
 *       Or #NULL in case of failure.
 *
 * This implementation happens to work for the first invocation when
 * 'table' is #NULL and 'sz' is 0, as well as subsequent append calls.
 * The 'text' can be #NULL or can be a non-empty string. When 'sz' is
 * not provided, then the 'table' must be a NULL terminated vector,
 * so that the routine can auto-determine the vector's current length.
 *
 * This routine re-allocates the vector as needed. Callers must not
 * rely on the memory address to remain the same across calls.
 */
static char **append_probe_name(char **table, size_t *sz, const char *text)
{
	size_t curr_size, alloc_size;
	char **new_table;

	/* Get the table's previous size (item count). */
	if (sz)
		curr_size = *sz;
	else if (table)
		curr_size = g_strv_length(table);
	else
		curr_size = 0;

	/* Extend storage to hold one more item, and the termination. */
	alloc_size = curr_size + (text ? 1 : 0) + 1;
	alloc_size *= sizeof(table[0]);
	new_table = g_realloc(table, alloc_size);
	if (!new_table) {
		g_strfreev(table);
		if (sz)
			*sz = 0;
		return NULL;
	}

	/* Append the item, NULL terminate. */
	if (text) {
		new_table[curr_size] = g_strdup(text);
		if (!new_table[curr_size]) {
			g_strfreev(new_table);
			if (sz)
				*sz = 0;
			return NULL;
		}
		curr_size++;
	}
	if (sz)
		*sz = curr_size;
	new_table[curr_size] = NULL;

	return new_table;
}

static char **append_probe_names(char **table, size_t *sz,
	const char **names)
{
	if (!names)
		return table;

	while (names[0]) {
		table = append_probe_name(table, sz, names[0]);
		names++;
	}
	return table;
}

static const struct {
	const char *name;
	const char **expands;
} probe_name_aliases[] = {
	{
		"ac97", (const char *[]){
			"sync", "clk",
			"out", "in", "rst",
			NULL,
		},
	},
	{
		"i2c", (const char *[]){
			"scl", "sda", NULL,
		},
	},
	{
		"jtag", (const char *[]){
			"tdi", "tdo", "tck", "tms", NULL,
		},
	},
	{
		"jtag-opt", (const char *[]){
			"tdi", "tdo", "tck", "tms",
			"trst", "srst", "rtck", NULL,
		},
	},
	{
		"ieee488", (const char *[]){
			"dio1", "dio2", "dio3", "dio4",
			"dio5", "dio6", "dio7", "dio8",
			"eoi", "dav", "nrfd", "ndac",
			"ifc", "srq", "atn", "ren", NULL,
		},
	},
	{
		"lpc", (const char *[]){
			"lframe", "lclk",
			"lad0", "lad1", "lad2", "lad3",
			NULL,
		},
	},
	{
		"lpc-opt", (const char *[]){
			"lframe", "lclk",
			"lad0", "lad1", "lad2", "lad3",
			"lreset", "ldrq", "serirq", "clkrun",
			"lpme", "lpcpd", "lsmi",
			NULL,
		},
	},
	{
		"mcs48", (const char *[]){
			"ale", "psen",
			"d0", "d1", "d2", "d3",
			"d4", "d5", "d6", "d7",
			"a8", "a9", "a10", "a11",
			"a12", "a13",
			NULL,
		},
	},
	{
		"microwire", (const char *[]){
			"cs", "sk", "si", "so", NULL,
		},
	},
	{
		"sdcard_sd", (const char *[]){
			"cmd", "clk",
			"dat0", "dat1", "dat2", "dat3",
			NULL,
		},
	},
	{
		"seven_segment", (const char *[]){
			"a", "b", "c", "d", "e", "f", "g",
			"dp", NULL,
		},
	},
	{
		"spi", (const char *[]){
			"clk", "miso", "mosi", "cs", NULL,
		},
	},
	{
		"swd", (const char *[]){
			"swclk", "swdio", NULL,
		},
	},
	{
		"uart", (const char *[]){
			"rx", "tx", NULL,
		},
	},
	{
		"usb", (const char *[]){
			"dp", "dm", NULL,
		},
	},
	{
		"z80", (const char *[]){
			"d0", "d1", "d2", "d3",
			"d4", "d5", "d6", "d7",
			"m1", "rd", "wr",
			"mreq", "iorq",
			"a0", "a1", "a2", "a3",
			"a4", "a5", "a6", "a7",
			"a8", "a9", "a10", "a11",
			"a12", "a13", "a14", "a15",
			NULL,
		},
	},
};

/* Case insensitive lookup of an alias name. */
static const char **lookup_probe_alias(const char *name)
{
	size_t idx;

	for (idx = 0; idx < ARRAY_SIZE(probe_name_aliases); idx++) {
		if (g_ascii_strcasecmp(probe_name_aliases[idx].name, name) != 0)
			continue;
		return probe_name_aliases[idx].expands;
	}
	return NULL;
}

/**
 * Parse a probe names specification, allocate a string vector.
 *
 * @param[in] spec The input spec, list of probes or aliases.
 * @param[in] dflt_names The default probe names, a string array.
 * @param[in] dflt_count The default probe names count. Either must
 *        match the unterminated array size, or can be 0 when the
 *        default names are NULL terminated.
 * @param[in] max_count Optional resulting vector size limit.
 * @param[out] ret_count Optional result vector size (return value).
 *
 * @returns A string vector with resulting probe names. Or #NULL
 *        in case of failure.
 *
 * The input spec is a comma separated list of probe names. Items can
 * be aliases which expand to a corresponding set of signal names.
 * The resulting names list optionally gets padded from the caller's
 * builtin probe names, an empty input spec yields the original names
 * as provided by the caller. Padding is omitted when the spec starts
 * with '-', which may result in a device with fewer channels being
 * created, enough to cover the user's spec, but none extra to maybe
 * enable and use later on. An optional maximum length spec will trim
 * the result set to that size. The resulting vector length optionally
 * is returned to the caller, so that it need not re-get the length.
 *
 * Calling applications must release the allocated vector by means
 * of @ref sr_free_probe_names().
 *
 * @since 0.6.0
 */
SR_API char **sr_parse_probe_names(const char *spec,
	const char **dflt_names, size_t dflt_count,
	size_t max_count, size_t *ret_count)
{
	char **result_names;
	size_t result_count;
	gboolean pad_from_dflt;
	char **spec_names, *spec_name;
	size_t spec_idx;
	const char **alias_names;

	if (!spec || !*spec)
		spec = NULL;

	/*
	 * Accept zero length spec for default input names. Determine
	 * the name table's length here. Cannot re-use g_strv_length()
	 * because of the 'const' decoration in application code.
	 */
	if (!dflt_count) {
		while (dflt_names && dflt_names[dflt_count])
			dflt_count++;
	}
	if (!dflt_count)
		return NULL;

	/*
	 * Start with an empty resulting names table. Will grow
	 * dynamically as more names get appended.
	 */
	result_names = NULL;
	result_count = 0;
	pad_from_dflt = TRUE;

	/*
	 * When an input spec exists, use its content. Lookup alias
	 * names, and append their corresponding signals. Or append
	 * the verbatim input name if it is not an alias. Recursion
	 * is not supported in this implementation.
	 *
	 * A leading '-' before the signal names list suppresses the
	 * padding of the resulting list from the device's default
	 * probe names.
	 */
	spec_names = NULL;
	if (spec && *spec == '-') {
		spec++;
		pad_from_dflt = FALSE;
	}
	if (spec && *spec)
		spec_names = g_strsplit(spec, ",", 0);
	for (spec_idx = 0; spec_names && spec_names[spec_idx]; spec_idx++) {
		spec_name = spec_names[spec_idx];
		if (!*spec_name)
			continue;
		alias_names = lookup_probe_alias(spec_name);
		if (alias_names) {
			result_names = append_probe_names(result_names,
				&result_count, alias_names);
		} else {
			result_names = append_probe_name(result_names,
				&result_count, spec_name);
		}
	}
	g_strfreev(spec_names);

	/*
	 * By default pad the resulting names from the caller's
	 * probe names. Don't pad if the input spec started with
	 * '-', when the spec's exact length was requested.
	 */
	if (pad_from_dflt) do {
		if (max_count && result_count >= max_count)
			break;
		if (result_count >= dflt_count)
			break;
		result_names = append_probe_name(result_names, &result_count,
			dflt_names[result_count]);
	} while (1);

	/* Optionally trim the result to the caller's length limit. */
	if (max_count) {
		while (result_count > max_count) {
			--result_count;
			g_free(result_names[result_count]);
			result_names[result_count] = NULL;
		}
	}

	if (ret_count)
		*ret_count = result_count;

	return result_names;
}

/**
 * Release previously allocated probe names (string vector).
 *
 * @param[in] names The previously allocated string vector.
 *
 * @since 0.6.0
 */
SR_API void sr_free_probe_names(char **names)
{
	g_strfreev(names);
}

/**
 * Trim leading and trailing whitespace off text.
 *
 * @param[in] s The input text.
 *
 * @return Start of trimmed input text.
 *
 * Manipulates the caller's input text in place.
 *
 * @since 0.6.0
 */
SR_API char *sr_text_trim_spaces(char *s)
{
	char *p;

	if (!s || !*s)
		return s;

	p = s + strlen(s);
	while (p > s && isspace((int)p[-1]))
		*(--p) = '\0';
	while (isspace((int)*s))
		s++;

	return s;
}

/**
 * Check for another complete text line, trim, return consumed char count.
 *
 * @param[in] s The input text, current read position.
 * @param[in] l The input text, remaining available characters.
 * @param[out] next Position after the current text line.
 * @param[out] taken Count of consumed chars in current text line.
 *
 * @return Start of trimmed and NUL terminated text line.
 *   Or #NULL when no text line was found.
 *
 * Checks for the availability of another text line of input data.
 * Manipulates the caller's input text in place.
 *
 * The end-of-line condition is the LF character ('\n'). Which covers
 * LF-only as well as CR/LF input data. CR-only and LF/CR are considered
 * unpopular and are not supported. LF/CR may appear to work at the
 * caller's when leading whitespace gets trimmed (line boundaries will
 * be incorrect, but content may get processed as expected). Support for
 * all of the above combinations breaks the detection of empty lines (or
 * becomes unmaintainably complex).
 *
 * The input buffer must be end-of-line terminated, lack of EOL results
 * in failure to detect the text line. This is motivated by accumulating
 * input in chunks, and the desire to not process incomplete lines before
 * their reception has completed. Callers should enforce EOL if their
 * source of input provides an EOF condition and is unreliable in terms
 * of text line termination.
 *
 * When another text line is available, it gets NUL terminated and
 * space gets trimmed of both ends. The start position of the trimmed
 * text line is returned. Optionally the number of consumed characters
 * is returned to the caller. Optionally 'next' points to after the
 * returned text line, or #NULL when no other text is available in the
 * input buffer.
 *
 * The 'taken' value is not preset by this routine, only gets updated.
 * This is convenient for callers which expect to find multiple text
 * lines in a received chunk, before finally discarding processed data
 * from the input buffer (which can involve expensive memory move
 * operations, and may be desirable to defer as much as possible).
 *
 * @since 0.6.0
 */
SR_API char *sr_text_next_line(char *s, size_t l, char **next, size_t *taken)
{
	char *p;

	if (next)
		*next = NULL;
	if (!l)
		l = strlen(s);

	/* Immediate reject incomplete input data. */
	if (!s || !*s || !l)
		return NULL;

	/* Search for the next line termination. NUL terminate. */
	p = g_strstr_len(s, l, "\n");
	if (!p)
		return NULL;
	*p++ = '\0';
	if (taken)
		*taken += p - s;
	l -= p - s;
	if (next)
		*next = l ? p : NULL;

	/* Trim NUL terminated text line at both ends. */
	s = sr_text_trim_spaces(s);
	return s;
}

/**
 * Isolates another space separated word in a text line.
 *
 * @param[in] s The input text, current read position.
 * @param[out] next The position after the current word.
 *
 * @return The start of the current word. Or #NULL if there is none.
 *
 * Advances over leading whitespace. Isolates (NUL terminates) the next
 * whitespace separated word. Optionally returns the position after the
 * current word. Manipulates the caller's input text in place.
 *
 * @since 0.6.0
 */
SR_API char *sr_text_next_word(char *s, char **next)
{
	char *word, *p;

	word = s;
	if (next)
		*next = NULL;

	/* Immediately reject incomplete input data. */
	if (!word || !*word)
		return NULL;

	/* Advance over optional leading whitespace. */
	while (isspace((int)*word))
		word++;
	if (!*word)
		return NULL;

	/*
	 * Advance until whitespace or end of text. Quick return when
	 * end of input is seen. Otherwise advance over whitespace and
	 * return the position of trailing text.
	 */
	p = word;
	while (*p && !isspace((int)*p))
		p++;
	if (!*p)
		return word;
	*p++ = '\0';
	while (isspace((int)*p))
		p++;
	if (!*p)
		return word;
	if (next)
		*next = p;
	return word;
}

/**
 * Get the number of necessary bits to hold a given value. Also gets
 * the next power-of-two value at or above the caller provided value.
 *
 * @param[in] value The value that must get stored.
 * @param[out] bits The required number of bits.
 * @param[out] power The corresponding power-of-two.
 *
 * @return SR_OK upon success, SR_ERR* otherwise.
 *
 * TODO Move this routine to a more appropriate location, it is not
 * strictly string related.
 *
 * @since 0.6.0
 */
SR_API int sr_next_power_of_two(size_t value, size_t *bits, size_t *power)
{
	size_t need_bits;
	size_t check_mask;

	if (bits)
		*bits = 0;
	if (power)
		*power = 0;

	/*
	 * Handle the special case of input value 0 (needs 1 bit
	 * and results in "power of two" value 1) here. It is not
	 * covered by the generic logic below.
	 */
	if (!value) {
		if (bits)
			*bits = 1;
		if (power)
			*power = 1;
		return SR_OK;
	}

	need_bits = 0;
	check_mask = 0;
	do {
		need_bits++;
		check_mask <<= 1;
		check_mask |= 1UL << 0;
	} while (value & ~check_mask);

	if (bits)
		*bits = need_bits;
	if (power)
		*power = ++check_mask;
	return SR_OK;
}

/** @} */
