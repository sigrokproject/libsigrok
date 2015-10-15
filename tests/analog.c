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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
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

#if 0
START_TEST(test_analog_float_to_string)
{
	int ret;
	unsigned int i;
	char *result;
	const char *r[] = {"3", "3.1", "3.14", "3.145", "3.1415", "3.15159"};

	for (i = 0; i < ARRAY_SIZE(r); i++) {
		ret = sr_analog_float_to_string(G_PI, i, &result);
		fail_unless(ret == SR_OK);
		fail_unless(result != NULL);
		fail_unless(!strcmp(result, r[i]), "%s != %s", result, r[i]);
		g_free(result);
	}
}
END_TEST
#endif

START_TEST(test_analog_float_to_string_null)
{
	int ret;

	ret = sr_analog_float_to_string(0, 0, NULL);
	fail_unless(ret == SR_ERR_ARG);
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
	const char *r[] = {" V RMS"};

	sr_analog_init_(&analog, &encoding, &meaning, &spec, 3);

	for (i = -1; i < ARRAY_SIZE(r); i++) {
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

Suite *suite_analog(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("analog");

	tc = tcase_create("analog_to_float");
	tcase_add_test(tc, test_analog_to_float);
	tcase_add_test(tc, test_analog_to_float_null);
#if 0
	tcase_add_test(tc, test_analog_float_to_string);
#endif
	tcase_add_test(tc, test_analog_float_to_string_null);
	tcase_add_test(tc, test_analog_unit_to_string);
	tcase_add_test(tc, test_analog_unit_to_string_null);
	tcase_add_test(tc, test_set_rational);
	tcase_add_test(tc, test_set_rational_null);
	suite_add_tcase(s, tc);

	return s;
}
