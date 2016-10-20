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
#include <stdio.h>
#include <stdlib.h>
#include <check.h>
#include <libsigrok/libsigrok.h>
#include "lib.h"

/* Test lots of triggers/stages/matches/channels */
#define NUM_TRIGGERS 70
#define NUM_STAGES 30
#define NUM_MATCHES 70
#define NUM_CHANNELS NUM_MATCHES

/* Check whether creating/freeing triggers with valid names works. */
START_TEST(test_trigger_new_free)
{
	int i;
	struct sr_trigger *t[NUM_TRIGGERS];
	char name[10];

	/* Create a few triggers with a valid name. */
	for (i = 0; i < NUM_TRIGGERS; i++) {
		sprintf((char *)&name, "T%d", i);
		t[i] = sr_trigger_new((const char *)&name);
		fail_unless(t[i] != NULL);
		fail_unless(!strcmp(t[i]->name, (const char *)&name));
		fail_unless(t[i]->stages == NULL);
	}

	/* Free the triggers again (must not segfault). */
	for (i = 0; i < NUM_TRIGGERS; i++)
		sr_trigger_free(t[i]);
}
END_TEST

/* Check whether creating/freeing triggers with NULL names works. */
START_TEST(test_trigger_new_free_null)
{
	int i;
	struct sr_trigger *t[NUM_TRIGGERS];

	/* Create a few triggers with a NULL name (which is allowed). */
	for (i = 0; i < NUM_TRIGGERS; i++) {
		t[i] = sr_trigger_new(NULL);
		fail_unless(t[i] != NULL);
		fail_unless(t[i]->name == NULL);
		fail_unless(t[i]->stages == NULL);
	}

	/* Free the triggers again (must not segfault). */
	for (i = 0; i < NUM_TRIGGERS; i++)
		sr_trigger_free(t[i]);
}
END_TEST

/* Check whether sr_trigger_free(NULL) works without segfaulting. */
START_TEST(test_trigger_free_null)
{
	sr_trigger_free(NULL);
}
END_TEST

/* Check whether creating/freeing triggers with stages works. */
START_TEST(test_trigger_stage_add)
{
	int i, j;
	struct sr_trigger *t[NUM_TRIGGERS];
	struct sr_trigger_stage *s[NUM_STAGES];

	/* Create a few triggers with a valid name. */
	for (i = 0; i < NUM_TRIGGERS; i++) {
		t[i] = sr_trigger_new("T");

		/* Add a bunch of trigger stages to this trigger. */
		for (j = 0; j < NUM_STAGES; j++) {
			s[j] = sr_trigger_stage_add(t[i]);
			fail_unless(s[j] != NULL);
			fail_unless(t[i]->stages != NULL);
			fail_unless((int)g_slist_length(t[i]->stages) == (j + 1));
			fail_unless(s[j]->stage == j);
			fail_unless(s[j]->matches == NULL);
		}
	}

	/* Free the triggers again (must not segfault). */
	for (i = 0; i < NUM_TRIGGERS; i++)
		sr_trigger_free(t[i]);
}
END_TEST

/* Check whether creating NULL trigger stages fails (as it should). */
START_TEST(test_trigger_stage_add_null)
{
	/* Should not segfault, but rather return NULL. */
	fail_unless(sr_trigger_stage_add(NULL) == NULL);
}
END_TEST

/* Check whether creating/freeing triggers with matches works. */
START_TEST(test_trigger_match_add)
{
	int i, j, k, tm, ret;
	struct sr_trigger *t[NUM_TRIGGERS];
	struct sr_trigger_stage *s[NUM_STAGES];
	struct sr_channel *chl[NUM_CHANNELS];
	struct sr_channel *cha[NUM_CHANNELS];
	char name[10];

	/* Create a bunch of logic and analog channels. */
	for (i = 0; i < NUM_CHANNELS; i++) {
		sprintf((char *)&name, "L%d", i);
		chl[i] = g_malloc0(sizeof(struct sr_channel));
		chl[i]->index = i;
		chl[i]->type = SR_CHANNEL_LOGIC;
		chl[i]->enabled = TRUE;
		chl[i]->name = g_strdup((const char *)&name);

		sprintf((char *)&name, "A%d", i);
		cha[i] = g_malloc0(sizeof(struct sr_channel));
		cha[i]->index = i;
		cha[i]->type = SR_CHANNEL_ANALOG;
		cha[i]->enabled = TRUE;
		cha[i]->name = g_strdup((const char *)&name);
	}

	/* Create a few triggers with a valid name. */
	for (i = 0; i < NUM_TRIGGERS; i++) {
		t[i] = sr_trigger_new("T");

		/* Add a bunch of trigger stages to this trigger. */
		for (j = 0; j < NUM_STAGES; j++) {
			s[j] = sr_trigger_stage_add(t[i]);

			/* Add a bunch of matches to this stage. */
			for (k = 0; k < NUM_MATCHES; k++) {
				/* Logic channel matches. */
				tm = 1 + (k % 5); /* *_ZERO .. *_EDGE */
				ret = sr_trigger_match_add(s[j], chl[k], tm, 0);
				fail_unless(ret == SR_OK);

				/* Analog channel matches. */
				tm = 3 + (k % 4); /* *_RISING .. *_UNDER */
				ret = sr_trigger_match_add(s[j], cha[k],
					tm, ((rand() % 500) - 500) * 1.739);
				fail_unless(ret == SR_OK);
			}
		}
	}

	/* Free the triggers again (must not segfault). */
	for (i = 0; i < NUM_TRIGGERS; i++)
		sr_trigger_free(t[i]);

	/* Free the channels. */
	for (i = 0; i < NUM_CHANNELS; i++) {
		g_free(chl[i]->name);
		g_free(chl[i]);
		g_free(cha[i]->name);
		g_free(cha[i]);
	}
}
END_TEST

/* Check whether trigger_match_add() copes well with incorrect input. */
START_TEST(test_trigger_match_add_bogus)
{
	int ret;
	struct sr_trigger *t;
	struct sr_trigger_stage *s, *sl;
	struct sr_channel *chl, *cha;

	t = sr_trigger_new("T");
	s = sr_trigger_stage_add(t);
	chl = g_malloc0(sizeof(struct sr_channel));
	chl->index = 0;
	chl->type = SR_CHANNEL_LOGIC;
	chl->enabled = TRUE;
	chl->name = g_strdup("L0");
	cha = g_malloc0(sizeof(struct sr_channel));
	cha->index = 1;
	cha->type = SR_CHANNEL_ANALOG;
	cha->enabled = TRUE;
	cha->name = g_strdup("A0");

	/* Initially we have no matches at all. */
	sl = t->stages->data;
	fail_unless(g_slist_length(sl->matches) == 0);

	/* NULL stage */
	ret = sr_trigger_match_add(NULL, chl, SR_TRIGGER_ZERO, 0);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);

	/* NULL channel */
	ret = sr_trigger_match_add(s, NULL, SR_TRIGGER_ZERO, 0);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);

	/* Invalid trigger matches for logic channels. */
	ret = sr_trigger_match_add(s, chl, SR_TRIGGER_OVER, 0);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);
	ret = sr_trigger_match_add(s, chl, SR_TRIGGER_UNDER, 0);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);

	/* Invalid trigger matches for analog channels. */
	ret = sr_trigger_match_add(s, cha, SR_TRIGGER_ZERO, 9.4);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);
	ret = sr_trigger_match_add(s, cha, SR_TRIGGER_ONE, -9.4);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);

	/* Invalid channel type. */
	chl->type = -1;
	ret = sr_trigger_match_add(s, chl, SR_TRIGGER_ZERO, 0);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);
	chl->type = 270;
	ret = sr_trigger_match_add(s, chl, SR_TRIGGER_ZERO, 0);
	fail_unless(ret == SR_ERR_ARG);
	fail_unless(g_slist_length(sl->matches) == 0);

	sr_trigger_free(t);
	g_free(chl->name);
	g_free(chl);
	g_free(cha->name);
	g_free(cha);
}
END_TEST

Suite *suite_trigger(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("trigger");

	tc = tcase_create("new_free");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_trigger_new_free);
	tcase_add_test(tc, test_trigger_new_free_null);
	tcase_add_test(tc, test_trigger_free_null);
	suite_add_tcase(s, tc);

	tc = tcase_create("stage");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_trigger_stage_add);
	tcase_add_test(tc, test_trigger_stage_add_null);
	suite_add_tcase(s, tc);

	tc = tcase_create("match");
	tcase_set_timeout(tc, 0);
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_trigger_match_add);
	tcase_add_test(tc, test_trigger_match_add_bogus);
	suite_add_tcase(s, tc);

	return s;
}
