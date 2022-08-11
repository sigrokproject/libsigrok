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
static const uint8_t buff8125fb[] = {
	0x41, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t buff8125fl[] = {
	0x00, 0x00, 0x02, 0x41, 0x00, 0x00, 0x00, 0x00,
};
static const uint8_t buff1234large[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
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

START_TEST(test_endian_read)
{
	fail_unless(read_u8(&buff1234[0]) == 0x11);
	fail_unless(read_u8(&buff1234[3]) == 0x44);
	fail_unless(read_u8(&buff1234[7]) == 0x88);

	fail_unless(read_u16be(&buff1234[0]) == 0x1122);
	fail_unless(read_u16be(&buff1234[6]) == 0x7788);

	fail_unless(read_u16le(&buff1234[0]) == 0x2211);
	fail_unless(read_u16le(&buff1234[6]) == 0x8877);

	fail_unless(read_i16be(&buff1234[6]) == 0x7788);
	fail_unless(read_i16le(&buff1234[6]) == (int16_t)0x8877);

	fail_unless(read_u32be(&buff1234[0]) == 0x11223344);
	fail_unless(read_u32be(&buff1234[4]) == 0x55667788);

	fail_unless(read_u32le(&buff1234[0]) == 0x44332211);
	fail_unless(read_u32le(&buff1234[4]) == 0x88776655);

	fail_unless(read_i32be(&buff1234[0]) == 0x11223344);
	fail_unless(read_i32be(&buff1234[4]) == 0x55667788);
	fail_unless(read_i32le(&buff1234[4]) == (int32_t)0x88776655ull);

	fail_unless(read_u64be(&buff1234[0]) == 0x1122334455667788ull);
	fail_unless(read_u64le(&buff1234[0]) == 0x8877665544332211ull);
	fail_unless(read_i64be(&buff1234[0]) == 0x1122334455667788ull);
	fail_unless(read_i64le(&buff1234[0]) == (int64_t)0x8877665544332211ull);

	fail_unless(read_fltbe(&buff8125fb[0]) == 8.125);
	fail_unless(read_fltle(&buff8125fl[0]) == 8.125);
}
END_TEST

START_TEST(test_endian_read_inc)
{
	const uint8_t *p;

	p = &buff1234[0];
	fail_unless(read_u8_inc(&p) == 0x11);
	fail_unless(read_u8_inc(&p) == 0x22);
	fail_unless(read_u8_inc(&p) == 0x33);
	fail_unless(p == &buff1234[3 * sizeof(uint8_t)]);

	p = &buff1234[0];
	fail_unless(read_u16be_inc(&p) == 0x1122);
	fail_unless(read_u16be_inc(&p) == 0x3344);
	fail_unless(p == &buff1234[2 * sizeof(uint16_t)]);

	p = &buff1234[0];
	fail_unless(read_u16le_inc(&p) == 0x2211);
	fail_unless(read_u16le_inc(&p) == 0x4433);
	fail_unless(p == &buff1234[2 * sizeof(uint16_t)]);

	p = &buff1234[0];
	fail_unless(read_u24le_inc(&p) == 0x332211);
	fail_unless(read_u24le_inc(&p) == 0x665544);
	fail_unless(p == &buff1234[2 * 3 * sizeof(uint8_t)]);

	p = &buff1234[0];
	fail_unless(read_u32be_inc(&p) == 0x11223344ul);
	fail_unless(read_u32be_inc(&p) == 0x55667788ul);
	fail_unless(p == &buff1234[2 * sizeof(uint32_t)]);

	p = &buff1234[0];
	fail_unless(read_u32le_inc(&p) == 0x44332211ul);
	fail_unless(read_u32le_inc(&p) == 0x88776655ul);
	fail_unless(p == &buff1234[2 * sizeof(uint32_t)]);

	p = &buff1234[0];
	fail_unless(read_u64be_inc(&p) == 0x1122334455667788);
	fail_unless(p == &buff1234[sizeof(uint64_t)]);

	p = &buff1234[0];
	fail_unless(read_u64le_inc(&p) == 0x8877665544332211ull);
	fail_unless(p == &buff1234[sizeof(uint64_t)]);
}
END_TEST

START_TEST(test_endian_write)
{
	uint8_t buff[2 * sizeof(uint64_t)];

	memset(buff, 0, sizeof(buff));
	write_u8(&buff[0], 0x11);
	fail_unless(memcmp(&buff[0], &buff1234[0], sizeof(uint8_t)) == 0);

	memset(buff, 0, sizeof(buff));
	write_u8(&buff[0], 0x22);
	write_u8(&buff[1], 0x33);
	write_u8(&buff[2], 0x44);
	write_u8(&buff[3], 0x55);
	fail_unless(memcmp(&buff[0], &buff1234[1], 4 * sizeof(uint8_t)) == 0);

	memset(buff, 0, sizeof(buff));
	write_u16be(&buff[0 * sizeof(uint16_t)], 0x1122);
	write_u16be(&buff[1 * sizeof(uint16_t)], 0x3344);
	fail_unless(memcmp(&buff[0], &buff1234[0], 2 * sizeof(uint16_t)) == 0);

	memset(buff, 0, sizeof(buff));
	write_u16le(&buff[0 * sizeof(uint16_t)], 0x4433);
	write_u16le(&buff[1 * sizeof(uint16_t)], 0x6655);
	fail_unless(memcmp(&buff[0], &buff1234[2], 2 * sizeof(uint16_t)) == 0);

	memset(buff, 0, sizeof(buff));
	write_u32be(&buff[0 * sizeof(uint32_t)], 0x11223344);
	write_u32be(&buff[1 * sizeof(uint32_t)], 0x55667788);
	fail_unless(memcmp(&buff[0], &buff1234[0], 2 * sizeof(uint32_t)) == 0);

	memset(buff, 0, sizeof(buff));
	write_u32le(&buff[0 * sizeof(uint32_t)], 0x44332211);
	write_u32le(&buff[1 * sizeof(uint32_t)], 0x88776655);
	fail_unless(memcmp(&buff[0], &buff1234[0], 2 * sizeof(uint32_t)) == 0);

	memset(buff, 0, sizeof(buff));
	write_fltbe(&buff[0], 8.125);
	fail_unless(memcmp(&buff[0], &buff8125fb[0], sizeof(float)) == 0);

	memset(buff, 0, sizeof(buff));
	write_fltle(&buff[0], 8.125);
	fail_unless(memcmp(&buff[0], &buff8125fl[0], sizeof(float)) == 0);
}
END_TEST

START_TEST(test_endian_write_inc)
{
	uint8_t buff[3 * sizeof(uint64_t)];
	uint8_t *p;
	size_t l;

	memset(buff, 0, sizeof(buff));

	p = &buff[0];
	write_u8_inc(&p, 0x11);
	write_u16be_inc(&p, 0x2233);
	write_u32be_inc(&p, 0x44556677);
	l = p - &buff[0];
	fail_unless(l == sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t));
	fail_unless(memcmp(&buff[0], &buff1234[0], l) == 0);

	p = &buff[0];
	write_u48le_inc(&p, 0x060504030201);
	write_u48le_inc(&p, 0x0c0b0a090807);
	write_u48le_inc(&p, 0x1211100f0e0d);
	write_u48le_inc(&p, 0x181716151413);
	l = p - &buff[0];
	fail_unless(l == 4 * 48 / 8 * sizeof(uint8_t));
	fail_unless(memcmp(&buff[0], &buff1234large[0], l) == 0);

	p = &buff[0];
	write_u24le_inc(&p, 0xfe030201);
	write_u40le_inc(&p, 0xdcba0807060504ul);
	l = p - &buff[0];
	fail_unless(l == 24 / 8 + 40 / 8);
	fail_unless(memcmp(&buff[0], &buff1234large[0], l) == 0);
}
END_TEST

Suite *suite_conv(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("conv");

	tc = tcase_create("endian");
	tcase_add_test(tc, test_endian_macro);
	tcase_add_test(tc, test_endian_read);
	tcase_add_test(tc, test_endian_read_inc);
	tcase_add_test(tc, test_endian_write);
	tcase_add_test(tc, test_endian_write_inc);
	suite_add_tcase(s, tc);

	return s;
}
