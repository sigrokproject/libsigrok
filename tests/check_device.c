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

#include <string.h>
#include <check.h>
#include "../include/libsigrok/libsigrok.h"
#include "lib.h"

START_TEST(test_user_new)
{
	struct sr_dev_inst *sdi;

	sdi = sr_dev_inst_user_new("Vendor", "Model", "Version");

	fail_unless(sdi != NULL, "sr_dev_inst_user_new() failed.");

	fail_unless(!strcmp("Vendor", sr_dev_inst_vendor_get(sdi)));
	fail_unless(!strcmp("Model", sr_dev_inst_model_get(sdi)));
	fail_unless(!strcmp("Version", sr_dev_inst_version_get(sdi)));
}
END_TEST

Suite *suite_device(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("device");

	tc = tcase_create("sr_dev_inst_user_new");
	tcase_add_test(tc, test_user_new);
	suite_add_tcase(s, tc);

	return s;
}
