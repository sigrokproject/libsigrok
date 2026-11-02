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
#include <stdlib.h>
#include <inttypes.h>
#include <check.h>
#include <libsigrok/libsigrok.h>
#include "lib.h"

/* Check whether at least one driver is available. */
START_TEST(test_driver_available)
{
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list(srtest_ctx);
	ck_assert_msg(drivers != NULL, "No drivers found.");
}
END_TEST

/* Check whether initializing all drivers works. */
START_TEST(test_driver_init_all)
{
	srtest_driver_init_all(srtest_ctx);
}
END_TEST

/*
 * Check whether setting a samplerate works.
 *
 * Additionally, this also checks whether SR_CONF_SAMPLERATE can be both
 * set and read back properly.
 */
#if 0
START_TEST(test_config_get_set_samplerate)
{
	/*
	 * Note: This currently only works for the demo driver.
	 *       For other drivers, a scan is needed and the respective
	 *       hardware must be attached to the host running the testsuite.
	 */
	srtest_check_samplerate(sr_ctx, "demo", SR_KHZ(19));
}
END_TEST
#endif

#ifdef HAVE_HW_DEMO
struct demo_counter_state {
	uint64_t samples;
	unsigned int channels;
	unsigned int end_packets;
};

static void demo_counter_feed(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	struct demo_counter_state *state = cb_data;
	const struct sr_datafeed_logic *logic;
	const uint8_t *data;
	uint64_t i;
	unsigned int unitsize, byte;
	uint8_t expected;

	(void)sdi;
	if (packet->type == SR_DF_END) {
		state->end_packets++;
		return;
	}
	if (packet->type != SR_DF_LOGIC)
		return;

	logic = packet->payload;
	unitsize = (state->channels + 7) / 8;
	ck_assert_int_eq(logic->unitsize, unitsize);
	ck_assert_int_eq(logic->length % unitsize, 0);
	data = logic->data;
	for (i = 0; i < logic->length; i++) {
		byte = i % unitsize;
		expected = (state->samples + i / unitsize) & 0xff;
		if (byte == unitsize - 1 && state->channels % 8)
			expected &= (1U << (state->channels % 8)) - 1;
		ck_assert_msg(data[i] == expected,
				"%u channels, sample %" PRIu64 ", byte %u: %02x != %02x",
				state->channels, state->samples + i / unitsize,
				byte, data[i], expected);
	}
	state->samples += logic->length / unitsize;
}

START_TEST(test_demo_incremental)
{
	static const unsigned int widths[] = { 1, 8, 9, 16, 17, 24, 25, 32 };
	struct demo_counter_state state = { 0, widths[_i], 0 };
	struct sr_dev_driver *driver;
	struct sr_dev_inst *sdi;
	struct sr_channel_group *cg;
	struct sr_session *session;
	struct sr_config options[2];
	GSList *scan_options = NULL, *devices;

	driver = srtest_driver_get("demo");
	srtest_driver_init(srtest_ctx, driver);
	options[0].key = SR_CONF_NUM_LOGIC_CHANNELS;
	options[0].data = g_variant_ref_sink(g_variant_new_int32(state.channels));
	options[1].key = SR_CONF_NUM_ANALOG_CHANNELS;
	options[1].data = g_variant_ref_sink(g_variant_new_int32(0));
	scan_options = g_slist_append(scan_options, &options[0]);
	scan_options = g_slist_append(scan_options, &options[1]);
	devices = sr_driver_scan(driver, scan_options);
	g_slist_free(scan_options);
	g_variant_unref(options[0].data);
	g_variant_unref(options[1].data);
	ck_assert_int_eq(g_slist_length(devices), 1);
	sdi = devices->data;
	g_slist_free(devices);
	cg = sr_dev_inst_channel_groups_get(sdi)->data;
	ck_assert_str_eq(cg->name, "Logic");
	ck_assert_int_eq(sr_dev_open(sdi), SR_OK);
	ck_assert_int_eq(sr_config_set(sdi, NULL, SR_CONF_SAMPLERATE,
			g_variant_new_uint64(SR_MHZ(1))), SR_OK);
	ck_assert_int_eq(sr_config_set(sdi, NULL, SR_CONF_LIMIT_SAMPLES,
			g_variant_new_uint64(8193)), SR_OK);
	ck_assert_int_eq(sr_config_set(sdi, cg, SR_CONF_PATTERN_MODE,
			g_variant_new_string("incremental")), SR_OK);
	ck_assert_int_eq(sr_session_new(srtest_ctx, &session), SR_OK);
	ck_assert_int_eq(sr_session_dev_add(session, sdi), SR_OK);
	ck_assert_int_eq(sr_session_datafeed_callback_add(session,
			demo_counter_feed, &state), SR_OK);
	ck_assert_int_eq(sr_session_start(session), SR_OK);
	ck_assert_int_eq(sr_session_run(session), SR_OK);
	ck_assert_msg(state.samples == 8193, "Unexpected sample count.");
	ck_assert_int_eq(state.end_packets, 1);
	ck_assert_int_eq(sr_session_destroy(session), SR_OK);
	ck_assert_int_eq(sr_dev_close(sdi), SR_OK);
}
END_TEST
#endif

Suite *suite_driver_all(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("driver-all");

	tc = tcase_create("config");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_driver_available);
	tcase_add_test(tc, test_driver_init_all);
	// TODO: Currently broken.
	// tcase_add_test(tc, test_config_get_set_samplerate);
	suite_add_tcase(s, tc);

#ifdef HAVE_HW_DEMO
	tc = tcase_create("demo");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_loop_test(tc, test_demo_incremental, 0, 8);
	suite_add_tcase(s, tc);
#endif

	return s;
}
