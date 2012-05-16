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
#include "sigrok.h"
#include "sigrok-internal.h"

/**
 * Convert a numeric samplerate value to its "natural" string representation.
 *
 * E.g. a value of 3000000 would be converted to "3 MHz", 20000 to "20 kHz",
 * 31500 would become "31.5 kHz".
 *
 * @param samplerate The samplerate in Hz.
 *
 * @return A g_try_malloc()ed string representation of the samplerate value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_samplerate_string(uint64_t samplerate)
{
	char *o;
	uint64_t s = samplerate;

	if ((s >= SR_GHZ(1)) && (s % SR_GHZ(1) == 0)) {
		o = g_strdup_printf("%" PRIu64 " GHz", s / SR_GHZ(1));
	} else if ((s >= SR_GHZ(1)) && (s % SR_GHZ(1) != 0)) {
		o = g_strdup_printf("%" PRIu64 ".%" PRIu64 " GHz",
				    s / SR_GHZ(1), s % SR_GHZ(1));
	} else if ((s >= SR_MHZ(1)) && (s % SR_MHZ(1) == 0)) {
		o = g_strdup_printf("%" PRIu64 " MHz", s / SR_MHZ(1));
	} else if ((s >= SR_MHZ(1)) && (s % SR_MHZ(1) != 0)) {
		o = g_strdup_printf("%" PRIu64 ".%" PRIu64 " MHz",
				    s / SR_MHZ(1), s % SR_MHZ(1));
	} else if ((s >= SR_KHZ(1)) && (s % SR_KHZ(1) == 0)) {
		o = g_strdup_printf("%" PRIu64 " kHz", s / SR_KHZ(1));
	} else if ((s >= SR_KHZ(1)) && (s % SR_KHZ(1) != 0)) {
		o = g_strdup_printf("%" PRIu64 ".%" PRIu64 " kHz",
				    s / SR_KHZ(1), s % SR_KHZ(1));
	} else {
		o = g_strdup_printf("%" PRIu64 " Hz", s);
	}

	if (!o) {
		sr_err("strutil: %s: Error creating samplerate string.",
		       __func__);
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
 *
 * @return A g_try_malloc()ed string representation of the frequency value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_period_string(uint64_t frequency)
{
	char *o;
	int r;

	/* Allocate enough for a uint64_t as string + " ms". */
	if (!(o = g_try_malloc0(30 + 1))) {
		sr_err("strutil: %s: o malloc failed", __func__);
		return NULL;
	}

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
		g_free(o);
		return NULL;
	}

	return o;
}

/**
 * Convert a numeric frequency value to the "natural" string representation
 * of its voltage value.
 *
 * E.g. a value of 300000 would be converted to "300mV", 2 to "2V".
 *
 * @param voltage The voltage represented as a rational number, with the
 *                denominator a divisor of 1V.
 *
 * @return A g_try_malloc()ed string representation of the voltage value,
 *         or NULL upon errors. The caller is responsible to g_free() the
 *         memory.
 */
SR_API char *sr_voltage_string(struct sr_rational *voltage)
{
	char *o;
	int r;

	if (!(o = g_try_malloc0(30 + 1))) {
		sr_err("strutil: %s: o malloc failed", __func__);
		return NULL;
	}

	if (voltage->q == 1000)
		r = snprintf(o, 30, "%" PRIu64 "mV", voltage->p);
	else if (voltage->q == 1)
		r = snprintf(o, 30, "%" PRIu64 "V", voltage->p);
	else
		r = -1;

	if (r < 0) {
		/* Something went wrong... */
		g_free(o);
		return NULL;
	}

	return o;
}

/**
 * Parse a trigger specification string.
 *
 * @param dev The device for which the trigger specification is intended.
 * @param triggerstring The string containing the trigger specification for
 *        one or more probes of this device. Entries for multiple probes are
 *        comma-separated. Triggers are specified in the form key=value,
 *        where the key is a probe number (or probe name) and the value is
 *        the requested trigger type. Valid trigger types currently
 *        include 'r' (rising edge), 'f' (falling edge), 'c' (any pin value
 *        change), '0' (low value), or '1' (high value).
 *        Example: "1=r,sck=f,miso=0,7=c"
 *
 * @return Pointer to a list of trigger types (strings), or NULL upon errors.
 *         The pointer list (if non-NULL) has as many entries as the
 *         respective device has probes (all physically available probes,
 *         not just enabled ones). Entries of the list which don't have
 *         a trigger value set in 'triggerstring' are NULL, the other entries
 *         contain the respective trigger type which is requested for the
 *         respective probe (e.g. "r", "c", and so on).
 */
SR_API char **sr_parse_triggerstring(struct sr_dev *dev,
				     const char *triggerstring)
{
	GSList *l;
	struct sr_probe *probe;
	int max_probes, probenum, i;
	char **tokens, **triggerlist, *trigger, *tc, *trigger_types;
	gboolean error;

	max_probes = g_slist_length(dev->probes);
	error = FALSE;

	if (!(triggerlist = g_try_malloc0(max_probes * sizeof(char *)))) {
		sr_err("strutil: %s: triggerlist malloc failed", __func__);
		return NULL;
	}

	tokens = g_strsplit(triggerstring, ",", max_probes);

	trigger_types = dev->driver->dev_info_get(0, SR_DI_TRIGGER_TYPES);
	if (!trigger_types) {
		sr_err("strutil: %s: Device doesn't support any triggers.",
		       __func__);
		return NULL;
	}

	for (i = 0; tokens[i]; i++) {
		if (tokens[i][0] < '0' || tokens[i][0] > '9') {
			/* Named probe */
			probenum = 0;
			for (l = dev->probes; l; l = l->next) {
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
			sr_err("strutil: Invalid probe (%d).", probenum);
			error = TRUE;
			break;
		}

		if ((trigger = strchr(tokens[i], '='))) {
			for (tc = ++trigger; *tc; tc++) {
				if (strchr(trigger_types, *tc) == NULL) {
					sr_err("strutil: Unsupported trigger "
					       "type '%c'.", *tc);
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
 *
 * @return SR_OK upon success, SR_ERR upon errors.
 */
SR_API int sr_parse_sizestring(const char *sizestring, uint64_t *size)
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
SR_API uint64_t sr_parse_timestring(const char *timestring)
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

SR_API gboolean sr_parse_boolstring(const char *boolstr)
{
	if (!boolstr)
		return FALSE;

	if (!g_ascii_strncasecmp(boolstr, "true", 4) ||
	    !g_ascii_strncasecmp(boolstr, "yes", 3) ||
	    !g_ascii_strncasecmp(boolstr, "on", 2) ||
	    !g_ascii_strncasecmp(boolstr, "1", 1))
		return TRUE;

	return FALSE;
}

SR_API int sr_parse_period(const char *periodstr, struct sr_rational *r)
{
	char *s;

	r->p = strtoull(periodstr, &s, 10);
	if (r->p == 0 && s == periodstr)
		/* No digits found. */
		return SR_ERR_ARG;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcmp(s, "ns"))
			r->q = 1000000000L;
		else if (!strcmp(s, "us"))
			r->q = 1000000;
		else if (!strcmp(s, "ms"))
			r->q = 1000;
		else if (!strcmp(s, "s"))
			r->q = 1;
		else
			/* Must have a time suffix. */
			return SR_ERR_ARG;
	}

	return SR_OK;
}


SR_API int sr_parse_voltage(const char *voltstr, struct sr_rational *r)
{
	char *s;

	r->p = strtoull(voltstr, &s, 10);
	if (r->p == 0 && s == voltstr)
		/* No digits found. */
		return SR_ERR_ARG;

	if (s && *s) {
		while (*s == ' ')
			s++;
		if (!strcasecmp(s, "mv"))
			r->q = 1000L;
		else if (!strcasecmp(s, "v"))
			r->q = 1;
		else
			/* Must have a base suffix. */
			return SR_ERR_ARG;
	}

	return SR_OK;
}


