/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include <check.h>
#include <libsigrok/libsigrok.h>
#include <stdlib.h>
#include <string.h>
#include "lib.h"
#include "libsigrok-internal.h"

static const uint8_t buff1234[] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};

START_TEST(test_endian_macro)
{
	const uint8_t *p8;
	const uint16_t *p16;
	const uint32_t *p32;

	p8 = (const void *)&buff1234[0];
	fail_unless(R8(&p8[0]) == 0x11);
	fail_unless(R8(&p8[1]) == 0x22);
	fail_unless(R8(&p8[2]) == 0x33);
	fail_unless(R8(&p8[3]) == 0x44);

	p16 = (const void *)&buff1234[0];
	fail_unless(RB16(&p16[0]) == 0x1122);
	fail_unless(RB16(&p16[1]) == 0x3344);

	p16 = (const void *)&buff1234[0];
	fail_unless(RL16(&p16[0]) == 0x2211);
	fail_unless(RL16(&p16[1]) == 0x4433);

	p32 = (const void *)&buff1234[0];
	fail_unless(RB32(&p32[0]) == 0x11223344);
	fail_unless(RB32(&p32[1]) == 0x55667788);

	p32 = (const void *)&buff1234[0];
	fail_unless(RL32(&p32[0]) == 0x44332211);
	fail_unless(RL32(&p32[1]) == 0x88776655);

	p16 = (const void *)&buff1234[0];
	fail_unless(RB16(p16++) == 0x1122);
	fail_unless(RB16(p16++) == 0x3344);
}
END_TEST

Suite *suite_conv(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("conv");

	tc = tcase_create("endian");
	tcase_add_test(tc, test_endian_macro);
	suite_add_tcase(s, tc);

	return s;
}
