/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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
#include <check.h>
#include <errno.h>
#include <locale.h>
#include <libsigrok/libsigrok.h>
#include "lib.h"

#if 0
static void test_vsnprintf(const char *expected, char *format, ...)
{
	va_list args;
	char *s;
	int len;

	len = 16;
	s = g_malloc0(len + 1);

	va_start(args, format);
	len = vsnprintf(s, len, format, args);
	va_end(args);

	fail_unless(s != NULL,
			"Invalid result for '%s': len = %i.", expected, len);
	fail_unless(!strcmp(s, expected),
			"Invalid result for '%s': %s.", expected, s);
	g_free(s);
}
#endif

static void test_sr_vsnprintf_ascii(const char *expected, char *format, ...)
{
	va_list args;
	char *s;
	int len;

	len = 16;
	s = g_malloc0(len + 1);

	va_start(args, format);
	len = sr_vsnprintf_ascii(s, len, format, args);
	va_end(args);

	ck_assert_msg(s != NULL, "Invalid result for '%s': len = %i.",
		      expected, len);
	ck_assert_msg(!strcmp(s, expected), "Invalid result for '%s': %s.",
		      expected, s);
	g_free(s);
}

static void test_sr_vsprintf_ascii(const char *expected, char *format, ...)
{
	va_list args, args_copy;
	char *s;
	int len;

	/* Get length of buffer required. */
	va_start(args, format);
	va_copy(args_copy, args);
	len = sr_vsnprintf_ascii(NULL, 0, format, args);
	va_end(args);

	/* Allocate buffer and write out command. */
	s = g_malloc0(len + 1);
	len = sr_vsprintf_ascii(s, format, args_copy);
	va_end(args_copy);

	ck_assert_msg(s != NULL, "Invalid result for '%s': len = %i.",
		      expected, len);
	ck_assert_msg(!strcmp(s, expected), "Invalid result for '%s': %s.",
		      expected, s);
	g_free(s);
}

static void test_samplerate(uint64_t samplerate, const char *expected)
{
	char *s;

	s = sr_samplerate_string(samplerate);
	ck_assert(s != NULL);
	ck_assert_msg(!strcmp(s, expected), "Invalid result for '%s': %s.",
		      expected, s);
	g_free(s);
}

static void test_period(uint64_t v_p, uint64_t v_q, const char *expected)
{
	char *s;

	s = sr_period_string(v_p, v_q);
	ck_assert(s != NULL);
	ck_assert_msg(!strcmp(s, expected), "Invalid result for '%s': %s.",
		      expected, s);
	g_free(s);
}

static void test_rational(const char *input, struct sr_rational expected)
{
	int ret;
	struct sr_rational rational;

	ret = sr_parse_rational(input, &rational);
	ck_assert_msg(ret == SR_OK, "Unexpected rc for '%s': %d, errno %d.",
		      input, ret, errno);
	ck_assert_msg((expected.p == rational.p) && (expected.q == rational.q),
		      "Invalid result for '%s': %" PRIi64 "/%" PRIu64 "'.",
		      input, rational.p, rational.q);
}

static void test_rational_fail(const char *input)
{
	int ret;
	struct sr_rational rational;

	ret = sr_parse_rational(input, &rational);
	ck_assert_msg(ret != SR_OK, "Unexpected success for '%s'.", input);
}

static void test_voltage(uint64_t v_p, uint64_t v_q, const char *expected)
{
	char *s;

	s = sr_voltage_string(v_p, v_q);
	ck_assert(s != NULL);
	ck_assert_msg(!strcmp(s, expected), "Invalid result for '%s': %s.",
		      expected, s);
	g_free(s);
}

START_TEST(test_locale)
{
	char *old_locale, *saved_locale;

	/* Get the the current locale. */
	old_locale = setlocale(LC_NUMERIC, NULL);
	fprintf(stderr, "Old locale = %s\n", old_locale);
	/* Copy the name so it won’t be clobbered by setlocale. */
	saved_locale = g_strdup(old_locale);
	ck_assert(saved_locale != NULL);

#ifdef _WIN32
	/*
	 * See: https://msdn.microsoft.com/en-us/library/cc233982.aspx
	 * Doesn't work! Locale is not set!
	 */
	setlocale(LC_NUMERIC, "de-DE");
#else
	/*
	 * For all *nix and OSX systems, change the locale for all threads to
	 * one that is known for not working correctly with printf(), e.g.
	 * "de_DE.UTF-8".
	 *
	 * Find all your available system locales with "locale -a".
	 */
	setlocale(LC_NUMERIC, "de_DE.UTF-8");
#endif
	fprintf(stderr, "New locale = %s\n", setlocale(LC_NUMERIC, NULL));

	test_sr_vsnprintf_ascii("0.1", "%.1f", (double)0.1);
	test_sr_vsnprintf_ascii("0.12", "%.2f", (double)0.12);
	test_sr_vsnprintf_ascii("0.123", "%.3f", (double)0.123);
	test_sr_vsnprintf_ascii("0.1234", "%.4f", (double)0.1234);
	test_sr_vsnprintf_ascii("0.12345", "%.5f", (double)0.12345);
	test_sr_vsnprintf_ascii("0.123456", "%.6f", (double)0.123456);

	test_sr_vsprintf_ascii("0.1", "%.1f", (double)0.1);
	test_sr_vsprintf_ascii("0.12", "%.2f", (double)0.12);
	test_sr_vsprintf_ascii("0.123", "%.3f", (double)0.123);
	test_sr_vsprintf_ascii("0.1234", "%.4f", (double)0.1234);
	test_sr_vsprintf_ascii("0.12345", "%.5f", (double)0.12345);
	test_sr_vsprintf_ascii("0.123456", "%.6f", (double)0.123456);

#if 0
	/*
	 * These tests can be used to tell on which platforms the printf()
	 * functions are locale-dependent (i.e. these tests will fail).
	 */
	test_vsnprintf("0.1", "%.1f", (double)0.1);
	test_vsnprintf("0.12", "%.2f", (double)0.12);
	test_vsnprintf("0.123", "%.3f", (double)0.123);
	test_vsnprintf("0.1234", "%.4f", (double)0.1234);
	test_vsnprintf("0.12345", "%.5f", (double)0.12345);
	test_vsnprintf("0.123456", "%.6f", (double)0.123456);
#endif

	/* Restore the original locale. */
	setlocale(LC_NUMERIC, saved_locale);
	g_free(saved_locale);
}
END_TEST

/*
 * Check various inputs for sr_samplerate_string():
 *
 *  - One, two, or three digit results (e.g. 5/55/555 MHz).
 *  - Results which contain commas (e.g. 1.234 / 12.34 / 123.4 kHz).
 *  - Results with zeroes right after the comma (e.g. 1.034 Hz).
 *    See also: http://sigrok.org/bugzilla/show_bug.cgi?id=73
 *  - Results with zeroes in the middle (e.g. 1.204 kHz).
 *  - All of the above, but using SR_MHZ() and friends.
 *    See also: http://sigrok.org/bugzilla/show_bug.cgi?id=72
 *
 * All of the above tests are done for the Hz/kHz/MHz/GHz ranges.
 */

START_TEST(test_hz)
{
	test_samplerate(0, "0 Hz");
	test_samplerate(1, "1 Hz");
	test_samplerate(23, "23 Hz");
	test_samplerate(644, "644 Hz");
	test_samplerate(604, "604 Hz");
	test_samplerate(550, "550 Hz");

	/* Again, but now using SR_HZ(). */
	test_samplerate(SR_HZ(0), "0 Hz");
	test_samplerate(SR_HZ(1), "1 Hz");
	test_samplerate(SR_HZ(23), "23 Hz");
	test_samplerate(SR_HZ(644), "644 Hz");
	test_samplerate(SR_HZ(604), "604 Hz");
	test_samplerate(SR_HZ(550), "550 Hz");
}
END_TEST

START_TEST(test_khz)
{
	test_samplerate(1000, "1 kHz");
	test_samplerate(99000, "99 kHz");
	test_samplerate(225000, "225 kHz");
	test_samplerate(1234, "1.234 kHz");
	test_samplerate(12345, "12.345 kHz");
	test_samplerate(123456, "123.456 kHz");
	test_samplerate(1034, "1.034 kHz");
	test_samplerate(1004, "1.004 kHz");
	test_samplerate(1230, "1.23 kHz");

	/* Again, but now using SR_KHZ(). */
	test_samplerate(SR_KHZ(1), "1 kHz");
	test_samplerate(SR_KHZ(99), "99 kHz");
	test_samplerate(SR_KHZ(225), "225 kHz");
	test_samplerate(SR_KHZ(1.234), "1.234 kHz");
	test_samplerate(SR_KHZ(12.345), "12.345 kHz");
	test_samplerate(SR_KHZ(123.456), "123.456 kHz");
	test_samplerate(SR_KHZ(1.204), "1.204 kHz");
	test_samplerate(SR_KHZ(1.034), "1.034 kHz");
	test_samplerate(SR_KHZ(1.004), "1.004 kHz");
	test_samplerate(SR_KHZ(1.230), "1.23 kHz");
}
END_TEST

START_TEST(test_mhz)
{
	test_samplerate(1000000, "1 MHz");
	test_samplerate(28000000, "28 MHz");
	test_samplerate(775000000, "775 MHz");
	test_samplerate(1234567, "1.234567 MHz");
	test_samplerate(12345678, "12.345678 MHz");
	test_samplerate(123456789, "123.456789 MHz");
	test_samplerate(1230007, "1.230007 MHz");
	test_samplerate(1034567, "1.034567 MHz");
	test_samplerate(1000007, "1.000007 MHz");
	test_samplerate(1234000, "1.234 MHz");

	/* Again, but now using SR_MHZ(). */
	test_samplerate(SR_MHZ(1), "1 MHz");
	test_samplerate(SR_MHZ(28), "28 MHz");
	test_samplerate(SR_MHZ(775), "775 MHz");
	test_samplerate(SR_MHZ(1.234567), "1.234567 MHz");
	test_samplerate(SR_MHZ(12.345678), "12.345678 MHz");
	test_samplerate(SR_MHZ(123.456789), "123.456789 MHz");
	test_samplerate(SR_MHZ(1.230007), "1.230007 MHz");
	test_samplerate(SR_MHZ(1.034567), "1.034567 MHz");
	test_samplerate(SR_MHZ(1.000007), "1.000007 MHz");
	test_samplerate(SR_MHZ(1.234000), "1.234 MHz");
}
END_TEST

START_TEST(test_ghz)
{
	test_samplerate(UINT64_C(1000000000), "1 GHz");
	test_samplerate(UINT64_C(5000000000), "5 GHz");
	test_samplerate(UINT64_C(72000000000), "72 GHz");
	test_samplerate(UINT64_C(388000000000), "388 GHz");
	test_samplerate(UINT64_C(4417594444), "4.417594444 GHz");
	test_samplerate(UINT64_C(44175944444), "44.175944444 GHz");
	test_samplerate(UINT64_C(441759444441), "441.759444441 GHz");
	test_samplerate(UINT64_C(441759000001), "441.759000001 GHz");
	test_samplerate(UINT64_C(441050000000), "441.05 GHz");
	test_samplerate(UINT64_C(441000000005), "441.000000005 GHz");
	test_samplerate(UINT64_C(441500000000), "441.5 GHz");

	/* Again, but now using SR_GHZ(). */
	test_samplerate(SR_GHZ(1), "1 GHz");
	test_samplerate(SR_GHZ(5), "5 GHz");
	test_samplerate(SR_GHZ(72), "72 GHz");
	test_samplerate(SR_GHZ(388), "388 GHz");
	test_samplerate(SR_GHZ(4.417594444), "4.417594444 GHz");
	test_samplerate(SR_GHZ(44.175944444), "44.175944444 GHz");
	test_samplerate(SR_GHZ(441.759444441), "441.759444441 GHz");
	test_samplerate(SR_GHZ(441.759000001), "441.759000001 GHz");
	test_samplerate(SR_GHZ(441.050000000), "441.05 GHz");
	test_samplerate(SR_GHZ(441.000000005), "441.000000005 GHz");
	test_samplerate(SR_GHZ(441.500000000), "441.5 GHz");

	/* Now check the biggest-possible samplerate (2^64 Hz). */
	// test_samplerate(UINT64_C(18446744073709551615), "18446744073.709551615 GHz");
	// test_samplerate(SR_GHZ(UINT64_C(18446744073)), "18446744073 GHz");
}
END_TEST

START_TEST(test_hz_period)
{
	test_period(1, 1, "1 s");
	test_period(1, 5, "200 ms");
	test_period(1, 72, "13.889 ms");
	test_period(1, 388, "2.577 ms");
	test_period(10, 1000, "10 ms");

	/* Again, but now using SR_HZ(). */
	test_period(1, SR_HZ(1), "1 s");
	test_period(1, SR_HZ(5), "200 ms");
	test_period(1, SR_HZ(72), "13.889 ms");
	test_period(1, SR_HZ(388), "2.577 ms");
	test_period(10, SR_HZ(100), "100 ms");
}
END_TEST

START_TEST(test_ghz_period)
{
	test_period(1, UINT64_C(1000000000), "1 ns");
	test_period(1, UINT64_C(5000000000), "200 ps");
	test_period(1, UINT64_C(72000000000), "13.889 ps");
	test_period(1, UINT64_C(388000000000), "2.577 ps");
	test_period(10, UINT64_C(1000000000000), "10 ps");
	test_period(200, UINT64_C(1000000000000), "200 ps");

	/* Again, but now using SR_GHZ(). */
	test_period(1, SR_GHZ(1), "1 ns");
	test_period(1, SR_GHZ(5), "200 ps");
	test_period(1, SR_GHZ(72), "13.889 ps");
	test_period(1, SR_GHZ(388), "2.577 ps");
	test_period(10, SR_GHZ(1), "10 ns");
	test_period(200, SR_GHZ(1000), "200 ps");
}
END_TEST

START_TEST(test_volt)
{
	test_voltage(34, 1, "34 V");
	test_voltage(34, 2, "17 V");
	test_voltage(1, 1, "1 V");
	test_voltage(1, 5, "0.2 V");
	test_voltage(200, 1000, "200 mV");
	test_voltage(1, 72, "0.0138889 V");
	test_voltage(1, 388, "0.00257732 V");
	test_voltage(10, 1000, "10 mV");
}
END_TEST

START_TEST(test_integral)
{
	test_rational("1", (struct sr_rational){1, 1});
	test_rational("2", (struct sr_rational){2, 1});
	test_rational("10", (struct sr_rational){10, 1});
	test_rational("-255", (struct sr_rational){-255, 1});
}
END_TEST

START_TEST(test_fractional)
{
	test_rational("0.1", (struct sr_rational){1, 10});
	test_rational("1.0", (struct sr_rational){10, 10});
	test_rational("1.2", (struct sr_rational){12, 10});
	test_rational("12.34", (struct sr_rational){1234, 100});
	test_rational("-12.34", (struct sr_rational){-1234, 100});
	test_rational("10.00", (struct sr_rational){1000, 100});
	test_rational(".1", (struct sr_rational){1, 10});
	test_rational("+0.1", (struct sr_rational){1, 10});
	test_rational("+.1", (struct sr_rational){1, 10});
	test_rational("-0.1", (struct sr_rational){-1, 10});
	test_rational("-.1", (struct sr_rational){-1, 10});
	test_rational(".1", (struct sr_rational){1, 10});
	test_rational(".123", (struct sr_rational){123, 1000});
	test_rational("1.", (struct sr_rational){1, 1});
	test_rational("123.", (struct sr_rational){123, 1});
	test_rational("-.1", (struct sr_rational){-1, 10});
	test_rational(" .1", (struct sr_rational){1, 10});
	test_rational("+.1", (struct sr_rational){1, 10});
	test_rational_fail(".");
	test_rational_fail(".e");
	test_rational_fail(".e1");
}
END_TEST

START_TEST(test_exponent)
{
	test_rational("1e0", (struct sr_rational){1, 1});
	test_rational("1E0", (struct sr_rational){1, 1});
	test_rational("1E1", (struct sr_rational){10, 1});
	test_rational("1e-1", (struct sr_rational){1, 10});
	test_rational("-1.234e-0", (struct sr_rational){-1234, 1000});
	test_rational("-1.234e3", (struct sr_rational){-1234, 1});
	test_rational("-1.234e-3", (struct sr_rational){-1234, 1000000});
	test_rational("0.001e3", (struct sr_rational){1, 1});
	test_rational("0.001e0", (struct sr_rational){1, 1000});
	test_rational("0.001e-3", (struct sr_rational){1, 1000000});
	test_rational("43.737E-3", (struct sr_rational){43737, 1000000});
	test_rational("-0.1e-2", (struct sr_rational){-1, 1000});
	test_rational("-.1e-2", (struct sr_rational){-1, 1000});
	test_rational("-.0e-2", (struct sr_rational){0, 1000});
	test_rational("+.0e-2", (struct sr_rational){0, 1000});
}
END_TEST

START_TEST(test_text_line)
{
	/*
	 * Covers text line splitting as used in input modules. Accepts
	 * input with differing end-of-line conventions, accepts leading
	 * and trailing whitespace. Isolates "the core" of a text line.
	 * Supports repeated calls which accumulate what later needs to
	 * get discarded after input data got processed in pieces.
	 */
#define EOL		"\n"

#define TEXT_CORE_1	"Need to provide"
#define TEXT_CORE_2	"an input text"
#define TEXT_CORE_3	""
#define TEXT_CORE_4	"with empty lines and  funny  spacing perhaps?"

#define TEXT_LINE_1	TEXT_CORE_1 " \n"
#define TEXT_LINE_2	"  " TEXT_CORE_2 "\n"
#define TEXT_LINE_3	TEXT_CORE_3 "\r\n"
#define TEXT_LINE_4	TEXT_CORE_4 "\n"

#define TEXT_INPUT	TEXT_LINE_1 TEXT_LINE_2 TEXT_LINE_3 TEXT_LINE_4

	char *input_text, *read_pos, *next_pos, *line;
	size_t input_len, taken;

	input_text = g_strdup(TEXT_INPUT);
	read_pos = input_text;
	input_len = strlen(input_text);

	/* Cover first line in tests. */
	taken = 0;
	line = sr_text_next_line(read_pos, input_len, &next_pos, &taken);
	ck_assert_msg(line, "Text line not found");
	ck_assert_msg(strcmp(line, TEXT_CORE_1) == 0,
		      "Unexpected line content");
	ck_assert_msg(next_pos, "No next line found");
	ck_assert_msg(strncmp(next_pos, TEXT_LINE_2, strlen(TEXT_LINE_2)) == 0,
		      "Unexpected next line content");
	ck_assert_msg(taken == strlen(TEXT_LINE_1),
		      "Unexpected consumed count");
	read_pos = next_pos;
	input_len -= taken;
	taken = 0;

	/* Cover second line in tests. DO NOT void 'taken' yet. */
	line = sr_text_next_line(read_pos, input_len, &next_pos, &taken);
	ck_assert_msg(line, "Text line not found");
	ck_assert_msg(strcmp(line, TEXT_CORE_2) == 0,
		      "Unexpected text line content");
	ck_assert_msg(next_pos, "No next line found");
	ck_assert_msg(strncmp(next_pos, TEXT_LINE_3, strlen(TEXT_LINE_3)) == 0,
		      "Unexpected next line content");
	ck_assert_msg(taken == strlen(TEXT_LINE_2),
		      "Unexpected consumed count");
	input_len -= next_pos - read_pos;
	read_pos = next_pos;

	/* Cover third line in tests. Accumulates 'taken'. */
	line = sr_text_next_line(read_pos, input_len, &next_pos, &taken);
	ck_assert_msg(line, "Text line not found");
	ck_assert_msg(strcmp(line, TEXT_CORE_3) == 0,
		      "Unexpected line content");
	ck_assert_msg(next_pos, "No next line found");
	ck_assert_msg(strncmp(next_pos, TEXT_LINE_4, strlen(TEXT_LINE_4)) == 0,
		      "Unexpected next line content");
	ck_assert_msg(taken == strlen(TEXT_LINE_2) + strlen(TEXT_LINE_3),
		      "Unexpected consumed count (totalled)");
	input_len -= next_pos - read_pos;
	read_pos = next_pos;
	taken = 0;

	/* Cover last line in tests. */
	line = sr_text_next_line(read_pos, input_len, &next_pos, &taken);
	ck_assert_msg(line, "Text line not found");
	ck_assert_msg(strcmp(line, TEXT_CORE_4) == 0,
		      "Unexpected text line content");
	ck_assert_msg(!next_pos, "Next line found, unexpected");
	ck_assert_msg(taken == strlen(TEXT_LINE_4),
		      "Unexpected consumed count");
	input_len -= taken;
	read_pos = next_pos;

	/* All input must have been consumed. */
	ck_assert(!read_pos);
	ck_assert(!input_len);

	g_free(input_text);
}
END_TEST

/*
 * TODO Ideally this table of test cases should reside within the
 * test_text_word() routine. But compilation fails when it's put there
 * (initializers are said to not be constant, cause is yet uncertain).
 */
static const struct {
	const char *line;
	const char **words;
} word_cases[] = {
	{ "", (const char *[]){ NULL, }, },
	{ " ", (const char *[]){ NULL, }, },
	{ "one", (const char *[]){ "one", NULL, }, },
	{ "one ", (const char *[]){ "one", NULL, }, },
	{ " one ", (const char *[]){ "one", NULL, }, },
	{ " one two ", (const char *[]){ "one", "two", NULL, }, },
	{ "one  two three ",
		(const char *[]){ "one", "two", "three", NULL, },
	},
};

START_TEST(test_text_word)
{
	size_t case_idx, word_idx;
	char *line;
	const char **words, *want;
	char *read_pos, *next_pos, *have;

	for (case_idx = 0; case_idx < ARRAY_SIZE(word_cases); case_idx++) {
		line = g_strdup(word_cases[case_idx].line);
		words = word_cases[case_idx].words;
		word_idx = 0;

		read_pos = line;
		while (read_pos) {
			want = words[word_idx];
			have = sr_text_next_word(read_pos, &next_pos);
			if (!want) {
				ck_assert_msg(!have, "word found, unexpected");
				ck_assert_msg(!next_pos,
					      "next found after end");
				break;
			}
			word_idx++;
			read_pos = next_pos;
			ck_assert_msg(have, "word not found");
			ck_assert_msg(strcmp(have, want) == 0,
				      "unexpected word found");
		}
		ck_assert_msg(!words[word_idx], "missed expected words");

		g_free(line);
	}
}
END_TEST

static const struct power_case_t {
	size_t value;
	size_t want_bits;
	size_t want_power;
} power_cases[] = {
	{ 0, 1, 1, },
	{ 1, 1, 2, },
	{ 2, 2, 4, },
	{ 3, 2, 4, },
	{ 4, 3, 8, },
	{ 5, 3, 8, },
	{ 6, 3, 8, },
	{ 7, 3, 8, },
	{ 8, 4, 16, },
	{ 15, 4, 16, },
	{ 16, 5, 32, },
	{ 31, 5, 32, },
};

START_TEST(test_calc_power_of_two)
{
	size_t case_idx, bits, power;
	const struct power_case_t *tcase;
	int ret;

	for (case_idx = 0; case_idx < ARRAY_SIZE(power_cases); case_idx++) {
		tcase = &power_cases[case_idx];
		ret = sr_next_power_of_two(tcase->value, &bits, &power);
		ck_assert_msg(ret == SR_OK, "bits count not found");
		ck_assert_msg(bits == tcase->want_bits, "bits count differs");
		ck_assert_msg(power == tcase->want_power, "power differs");
	}
}
END_TEST

Suite *suite_strutil(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("strutil");

	tc = tcase_create("sr_samplerate_string");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_locale);
	tcase_add_test(tc, test_hz);
	tcase_add_test(tc, test_khz);
	tcase_add_test(tc, test_mhz);
	tcase_add_test(tc, test_ghz);
	tcase_add_test(tc, test_hz_period);
	tcase_add_test(tc, test_ghz_period);
	tcase_add_test(tc, test_volt);
	tcase_add_test(tc, test_integral);
	tcase_add_test(tc, test_fractional);
	tcase_add_test(tc, test_exponent);
	suite_add_tcase(s, tc);

	tc = tcase_create("text");
	tcase_add_test(tc, test_text_line);
	tcase_add_test(tc, test_text_word);
	suite_add_tcase(s, tc);

	tc = tcase_create("calc");
	tcase_add_test(tc, test_calc_power_of_two);
	suite_add_tcase(s, tc);

	return s;
}
