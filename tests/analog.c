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

#include <config.h>
#include <stdlib.h>
#include <math.h>
#include <check.h>
#include <libsigrok/libsigrok.h>
#include "lib.h"

static int sr_analog_init_(struct sr_datafeed_analog *analog,
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

START_TEST(test_analog_to_float)
{
	int ret;
	unsigned int i;
	float f, fout;
	struct sr_channel ch;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	const float v[] = {-12.9, -333.999, 0, 3.1415, 29.7, 989898.121212};

	sr_analog_init_(&analog, &encoding, &meaning, &spec, 3);
	analog.num_samples = 1;
	analog.data = &f;
	meaning.channels = g_slist_append(NULL, &ch);

	for (i = 0; i < ARRAY_SIZE(v); i++) {
		fout = 19;
		f = v[i];
		ret = sr_analog_to_float(&analog, &fout);
		fail_unless(ret == SR_OK, "sr_analog_to_float() failed: %d.", ret);
		fail_unless(fabs(f - fout) <= 0.001, "%f != %f", f, fout);
	}
}
END_TEST

START_TEST(test_analog_to_float_null)
{
	int ret;
	float f, fout;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	f = G_PI;
	sr_analog_init_(&analog, &encoding, &meaning, &spec, 3);
	analog.num_samples = 1;
	analog.data = &f;

	ret = sr_analog_to_float(NULL, &fout);
	fail_unless(ret == SR_ERR_ARG);
	ret = sr_analog_to_float(&analog, NULL);
	fail_unless(ret == SR_ERR_ARG);
	ret = sr_analog_to_float(NULL, NULL);
	fail_unless(ret == SR_ERR_ARG);

	analog.data = NULL;
	ret = sr_analog_to_float(&analog, &fout);
	fail_unless(ret == SR_ERR_ARG);
	analog.data = &f;

	analog.meaning = NULL;
	ret = sr_analog_to_float(&analog, &fout);
	fail_unless(ret == SR_ERR_ARG);
	analog.meaning = &meaning;

	analog.encoding = NULL;
	ret = sr_analog_to_float(&analog, &fout);
	fail_unless(ret == SR_ERR_ARG);
	analog.encoding = &encoding;
}
END_TEST

START_TEST(test_analog_si_prefix)
{
	struct {
		float input_value;
		int input_digits;
		float output_value;
		int output_digits;
		const char *output_si_prefix;
	} v[] = {
		{   12.0     ,  0,  12.0  ,    0, ""  },
		{   12.0     ,  1,  12.0  ,    1, ""  },
		{   12.0     , -1,   0.012,    2, "k" },
		{ 1024.0     ,  0,   1.024,    3, "k" },
		{ 1024.0     , -1,   1.024,    2, "k" },
		{ 1024.0     , -3,   1.024,    0, "k" },
		{   12.0e5   ,  0,   1.2,      6, "M" },
		{    0.123456,  0,   0.123456, 0, ""  },
		{    0.123456,  1,   0.123456, 1, ""  },
		{    0.123456,  2,   0.123456, 2, ""  },
		{    0.123456,  3, 123.456,    0, "m" },
		{    0.123456,  4, 123.456,    1, "m" },
		{    0.123456,  5, 123.456,    2, "m" },
		{    0.123456,  6, 123.456,    3, "m" },
		{    0.123456,  7, 123.456,    4, "m" },
		{    0.0123  ,  4,  12.3,      1, "m" },
		{    0.00123 ,  5,   1.23,     2, "m" },
		{    0.000123,  4,   0.123,    1, "m" },
		{    0.000123,  5,   0.123,    2, "m" },
		{    0.000123,  6, 123.0,      0, "µ" },
		{    0.000123,  7, 123.0,      1, "µ" },
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(v); i++) {
		float value = v[i].input_value;
		int digits = v[i].input_digits;
		const char *si_prefix = sr_analog_si_prefix(&value, &digits);

		fail_unless(fabs(value - v[i].output_value) <= 0.00001,
			"sr_analog_si_prefix() unexpected output value %f (i=%d).",
			value , i);
		fail_unless(digits == v[i].output_digits,
			"sr_analog_si_prefix() unexpected output digits %d (i=%d).",
			digits, i);
		fail_unless(!strcmp(si_prefix, v[i].output_si_prefix),
			"sr_analog_si_prefix() unexpected output prefix \"%s\" (i=%d).",
			si_prefix, i);
	}
}
END_TEST

START_TEST(test_analog_si_prefix_null)
{
	float value = 1.23;
	int digits = 1;
	const char *si_prefix;

	si_prefix = sr_analog_si_prefix(NULL, &digits);
	fail_unless(!strcmp(si_prefix, ""));
	si_prefix = sr_analog_si_prefix(&value, NULL);
	fail_unless(!strcmp(si_prefix, ""));
	si_prefix = sr_analog_si_prefix(NULL, NULL);
	fail_unless(!strcmp(si_prefix, ""));
}
END_TEST

START_TEST(test_analog_unit_to_string)
{
	int ret;
	unsigned int i;
	char *result;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	const char *r[] = {"V RMS"};

	sr_analog_init_(&analog, &encoding, &meaning, &spec, 3);

	for (i = 0; i < ARRAY_SIZE(r); i++) {
		meaning.unit = SR_UNIT_VOLT;
		meaning.mqflags = SR_MQFLAG_RMS;
		ret = sr_analog_unit_to_string(&analog, &result);
		fail_unless(ret == SR_OK);
		fail_unless(result != NULL);
		fail_unless(!strcmp(result, r[i]), "%s != %s", result, r[i]);
		g_free(result);
	}
}
END_TEST

START_TEST(test_analog_unit_to_string_null)
{
	int ret;
	char *result;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init_(&analog, &encoding, &meaning, &spec, 3);

	meaning.unit = SR_UNIT_VOLT;
	meaning.mqflags = SR_MQFLAG_RMS;

	ret = sr_analog_unit_to_string(NULL, &result);
	fail_unless(ret == SR_ERR_ARG);
	ret = sr_analog_unit_to_string(&analog, NULL);
	fail_unless(ret == SR_ERR_ARG);
	ret = sr_analog_unit_to_string(NULL, NULL);
	fail_unless(ret == SR_ERR_ARG);

	analog.meaning = NULL;
	ret = sr_analog_unit_to_string(&analog, &result);
	fail_unless(ret == SR_ERR_ARG);
}
END_TEST

START_TEST(test_set_rational)
{
	unsigned int i;
	struct sr_rational r;
	const int64_t p[] = {0, 1, -5, INT64_MAX};
	const uint64_t q[] = {0, 2, 7, UINT64_MAX};

	for (i = 0; i < ARRAY_SIZE(p); i++) {
		sr_rational_set(&r, p[i], q[i]);
		fail_unless(r.p == p[i] && r.q == q[i]);
	}
}
END_TEST

START_TEST(test_set_rational_null)
{
	sr_rational_set(NULL, 5, 7);
}
END_TEST

START_TEST(test_cmp_rational)
{
	const struct sr_rational r[] = { { 1, 1 },
		{ 2, 2 },
		{ 1000, 1000 },
		{ INT64_MAX, INT64_MAX },
		{ 1, 4 },
		{ 2, 8 },
		{ INT64_MAX, UINT64_MAX },
		{ INT64_MIN, UINT64_MAX },
	};

	fail_unless(sr_rational_eq(&r[0], &r[0]) == 1);
	fail_unless(sr_rational_eq(&r[0], &r[1]) == 1);
	fail_unless(sr_rational_eq(&r[1], &r[2]) == 1);
	fail_unless(sr_rational_eq(&r[2], &r[3]) == 1);
	fail_unless(sr_rational_eq(&r[3], &r[3]) == 1);

	fail_unless(sr_rational_eq(&r[4], &r[4]) == 1);
	fail_unless(sr_rational_eq(&r[4], &r[5]) == 1);
	fail_unless(sr_rational_eq(&r[5], &r[5]) == 1);

	fail_unless(sr_rational_eq(&r[6], &r[6]) == 1);
	fail_unless(sr_rational_eq(&r[7], &r[7]) == 1);

	fail_unless(sr_rational_eq(&r[1], &r[4]) == 0);
}
END_TEST

START_TEST(test_mult_rational)
{
	const struct sr_rational r[][3] = {
		/*   a    *    b    =    c   */
		{ { 1, 1 }, { 1, 1 }, { 1, 1 }},
		{ { 2, 1 }, { 3, 1 }, { 6, 1 }},
		{ { 1, 2 }, { 2, 1 }, { 1, 1 }},
		/* Test negative numbers */
		{ { -1, 2 }, { 2, 1 }, { -1, 1 }},
		{ { -1, 2 }, { -2, 1 }, { 1, 1 }},
		{ { -(1ll<<20), (1ll<<10) }, { -(1ll<<20), 1 }, { (1ll<<30), 1 }},
		/* Test reduction */
		{ { INT32_MAX, (1ll<<12) }, { (1<<2), 1 }, { INT32_MAX, (1ll<<10) }},
		{ { INT64_MAX, (1ll<<63) }, { (1<<3), 1 }, { INT64_MAX, (1ll<<60) }},
		/* Test large numbers */
		{ {  (1ll<<40), (1ll<<10) }, {  (1ll<<30), 1 }, { (1ll<<60), 1 }},
		{ { -(1ll<<40), (1ll<<10) }, { -(1ll<<30), 1 }, { (1ll<<60), 1 }},

		{ { 1000, 1 }, { 8000, 1 }, { 8000000, 1 }},
		{ { 10000, 1 }, { 80000, 1 }, { 800000000, 1 }},
		{ { 10000*3, 4 }, { 80000*3, 1 }, { 200000000*9, 1 }},
		{ { 1, 1000 }, { 1, 8000 }, { 1, 8000000 }},
		{ { 1, 10000 }, { 1, 80000 }, { 1, 800000000 }},
		{ { 4, 10000*3 }, { 1, 80000*3 }, { 1, 200000000*9 }},

		{ { -10000*3, 4 }, { 80000*3, 1 }, { -200000000*9, 1 }},
		{ { 10000*3, 4 }, { -80000*3, 1 }, { -200000000*9, 1 }},
	};

	for (unsigned i = 0; i < ARRAY_SIZE(r); i++) {
		struct sr_rational res;

		int rc = sr_rational_mult(&res, &r[i][0], &r[i][1]);
		fail_unless(rc == SR_OK);
		fail_unless(sr_rational_eq(&res, &r[i][2]) == 1,
			"sr_rational_mult() failed: [%d] %ld/%lu != %ld/%lu.",
			i, res.p, res.q, r[i][2].p, r[i][2].q);
	}
}
END_TEST

START_TEST(test_div_rational)
{
	const struct sr_rational r[][3] = {
		/*   a    *    b    =    c   */
		{ { 1, 1 }, { 1, 1 }, { 1, 1 }},
		{ { 2, 1 }, { 1, 3 }, { 6, 1 }},
		{ { 1, 2 }, { 1, 2 }, { 1, 1 }},
		/* Test negative numbers */
		{ { -1, 2 }, { 1, 2 }, { -1, 1 }},
		{ { -1, 2 }, { -1, 2 }, { 1, 1 }},
		{ { -(1ll<<20), (1ll<<10) }, { -1, (1ll<<20) }, { (1ll<<30), 1 }},
		/* Test reduction */
		{ { INT32_MAX, (1ll<<12) }, { 1, (1<<2) }, { INT32_MAX, (1ll<<10) }},
		{ { INT64_MAX, (1ll<<63) }, { 1, (1<<3) }, { INT64_MAX, (1ll<<60) }},
		/* Test large numbers */
		{ {  (1ll<<40), (1ll<<10) }, {  1, (1ll<<30) }, { (1ll<<60), 1 }},
		{ { -(1ll<<40), (1ll<<10) }, { -1, (1ll<<30) }, { (1ll<<60), 1 }},

		{ { 10000*3, 4 }, { 1, 80000*3 }, { 200000000*9, 1 }},
		{ { 4, 10000*3 }, { 80000*3, 1 }, { 1, 200000000*9 }},

		{ { -10000*3, 4 }, { 1, 80000*3 }, { -200000000*9, 1 }},
		{ { 10000*3, 4 }, { -1, 80000*3 }, { -200000000*9, 1 }},
	};

	for (unsigned i = 0; i < ARRAY_SIZE(r); i++) {
		struct sr_rational res;

		int rc = sr_rational_div(&res, &r[i][0], &r[i][1]);
		fail_unless(rc == SR_OK);
		fail_unless(sr_rational_eq(&res, &r[i][2]) == 1,
			"sr_rational_mult() failed: [%d] %ld/%lu != %ld/%lu.",
			i, res.p, res.q, r[i][2].p, r[i][2].q);
	}

	{
		struct sr_rational res;
		int rc = sr_rational_div(&res, &r[0][0], &((struct sr_rational){ 0, 5 }));

		fail_unless(rc == SR_ERR_ARG);
	}
}
END_TEST

Suite *suite_analog(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("analog");

	tc = tcase_create("analog_to_float");
	tcase_add_test(tc, test_analog_to_float);
	tcase_add_test(tc, test_analog_to_float_null);
	tcase_add_test(tc, test_analog_si_prefix);
	tcase_add_test(tc, test_analog_si_prefix_null);
	tcase_add_test(tc, test_analog_unit_to_string);
	tcase_add_test(tc, test_analog_unit_to_string_null);
	tcase_add_test(tc, test_set_rational);
	tcase_add_test(tc, test_set_rational_null);
	tcase_add_test(tc, test_cmp_rational);
	tcase_add_test(tc, test_mult_rational);
	tcase_add_test(tc, test_div_rational);
	suite_add_tcase(s, tc);

	return s;
}
