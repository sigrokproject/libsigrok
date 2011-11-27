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
#include <sigrok-internal.h>

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

	if (samplerate >= SR_GHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " GHz", samplerate / 1000000000);
	else if (samplerate >= SR_MHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " MHz", samplerate / 1000000);
	else if (samplerate >= SR_KHZ(1))
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
 * Convert a numeric frequency value to the "natural" string representation
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

	if (frequency >= SR_GHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " ns", frequency / 1000000000);
	else if (frequency >= SR_MHZ(1))
		r = snprintf(o, 30, "%" PRIu64 " us", frequency / 1000000);
	else if (frequency >= SR_KHZ(1))
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

/**
 * TODO
 *
 * @param device TODO
 * @param triggerstring TODO
 * @return TODO
 */
char **sr_parse_triggerstring(struct sr_device *device,
			      const char *triggerstring)
{
	GSList *l;
	struct sr_probe *probe;
	int max_probes, probenum, i;
	char **tokens, **triggerlist, *trigger, *tc, *trigger_types;
	gboolean error;

	max_probes = g_slist_length(device->probes);
	error = FALSE;

	if (!(triggerlist = g_try_malloc0(max_probes * sizeof(char *)))) {
		sr_err("session file: %s: metafile malloc failed", __func__);
		return NULL;
	}

	tokens = g_strsplit(triggerstring, ",", max_probes);
	trigger_types = device->plugin->get_device_info(0, SR_DI_TRIGGER_TYPES);
	if (trigger_types == NULL)
		return NULL;

	for (i = 0; tokens[i]; i++) {
		if (tokens[i][0] < '0' || tokens[i][0] > '9') {
			/* Named probe */
			probenum = 0;
			for (l = device->probes; l; l = l->next) {
				probe = (struct sr_probe *)l->data;
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
			sr_err("Invalid probe.\n");
			error = TRUE;
			break;
		}

		if ((trigger = strchr(tokens[i], '='))) {
			for (tc = ++trigger; *tc; tc++) {
				if (strchr(trigger_types, *tc) == NULL) {
					sr_err("Unsupported trigger type "
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

/**
 * Convert a "natural" string representation of a size value to uint64_t.
 *
 * E.g. a value of "3k" or "3 K" would be converted to 3000, a value
 * of "15M" would be converted to 15000000.
 *
 * Value representations other than decimal (such as hex or octal) are not
 * supported. Only 'k' (kilo), 'm' (mega), 'g' (giga) suffixes are supported.
 * Spaces (but not other whitespace) between value and suffix are allowed.
 *
 * @param sizestring A string containing a (decimal) size value.
 * @param size Pointer to uint64_t which will contain the string's size value.
 * @return SR_OK or error code
 *
 */
int sr_parse_sizestring(const char *sizestring, uint64_t *size)
{
	int multiplier, done;
	char *s;

	*size = strtoull(sizestring, &s, 10);
	multiplier = 0;
	done = FALSE;
	while (s && *s && multiplier == 0 && !done) {
		switch (*s) {
		case ' ':
			break;
		case 'k':
		case 'K':
			multiplier = SR_KHZ(1);
			break;
		case 'm':
		case 'M':
			multiplier = SR_MHZ(1);
			break;
		case 'g':
		case 'G':
			multiplier = SR_GHZ(1);
			break;
		default:
			done = TRUE;
			s--;
		}
		s++;
	}
	if (multiplier > 0)
		*size *= multiplier;

	if (*s && strcasecmp(s, "Hz"))
		return SR_ERR;

	return SR_OK;
}

/**
 * Convert a "natural" string representation of a time value to an
 * uint64_t value in milliseconds.
 *
 * E.g. a value of "3s" or "3 s" would be converted to 3000, a value
 * of "15ms" would be converted to 15.
 *
 * Value representations other than decimal (such as hex or octal) are not
 * supported. Only lower-case "s" and "ms" time suffixes are supported.
 * Spaces (but not other whitespace) between value and suffix are allowed.
 *
 * @param timestring A string containing a (decimal) time value.
 * @return The string's time value as uint64_t, in milliseconds.
 *
 * TODO: Error handling.
 * TODO: Add support for "m" (minutes) and others.
 * TODO: picoseconds?
 * TODO: Allow both lower-case and upper-case.
 */
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

gboolean sr_parse_boolstring(const char *boolstr)
{
	if (!boolstr)
		return FALSE;

	if (!g_strcasecmp(boolstr, "true") || 
	    !g_strcasecmp(boolstr, "yes") ||
	    !g_strcasecmp(boolstr, "on") ||
	    !g_strcasecmp(boolstr, "1")) 
		return TRUE;

	return FALSE;
}

