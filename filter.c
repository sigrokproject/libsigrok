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

/**
 * Remove unused probes from samples.
 *
 * Convert sample from maximum probes -- the way the hardware driver sent
 * it -- to a sample taking up only as much space as required, with
 * unused probes removed.
 *
 * @param in_unitsize The unit size of the input (data_in).
 * @param out_unitsize The unit size of the output (data_out).
 * @param probelist Pointer to a list of integers (probe numbers).
 * @param data_in The input data.
 * @param length_in The input data length.
 * @param data_out The output data.
 * @param length_out The output data length.
 * @return SR_OK upon success, SR_ERR_MALLOC upon memory allocation errors.
 */
int sr_filter_probes(int in_unitsize, int out_unitsize, int *probelist,
		     const unsigned char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out)
{
	unsigned int in_offset, out_offset;
	int num_enabled_probes, out_bit, i;
	uint64_t sample_in, sample_out;

	if (!(*data_out = malloc(length_in)))
		return SR_ERR_MALLOC;

	num_enabled_probes = 0;
	for (i = 0; probelist[i]; i++)
		num_enabled_probes++;

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
