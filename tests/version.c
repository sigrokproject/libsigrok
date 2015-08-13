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
#include <libsigrok/libsigrok.h>
#include "lib.h"

/*
 * Check the version number API calls and macros.
 *
 * The numbers returned by the sr_*_version*get() calls must match the
 * respective SR_*_VERSION* macro values, must be >= 0, and must not be
 * unreasonably high (> 20), otherwise something is probably wrong.
 */
START_TEST(test_version_numbers)
{
	int ver;

	ver = sr_package_version_major_get();
	fail_unless(ver == SR_PACKAGE_VERSION_MAJOR);
	fail_unless(ver >= 0 && ver <= 20);
	ver = sr_package_version_minor_get();
	fail_unless(ver == SR_PACKAGE_VERSION_MINOR);
	fail_unless(ver >= 0 && ver <= 20);
	ver = sr_package_version_micro_get();
	fail_unless(ver == SR_PACKAGE_VERSION_MICRO);
	fail_unless(ver >= 0 && ver <= 20);

	ver = sr_lib_version_current_get();
	fail_unless(ver == SR_LIB_VERSION_CURRENT);
	fail_unless(ver >= 0 && ver <= 20);
	ver = sr_lib_version_revision_get();
	fail_unless(ver == SR_LIB_VERSION_REVISION);
	fail_unless(ver >= 0 && ver <= 20);
	ver = sr_lib_version_age_get();
	fail_unless(ver == SR_LIB_VERSION_AGE);
	fail_unless(ver >= 0 && ver <= 20);
}
END_TEST

/*
 * Check the version number API calls and macros.
 *
 * The string representations of the package/lib version must match the
 * version numbers, the string lengths must be >= 5 (e.g. "0.1.0"), and
 * the strings length must be <= 20 characters, otherwise something is
 * probably wrong.
 */
START_TEST(test_version_strings)
{
	const char *str;

	str = sr_package_version_string_get();
	fail_unless(str != NULL);
	fail_unless(strlen(str) >= 5 && strlen(str) <= 20);
	str = sr_lib_version_string_get();
	fail_unless(str != NULL);
	fail_unless(strlen(str) >= 5 && strlen(str) <= 20);
}
END_TEST

Suite *suite_version(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("version");

	tc = tcase_create("version");
	tcase_add_test(tc, test_version_numbers);
	tcase_add_test(tc, test_version_strings);
	suite_add_tcase(s, tc);

	return s;
}
