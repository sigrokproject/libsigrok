/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Soeren Apel <soeren@apelpie.net>
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

/**
 * @file
 *
 * Conversion helper functions.
 */

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "conv"

/**
 * Convert analog values to logic values by using a fixed threshold.
 *
 * @param[in] analog The analog input values.
 * @param[in] threshold The threshold to use.
 * @param[out] output The converted output values; either 0 or 1. Must provide
 *                    space for count bytes.
 * @param[in] count The number of samples to process.
 *
 * @return SR_OK on success or SR_ERR on failure.
 */
SR_API int sr_a2l_threshold(const struct sr_datafeed_analog *analog,
		float threshold, uint8_t *output, uint64_t count)
{
	float *input;

	if (!analog->encoding->is_float) {
		input = g_try_malloc(sizeof(float) * count);
		if (!input)
			return SR_ERR;

		sr_analog_to_float(analog, input);
	} else
		input = analog->data;

	for (uint64_t i = 0; i < count; i++)
		output[i] = (input[i] >= threshold) ? 1 : 0;

	if (!analog->encoding->is_float)
		g_free(input);

	return SR_OK;
}

/**
 * Convert analog values to logic values by using a Schmitt-trigger algorithm.
 *
 * @param analog The analog input values.
 * @param lo_thr The low threshold - result becomes 0 below it.
 * @param lo_thr The high threshold - result becomes 1 above it.
 * @param state The internal converter state. Must contain the state of logic
 *        sample n-1, will contain the state of logic sample n+count upon exit.
 * @param output The converted output values; either 0 or 1. Must provide
 *        space for count bytes.
 * @param count The number of samples to process.
 *
 * @return SR_OK on success or SR_ERR on failure.
 */
SR_API int sr_a2l_schmitt_trigger(const struct sr_datafeed_analog *analog,
		float lo_thr, float hi_thr, uint8_t *state, uint8_t *output,
		uint64_t count)
{
	float *input;

	if (!analog->encoding->is_float) {
		input = g_try_malloc(sizeof(float) * count);
		if (!input)
			return SR_ERR;

		sr_analog_to_float(analog, input);
	} else
		input = analog->data;

	for (uint64_t i = 0; i < count; i++) {
		if (input[i] < lo_thr)
			*state = 0;
		else if (input[i] > hi_thr)
			*state = 1;

		output[i] = *state;
	}

	if (!analog->encoding->is_float)
		g_free(input);

	return SR_OK;
}
