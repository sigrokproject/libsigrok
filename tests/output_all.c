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

#include <config.h>
#include <stdlib.h>
#include <check.h>
#include <libsigrok/libsigrok.h>
#include "lib.h"

/* Check whether at least one output module is available. */
START_TEST(test_output_available)
{
	const struct sr_output_module **outputs;

	outputs = sr_output_list();
	fail_unless(outputs != NULL, "No output modules found.");
}
END_TEST

/* Check whether sr_output_id_get() works. */
START_TEST(test_output_id)
{
	const struct sr_output_module **outputs;
	const char *id;

	outputs = sr_output_list();

	id = sr_output_id_get(outputs[0]);
	fail_unless(id != NULL, "No id found in output module.");
}
END_TEST

/* Check whether sr_output_name_get() works. */
START_TEST(test_output_name)
{
	const struct sr_output_module **outputs;
	const char *name;

	outputs = sr_output_list();

	name = sr_output_name_get(outputs[0]);
	fail_unless(name != NULL, "No name found in output module.");
}
END_TEST

/* Check whether sr_output_description_get() works. */
START_TEST(test_output_desc)
{
	const struct sr_output_module **outputs;
	const char *desc;

	outputs = sr_output_list();

	desc = sr_output_description_get(outputs[0]);
	fail_unless(desc != NULL, "No description found in output module.");
}
END_TEST

/* Check whether sr_output_find() works. */
START_TEST(test_output_find)
{
	const struct sr_output_module *omod;
	const char *id;

	omod = sr_output_find("bits");
	fail_unless(omod != NULL, "Couldn't find the 'bits' output module.");
	id = sr_output_id_get(omod);
	fail_unless(!strcmp(id, "bits"), "That is not the 'bits' module!");
}
END_TEST

/* Check whether sr_output_options_get() works. */
START_TEST(test_output_options)
{
	const struct sr_option **opt;

	opt = sr_output_options_get(sr_output_find("bits"));
	fail_unless(opt != NULL, "Couldn't find 'bits' options.");
	fail_unless(!strcmp((*opt)->id, "width"), "Wrong 'bits' option found!");
}
END_TEST

Suite *suite_output_all(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("output-all");

	tc = tcase_create("basic");
	tcase_add_test(tc, test_output_available);
	tcase_add_test(tc, test_output_id);
	tcase_add_test(tc, test_output_name);
	tcase_add_test(tc, test_output_desc);
	tcase_add_test(tc, test_output_find);
	tcase_add_test(tc, test_output_options);
	suite_add_tcase(s, tc);

	return s;
}
