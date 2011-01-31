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

char **sr_parse_triggerstring(struct sr_device *device, const char *triggerstring)
{
	GSList *l;
	struct probe *probe;
	int max_probes, probenum, i;
	char **tokens, **triggerlist, *trigger, *tc, *trigger_types;
	gboolean error;

	max_probes = g_slist_length(device->probes);
	error = FALSE;
	triggerlist = g_malloc0(max_probes * sizeof(char *));
	tokens = g_strsplit(triggerstring, ",", max_probes);
	trigger_types = device->plugin->get_device_info(0, SR_DI_TRIGGER_TYPES);
	if (trigger_types == NULL)
		return NULL;

	for (i = 0; tokens[i]; i++) {
		if (tokens[i][0] < '0' || tokens[i][0] > '9') {
			/* Named probe */
			probenum = 0;
			for (l = device->probes; l; l = l->next) {
				probe = (struct probe *)l->data;
				if (probe->enabled
				    && !strncmp(probe->name, tokens[i],
						strlen(probe->name))) {
					probenum = probe->index;
					break;
				}
			}
		} else {
			probenum = strtol(tokens[i], NULL, 10);
		}

		if (probenum < 1 || probenum > max_probes) {
			printf("Invalid probe.\n");
			error = TRUE;
			break;
		}

		if ((trigger = strchr(tokens[i], '='))) {
			for (tc = ++trigger; *tc; tc++) {
				if (strchr(trigger_types, *tc) == NULL) {
					printf("Unsupported trigger type "
					       "'%c'\n", *tc);
					error = TRUE;
					break;
				}
			}
			if (!error)
				triggerlist[probenum - 1] = g_strdup(trigger);
		}
	}
	g_strfreev(tokens);

	if (error) {
		for (i = 0; i < max_probes; i++)
			if (triggerlist[i])
				g_free(triggerlist[i]);
		g_free(triggerlist);
		triggerlist = NULL;
	}

	return triggerlist;
}

uint64_t sr_parse_sizestring(const char *sizestring)
{
	int multiplier;
	uint64_t val;
	char *s;

	val = strtoull(sizestring, &s, 10);
	multiplier = 0;
	while (s && *s && multiplier == 0) {
		switch (*s) {
		case ' ':
			break;
		case 'k':
		case 'K':
			multiplier = KHZ(1);
			break;
		case 'm':
		case 'M':
			multiplier = MHZ(1);
			break;
		case 'g':
		case 'G':
			multiplier = GHZ(1);
			break;
		default:
			val = 0;
			multiplier = -1;
		}
		s++;
	}
	if (multiplier > 0)
		val *= multiplier;

	return val;
}

uint64_t sr_parse_timestring(const char *timestring)
{
	uint64_t time_msec;
	char *s;

	time_msec = strtoull(timestring, &s, 10);
	if (time_msec == 0 && s == timestring)
		return 0;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcmp(s, "s"))
			time_msec *= 1000;
		else if (!strcmp(s, "ms"))
			; /* redundant */
		else
			return 0;
	}

	return time_msec;
}

