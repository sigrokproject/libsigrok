/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 whatshu <shussm@qq.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <check.h>
#include "lib.h"
#include "../src/hardware/fnirsi-dla16/protocol.h"

START_TEST(test_command_vectors)
{
	uint8_t packet[DLA_COMMAND_SIZE];
	const uint8_t stop[] = {
		0x0a, 0x15, 0x02, 0x00, 0x0b, 0xe6, 0xfc, 0x24, 0xd7,
	};
	const uint8_t setup_crc[] = { 0x1a, 0x6d, 0x35, 0xbd };

	dla_stop(packet);
	ck_assert(!memcmp(packet + 8, stop, sizeof(stop)));
	dla_setup(packet, 160, 2, 50000);
	ck_assert(!memcmp(packet + 41, setup_crc, sizeof(setup_crc)));
	dla_setup(packet, 60, 7, 50000);
	ck_assert(!memcmp(packet + 41, "\x9f\x73\x5d\x5b", 4));
	dla_setup(packet, 250, 1, 50000);
	ck_assert(!memcmp(packet + 41, "\x1f\x30\x56\x12", 4));
	dla_setup(packet, 160, 2, 100000);
	ck_assert(!memcmp(packet + 41, "\xd7\x43\xda\x53", 4));
	dla_setup(packet, 160, 2, 250000);
	ck_assert(!memcmp(packet + 41, "\xf8\x0e\x11\xfd", 4));
	dla_setup(packet, 160, 2, 500000);
	ck_assert(!memcmp(packet + 41, "\xce\x5b\x77\x73", 4));
	dla_setup(packet, 160, 2, 50001);
	ck_assert(!memcmp(packet + 41, "\xd7\x43\xda\x53", 4));
}
END_TEST

START_TEST(test_pwm_commands)
{
	uint8_t packet[DLA_COMMAND_SIZE];
	/* Independent captured 5 MHz / 20% PWM command and stop vector. */
	const uint8_t pwm_on[] = {
		0x0a, 0x17, 0x0b, 0x00, 0x10, 0x40, 0x4b, 0x4c,
		0x00, 0x14, 0x00, 0x00, 0x00, 0x0b, 0x08, 0xd7, 0xee, 0xc0,
	};
	const uint8_t pwm_off[] = {
		0x0a, 0x17, 0x03, 0x00, 0x10, 0x0b, 0xe4, 0x3e, 0xc0, 0x2d,
	};

	ck_assert_int_eq(dla_pwm(packet, 0, 5000000, 20, 1), 0);
	ck_assert(!memcmp(packet + 8, pwm_on, sizeof(pwm_on)));
	ck_assert_int_eq(dla_pwm(packet, 0, 0, 0, 0), 0);
	ck_assert(!memcmp(packet + 8, pwm_off, sizeof(pwm_off)));
	ck_assert(dla_pwm(packet, 2, 1000, 50, 1) < 0);
	ck_assert(dla_pwm(packet, 0, 0, 50, 1) < 0);
	ck_assert(dla_pwm(packet, 0, 20000001, 50, 1) < 0);
	ck_assert(dla_pwm(packet, 0, 1000, 0, 1) < 0);
	ck_assert(dla_pwm(packet, 0, 1000, 100, 1) < 0);
}
END_TEST

START_TEST(test_channel_commands)
{
	uint8_t packet[DLA_COMMAND_SIZE];
	uint8_t triggers[16] = { 1, 3 };

	/* Factory layout: immediate flag, physical mask, 32 trigger bytes. */
	ck_assert_int_eq(dla_channels_config(packet, 3, triggers, 0), 0);
	ck_assert_int_eq(packet[12], 0);
	ck_assert_int_eq(packet[13], 3);
	ck_assert_int_eq(packet[14], 0);
	ck_assert_int_eq(packet[17], 1);
	ck_assert_int_eq(packet[18], 3);
	ck_assert_int_eq(packet[19], 0);
	ck_assert_int_eq(packet[49], 0x0b);
	ck_assert(dla_channels_config(packet, 0, triggers, 0) < 0);
	triggers[0] = 6;
	ck_assert(dla_channels_config(packet, 3, triggers, 0) < 0);
}
END_TEST

START_TEST(test_sample_count)
{
	uint8_t packet[DLA_COMMAND_SIZE];
	struct dla_setup_config cfg = {
		.samplerate = 1000000000, .samples = 1000000,
		.threshold = 160, .threshold_range = 2,
		.duration_ms = 1, .interval_ms = 3,
		.samplerate_index = 14, .buffer = 1,
		.trigger_position = 10,
	};

	/* Captured 1 GHz / 1 ms buffer setup: known independent CRC. */
	dla_setup_configure(packet, &cfg);
	ck_assert(!memcmp(packet + 41, "\x20\x7f\x25\x5f", 4));
	/* Preserve all 64 sample-count bits beyond 2^32 samples. */
	cfg.buffer = 0;
	cfg.mode = 3;
	cfg.samples = UINT64_C(10000000000);
	dla_setup_configure(packet, &cfg);
	ck_assert_int_eq(packet[13], 0);
	ck_assert_int_eq(packet[14], 3);
	ck_assert(!memcmp(packet + 32, "\x00\xe4\x0b\x54\x02\x00\x00\x00", 8));
}
END_TEST

START_TEST(test_channel_time_bits)
{
	uint8_t input[16], output[16];
	unsigned int ch, t, sample, word;

	for (ch = 0; ch < 16; ch++) {
		for (t = 0; t < 8; t++) {
			memset(input, 0, sizeof(input));
			input[ch] = 1U << t;
			dla_decode(output, input, 1);
			for (sample = 0; sample < 8; sample++) {
				word = output[sample * 2] | (output[sample * 2 + 1] << 8);
				ck_assert_uint_eq(word, sample == t ? 1U << ch : 0);
			}
		}
	}
}
END_TEST

START_TEST(test_sparse_channel_framing)
{
	const uint16_t masks[] = { 1, 3, 5, 0x81, 0x101, 0x8000, 0xffff };
	struct dla_decoder decoder;
	uint8_t wire[32], decoded[32], pattern;
	uint16_t expected[16], value;
	unsigned int ch, t, stream, lane, frame;
	size_t m;

	/*
	 * Two-lane Stream framing differs from the minimum eight-lane Buffer
	 * framing. Sparse physical channels retain their physical bit IDs.
	 */
	for (stream = 0; stream < 2; stream++) {
		for (m = 0; m < ARRAY_SIZE(masks); m++) {
			memset(expected, 0, sizeof(expected));
			lane = 0;
			ck_assert_int_eq(dla_decoder_init(&decoder, masks[m], stream), 0);
			memset(wire, 0xff, sizeof(wire)); /* Ignore padded lanes. */
			for (ch = 0; ch < 16; ch++) {
				if (!(masks[m] & (1U << ch)))
					continue;
				for (frame = 0; frame < 2; frame++) {
					pattern = (uint8_t)(0xa6U ^ (ch * 29U + frame * 83U));
					wire[frame * decoder.wire_channels + lane] = pattern;
					for (t = 0; t < 8; t++)
						if (pattern & (1U << t))
							expected[frame * 8 + t] |= 1U << ch;
				}
				lane++;
			}
			dla_decode_frames(&decoder, decoded, wire, 2);
			for (t = 0; t < 16; t++) {
				value = decoded[t * decoder.unitsize];
				if (decoder.unitsize == 2)
					value |= decoded[t * 2 + 1] << 8;
				ck_assert_uint_eq(value, expected[t]);
			}
			if (masks[m] == 3)
				ck_assert_uint_eq(decoder.wire_channels, stream ? 2 : 8);
		}
	}
}
END_TEST

Suite *suite_fnirsi_dla16(void)
{
	Suite *s;
	TCase *tc;

	s = suite_create("fnirsi-dla16");
	tc = tcase_create("protocol");
	tcase_add_test(tc, test_command_vectors);
	tcase_add_test(tc, test_pwm_commands);
	tcase_add_test(tc, test_channel_commands);
	tcase_add_test(tc, test_sample_count);
	tcase_add_test(tc, test_channel_time_bits);
	tcase_add_test(tc, test_sparse_channel_framing);
	suite_add_tcase(s, tc);

	return s;
}
