/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sigrok.h>
#include "sigrok-internal.h"

/**
 * Remove unused probes from samples.
 *
 * Convert sample from maximum probes -- the way the hardware driver sent
 * it -- to a sample taking up only as much space as required, with
 * unused probes removed.
 *
 * The "unit size" is the number of bytes used to store probe values.
 * For example, a unit size of 1 means one byte is used (which can store
 * 8 probe values, each of them is 1 bit). A unit size of 2 means we can
 * store 16 probe values, 3 means we can store 24 probe values, and so on.
 *
 * If the data coming from the logic analyzer has a unit size of 4 for
 * example (as the device has 32 probes), but only 2 of them are actually
 * used in an acquisition, this function can convert the samples to only
 * use up 1 byte per sample (unit size = 1) instead of 4 bytes per sample.
 *
 * The output will contain the probe values in the order specified via the
 * probelist. For example, if in_unitsize = 4, probelist = [5, 16, 30], and
 * out_unitsize = 1, then the output samples (each of them one byte in size)
 * will have the following format: bit 0 = value of probe 5, bit 1 = value
 * of probe 16, bit 2 = value of probe 30. Unused bit(s) in the output byte(s)
 * are zero.
 *
 * The caller must make sure that length_in is not bigger than the memory
 * actually allocated for the input data (data_in), as this function does
 * not check that.
 *
 * @param in_unitsize The unit size (>= 1) of the input (data_in).
 * @param out_unitsize The unit size (>= 1) the output shall have (data_out).
 *                     The requested unit size must be big enough to hold as
 *                     much data as is specified by the number of enabled
 *                     probes in 'probelist'.
 * @param probelist Pointer to a list of integers (probe numbers). The probe
 *                  numbers in this list are 1-based, i.e. the first probe
 *                  is expected to be numbered 1 (not 0!). Must not be NULL.
 * @param data_in Pointer to the input data buffer. Must not be NULL.
 * @param length_in The input data length (>= 1), in number of bytes.
 * @param data_out Variable which will point to the newly allocated buffer
 *                 of output data. The caller is responsible for g_free()'ing
 *                 the buffer when it's no longer needed. Must not be NULL.
 * @param length_out Pointer to the variable which will contain the output
 *                   data length (in number of bytes) when the function
 *                   returns SR_OK. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_MALLOC upon memory allocation errors,
 *         or SR_ERR_ARG upon invalid arguments.
 *         If something other than SR_OK is returned, the values of
 *         out_unitsize, data_out, and length_out are undefined.
 */
int sr_filter_probes(int in_unitsize, int out_unitsize, const int *probelist,
		     const unsigned char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out)
{
	unsigned int in_offset, out_offset;
	int num_enabled_probes, out_bit, i;
	uint64_t sample_in, sample_out;

	if (!probelist) {
		sr_err("filter: %s: probelist was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_in) {
		sr_err("filter: %s: data_in was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_out) {
		sr_err("filter: %s: data_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!length_out) {
		sr_err("filter: %s: length_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!length_out) {
		sr_err("filter: %s: length_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	num_enabled_probes = 0;
	for (i = 0; probelist[i]; i++)
		num_enabled_probes++;

	/* Are there more probes than the target unit size supports? */
	if (num_enabled_probes > out_unitsize * 8) {
		sr_err("filter: %s: too many probes (%d) for the target unit "
		       "size (%d)", num_enabled_probes, out_unitsize, __func__);
		return SR_ERR_ARG;
	}

	if (!(*data_out = g_try_malloc(length_in))) {
		sr_err("filter: %s: data_out malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (num_enabled_probes == in_unitsize * 8) {
		/* All probes are used -- no need to compress anything. */
		memcpy(*data_out, data_in, length_in);
		*length_out = length_in;
		return SR_OK;
	}

	/* If we reached this point, not all probes are used, so "compress". */
	in_offset = out_offset = 0;
	while (in_offset <= length_in - in_unitsize) {
		memcpy(&sample_in, data_in + in_offset, in_unitsize);
		sample_out = out_bit = 0;
		for (i = 0; probelist[i]; i++) {
			if (sample_in & (1 << (probelist[i] - 1)))
				sample_out |= (1 << out_bit);
			out_bit++;
		}
		memcpy((*data_out) + out_offset, &sample_out, out_unitsize);
		in_offset += in_unitsize;
		out_offset += out_unitsize;
	}
	*length_out = out_offset;

	return SR_OK;
}
