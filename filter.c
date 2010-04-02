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
#include "sigrok.h"

/* convert sample from maximum probes -- the way the hardware driver sent
 * it -- to a sample taking up only as much space as required, with
 * unused probes removed.
 */
int filter_probes(int in_unitsize, int out_unitsize, int *probelist,
		char *data_in, uint64_t length_in, char **data_out, uint64_t *length_out)
{
	int num_enabled_probes, in_offset, out_offset, out_bit, i;
	uint64_t sample_in, sample_out;

	*data_out = malloc(length_in);
	num_enabled_probes = 0;
	for(i = 0; probelist[i]; i++)
		num_enabled_probes++;

	if(num_enabled_probes != in_unitsize * 8) {
		in_offset = out_offset = 0;
		while(in_offset <= length_in - in_unitsize) {
			memcpy(&sample_in, data_in + in_offset, in_unitsize);
			sample_out = 0;
			out_bit = 0;
			for(i = 0; probelist[i]; i++) {
				if(sample_in & (1 << (probelist[i]-1)))
					sample_out |= 1 << out_bit;
				out_bit++;
			}
			memcpy((*data_out) + out_offset, &sample_out, out_unitsize);
			in_offset += in_unitsize;
			out_offset += out_unitsize;
		}
		*length_out = out_offset;
	}
	else {
		/* all probes are used -- no need to compress anything */
		memcpy(*data_out, data_in, length_in);
		*length_out = length_in;
	}

	return SIGROK_OK;
}


