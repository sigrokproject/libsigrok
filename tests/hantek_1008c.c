/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 VK4TMZ
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

#include <config.h>
#include <stdlib.h>
#include <check.h>

#include "../src/hardware/hantek-1008c/protocol.h"

START_TEST(test_acquisition_geometry)
{
	static const unsigned int triggered[] = { 1, 2, 4, 4, 6, 6, 8, 8 };
	unsigned int channels;

	ck_assert_uint_eq(h1008c_acquisition_width(0), 0);
	ck_assert_uint_eq(h1008c_acquisition_width(9), 0);
	for (channels = 1; channels <= H1008C_NUM_HW_CHANNELS; channels++) {
		ck_assert_uint_eq(h1008c_acquisition_width(channels),
			triggered[channels - 1]);
		ck_assert_uint_eq(h1008c_rate_divisor(H1008C_MODE_TRIGGERED,
			channels), triggered[channels - 1]);
		ck_assert_uint_eq(h1008c_rate_divisor(H1008C_MODE_SCAN,
			channels), channels);
		ck_assert_uint_eq(h1008c_rate_divisor(H1008C_MODE_ROLL,
			channels), channels + 1);
	}
}
END_TEST

static void put_word(uint8_t *dest, uint16_t value)
{
	dest[0] = value & 0xff;
	dest[1] = value >> 8;
}

START_TEST(test_scan_fragmented_rows)
{
	uint8_t raw[12], carry[16] = { 0 };
	float *samples;
	size_t carry_len = 0, rows;
	int ret;

	put_word(raw + 0, 0x0065);  /* sparse lane CH1 identity: 101 */
	put_word(raw + 2, 0x01f9);  /* sparse lane CH5 identity: 505 */
	put_word(raw + 4, 0x0328);  /* sparse lane CH8 identity: 808 */
	put_word(raw + 6, 0xf066);  /* upper bits must be masked */
	put_word(raw + 8, 0x01fa);
	put_word(raw + 10, 0x0329);

	ret = h1008c_decode_stream_rows(raw, 5, 3, 0,
		carry, &carry_len, sizeof(carry), &samples, &rows);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert_ptr_null(samples);
	ck_assert_uint_eq(rows, 0);
	ck_assert_uint_eq(carry_len, 5);

	ret = h1008c_decode_stream_rows(raw + 5, sizeof(raw) - 5, 3, 0,
		carry, &carry_len, sizeof(carry), &samples, &rows);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert_uint_eq(rows, 2);
	ck_assert_uint_eq(carry_len, 0);
	ck_assert_float_eq(samples[0], 101.0f);
	ck_assert_float_eq(samples[1], 505.0f);
	ck_assert_float_eq(samples[2], 808.0f);
	ck_assert_float_eq(samples[3], 102.0f);
	ck_assert_float_eq(samples[4], 506.0f);
	ck_assert_float_eq(samples[5], 809.0f);
	g_free(samples);
}
END_TEST

START_TEST(test_roll_aux_removed_across_fragments)
{
	uint8_t raw[16], carry[18] = { 0 };
	float *samples;
	size_t carry_len = 0, rows;
	int ret;

	put_word(raw + 0, 101);
	put_word(raw + 2, 505);
	put_word(raw + 4, 808);
	put_word(raw + 6, 1720);  /* AUX */
	put_word(raw + 8, 102);
	put_word(raw + 10, 506);
	put_word(raw + 12, 809);
	put_word(raw + 14, 1721); /* AUX */

	ret = h1008c_decode_stream_rows(raw, 11, 3, 1,
		carry, &carry_len, sizeof(carry), &samples, &rows);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert_uint_eq(rows, 1);
	ck_assert_uint_eq(carry_len, 3);
	ck_assert_float_eq(samples[0], 101.0f);
	ck_assert_float_eq(samples[1], 505.0f);
	ck_assert_float_eq(samples[2], 808.0f);
	g_free(samples);

	ret = h1008c_decode_stream_rows(raw + 11, sizeof(raw) - 11, 3, 1,
		carry, &carry_len, sizeof(carry), &samples, &rows);
	ck_assert_int_eq(ret, SR_OK);
	ck_assert_uint_eq(rows, 1);
	ck_assert_uint_eq(carry_len, 0);
	ck_assert_float_eq(samples[0], 102.0f);
	ck_assert_float_eq(samples[1], 506.0f);
	ck_assert_float_eq(samples[2], 809.0f);
	g_free(samples);
}
END_TEST

START_TEST(test_stream_row_argument_guards)
{
	uint8_t raw[2] = { 0 }, carry[2] = { 0 };
	float *samples;
	size_t carry_len = 0, rows;

	ck_assert_int_eq(h1008c_decode_stream_rows(raw, sizeof(raw), 0, 0,
		carry, &carry_len, sizeof(carry), &samples, &rows), SR_ERR_ARG);
	ck_assert_int_eq(h1008c_decode_stream_rows(raw, sizeof(raw), 2, 0,
		carry, &carry_len, sizeof(carry), &samples, &rows), SR_ERR_ARG);
}
END_TEST

START_TEST(test_range_names)
{
	ck_assert_str_eq(h1008c_range_name(1), "Narrow");
	ck_assert_str_eq(h1008c_range_name(2), "Medium");
	ck_assert_str_eq(h1008c_range_name(3), "Wide");
	ck_assert_ptr_null(h1008c_range_name(0));
	ck_assert_ptr_null(h1008c_range_name(4));
	ck_assert_int_eq(h1008c_range_id("Narrow"), 1);
	ck_assert_int_eq(h1008c_range_id("Medium"), 2);
	ck_assert_int_eq(h1008c_range_id("Wide"), 3);
	ck_assert_int_eq(h1008c_range_id("A2=03"), -1);
}
END_TEST

START_TEST(test_trigger_source_names)
{
	unsigned int channel;
	char name[8];

	ck_assert_str_eq(h1008c_trigger_source_name(
		H1008C_TRIGGER_SOURCE_NONE), "None");
	ck_assert_int_eq(h1008c_trigger_source_id("None"),
		H1008C_TRIGGER_SOURCE_NONE);
	for (channel = 0; channel < H1008C_NUM_HW_CHANNELS; channel++) {
		g_snprintf(name, sizeof(name), "CH%u", channel + 1);
		ck_assert_str_eq(h1008c_trigger_source_name(channel), name);
		ck_assert_int_eq(h1008c_trigger_source_id(name), channel);
	}
	ck_assert_ptr_null(h1008c_trigger_source_name(H1008C_NUM_HW_CHANNELS));
	ck_assert_int_eq(h1008c_trigger_source_id("CH9"), -1);
}
END_TEST

START_TEST(test_trigger_level_voltage_conversion)
{
	uint16_t raw_adc;

	ck_assert_int_eq(h1008c_trigger_level_to_adc(
		0.0, 2004.929, 0.01, &raw_adc), SR_OK);
	ck_assert_uint_eq(raw_adc, 2005);
	ck_assert_int_eq(h1008c_trigger_level_to_adc(
		0.25, 2004.929, 0.01, &raw_adc), SR_OK);
	ck_assert_uint_eq(raw_adc, 2030);
	ck_assert_int_eq(h1008c_trigger_level_to_adc(
		-0.10, 1985.888, 0.00125, &raw_adc), SR_OK);
	ck_assert_uint_eq(raw_adc, 1906);
	ck_assert_int_eq(h1008c_trigger_level_to_adc(
		25.0, 2004.929, 0.01, &raw_adc), SR_ERR_ARG);
	ck_assert_int_eq(h1008c_trigger_level_to_adc(
		0.0, 2004.929, 0.0, &raw_adc), SR_ERR_ARG);
	ck_assert_int_eq(h1008c_trigger_level_to_adc(
		0.0, 2004.929, 0.01, NULL), SR_ERR_ARG);
}
END_TEST

static void check_rates(enum h1008c_acquisition_mode mode,
		const uint64_t *rates, const uint8_t *a3, size_t count)
{
	const struct h1008c_rate *selected;
	struct h1008c_rate rate;
	unsigned int channels, divisor;
	size_t i;

	ck_assert_uint_eq(h1008c_rate_count(mode), count);
	for (i = 0; i < count; i++) {
		ck_assert_int_eq(h1008c_rate_get(mode, i, &rate), SR_OK);
		ck_assert_uint_eq(rate.samplerate, rates[i]);
		ck_assert_uint_eq(rate.a3, a3[i]);
		ck_assert_int_eq(rate.mode, mode);
		for (channels = 1; channels <= H1008C_NUM_HW_CHANNELS; channels++) {
			divisor = h1008c_rate_divisor(mode, channels);
			if (mode != H1008C_MODE_TRIGGERED && rates[i] < divisor)
				continue;
			selected = h1008c_find_effective_rate(
				rates[i] / divisor, divisor, mode);
			ck_assert_ptr_nonnull(selected);
			ck_assert_uint_eq(selected->samplerate, rates[i]);
			ck_assert_uint_eq(selected->a3, a3[i]);
		}
	}
	ck_assert_int_eq(h1008c_rate_get(mode, count, &rate), SR_ERR_ARG);
	ck_assert_ptr_null(h1008c_find_effective_rate(
		UINT64_C(123456789), 1, mode));
}

START_TEST(test_validated_rate_tables)
{
	static const uint64_t triggered_rates[] = {
		2000, 4000, 8000, 20000, 40000, 80000,
		200000, 400000, 800000, 2400000,
	};
	static const uint8_t triggered_a3[] = {
		0x19, 0x18, 0x17, 0x16, 0x15, 0x14,
		0x13, 0x12, 0x11, 0x0f,
	};
	static const uint64_t scan_rates[] = {
		2, 4, 8, 20, 40, 80, 200, 400, 800,
	};
	static const uint8_t scan_a3[] = {
		0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a,
	};
	static const uint64_t roll_rates[] = {
		2, 4, 10, 18, 46, 100, 200, 402, 802, 2006, 4012,
	};
	static const uint8_t roll_a3[] = {
		0x22, 0x21, 0x20, 0x1f, 0x1e, 0x1d,
		0x1c, 0x1b, 0x1a, 0x19, 0x18,
	};

	check_rates(H1008C_MODE_TRIGGERED, triggered_rates, triggered_a3,
		ARRAY_SIZE(triggered_rates));
	check_rates(H1008C_MODE_SCAN, scan_rates, scan_a3,
		ARRAY_SIZE(scan_rates));
	check_rates(H1008C_MODE_ROLL, roll_rates, roll_a3,
		ARRAY_SIZE(roll_rates));
}
END_TEST

int main(void)
{
	Suite *suite;
	TCase *tc;
	SRunner *runner;
	int failed;

	suite = suite_create("hantek-1008c");
	tc = tcase_create("model");
	tcase_add_test(tc, test_acquisition_geometry);
	tcase_add_test(tc, test_range_names);
	tcase_add_test(tc, test_trigger_source_names);
	tcase_add_test(tc, test_trigger_level_voltage_conversion);
	tcase_add_test(tc, test_validated_rate_tables);
	tcase_add_test(tc, test_scan_fragmented_rows);
	tcase_add_test(tc, test_roll_aux_removed_across_fragments);
	tcase_add_test(tc, test_stream_row_argument_guards);
	suite_add_tcase(suite, tc);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_VERBOSE);
	failed = srunner_ntests_failed(runner);
	srunner_free(runner);
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
