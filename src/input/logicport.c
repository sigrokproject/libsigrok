/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * See the LA1034 vendor's http://www.pctestinstruments.com/ website.
 *
 * The hardware comes with (Windows only) software which uses the .lpf
 * ("LogicPort File") filename extension for project files, which hold
 * both the configuration as well as sample data (up to 2K samples). In
 * the absence of an attached logic analyzer, the software provides a
 * demo mode which generates random input signals. The software installs
 * example project files (with samples), too.
 *
 * The file format is "mostly text", is line oriented, though it uses
 * funny DC1 separator characters as well as line continuation by means
 * of a combination of DC1 and slashes. Fortunately the last text line
 * is terminated by means of CRLF.
 *
 * The software is rather complex and has features which don't easily
 * translate to sigrok semantics (like one signal being a member of
 * multiple groups, display format specs for groups' values).
 *
 * This input module implementation supports the following features:
 * - input format auto detection
 * - sample period to sample rate conversion
 * - wire names, acquisition filters ("enabled") and inversion flags
 * - decompression (repetition counters for sample data)
 * - strict '0' and '1' levels (as well as ignoring 'U' values)
 * - signal names (user assigned names, "aliases" for "wires")
 * - signal groups (no support for multiple assignments, no support for
 *   display format specs)
 * - "logic" channels (mere bits, no support for analog channels, also
 *   nothing analog "gets derived from" any signal groups) -- libsigrok
 *   using applications might provide such a feature if they want to
 */

#include <config.h>
#include <ctype.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/* TODO: Move these helpers to some library API routine group. */
struct sr_channel_group *sr_channel_group_new(const char *name, void *priv);
void sr_channel_group_free(struct sr_channel_group *cg);

#define LOG_PREFIX	"input/logicport"

#define MAX_CHANNELS	34
#define CHUNK_SIZE	(4 * 1024 * 1024)

#define CRLF		"\r\n"
#define DC1_CHR		'\x11'
#define DC1_STR		"\x11"
#define CONT_OPEN	"/" DC1_STR
#define CONT_CLOSE	DC1_STR "/"

/*
 * This is some heuristics (read: a HACK). The current implementation
 * neither processes nor displays the user's notes, but takes their
 * presence as a hint that all relevant input was seen, and sample data
 * can get forwarded to the session bus.
 */
#define LAST_KEYWORD	"NotesString"

/*
 * The vendor software supports signal groups, and a single signal can
 * be a member in multiple groups at the same time. The sigrok project
 * does not support that configuration. Let's ignore the "All Signals"
 * group by default, thus reducing the probability of a conflict.
 */
#define SKIP_SIGNAL_GROUP	"All Signals"

struct signal_group_desc {
	char *name;
	uint64_t mask;
};

struct context {
	gboolean got_header;
	gboolean ch_feed_prep;
	gboolean header_sent;
	gboolean rate_sent;
	char *sw_version;
	size_t sw_build;
	GString *cont_buff;
	size_t channel_count;
	size_t sample_lines_total;
	size_t sample_lines_read;
	size_t sample_lines_fed;
	uint64_t samples_got_uncomp;
	enum {
		SAMPLEDATA_NONE,
		SAMPLEDATA_OPEN_BRACE,
		SAMPLEDATA_WIRES_COUNT,
		SAMPLEDATA_DATA_LINES,
		SAMPLEDATA_CLOSE_BRACE,
	} in_sample_data;
	struct sample_data_entry {
		uint64_t bits;
		size_t repeat;
	} *sample_data_queue;
	uint64_t sample_rate;
	uint64_t wires_all_mask;
	uint64_t wires_enabled;
	uint64_t wires_inverted;
	uint64_t wires_undefined;
	char *wire_names[MAX_CHANNELS];
	char *signal_names[MAX_CHANNELS];
	uint64_t wires_grouped;
	GSList *signal_groups;
	GSList *channels;
	size_t unitsize;
	size_t samples_per_chunk;
	size_t samples_in_buffer;
	uint8_t *feed_buffer;
};

static struct signal_group_desc *alloc_signal_group(const char *name)
{
	struct signal_group_desc *desc;

	desc = g_malloc0(sizeof(*desc));
	if (!desc)
		return NULL;
	if (name) {
		desc->name = g_strdup(name);
		if (!desc->name) {
			g_free(desc);
			return NULL;
		}
	}

	return desc;
}

static void free_signal_group(struct signal_group_desc *desc)
{
	if (!desc)
		return;
	g_free(desc->name);
	g_free(desc);
}

struct sr_channel_group *sr_channel_group_new(const char *name, void *priv)
{
	struct sr_channel_group *cg;

	cg = g_malloc0(sizeof(*cg));
	if (!cg)
		return NULL;
	if (name && *name) {
		cg->name = g_strdup(name);
		if (!cg->name) {
			g_free(cg);
			return NULL;
		}
	}
	cg->priv = priv;

	return cg;
}

void sr_channel_group_free(struct sr_channel_group *cg)
{
	if (!cg)
		return;
	g_free(cg->name);
	g_slist_free(cg->channels);
}

/* Wrapper for GDestroyNotify compatibility. */
static void sg_free(void *p)
{
	return free_signal_group(p);
}

static int check_vers_line(char *line, int need_key,
	gchar **version, gchar **build)
{
	static const char *keyword = "Version";
	static const char *caution = " CAUTION: Do not change the contents of this file.";
	char *read_ptr;
	const char *prev_ptr;

	read_ptr = line;
	if (version)
		*version = NULL;
	if (build)
		*build = NULL;

	/* Expect the 'Version' literal, followed by a DC1 separator. */
	if (need_key) {
		if (strncmp(read_ptr, keyword, strlen(keyword)) != 0)
			return SR_ERR_DATA;
		read_ptr += strlen(keyword);
		if (*read_ptr != DC1_CHR)
			return SR_ERR_DATA;
		read_ptr++;
	}

	/* Expect some "\d+\.\d+" style version string and DC1. */
	if (!*read_ptr)
		return SR_ERR_DATA;
	if (version)
		*version = read_ptr;
	prev_ptr = read_ptr;
	read_ptr += strspn(read_ptr, "0123456789.");
	if (read_ptr == prev_ptr)
		return SR_ERR_DATA;
	if (*read_ptr != DC1_CHR)
		return SR_ERR_DATA;
	*read_ptr++ = '\0';

	/* Expect some "\d+" style build number and DC1. */
	if (!*read_ptr)
		return SR_ERR_DATA;
	if (build)
		*build = read_ptr;
	prev_ptr = read_ptr;
	read_ptr += strspn(read_ptr, "0123456789");
	if (read_ptr == prev_ptr)
		return SR_ERR_DATA;
	if (*read_ptr != DC1_CHR)
		return SR_ERR_DATA;
	*read_ptr++ = '\0';

	/* Expect the 'CAUTION...' text (weak test, only part of the text). */
	if (strncmp(read_ptr, caution, strlen(caution)) != 0)
		return SR_ERR_DATA;
	read_ptr += strlen(caution);

	/* No check for CRLF, due to the weak CAUTION test. */
	return SR_OK;
}

static int process_wire_names(struct context *inc, char **names)
{
	size_t count, idx;

	/*
	 * The 'names' array contains the *wire* names, plus a 'Count'
	 * label for the last column.
	 */
	count = g_strv_length(names);
	if (count != inc->channel_count + 1)
		return SR_ERR_DATA;
	if (strcmp(names[inc->channel_count], "Count") != 0)
		return SR_ERR_DATA;

	for (idx = 0; idx < inc->channel_count; idx++)
		inc->wire_names[idx] = g_strdup(names[idx]);

	return SR_OK;
}

static int process_signal_names(struct context *inc, char **names)
{
	size_t count, idx;

	/*
	 * The 'names' array contains the *signal* names (and no other
	 * entries, unlike the *wire* names).
	 */
	count = g_strv_length(names);
	if (count != inc->channel_count)
		return SR_ERR_DATA;

	for (idx = 0; idx < inc->channel_count; idx++)
		inc->signal_names[idx] = g_strdup(names[idx]);

	return SR_OK;
}

static int process_signal_group(struct context *inc, char **args)
{
	char *name, *wires;
	struct signal_group_desc *desc;
	uint64_t bit_tmpl, bit_mask;
	char *p, *endp;
	size_t idx;

	/*
	 * List of arguments that we receive:
	 * - [0] group name
	 * - [1] - [5] uncertain meaning, four integers and one boolean
	 * - [6] comma separated list of wire indices (zero based)
	 * - [7] - [9] uncertain meaning, a boolean, two integers
	 * - [10] - [35] uncertain meaning, 26 empty columns
	 */

	/* Check for the minimum amount of input data. */
	if (!args)
		return SR_ERR_DATA;
	if (g_strv_length(args) < 7)
		return SR_ERR_DATA;
	name = args[0];
	wires = args[6];

	/* Accept empty names and empty signal lists. Silently ignore. */
	if (!name || !*name)
		return SR_OK;
	if (!wires || !*wires)
		return SR_OK;
	/*
	 * TODO: Introduce a user configurable "ignore" option? Skip the
	 * "All Signals" group by default, and in addition whatever
	 * the user specified?
	 */
	if (strcmp(name, SKIP_SIGNAL_GROUP) == 0) {
		sr_info("Skipping signal group '%s'", name);
		return SR_OK;
	}

	/*
	 * Create the descriptor here to store the member list to. We
	 * cannot access signal names and sigrok channels yet, they
	 * only become avilable at a later point in time.
	 */
	desc = alloc_signal_group(name);
	if (!desc)
		return SR_ERR_MALLOC;
	inc->signal_groups = g_slist_append(inc->signal_groups, desc);

	/*
	 * Determine the bit mask of the group's signals' indices.
	 *
	 * Implementation note: Use a "template" for a single bit, to
	 * avoid portability issues with upper bits. Without this 64bit
	 * intermediate variable, I would not know how to phrase e.g.
	 * (1ULL << 33) in portable, robust, and easy to maintain ways
	 * on all platforms that are supported by sigrok.
	 */
	bit_tmpl = 1UL << 0;
	bit_mask = 0;
	p = wires;
	while (p && *p) {
		endp = NULL;
		idx = strtoul(p, &endp, 0);
		if (!endp || endp == p)
			return SR_ERR_DATA;
		if (*endp && *endp != ',')
			return SR_ERR_DATA;
		p = endp;
		if (*p == ',')
			p++;
		if (idx >= MAX_CHANNELS)
			return SR_ERR_DATA;
		bit_mask = bit_tmpl << idx;
		if (inc->wires_grouped & bit_mask) {
			sr_warn("Not adding signal at index %zu to group %s (multiple assignments)",
				idx, name);
		} else {
			desc->mask |= bit_mask;
			inc->wires_grouped |= bit_mask;
		}
	}
	sr_dbg("'Group' done, name '%s', mask 0x%" PRIx64 ".",
		desc->name, desc->mask);

	return SR_OK;
}

static int process_ungrouped_signals(struct context *inc)
{
	uint64_t bit_mask;
	struct signal_group_desc *desc;

	/*
	 * Only create the "ungrouped" channel group if there are any
	 * groups of other signals already.
	 */
	if (!inc->signal_groups)
		return SR_OK;

	/*
	 * Determine the bit mask of signals that are part of the
	 * acquisition and are not a member of any other group.
	 */
	bit_mask = inc->wires_all_mask;
	bit_mask &= inc->wires_enabled;
	bit_mask &= ~inc->wires_grouped;
	sr_dbg("'ungrouped' check: all 0x%" PRIx64 ", en 0x%" PRIx64 ", grp 0x%" PRIx64 " -> un 0x%" PRIx64 ".",
		inc->wires_all_mask, inc->wires_enabled,
		inc->wires_grouped, bit_mask);
	if (!bit_mask)
		return SR_OK;

	/* Create a sigrok channel group without a name. */
	desc = alloc_signal_group(NULL);
	if (!desc)
		return SR_ERR_MALLOC;
	inc->signal_groups = g_slist_append(inc->signal_groups, desc);
	desc->mask = bit_mask;

	return SR_OK;
}

static int process_enabled_channels(struct context *inc, char **flags)
{
	size_t count, idx;
	uint64_t bits, mask;

	/*
	 * The 'flags' array contains (the textual representation of)
	 * the "enabled" state of the acquisition device's channels.
	 */
	count = g_strv_length(flags);
	if (count != inc->channel_count)
		return SR_ERR_DATA;
	bits = 0;
	mask = 1UL << 0;
	for (idx = 0; idx < inc->channel_count; idx++, mask <<= 1) {
		if (strcmp(flags[idx], "True") == 0)
			bits |= mask;
	}
	inc->wires_enabled = bits;

	return SR_OK;
}

static int process_inverted_channels(struct context *inc, char **flags)
{
	size_t count, idx;
	uint64_t bits, mask;

	/*
	 * The 'flags' array contains (the textual representation of)
	 * the "inverted" state of the acquisition device's channels.
	 */
	count = g_strv_length(flags);
	if (count != inc->channel_count)
		return SR_ERR_DATA;
	bits = 0;
	mask = 1UL << 0;
	for (idx = 0; idx < inc->channel_count; idx++, mask <<= 1) {
		if (strcmp(flags[idx], "True") == 0)
			bits |= mask;
	}
	inc->wires_inverted = bits;

	return SR_OK;
}

static int process_sample_line(struct context *inc, char **values)
{
	size_t count, idx;
	struct sample_data_entry *entry;
	uint64_t mask;
	long conv_ret;
	int rc;

	/*
	 * The 'values' array contains '0'/'1' text representation of
	 * wire's values, as well as a (a textual representation of a)
	 * repeat counter for that set of samples.
	 */
	count = g_strv_length(values);
	if (count != inc->channel_count + 1)
		return SR_ERR_DATA;
	entry = &inc->sample_data_queue[inc->sample_lines_read];
	entry->bits = 0;
	mask = 1UL << 0;
	for (idx = 0; idx < inc->channel_count; idx++, mask <<= 1) {
		if (strcmp(values[idx], "1") == 0)
			entry->bits |= mask;
		if (strcmp(values[idx], "U") == 0)
			inc->wires_undefined |= mask;
	}
	rc = sr_atol(values[inc->channel_count], &conv_ret);
	if (rc != SR_OK)
		return rc;
	entry->repeat = conv_ret;
	inc->samples_got_uncomp += entry->repeat;

	return SR_OK;
}

static int process_keyvalue_line(struct context *inc, char *line)
{
	char *sep, *key, *arg;
	char **args;
	int rc;
	char *version, *build;
	long build_num;
	int wires, samples;
	size_t alloc_size;
	double period, dbl_rate;
	uint64_t int_rate;

	/*
	 * Process lines of the 'SampleData' block. Inspection of the
	 * block got started below in the "regular keyword line" section.
	 * The code here handles the remaining number of lines: Opening
	 * and closing braces, wire names, and sample data sets. Note
	 * that the wire names and sample values are separated by comma,
	 * not by DC1 like other key/value pairs and argument lists.
	 */
	switch (inc->in_sample_data) {
	case SAMPLEDATA_OPEN_BRACE:
		if (strcmp(line, "{") != 0)
			return SR_ERR_DATA;
		inc->in_sample_data++;
		return SR_OK;
	case SAMPLEDATA_WIRES_COUNT:
		while (isspace(*line))
			line++;
		args = g_strsplit(line, ",", 0);
		rc = process_wire_names(inc, args);
		g_strfreev(args);
		if (rc)
			return rc;
		inc->in_sample_data++;
		inc->sample_lines_read = 0;
		return SR_OK;
	case SAMPLEDATA_DATA_LINES:
		while (isspace(*line))
			line++;
		args = g_strsplit(line, ",", 0);
		rc = process_sample_line(inc, args);
		g_strfreev(args);
		if (rc)
			return rc;
		inc->sample_lines_read++;
		if (inc->sample_lines_read == inc->sample_lines_total)
			inc->in_sample_data++;
		return SR_OK;
	case SAMPLEDATA_CLOSE_BRACE:
		if (strcmp(line, "}") != 0)
			return SR_ERR_DATA;
		sr_dbg("'SampleData' done: samples count %" PRIu64 ".",
			inc->samples_got_uncomp);
		inc->sample_lines_fed = 0;
		inc->in_sample_data = SAMPLEDATA_NONE;
		return SR_OK;
	case SAMPLEDATA_NONE:
		/* EMPTY */ /* Fall through to regular keyword-line logic. */
		break;
	}

	/* Process regular key/value lines separated by DC1. */
	key = line;
	sep = strchr(line, DC1_CHR);
	if (!sep)
		return SR_ERR_DATA;
	*sep++ = '\0';
	arg = sep;
	if (strcmp(key, "Version") == 0) {
		rc = check_vers_line(arg, 0, &version, &build);
		if (rc == SR_OK) {
			inc->sw_version = g_strdup(version ? version : "?");
			rc = sr_atol(build, &build_num);
			inc->sw_build = build_num;
		}
		sr_dbg("'Version' line: version %s, build %zu.",
			inc->sw_version, inc->sw_build);
		return rc;
	}
	if (strcmp(key, "AcquiredSamplePeriod") == 0) {
		rc = sr_atod(arg, &period);
		if (rc != SR_OK)
			return rc;
		/*
		 * Implementation detail: The vendor's software provides
		 * 1/2/5 choices in the 1kHz - 500MHz range. Unfortunately
		 * the choice of saving the sample _period_ as a floating
		 * point number in the text file yields inaccurate results
		 * for naive implementations of the conversion (0.1 is an
		 * "odd number" in the computer's internal representation).
		 * The below logic of rounding to integer and then rounding
		 * to full kHz works for the samplerate value's range.
		 * "Simplifying" the implementation will introduce errors.
		 */
		dbl_rate = 1.0 / period;
		int_rate = (uint64_t)(dbl_rate + 0.5);
		int_rate += 500;
		int_rate /= 1000;
		int_rate *= 1000;
		inc->sample_rate = int_rate;
		if (!inc->sample_rate)
			return SR_ERR_DATA;
		sr_dbg("Sample rate: %" PRIu64 ".", inc->sample_rate);
		return SR_OK;
	}
	if (strcmp(key, "AcquiredChannelList") == 0) {
		args = g_strsplit(arg, DC1_STR, 0);
		rc = process_enabled_channels(inc, args);
		g_strfreev(args);
		if (rc)
			return rc;
		sr_dbg("Enabled channels: 0x%" PRIx64 ".",
			inc->wires_enabled);
		return SR_OK;
	}
	if (strcmp(key, "InvertedChannelList") == 0) {
		args = g_strsplit(arg, DC1_STR, 0);
		rc = process_inverted_channels(inc, args);
		g_strfreev(args);
		sr_dbg("Inverted channels: 0x%" PRIx64 ".",
			inc->wires_inverted);
		return SR_OK;
	}
	if (strcmp(key, "Signals") == 0) {
		args = g_strsplit(arg, DC1_STR, 0);
		rc = process_signal_names(inc, args);
		g_strfreev(args);
		if (rc)
			return rc;
		sr_dbg("Got signal names.");
		return SR_OK;
	}
	if (strcmp(key, "SampleData") == 0) {
		args = g_strsplit(arg, DC1_STR, 3);
		if (!args || !args[0] || !args[1]) {
			g_strfreev(args);
			return SR_ERR_DATA;
		}
		rc = sr_atoi(args[0], &wires);
		if (rc) {
			g_strfreev(args);
			return SR_ERR_DATA;
		}
		rc = sr_atoi(args[1], &samples);
		if (rc) {
			g_strfreev(args);
			return SR_ERR_DATA;
		}
		g_strfreev(args);
		if (!wires || !samples)
			return SR_ERR_DATA;
		inc->channel_count = wires;
		inc->sample_lines_total = samples;
		sr_dbg("'SampleData' start: wires %zu, sample lines %zu.",
			inc->channel_count, inc->sample_lines_total);
		if (inc->channel_count > MAX_CHANNELS)
			return SR_ERR_DATA;
		inc->in_sample_data = SAMPLEDATA_OPEN_BRACE;
		alloc_size = sizeof(inc->sample_data_queue[0]);
		alloc_size *= inc->sample_lines_total;
		inc->sample_data_queue = g_malloc0(alloc_size);
		if (!inc->sample_data_queue)
			return SR_ERR_DATA;
		inc->sample_lines_fed = 0;
		return SR_OK;
	}
	if (strcmp(key, "Group") == 0) {
		args = g_strsplit(arg, DC1_STR, 0);
		rc = process_signal_group(inc, args);
		g_strfreev(args);
		if (rc)
			return rc;
		return SR_OK;
	}
	if (strcmp(key, LAST_KEYWORD) == 0) {
		sr_dbg("'" LAST_KEYWORD "' seen, assuming \"header done\".");
		inc->got_header = TRUE;
		return SR_OK;
	}

	/* Unsupported keyword, silently ignore the line. */
	return SR_OK;
}

/* Check for, and isolate another line of text input. */
static int have_text_line(struct sr_input *in, char **line, char **next)
{
	char *sol_ptr, *eol_ptr;

	if (!in || !in->buf || !in->buf->str)
		return 0;
	sol_ptr = in->buf->str;
	eol_ptr = strstr(sol_ptr, CRLF);
	if (!eol_ptr)
		return 0;
	if (line)
		*line = sol_ptr;
	*eol_ptr = '\0';
	eol_ptr += strlen(CRLF);
	if (next)
		*next = eol_ptr;

	return 1;
}

/* Handle line continuation. Have logical lines processed. */
static int process_text_line(struct context *inc, char *line)
{
	char *p;
	int is_cont_end;
	int rc;

	/*
	 * Handle line continuation in the input stream. Notice that
	 * continued lines can start and end on the same input line.
	 * The text between the markers can be empty, too.
	 *
	 * Make the result look like a regular line. Put a DC1 delimiter
	 * between the keyword and the right hand side. Strip the /<DC1>
	 * and <DC1>/ "braces". Put CRLF between all continued parts,
	 * this makes the data appear "most intuitive and natural"
	 * should we e.g. pass on user's notes in a future version.
	 */
	is_cont_end = 0;
	if (!inc->cont_buff) {
		p = strstr(line, CONT_OPEN);
		if (p) {
			/* Start of continuation. */
			inc->cont_buff = g_string_new_len(line, p - line + 1);
			inc->cont_buff->str[inc->cont_buff->len - 1] = DC1_CHR;
			line = p + strlen(CONT_OPEN);
		}
		/* Regular line, fall through to below regular logic. */
	}
	if (inc->cont_buff) {
		p = strstr(line, CONT_CLOSE);
		is_cont_end = p != NULL;
		if (is_cont_end)
			*p = '\0';
		g_string_append_len(inc->cont_buff, line, strlen(line));
		if (!is_cont_end) {
			/* Keep accumulating. */
			g_string_append_len(inc->cont_buff, CRLF, strlen(CRLF));
			return SR_OK;
		}
		/* End of continuation. */
		line = inc->cont_buff->str;
	}

	/*
	 * Process a logical line of input. It either was received from
	 * the caller, or is the result of accumulating continued lines.
	 */
	rc = process_keyvalue_line(inc, line);

	/* Release the accumulation buffer when a continuation ended. */
	if (is_cont_end) {
		g_string_free(inc->cont_buff, TRUE);
		inc->cont_buff = NULL;
	}

	return rc;
}

/* Tell whether received data is sufficient for session feed preparation. */
static int have_header(GString *buf)
{
	const char *assumed_last_key = CRLF LAST_KEYWORD CONT_OPEN;

	if (strstr(buf->str, assumed_last_key))
		return TRUE;

	return FALSE;
}

/* Process/inspect previously received input data. Get header parameters. */
static int parse_header(struct sr_input *in)
{
	struct context *inc;
	char *line, *next;
	int rc;

	inc = in->priv;
	while (have_text_line(in, &line, &next)) {
		rc = process_text_line(inc, line);
		g_string_erase(in->buf, 0, next - line);
		if (rc)
			return rc;
	}

	return SR_OK;
}

/* Create sigrok channels and groups. Allocate the session feed buffer. */
static int create_channels_groups_buffer(struct sr_input *in)
{
	struct context *inc;
	uint64_t mask;
	size_t idx;
	const char *name;
	gboolean enabled;
	struct sr_channel *ch;
	struct sr_dev_inst *sdi;
	GSList *l;
	struct signal_group_desc *desc;
	struct sr_channel_group *cg;

	inc = in->priv;

	mask = 1UL << 0;
	for (idx = 0; idx < inc->channel_count; idx++, mask <<= 1) {
		name = inc->signal_names[idx];
		if (!name || !*name)
			name = inc->wire_names[idx];
		enabled = (inc->wires_enabled & mask) ? TRUE : FALSE;
		ch = sr_channel_new(in->sdi, idx,
			SR_CHANNEL_LOGIC, enabled, name);
		if (!ch)
			return SR_ERR_MALLOC;
		inc->channels = g_slist_append(inc->channels, ch);
	}

	sdi = in->sdi;
	for (l = inc->signal_groups; l; l = l->next) {
		desc = l->data;
		cg = sr_channel_group_new(desc->name, NULL);
		if (!cg)
			return SR_ERR_MALLOC;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
		mask = 1UL << 0;
		for (idx = 0; idx < inc->channel_count; idx++, mask <<= 1) {
			if (!(desc->mask & mask))
				continue;
			ch = g_slist_nth_data(inc->channels, idx);
			if (!ch)
				return SR_ERR_DATA;
			cg->channels = g_slist_append(cg->channels, ch);
		}
	}

	inc->unitsize = (inc->channel_count + 7) / 8;
	inc->samples_per_chunk = CHUNK_SIZE / inc->unitsize;
	inc->samples_in_buffer = 0;
	inc->feed_buffer = g_malloc0(inc->samples_per_chunk * inc->unitsize);
	if (!inc->feed_buffer)
		return SR_ERR_MALLOC;

	return SR_OK;
}

/* Send all accumulated sample data values to the session. */
static int send_buffer(struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct sr_datafeed_logic logic;
	int rc;

	inc = in->priv;
	if (!inc->samples_in_buffer)
		return SR_OK;

	if (!inc->header_sent) {
		rc = std_session_send_df_header(in->sdi);
		if (rc)
			return rc;
		inc->header_sent = TRUE;
	}

	if (inc->sample_rate && !inc->rate_sent) {
		packet.type = SR_DF_META;
		packet.payload = &meta;
		src = sr_config_new(SR_CONF_SAMPLERATE,
			g_variant_new_uint64(inc->sample_rate));
		meta.config = g_slist_append(NULL, src);
		rc = sr_session_send(in->sdi, &packet);
		g_slist_free(meta.config);
		sr_config_free(src);
		if (rc)
			return rc;
		inc->rate_sent = TRUE;
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = inc->unitsize;
	logic.data = inc->feed_buffer;
	logic.length = inc->unitsize * inc->samples_in_buffer;
	rc = sr_session_send(in->sdi, &packet);

	inc->samples_in_buffer = 0;

	if (rc)
		return rc;

	return SR_OK;
}

/*
 * Add N copies of the current sample to the buffer. Send the buffer to
 * the session feed when a maximum amount of data was collected.
 */
static int add_samples(struct sr_input *in, uint64_t samples, size_t count)
{
	struct context *inc;
	uint8_t sample_buffer[sizeof(uint64_t)];
	size_t idx;
	size_t copy_count;
	uint8_t *p;
	int rc;

	inc = in->priv;
	for (idx = 0; idx < inc->unitsize; idx++) {
		sample_buffer[idx] = samples & 0xff;
		samples >>= 8;
	}
	while (count) {
		copy_count = inc->samples_per_chunk - inc->samples_in_buffer;
		if (copy_count > count)
			copy_count = count;
		count -= copy_count;

		p = inc->feed_buffer + inc->samples_in_buffer * inc->unitsize;
		while (copy_count-- > 0) {
			memcpy(p, sample_buffer, inc->unitsize);
			p += inc->unitsize;
			inc->samples_in_buffer++;
		}

		if (inc->samples_in_buffer == inc->samples_per_chunk) {
			rc = send_buffer(in);
			if (rc)
				return rc;
		}
	}

	return SR_OK;
}

/* Pass on previously received samples to the session. */
static int process_queued_samples(struct sr_input *in)
{
	struct context *inc;
	struct sample_data_entry *entry;
	uint64_t sample_bits;
	int rc;

	inc = in->priv;
	while (inc->sample_lines_fed < inc->sample_lines_total) {
		entry = &inc->sample_data_queue[inc->sample_lines_fed++];
		sample_bits = entry->bits;
		sample_bits ^= inc->wires_inverted;
		sample_bits &= inc->wires_enabled;
		rc = add_samples(in, sample_bits, entry->repeat);
		if (rc)
			return rc;
	}

	return SR_OK;
}

/*
 * Create required resources between having read the input file and
 * sending sample data to the session. Send initial packets before
 * sample data follows.
 */
static int prepare_session_feed(struct sr_input *in)
{
	struct context *inc;
	int rc;

	inc = in->priv;
	if (inc->ch_feed_prep)
		return SR_OK;

	/* Got channel names? At least fallbacks? */
	if (!inc->wire_names[0] || !inc->wire_names[0][0])
		return SR_ERR_DATA;
	/* Samples seen? Seen them all? */
	if (!inc->channel_count)
		return SR_ERR_DATA;
	if (!inc->sample_lines_total)
		return SR_ERR_DATA;
	if (inc->in_sample_data)
		return SR_ERR_DATA;
	if (!inc->sample_data_queue)
		return SR_ERR_DATA;
	inc->sample_lines_fed = 0;

	/*
	 * Normalize some variants of input data.
	 * - Let's create a mask for the maximum possible
	 *   bit positions, it will be useful to avoid garbage
	 *   in other code paths, too.
	 * - Input files _might_ specify which channels were
	 *   enabled during acquisition. _Or_ not specify the
	 *   enabled channels, but provide 'U' values in some
	 *   columns. When neither was seen, assume that all
	 *   channels are enabled.
	 * - If there are any signal groups, put all signals into
	 *   an anonymous group that are not part of another group.
	 */
	inc->wires_all_mask = 1UL << 0;
	inc->wires_all_mask <<= inc->channel_count;
	inc->wires_all_mask--;
	sr_dbg("all wires mask: 0x%" PRIx64 ".", inc->wires_all_mask);
	if (!inc->wires_enabled) {
		inc->wires_enabled = ~inc->wires_undefined;
		inc->wires_enabled &= ~inc->wires_all_mask;
		sr_dbg("enabled from undefined: 0x%" PRIx64 ".",
			inc->wires_enabled);
	}
	if (!inc->wires_enabled) {
		inc->wires_enabled = inc->wires_all_mask;
		sr_dbg("enabled from total mask: 0x%" PRIx64 ".",
			inc->wires_enabled);
	}
	sr_dbg("enabled mask: 0x%" PRIx64 ".",
		inc->wires_enabled);
	rc = process_ungrouped_signals(inc);
	if (rc)
		return rc;

	/*
	 * "Start" the session: Create channels, send the DF
	 * header to the session. Optionally send the sample
	 * rate before sample data will be sent.
	 */
	rc = create_channels_groups_buffer(in);
	if (rc)
		return rc;

	inc->ch_feed_prep = TRUE;

	return SR_OK;
}

static int format_match(GHashTable *metadata)
{
	GString *buf, *tmpbuf;
	int rc;
	gchar *version, *build;

	/* Get a copy of the start of the file's content. */
	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
	if (!buf || !buf->str)
		return SR_ERR_ARG;
	tmpbuf = g_string_new_len(buf->str, buf->len);
	if (!tmpbuf || !tmpbuf->str)
		return SR_ERR_MALLOC;

	/* See if we can spot a typical first LPF line. */
	rc = check_vers_line(tmpbuf->str, 1, &version, &build);
	if (rc == SR_OK && version && build) {
		sr_dbg("Looks like a LogicProbe project, version %s, build %s.",
			version, build);
	}
	g_string_free(tmpbuf, TRUE);

	return rc;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;

	(void)options;

	in->sdi = g_malloc0(sizeof(*in->sdi));

	inc = g_malloc0(sizeof(*inc));
	if (!inc)
		return SR_ERR_MALLOC;
	in->priv = inc;

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int rc;

	/* Accumulate another chunk of input data. */
	g_string_append_len(in->buf, buf->str, buf->len);

	/*
	 * Wait for the full header's availability, then process it in a
	 * single call, and set the "ready" flag. Make sure sample data
	 * and the header get processed in disjoint calls to receive(),
	 * the backend requires those separate phases.
	 */
	inc = in->priv;
	if (!inc->got_header) {
		if (!have_header(in->buf))
			return SR_OK;
		rc = parse_header(in);
		if (rc)
			return rc;
		rc = prepare_session_feed(in);
		if (rc)
			return rc;
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	/* Process sample data, after the header got processed. */
	rc = process_queued_samples(in);

	return rc;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int rc;

	/* Nothing to do here if we never started feeding the session. */
	if (!in->sdi_ready)
		return SR_OK;

	/*
	 * Process sample data that may not have been forwarded before.
	 * Flush any potentially queued samples.
	 */
	rc = process_queued_samples(in);
	if (rc)
		return rc;
	rc = send_buffer(in);
	if (rc)
		return rc;

	/* End the session feed if one was started. */
	inc = in->priv;
	if (inc->header_sent) {
		rc = std_session_send_df_end(in->sdi);
		inc->header_sent = FALSE;
	}

	return rc;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;
	size_t idx;

	if (!in)
		return;

	inc = in->priv;
	if (!inc)
		return;

	/*
	 * Release potentially allocated resources. Void all references
	 * and scalars, so that re-runs start out fresh again.
	 */
	g_free(inc->sw_version);
	g_string_free(inc->cont_buff, TRUE);
	g_free(inc->sample_data_queue);
	for (idx = 0; idx < inc->channel_count; idx++)
		g_free(inc->wire_names[idx]);
	for (idx = 0; idx < inc->channel_count; idx++)
		g_free(inc->signal_names[idx]);
	g_slist_free_full(inc->signal_groups, sg_free);
	g_slist_free_full(inc->channels, g_free);
	g_free(inc->feed_buffer);
	memset(inc, 0, sizeof(*inc));
}

static int reset(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;
	cleanup(in);
	inc->ch_feed_prep = FALSE;
	inc->header_sent = FALSE;
	inc->rate_sent = FALSE;
	g_string_truncate(in->buf, 0);

	return SR_OK;
}

static struct sr_option options[] = {
	ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	return options;
}

SR_PRIV struct sr_input_module input_logicport = {
	.id = "logicport",
	.name = "LogicPort File",
	.desc = "Intronix LA1034 LogicPort project",
	.exts = (const char *[]){ "lpf", NULL },
	.metadata = { SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
