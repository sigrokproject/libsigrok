/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Aurelien Jacobs <aurel@gnuage.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdint.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

SR_PRIV uint16_t sr_crc16(uint16_t crc, const uint8_t *buffer, int len)
{
	int i;

	if (!buffer || len < 0)
		return crc;

	while (len--) {
		crc ^= *buffer++;
		for (i = 0; i < 8; i++) {
			int carry = crc & 1;
			crc >>= 1;
			if (carry)
				crc ^= 0xA001;
		}
	}

	return crc;
}
