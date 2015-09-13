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

/* Check whether at least one driver is available. */
START_TEST(test_driver_available)
{
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list(srtest_ctx);
	fail_unless(drivers != NULL, "No drivers found.");
}
END_TEST

/* Check whether initializing all drivers works. */
START_TEST(test_driver_init_all)
{
	srtest_driver_init_all(srtest_ctx);
}
END_TEST

/*
 * Check whether setting a samplerate works.
 *
 * Additionally, this also checks whether SR_CONF_SAMPLERATE can be both
 * set and read back properly.
 */
#if 0
START_TEST(test_config_get_set_samplerate)
{
	/*
	 * Note: This currently only works for the demo driver.
	 *       For other drivers, a scan is needed and the respective
	 *       hardware must be attached to the host running the testsuite.
	 */
	srtest_check_samplerate(sr_ctx, "demo", SR_KHZ(19));
}
END_TEST
#endif

Suite *suite_driver_all(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("driver-all");

	tc = tcase_create("config");
	tcase_add_checked_fixture(tc, srtest_setup, srtest_teardown);
	tcase_add_test(tc, test_driver_available);
	tcase_add_test(tc, test_driver_init_all);
	// TODO: Currently broken.
	// tcase_add_test(tc, test_config_get_set_samplerate);
	suite_add_tcase(s, tc);

	return s;
}
