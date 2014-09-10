/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013-2014 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <check.h>
#include <glib/gstdio.h>
#include "../include/libsigrok/libsigrok.h"
#include "lib.h"

#define BUFSIZE 1000000

enum {
	CHECK_ALL_LOW,
	CHECK_ALL_HIGH,
	CHECK_HELLO_WORLD,
};

static uint64_t df_packet_counter = 0, sample_counter = 0;
static gboolean have_seen_df_end = FALSE;
static GArray *logic_channellist = NULL;
static int check_to_perform;
static uint64_t expected_samples;
static uint64_t *expected_samplerate;

static void check_all_low(const struct sr_datafeed_logic *logic)
{
	uint64_t i;
	uint8_t *data;

	for (i = 0; i < logic->length; i++) {
		data = logic->data;
		if (data[i * logic->unitsize] != 0)
			fail("Logic data was not all-0x00.");
	}
}

static void check_all_high(const struct sr_datafeed_logic *logic)
{
	uint64_t i;
	uint8_t *data;

	for (i = 0; i < logic->length; i++) {
		data = logic->data;
		if (data[i * logic->unitsize] != 0xff)
			fail("Logic data was not all-0xff.");
	}
}

static void check_hello_world(const struct sr_datafeed_logic *logic)
{
	uint64_t i;
	uint8_t *data, b;
	const char *h = "Hello world";

	for (i = 0; i < logic->length; i++) {
		data = logic->data;
		b = data[sample_counter + i];
		if (b != h[sample_counter + i])
			fail("Logic data was not 'Hello world'.");
	}
}

static void datafeed_in(const struct sr_dev_inst *sdi,
	const struct sr_datafeed_packet *packet, void *cb_data)
{
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	struct sr_config *src;
	uint64_t samplerate, sample_interval;
	GSList *l;
	const void *p;

	(void)cb_data;

	fail_unless(sdi != NULL);
	fail_unless(packet != NULL);

	if (df_packet_counter++ == 0)
		fail_unless(packet->type == SR_DF_HEADER,
			    "The first packet must be an SR_DF_HEADER.");

	if (have_seen_df_end)
		fail("There must be no packets after an SR_DF_END, but we "
		     "received a packet of type %d.", packet->type);

	p = packet->payload;

	switch (packet->type) {
	case SR_DF_HEADER:
		// g_debug("Received SR_DF_HEADER.");
		// fail_unless(p != NULL, "SR_DF_HEADER payload was NULL.");

		logic_channellist = srtest_get_enabled_logic_channels(sdi);
		fail_unless(logic_channellist != NULL);
		fail_unless(logic_channellist->len != 0);
		// g_debug("Enabled channels: %d.", logic_channellist->len);
		break;
	case SR_DF_META:
		// g_debug("Received SR_DF_META.");

		meta = packet->payload;
		fail_unless(p != NULL, "SR_DF_META payload was NULL.");

		for (l = meta->config; l; l = l->next) {
			src = l->data;
			// g_debug("Got meta key: %d.", src->key);
			switch (src->key) {
			case SR_CONF_SAMPLERATE:
				samplerate = g_variant_get_uint64(src->data);
				if (!expected_samplerate)
					break;
				fail_unless(samplerate == *expected_samplerate,
					    "Expected samplerate=%" PRIu64 ", "
					    "got %" PRIu64 "", samplerate,
					    *expected_samplerate);
				// g_debug("samplerate = %" PRIu64 " Hz.",
				// 	samplerate);
				break;
			case SR_CONF_SAMPLE_INTERVAL:
				sample_interval = g_variant_get_uint64(src->data);
				(void)sample_interval;
				// g_debug("sample interval = %" PRIu64 " ms.",
				// 	sample_interval);
				break;
			default:
				/* Unknown metadata is not an error. */
				g_debug("Got unknown meta key: %d.", src->key);
				break;
			}
		}
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		fail_unless(p != NULL, "SR_DF_LOGIC payload was NULL.");

		// g_debug("Received SR_DF_LOGIC (%" PRIu64 " bytes, "
		// 	"unitsize %d).", logic->length, logic->unitsize);

		if (check_to_perform == CHECK_ALL_LOW)
			check_all_low(logic);
		else if (check_to_perform == CHECK_ALL_HIGH)
			check_all_high(logic);
		else if (check_to_perform == CHECK_HELLO_WORLD)
			check_hello_world(logic);

		sample_counter += logic->length / logic->unitsize;

		break;
	case SR_DF_END:
		// g_debug("Received SR_DF_END.");
		// fail_unless(p != NULL, "SR_DF_END payload was NULL.");
		have_seen_df_end = TRUE;
		if (sample_counter != expected_samples)
			fail("Expected %" PRIu64 " samples, got %" PRIu64 "",
			     expected_samples, sample_counter);
		break;
	default:
		/*
		 * Note: The binary input format doesn't support SR_DF_TRIGGER
		 * and some other types, those should yield an error.
		 */
		fail("Invalid packet type: %d.", packet->type);
		break;
	}
}

static void check_buf(GHashTable *options, const uint8_t *buf, int check,
		uint64_t samples, uint64_t *samplerate)
{
	int ret;
	struct sr_input *in;
	const struct sr_input_module *imod;
	struct sr_session *session;
	struct sr_dev_inst *sdi;
	GString *gbuf;

	/* Initialize global variables for this run. */
	df_packet_counter = sample_counter = 0;
	have_seen_df_end = FALSE;
	logic_channellist = NULL;
	check_to_perform = check;
	expected_samples = samples;
	expected_samplerate = samplerate;

	gbuf = g_string_new_len((gchar *)buf, (gssize)samples);

	imod = sr_input_find("binary");
	fail_unless(imod != NULL, "Failed to find input module.");

	in = sr_input_new(imod, options);
	fail_unless(in != NULL, "Failed to create input instance.");

	sdi = sr_input_dev_inst_get(in);
	fail_unless(sdi != NULL, "Failed to get device instance.");

	sr_session_new(&session);
	sr_session_datafeed_callback_add(session, datafeed_in, NULL);
	sr_session_dev_add(session, sdi);

	ret = sr_input_send(in, gbuf);
	fail_unless(ret == SR_OK, "sr_input_send() error: %d", ret);

	ret = sr_input_free(in);
	fail_unless(ret == SR_OK, "Failed to free input instance: %d", ret);

	sr_session_destroy(session);

	g_string_free(gbuf, TRUE);
}

START_TEST(test_input_binary_all_low)
{
	uint64_t i, samplerate;
	GHashTable *options;
	uint8_t *buf;
	GVariant *gvar;

	buf = g_malloc0(BUFSIZE);

	gvar = g_variant_new_uint64(1250);
	options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			(GDestroyNotify)g_variant_unref);
	g_hash_table_insert(options, g_strdup("samplerate"),
		g_variant_ref_sink(gvar));
	samplerate = SR_HZ(1250);

	/* Check various filesizes, with/without specifying a samplerate. */
	check_buf(NULL, buf, CHECK_ALL_LOW, 0, NULL);
	check_buf(options, buf, CHECK_ALL_LOW, 0, &samplerate);
	for (i = 1; i < BUFSIZE; i *= 3) {
		check_buf(NULL, buf, CHECK_ALL_LOW, i, NULL);
		check_buf(options, buf, CHECK_ALL_LOW, i, &samplerate);
	}

	g_hash_table_destroy(options);
	g_free(buf);
}
END_TEST

START_TEST(test_input_binary_all_high)
{
	uint64_t i;
	uint8_t *buf;

	buf = g_malloc(BUFSIZE);
	memset(buf, 0xff, BUFSIZE);

	check_buf(NULL, buf, CHECK_ALL_HIGH, 0, NULL);
	for (i = 1; i < BUFSIZE; i *= 3)
		check_buf(NULL, buf, CHECK_ALL_HIGH, i, NULL);

	g_free(buf);
}
END_TEST

START_TEST(test_input_binary_all_high_loop)
{
	uint8_t *buf;
	uint64_t bufsize;

	/* Note: _i is the loop variable from tcase_add_loop_test(). */

	bufsize = (_i * 10);
	buf = g_malloc(BUFSIZE);
	memset(buf, 0xff, BUFSIZE);

	check_buf(NULL, buf, CHECK_ALL_HIGH, bufsize, NULL);

	g_free(buf);
}
END_TEST

START_TEST(test_input_binary_hello_world)
{
	uint64_t samplerate;
	uint8_t *buf;
	GHashTable *options;
	GVariant *gvar;

	buf = (uint8_t *)g_strdup("Hello world");

	gvar = g_variant_new_uint64(1250);
	options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
			(GDestroyNotify)g_variant_unref);
	g_hash_table_insert(options, g_strdup("samplerate"),
			g_variant_ref_sink(gvar));
	samplerate = SR_HZ(1250);

	/* Check with and without specifying a samplerate. */
	check_buf(NULL, buf, CHECK_HELLO_WORLD, 11, NULL);
	check_buf(options, buf, CHECK_HELLO_WORLD, 11, &samplerate);

	g_hash_table_destroy(options);
	g_free(buf);
}
END_TEST

Suite *suite_input_binary(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("input-binary");

	tc = tcase_create("basic");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_input_binary_all_low);
	tcase_add_test(tc, test_input_binary_all_high);
	tcase_add_loop_test(tc, test_input_binary_all_high_loop, 1, 10);
	tcase_add_test(tc, test_input_binary_hello_world);
	suite_add_tcase(s, tc);

	return s;
}
