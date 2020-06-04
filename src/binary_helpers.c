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

SR_PRIV int bv_get_value(float *out, const struct binary_value_spec *spec, const void *data, size_t length)
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
		VALUE_TYPE(BVT_UINT8, R8, 1);

		VALUE_TYPE(BVT_BE_UINT16, RB16, 2);
		VALUE_TYPE(BVT_BE_UINT32, RB32, 4);
		VALUE_TYPE(BVT_BE_UINT64, RB64, 8);
		VALUE_TYPE(BVT_BE_FLOAT, RBFL, 4);

		VALUE_TYPE(BVT_LE_UINT16, RL16, 2);
		VALUE_TYPE(BVT_LE_UINT32, RL32, 4);
		VALUE_TYPE(BVT_LE_UINT64, RL64, 8);
		VALUE_TYPE(BVT_LE_FLOAT, RLFL, 4);

	default:
		return SR_ERR_ARG;
	}

#undef VALUE_TYPE

	*out = value * spec->scale;
	return SR_OK;
}

SR_PRIV int bv_send_analog_channel(const struct sr_dev_inst *sdi, struct sr_channel *ch,
				   const struct binary_analog_channel *bac, const void *data, size_t length)
{
	int err;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_analog analog;
	struct sr_datafeed_packet packet = {
		.type = SR_DF_ANALOG,
		.payload = &analog,
	};
	float value;

	err = bv_get_value(&value, &bac->spec, data, length);
	if (err != SR_OK)
		goto err_out;

	err = sr_analog_init(&analog, &encoding, &meaning, &spec, bac->digits);
	if (err != SR_OK)
		goto err_out;

	meaning.mq = bac->mq;
	meaning.unit = bac->unit;
	meaning.mqflags = 0;
	meaning.channels = g_slist_append(NULL, ch);

	spec.spec_digits = bac->digits;

	analog.data = &value;
	analog.num_samples = 1;

	err = sr_session_send(sdi, &packet);
	if (err != SR_OK)
		goto err_free;

	return SR_OK;

err_free:
	g_slist_free(meaning.channels);

err_out:
	return err;
}
