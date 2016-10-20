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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <check.h>
#include <libsigrok/libsigrok.h>
#include "lib.h"

/*
 * Check whether sr_session_new() works.
 * If it returns != SR_OK (or segfaults) this test will fail.
 */
START_TEST(test_session_new)
{
	int ret;
	struct sr_session *sess;

	ret = sr_session_new(srtest_ctx, &sess);
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

	ret = sr_session_new(srtest_ctx, NULL);
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
	ret = sr_session_new(srtest_ctx, &sess1);
	fail_unless(ret == SR_OK, "sr_session_new() 1 failed: %d.", ret);
	ret = sr_session_new(srtest_ctx, &sess2);
	fail_unless(ret == SR_OK, "sr_session_new() 2 failed: %d.", ret);
	ret = sr_session_new(srtest_ctx, &sess3);
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

	sr_session_new(srtest_ctx, &sess);
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

START_TEST(test_session_trigger_set_get)
{
	int ret;
	struct sr_session *sess;
	struct sr_trigger *t1, *t2;

	sr_session_new(srtest_ctx, &sess);
	t1 = sr_trigger_new("T1");

	/* Set a trigger and see if getting it works OK. */
	ret = sr_session_trigger_set(sess, t1);
	fail_unless(ret == SR_OK);
	t2 = sr_session_trigger_get(sess);
	fail_unless(t2 != NULL);
	fail_unless(t1 == t2);
	fail_unless(g_slist_length(t1->stages) == g_slist_length(t2->stages));
	fail_unless(!strcmp(t1->name, t2->name));

	sr_session_destroy(sess);
}
END_TEST

START_TEST(test_session_trigger_set_get_null)
{
	int ret;
	struct sr_session *sess;
	struct sr_trigger *t;

	sr_session_new(srtest_ctx, &sess);

	/* Adding a NULL trigger is allowed. */
	ret = sr_session_trigger_set(sess, NULL);
	fail_unless(ret == SR_OK);
	t = sr_session_trigger_get(sess);
	fail_unless(t == NULL);

	sr_session_destroy(sess);
}
END_TEST

START_TEST(test_session_trigger_set_null)
{
	int ret;
	struct sr_trigger *t;

	t = sr_trigger_new("T1");

	/* NULL session, must not segfault. */
	ret = sr_session_trigger_set(NULL, t);
	fail_unless(ret == SR_ERR_ARG);

	/* NULL session and NULL trigger, must not segfault. */
	ret = sr_session_trigger_set(NULL, NULL);
	fail_unless(ret == SR_ERR_ARG);
}
END_TEST

START_TEST(test_session_trigger_get_null)
{
	struct sr_trigger *t;

	/* NULL session, must not segfault. */
	t = sr_session_trigger_get(NULL);
	fail_unless(t == NULL);
}
END_TEST

Suite *suite_session(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("session");

	tc = tcase_create("new_destroy");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_session_new);
	tcase_add_test(tc, test_session_new_bogus);
	tcase_add_test(tc, test_session_new_multiple);
	tcase_add_test(tc, test_session_destroy);
	tcase_add_test(tc, test_session_destroy_bogus);
	suite_add_tcase(s, tc);

	tc = tcase_create("trigger");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_session_trigger_set_get);
	tcase_add_test(tc, test_session_trigger_set_get_null);
	tcase_add_test(tc, test_session_trigger_set_null);
	tcase_add_test(tc, test_session_trigger_get_null);
	suite_add_tcase(s, tc);

	return s;
}
