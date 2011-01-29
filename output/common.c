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
 * E.g. a value of 3000000 would be converted to "3 MHz", 20000 to "20 kHz".
 *
 * @param samplerate The samplerate in Hz.
 * @return A malloc()ed string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to free() the memory.
 */
char *sr_samplerate_string(uint64_t samplerate)
{
	char *o;
	int r;

	o = malloc(30 + 1); /* Enough for a uint64_t as string + " GHz". */
	if (!o)
		return NULL;

	if (samplerate >= GHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " GHz", samplerate / 1000000000);
	else if (samplerate >= MHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " MHz", samplerate / 1000000);
	else if (samplerate >= KHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " kHz", samplerate / 1000);
	else
		r = snprintf(o, 30, "%" PRIu64 " Hz", samplerate);

	if (r < 0) {
		/* Something went wrong... */
		free(o);
		return NULL;
	}

	return o;
}

/**
 * Convert a numeric samplerate value to the "natural" string representation
 * of its period.
 *
 * E.g. a value of 3000000 would be converted to "3 us", 20000 to "50 ms".
 *
 * @param frequency The frequency in Hz.
 * @return A malloc()ed string representation of the frequency value,
 *         or NULL upon errors. The caller is responsible to free() the memory.
 */
char *sr_period_string(uint64_t frequency)
{
	char *o;
	int r;

	o = malloc(30 + 1); /* Enough for a uint64_t as string + " ms". */
	if (!o)
		return NULL;

	if (frequency >= GHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " ns", frequency / 1000000000);
	else if (frequency >= MHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " us", frequency / 1000000);
	else if (frequency >= KHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " ms", frequency / 1000);
	else
		r = snprintf(o, 30, "%" PRIu64 " s", frequency);

	if (r < 0) {
		/* Something went wrong... */
		free(o);
		return NULL;
	}

	return o;
}
