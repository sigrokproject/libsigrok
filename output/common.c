/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sigrok.h>

/**
 * Convert a numeric samplerate value to its "natural" string representation.
 *
 * E.g. a value of 3000000 would be converted to "3 MHz", 20000 to "20 KHz".
 *
 * @param samplerate The samplerate in Hz.
 * @return A malloc()ed string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to free() the memory.
 */
char *sigrok_samplerate_string(uint64_t samplerate)
{
	char *o;
	int r;

	o = malloc(30 + 1); /* Enough for a uint64_t as string + " GHz". */
	if (o == NULL)
		return NULL;

	if (samplerate >= GHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " GHz", samplerate / 1000000000);
	else if (samplerate >= MHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " MHz", samplerate / 1000000);
	else if (samplerate >= KHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " KHz", samplerate / 1000);
	else
		r = snprintf(o, 30, "%" PRIu64 " Hz", samplerate);

	if (r < 0) {
		/* Something went wrong... */
		free(o);
		return NULL;
	}

	return o;
}
