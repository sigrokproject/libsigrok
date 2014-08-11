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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <check.h>
#include <glib/gstdio.h>
#include "../include/libsigrok/libsigrok.h"
#include "lib.h"

#define FILENAME		"foo.dat"
#define MAX_FILESIZE		(1 * 1000000)

#define CHECK_ALL_LOW		0
#define CHECK_ALL_HIGH		1
#define CHECK_HELLO_WORLD	2

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

static void check_buf(const char *filename, GHashTable *param,
		const uint8_t *buf, int check, uint64_t samples,
		uint64_t *samplerate)
{
	int ret;
	struct sr_input *in;
	struct sr_input_format *in_format;
	struct sr_session *session;

	/* Initialize global variables for this run. */
	df_packet_counter = sample_counter = 0;
	have_seen_df_end = FALSE;
	logic_channellist = NULL;
	check_to_perform = check;
	expected_samples = samples;
	expected_samplerate = samplerate;

	in_format = srtest_input_get("binary");

	in = g_try_malloc0(sizeof(struct sr_input));
	fail_unless(in != NULL);

	in->format = in_format;
	in->param = param;

	srtest_buf_to_file(filename, buf, samples); /* Create a file. */

	ret = in->format->init(in, filename);
	fail_unless(ret == SR_OK, "Input format init error: %d", ret);
	
	sr_session_new(&session);
	sr_session_datafeed_callback_add(session, datafeed_in, NULL);
	sr_session_dev_add(session, in->sdi);
	in_format->loadfile(in, filename);
	sr_session_destroy(session);

	g_unlink(filename); /* Delete file again. */
}

START_TEST(test_input_binary_all_low)
{
	uint64_t i, samplerate;
	uint8_t *buf;
	GHashTable *param;

	buf = g_try_malloc0(MAX_FILESIZE);
	fail_unless(buf != NULL);

	param = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	fail_unless(param != NULL);
	g_hash_table_insert(param, g_strdup("samplerate"), g_strdup("1250"));
	samplerate = SR_HZ(1250);

	/* Check various filesizes, with/without specifying a samplerate. */
	check_buf(FILENAME, NULL, buf, CHECK_ALL_LOW, 0, NULL);
	check_buf(FILENAME, param, buf, CHECK_ALL_LOW, 0, &samplerate);
	for (i = 1; i < MAX_FILESIZE; i *= 3) {
		check_buf(FILENAME, NULL, buf, CHECK_ALL_LOW, i, NULL);
		check_buf(FILENAME, param, buf, CHECK_ALL_LOW, i, &samplerate);

	}

	g_hash_table_destroy(param);
	g_free(buf);
}
END_TEST

START_TEST(test_input_binary_all_high)
{
	uint64_t i;
	uint8_t *buf;

	buf = g_try_malloc(MAX_FILESIZE);
	memset(buf, 0xff, MAX_FILESIZE);

	check_buf(FILENAME, NULL, buf, CHECK_ALL_LOW, 0, NULL);
	for (i = 1; i < MAX_FILESIZE; i *= 3)
		check_buf(FILENAME, NULL, buf, CHECK_ALL_HIGH, i, NULL);

	g_free(buf);
}
END_TEST

START_TEST(test_input_binary_all_high_loop)
{
	uint8_t *buf;

	/* Note: _i is the loop variable from tcase_add_loop_test(). */

	buf = g_try_malloc((_i * 10) + 1);
	memset(buf, 0xff, _i * 10);

	check_buf(FILENAME, NULL, buf, CHECK_ALL_HIGH, _i * 10, NULL);

	g_free(buf);
}
END_TEST

START_TEST(test_input_binary_hello_world)
{
	uint64_t samplerate;
	uint8_t *buf;
	GHashTable *param;

	buf = (uint8_t *)g_strdup("Hello world");

	param = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	fail_unless(param != NULL);
	g_hash_table_insert(param, g_strdup("samplerate"), g_strdup("1250"));
	samplerate = SR_HZ(1250);

	/* Check with and without specifying a samplerate. */
	check_buf(FILENAME, NULL, buf, CHECK_HELLO_WORLD, 11, NULL);
	check_buf(FILENAME, param, buf, CHECK_HELLO_WORLD, 11, &samplerate);

	g_hash_table_destroy(param);
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
	tcase_add_loop_test(tc, test_input_binary_all_high_loop, 0, 10);
	tcase_add_test(tc, test_input_binary_hello_world);
	suite_add_tcase(s, tc);

	return s;
}
