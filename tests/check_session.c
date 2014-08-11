/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <stdlib.h>
#include <check.h>
#include "../include/libsigrok/libsigrok.h"
#include "lib.h"

static struct sr_context *sr_ctx;

static void setup(void)
{
	int ret;

	ret = sr_init(&sr_ctx);
	fail_unless(ret == SR_OK, "sr_init() failed: %d.", ret);
}

static void teardown(void)
{
	int ret;

	ret = sr_exit(sr_ctx);
	fail_unless(ret == SR_OK, "sr_exit() failed: %d.", ret);
}

/*
 * Check whether sr_session_new() works.
 * If it returns != SR_OK (or segfaults) this test will fail.
 */
START_TEST(test_session_new)
{
	int ret;
	struct sr_session *sess;

	ret = sr_session_new(&sess);
	fail_unless(ret == SR_OK, "sr_session_new() failed: %d.", ret);
	sr_session_destroy(sess);
}
END_TEST

/*
 * Check whether sr_session_new() fails for bogus parameters.
 * If it returns SR_OK (or segfaults) this test will fail.
 */
START_TEST(test_session_new_bogus)
{
	int ret;

	ret = sr_session_new(NULL);
	fail_unless(ret != SR_OK, "sr_session_new(NULL) worked.");
}
END_TEST

/*
 * Check whether multiple sr_session_new() calls work.
 * If any call returns != SR_OK (or segfaults) this test will fail.
 */
START_TEST(test_session_new_multiple)
{
	int ret;
	struct sr_session *sess1, *sess2, *sess3;

	sess1 = sess2 = sess3 = NULL;

	/* Multiple sr_session_new() calls must work. */
	ret = sr_session_new(&sess1);
	fail_unless(ret == SR_OK, "sr_session_new() 1 failed: %d.", ret);
	ret = sr_session_new(&sess2);
	fail_unless(ret == SR_OK, "sr_session_new() 2 failed: %d.", ret);
	ret = sr_session_new(&sess3);
	fail_unless(ret == SR_OK, "sr_session_new() 3 failed: %d.", ret);

	/* The returned session pointers must all be non-NULL. */
	fail_unless(sess1 != NULL);
	fail_unless(sess2 != NULL);
	fail_unless(sess3 != NULL);

	/* The returned session pointers must not be the same. */
	fail_unless(sess1 != sess2);
	fail_unless(sess1 != sess3);
	fail_unless(sess2 != sess3);

	/* Destroying any of the sessions must work. */
	ret = sr_session_destroy(sess1);
	fail_unless(ret == SR_OK, "sr_session_destroy() 1 failed: %d.", ret);
	ret = sr_session_destroy(sess2);
	fail_unless(ret == SR_OK, "sr_session_destroy() 2 failed: %d.", ret);
	ret = sr_session_destroy(sess3);
	fail_unless(ret == SR_OK, "sr_session_destroy() 3 failed: %d.", ret);
}
END_TEST

/*
 * Check whether sr_session_destroy() works.
 * If it returns != SR_OK (or segfaults) this test will fail.
 */
START_TEST(test_session_destroy)
{
	int ret;
	struct sr_session *sess;

	sr_session_new(&sess);
	ret = sr_session_destroy(sess);
	fail_unless(ret == SR_OK, "sr_session_destroy() failed: %d.", ret);
}
END_TEST

/*
 * Check whether sr_session_destroy() fails for bogus sessions.
 * If it returns SR_OK (or segfaults) this test will fail.
 */
START_TEST(test_session_destroy_bogus)
{
	int ret;

	ret = sr_session_destroy(NULL);
	fail_unless(ret != SR_OK, "sr_session_destroy() worked.");
}
END_TEST

Suite *suite_session(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("session");

	tc = tcase_create("new_destroy");
	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_add_test(tc, test_session_new);
	tcase_add_test(tc, test_session_new_bogus);
	tcase_add_test(tc, test_session_new_multiple);
	tcase_add_test(tc, test_session_destroy);
	tcase_add_test(tc, test_session_destroy_bogus);
	suite_add_tcase(s, tc);

	return s;
}
