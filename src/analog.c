/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "analog"

SR_PRIV int sr_analog_init(struct sr_datafeed_analog2 *analog,
		struct sr_analog_encoding *encoding,
		struct sr_analog_meaning *meaning,
		struct sr_analog_spec *spec,
		int digits)
{
	memset(analog, 0, sizeof(*analog));
	memset(encoding, 0, sizeof(*encoding));
	memset(meaning, 0, sizeof(*meaning));
	memset(spec, 0, sizeof(*spec));

	analog->encoding = encoding;
	analog->meaning = meaning;
	analog->spec = spec;

	encoding->unitsize = sizeof(float);
	encoding->is_float = TRUE;
#ifdef WORDS_BIGENDIAN
	encoding->is_bigendian = TRUE;
#else
	encoding->is_bigendian = FALSE;
#endif
	encoding->digits = digits;
	encoding->is_digits_decimal = TRUE;
	encoding->scale.p = 1;
	encoding->scale.q = 1;
	encoding->offset.p = 0;
	encoding->offset.q = 1;

	spec->spec_digits = digits;

	return SR_OK;
}

SR_API int sr_analog_to_float(const struct sr_datafeed_analog2 *analog,
		float *buf)
{
	float offset;
	unsigned int b, i;
	gboolean bigendian;

#ifdef WORDS_BIGENDIAN
	bigendian = TRUE;
#else
	bigendian = FALSE;
#endif
	if (!analog->encoding->is_float) {
		/* TODO */
		sr_err("Only floating-point analog data supported so far.");
		return SR_ERR;
	}

	if (analog->encoding->unitsize == sizeof(float)
			&& analog->encoding->is_bigendian == bigendian
			&& (analog->encoding->scale.p == analog->encoding->scale.q)
			&& analog->encoding->scale.p / (float)analog->encoding->scale.q == 0) {
		/* The data is already in the right format. */
		memcpy(buf, analog->data, analog->num_samples * sizeof(float));
	} else {
		for (i = 0; i < analog->num_samples; i += analog->encoding->unitsize) {
			for (b = 0; b < analog->encoding->unitsize; b++) {
				if (analog->encoding->is_bigendian == bigendian)
					buf[i + b] = ((float *)analog->data)[i * analog->encoding->unitsize + b];
				else
					buf[i + (analog->encoding->unitsize - b)] = ((float *)analog->data)[i * analog->encoding->unitsize + b];
			}
			if (analog->encoding->scale.p != analog->encoding->scale.q)
				buf[i] = (buf[i] * analog->encoding->scale.p) / analog->encoding->scale.q;
			offset = ((float)analog->encoding->scale.p / (float)analog->encoding->scale.q);
			buf[i] += offset;
		}
	}

	return SR_OK;
}
