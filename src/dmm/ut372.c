/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Martin Ling <martin-sigrok@earth.li>
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

/*
 * UNI-T UT372 protocol parser.
 */

#include <stdlib.h>
#include <math.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "ut372"

char lookup[] = {
	0x7B,
	0x60,
	0x5E,
	0x7C,
	0x65,
	0x3D,
	0x3F,
	0x70,
	0x7F,
	0x7D
};

#define DECIMAL_POINT_MASK 0x80

SR_PRIV gboolean sr_ut372_packet_valid(const uint8_t *buf)
{
	return (buf[25] == '\r' && buf[26] == '\n');
}

SR_PRIV int sr_ut372_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	unsigned int i, j, segments, value, divisor;
	char hex[3];

	(void) info;

	hex[2] = '\0';
	value = 0;
	divisor = 1;

	for (i = 0; i < 5; i++) {
		for (j = 0; j < 2; j++) {
			hex[j] = buf[2*i + 1 + j];
			if (hex[j] > 0x39)
				hex[j] += 7;
		}
		segments = strtol(hex, NULL, 16);
		for (j = 0; j < ARRAY_SIZE(lookup); j++) {
			if (lookup[j] == (segments & ~DECIMAL_POINT_MASK)) {
				value += j * pow(10, i);
				break;
			}
		}
		if (segments & DECIMAL_POINT_MASK)
			divisor = pow(10, i);
	}

	*floatval = (float) value / divisor;

	analog->mq = SR_MQ_FREQUENCY;
	analog->unit = SR_UNIT_REVOLUTIONS_PER_MINUTE;

	return SR_OK;
}
