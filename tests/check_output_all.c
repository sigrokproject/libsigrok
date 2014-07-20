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

#include <stdlib.h>
#include <check.h>
#include "../include/libsigrok/libsigrok.h"
#include "lib.h"

/* Check whether at least one output module is available. */
START_TEST(test_output_available)
{
	struct sr_output_format **outputs;

	outputs = sr_output_list();
	fail_unless(outputs != NULL, "No output modules found.");
}
END_TEST

Suite *suite_output_all(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("output-all");

	tc = tcase_create("basic");
	tcase_add_test(tc, test_output_available);
	suite_add_tcase(s, tc);

	return s;
}
