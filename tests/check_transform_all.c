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

#include <stdlib.h>
#include <check.h>
#include "../include/libsigrok/libsigrok.h"
#include "lib.h"

/* Check whether at least one transform module is available. */
START_TEST(test_transform_available)
{
	const struct sr_transform_module **transforms;

	transforms = sr_transform_list();
	fail_unless(transforms != NULL, "No transform modules found.");
}
END_TEST

/* Check whether sr_transform_id_get() works. */
START_TEST(test_transform_id)
{
	const struct sr_transform_module **transforms;
	const char *id;

	transforms = sr_transform_list();

	id = sr_transform_id_get(transforms[0]);
	fail_unless(id != NULL, "No ID found in transform module.");
}
END_TEST

/* Check whether sr_transform_name_get() works. */
START_TEST(test_transform_name)
{
	const struct sr_transform_module **transforms;
	const char *name;

	transforms = sr_transform_list();

	name = sr_transform_name_get(transforms[0]);
	fail_unless(name != NULL, "No name found in transform module.");
}
END_TEST

/* Check whether sr_transform_description_get() works. */
START_TEST(test_transform_desc)
{
	const struct sr_transform_module **transforms;
	const char *desc;

	transforms = sr_transform_list();

	desc = sr_transform_description_get(transforms[0]);
	fail_unless(desc != NULL, "No description found in transform module.");
}
END_TEST

/* Check whether sr_transform_find() works. */
START_TEST(test_transform_find)
{
	const struct sr_transform_module *tmod;
	const char *id;

	tmod = sr_transform_find("nop");
	fail_unless(tmod != NULL, "Couldn't find the 'nop' transform module.");
	id = sr_transform_id_get(tmod);
	fail_unless(id != NULL, "No ID found in transform module.");
	fail_unless(!strcmp(id, "nop"), "That is not the 'nop' module!");
}
END_TEST

/* Check whether sr_transform_options_get() works. */
START_TEST(test_transform_options)
{
	const struct sr_option **opt;

	opt = sr_transform_options_get(sr_transform_find("nop"));
	fail_unless(opt == NULL, "Transform module 'nop' doesn't have options.");
}
END_TEST

Suite *suite_transform_all(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("transform-all");

	tc = tcase_create("basic");
	tcase_add_test(tc, test_transform_available);
	tcase_add_test(tc, test_transform_id);
	tcase_add_test(tc, test_transform_name);
	tcase_add_test(tc, test_transform_desc);
	tcase_add_test(tc, test_transform_find);
	tcase_add_test(tc, test_transform_options);
	suite_add_tcase(s, tc);

	return s;
}
