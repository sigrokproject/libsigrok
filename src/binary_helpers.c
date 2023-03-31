/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Andreas Sandberg <andreas@sandberg.pp.se>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

SR_PRIV int bv_get_value_with_length_check(float *out,
	const struct binary_value_spec *spec, const void *data, size_t length)
{
	float value;

	if (!out || !spec || !data)
		return SR_ERR_ARG;

#define VALUE_TYPE(T, R, L)				\
	case T:						\
		if (spec->offset + (L) > length)	\
			return SR_ERR_DATA;		\
		value = R(data + spec->offset);		\
		break

	switch (spec->type) {
	VALUE_TYPE(BVT_UINT8, read_u8, sizeof(uint8_t));

	VALUE_TYPE(BVT_BE_UINT16, read_u16be, sizeof(uint16_t));
	VALUE_TYPE(BVT_BE_UINT24, read_u24be, 3);
	VALUE_TYPE(BVT_BE_UINT32, read_u32be, sizeof(uint32_t));
	VALUE_TYPE(BVT_BE_UINT64, read_u64be, sizeof(uint64_t));
	VALUE_TYPE(BVT_BE_FLOAT,  read_fltbe, sizeof(float));

	VALUE_TYPE(BVT_LE_UINT16, read_u16le, sizeof(uint16_t));
	VALUE_TYPE(BVT_LE_UINT24, read_u24le, 3);
	VALUE_TYPE(BVT_LE_UINT32, read_u32le, sizeof(uint32_t));
	VALUE_TYPE(BVT_LE_UINT64, read_u64le, sizeof(uint64_t));
	VALUE_TYPE(BVT_LE_FLOAT,  read_fltle, sizeof(float));

	default:
		return SR_ERR_ARG;
	}

#undef VALUE_TYPE

	if (out)
		*out = value;
	return SR_OK;
}
