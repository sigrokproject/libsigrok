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
#include <libsigrok/libsigrok.h>
#include "lib.h"

static void test_samplerate(uint64_t samplerate, const char *expected)
{
	char *s;

	s = sr_samplerate_string(samplerate);
	fail_unless(s != NULL);
	fail_unless(!strcmp(s, expected),
		    "Invalid result for '%s': %s.", expected, s);
	g_free(s);
}

static void test_period(uint64_t frequency, const char *expected)
{
	char *s;

	s = sr_period_string(frequency);
	fail_unless(s != NULL);
	fail_unless(!strcmp(s, expected),
		    "Invalid result for '%s': %s.", expected, s);
	g_free(s);
}

static void test_rational(const char *input, struct sr_rational expected)
{
	int ret;
	struct sr_rational rational;

	ret = sr_parse_rational(input, &rational);
	fail_unless(ret == SR_OK);
	fail_unless((expected.p == rational.p) && (expected.q == rational.q),
		    "Invalid result for '%s': %ld/%ld'.",
		    input, rational.p, rational.q);
}

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
	/* Note: Numbers > 2^32 need a ULL suffix. */

	test_samplerate(1000000000, "1 GHz");
	test_samplerate(5000000000ULL, "5 GHz");
	test_samplerate(72000000000ULL, "72 GHz");
	test_samplerate(388000000000ULL, "388 GHz");
	test_samplerate(4417594444ULL, "4.417594444 GHz");
	test_samplerate(44175944444ULL, "44.175944444 GHz");
	test_samplerate(441759444441ULL, "441.759444441 GHz");
	test_samplerate(441759000001ULL, "441.759000001 GHz");
	test_samplerate(441050000000ULL, "441.05 GHz");
	test_samplerate(441000000005ULL, "441.000000005 GHz");
	test_samplerate(441500000000ULL, "441.5 GHz");

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
	// test_samplerate(18446744073709551615ULL, "18446744073.709551615 GHz");
	// test_samplerate(SR_GHZ(18446744073ULL), "18446744073 GHz");
}
END_TEST

START_TEST(test_hz_period)
{
	test_period(1, "1000 ms");
	test_period(5, "200 ms");
	test_period(72, "13 ms");
	test_period(388, "2 ms");

	/* Again, but now using SR_HZ(). */
	test_period(SR_HZ(1), "1000 ms");
	test_period(SR_HZ(5), "200 ms");
	test_period(SR_HZ(72), "13 ms");
	test_period(SR_HZ(388), "2 ms");
}
END_TEST

START_TEST(test_ghz_period)
{
	/* Note: Numbers > 2^32 need a ULL suffix. */

	test_period(1000000000, "1000 ps");
	test_period(5000000000ULL, "200 ps");
	test_period(72000000000ULL, "13 ps");
	test_period(388000000000ULL, "2 ps");

	/* Again, but now using SR_GHZ(). */
	test_period(SR_GHZ(1), "1000 ps");
	test_period(SR_GHZ(5), "200 ps");
	test_period(SR_GHZ(72), "13 ps");
	test_period(SR_GHZ(388), "2 ps");
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
}
END_TEST

Suite *suite_strutil(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("strutil");

	tc = tcase_create("sr_samplerate_string");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_hz);
	tcase_add_test(tc, test_khz);
	tcase_add_test(tc, test_mhz);
	tcase_add_test(tc, test_ghz);
	tcase_add_test(tc, test_hz_period);
	tcase_add_test(tc, test_ghz_period);
	tcase_add_test(tc, test_integral);
	tcase_add_test(tc, test_fractional);
	tcase_add_test(tc, test_exponent);
	suite_add_tcase(s, tc);

	return s;
}
