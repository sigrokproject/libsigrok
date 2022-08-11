/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Petteri Aimonen <jpa@sr.mail.kapsi.fi>
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2017-2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * The VCD input module has the following options. See the options[]
 * declaration near the bottom of the input module's source file.
 *
 * numchannels: Maximum number of sigrok channels to create. VCD signals
 *   are detected in their order of declaration in the VCD file header,
 *   and mapped to sigrok channels.
 *
 * skip: Allows to skip data at the start of the input file. This can
 *   speed up operation on long captures.
 *   Value < 0: Skip until first timestamp that is listed in the file.
 *     (This is the default behaviour.)
 *   Value = 0: Do not skip, instead generate samples beginning from
 *     timestamp 0.
 *   Value > 0: Start at the given timestamp.
 *
 * downsample: Divide the samplerate by the given factor. This can
 *   speed up operation on long captures.
 *
 * compress: Trim idle periods which are longer than this value to span
 *   only this many timescale ticks. This can speed up operation on long
 *   captures (default 0, don't compress).
 *
 * Based on Verilog standard IEEE Std 1364-2001 Version C
 *
 * Supported features:
 * - $var with 'wire' and 'reg' types of scalar variables
 * - $timescale definition for samplerate
 * - multiple character variable identifiers
 * - same identifer used for multiple signals (identical values)
 * - vector variables (bit vectors)
 * - integer variables (analog signals with 0 digits, passed as single
 *   precision float number)
 * - real variables (analog signals, passed on with single precision,
 *   arbitrary digits value, not user adjustable)
 * - nested $scope, results in prefixed sigrok channel names
 *
 * Most important unsupported features:
 * - $dumpvars initial value declaration (is not an issue if generators
 *   provide sample data for the #0 timestamp, otherwise session data
 *   starts from zero values, and catches up when the signal changes its
 *   state to a supported value)
 *
 * Implementor's note: This input module specifically does _not_ use
 * glib routines where they would hurt performance. Lots of memory
 * allocations increase execution time not by percents but by huge
 * factors. This motivated this module's custom code for splitting
 * words on text lines, and pooling previously allocated buffers.
 *
 * TODO (in arbitrary order)
 * - Map VCD scopes to sigrok channel groups?
 *   - Does libsigrok support nested channel groups? Or is this feature
 *     exclusive to Pulseview?
 * - Check VCD input to VCD output behaviour. Verify that export and
 *   re-import results in identical data (well, VCD's constraints on
 *   timescale values is known to result in differences).
 * - Check the minimum timestamp delta in the input data set, suggest
 *   the downsample=N option to users for reduced resource consumption.
 *   Popular VCD file creation utilities love to specify insanely tiny
 *   timescale values in the pico or even femto seconds range. Which
 *   results in huge sample counts after import, and potentially even
 *   terminates the application due to resource exhaustion. This issue
 *   only will vanish when common libsigrok infrastructure no longer
 *   depends on constant rate streams of samples at discrete points
 *   in time. The current input module implementation has code in place
 *   to gather timestamp statistics, but the most appropriate condition
 *   when to notify users is yet to be found.
 * - Cleanup the implementation.
 *   - Consistent use of the glib API (where appropriate).
 *   - More appropriate variable/function identifiers.
 *   - More robust handling of multi-word input phrases and chunked
 *     input buffers? This implementation assumes that e.g. b[01]+
 *     patterns are complete when they start, and the signal identifier
 *     is available as well. Which may be true assuming that input data
 *     comes in complete text lines.
 *   - See if other input modules have learned lessons that we could
 *     benefit from here as well? Pointless BOM (done), line oriented
 *     processing with EOL variants and with optional last EOL, module
 *     state reset and file re-read (stable channels list), buffered
 *     session feed, synchronized feed for mixed signal sources, digits
 *     or formats support for analog input, single vs double precision,
 *     etc.
 *   - Re-consider logging. Verbosity levels should be acceptable,
 *     but volume is an issue. Drop duplicates, and drop messages from
 *     known good code paths.
 */

#include <config.h>

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_PREFIX "input/vcd"

#define CHUNK_SIZE (4 * 1024 * 1024)
#define SCOPE_SEP '.'

struct context {
	struct vcd_user_opt {
		size_t maxchannels; /* sigrok channels (output) */
		uint64_t downsample;
		uint64_t compress;
		uint64_t skip_starttime;
		gboolean skip_specified;
	} options;
	gboolean use_skip;
	gboolean started;
	gboolean got_header;
	uint64_t prev_timestamp;
	uint64_t samplerate;
	size_t vcdsignals; /* VCD signals (input) */
	GSList *ignored_signals;
	gboolean data_after_timestamp;
	gboolean ignore_end_keyword;
	gboolean skip_until_end;
	GSList *channels;
	size_t unit_size;
	size_t logic_count;
	size_t analog_count;
	uint8_t *current_logic;
	float *current_floats;
	struct {
		size_t max_bits;
		size_t unit_size;
		uint8_t *value;
		size_t sig_count;
	} conv_bits;
	GString *scope_prefix;
	struct feed_queue_logic *feed_logic;
	struct split_state {
		size_t alloced;
		char **words;
		gboolean in_use;
	} split;
	struct ts_stats {
		size_t total_ts_seen;
		uint64_t last_ts_value;
		uint64_t last_ts_delta;
		size_t min_count;
		struct {
			uint64_t delta;
			size_t count;
		} min_items[2];
		uint32_t early_check_shift;
		size_t early_last_emitted;
	} ts_stats;
	struct vcd_prev {
		GSList *sr_channels;
		GSList *sr_groups;
	} prev;
};

struct vcd_channel {
	char *name;
	char *identifier;
	size_t size;
	enum sr_channeltype type;
	size_t array_index;
	size_t byte_idx;
	uint8_t bit_mask;
	char *base_name;
	size_t range_lower, range_upper;
	int submit_digits;
	struct feed_queue_analog *feed_analog;
};

static void free_channel(void *data)
{
	struct vcd_channel *vcd_ch;

	vcd_ch = data;
	if (!vcd_ch)
		return;

	g_free(vcd_ch->name);
	g_free(vcd_ch->identifier);
	g_free(vcd_ch->base_name);
	feed_queue_analog_free(vcd_ch->feed_analog);

	g_free(vcd_ch);
}

/*
 * Another timestamp delta was observed, update statistics: Update the
 * sorted list of minimum values, and increment the occurance counter.
 * Returns the position of the item's statistics slot, or returns a huge
 * invalid index when the current delta is larger than previously found
 * values.
 */
static size_t ts_stats_update_min(struct ts_stats *stats, uint64_t delta)
{
	size_t idx, copy_idx;

	/* Advance over previously recorded values which are smaller. */
	idx = 0;
	while (idx < stats->min_count && stats->min_items[idx].delta < delta)
		idx++;
	if (idx == ARRAY_SIZE(stats->min_items))
		return idx;

	/* Found the exact value that previously was registered? */
	if (stats->min_items[idx].delta == delta) {
		stats->min_items[idx].count++;
		return idx;
	}

	/* Allocate another slot, bubble up larger values as needed. */
	if (stats->min_count < ARRAY_SIZE(stats->min_items))
		stats->min_count++;
	for (copy_idx = stats->min_count - 1; copy_idx > idx; copy_idx--)
		stats->min_items[copy_idx] = stats->min_items[copy_idx - 1];

	/* Start tracking this value in the found or freed slot. */
	memset(&stats->min_items[idx], 0, sizeof(stats->min_items[idx]));
	stats->min_items[idx].delta = delta;
	stats->min_items[idx].count++;

	return idx;
}

/*
 * Intermediate check for extreme oversampling in the input data. Rate
 * limited emission of warnings to avoid noise, "late" emission of the
 * first potential message to avoid false positives, yet need to  emit
 * the messages early (*way* before EOF) to raise awareness.
 *
 * TODO
 * Tune the limits, improve perception and usefulness of these checks.
 * Need to start emitting messages soon enough to be seen by users. Yet
 * avoid unnecessary messages for valid input's idle/quiet phases. Slow
 * input transitions are perfectly legal before bursty phases are seen
 * in the input data. Needs the check become an option, on by default,
 * but suppressable by users?
 */
static void ts_stats_check_early(struct ts_stats *stats)
{
	static const struct {
		uint64_t delta;
		size_t count;
	} *cp, check_points[] = {
		{     100, 1000000, }, /* Still x100 after 1mio transitions. */
		{    1000,  100000, }, /* Still x1k after 100k transitions. */
		{   10000,   10000, }, /* Still x10k after 10k transitions. */
		{ 1000000,    2500, }, /* Still x1m after 2.5k transitions. */
	};

	size_t cp_idx;
	uint64_t seen_delta, check_delta;
	size_t seen_count;

	/* Get the current minimum's value and count. */
	if (!stats->min_count)
		return;
	seen_delta = stats->min_items[0].delta;
	seen_count = stats->min_items[0].count;

	/* Emit at most one weak message per import. */
	if (stats->early_last_emitted)
		return;

	/* Check arbitrary marks, emit rate limited warnings. */
	(void)seen_count;
	check_delta = seen_delta >> stats->early_check_shift;
	for (cp_idx = 0; cp_idx < ARRAY_SIZE(check_points); cp_idx++) {
		cp = &check_points[cp_idx];
		/* No other match can happen below. Done iterating. */
		if (stats->total_ts_seen > cp->count)
			return;
		/* Advance to the next checkpoint description. */
		if (stats->total_ts_seen != cp->count)
			continue;
		/* First occurance of that timestamp count. Check the value. */
		sr_dbg("TS early chk: total %zu, min delta %" PRIu64 " / %" PRIu64 ".",
			cp->count, seen_delta, check_delta);
		if (check_delta < cp->delta)
			return;
		sr_warn("Low change rate? (weak estimate, min TS delta %" PRIu64 " after %zu timestamps)",
			seen_delta, stats->total_ts_seen);
		sr_warn("Consider using the downsample=N option, or increasing its value.");
		stats->early_last_emitted = stats->total_ts_seen;
		return;
	}
}

/* Reset the internal state of the timestamp tracker. */
static int ts_stats_prep(struct context *inc)
{
	struct ts_stats *stats;
	uint64_t down_sample_value;
	uint32_t down_sample_shift;

	stats = &inc->ts_stats;
	memset(stats, 0, sizeof(*stats));

	down_sample_value = inc->options.downsample;
	down_sample_shift = 0;
	while (down_sample_value >= 2) {
		down_sample_shift++;
		down_sample_value /= 2;
	}
	stats->early_check_shift = down_sample_shift;

	return SR_OK;
}

/* Inspect another timestamp that was received. */
static int ts_stats_check(struct ts_stats *stats, uint64_t curr_ts)
{
	uint64_t last_ts, delta;

	last_ts = stats->last_ts_value;
	stats->last_ts_value = curr_ts;
	stats->total_ts_seen++;
	if (stats->total_ts_seen < 2)
		return SR_OK;

	delta = curr_ts - last_ts;
	stats->last_ts_delta = delta;
	(void)ts_stats_update_min(stats, delta);

	ts_stats_check_early(stats);

	return SR_OK;
}

/* Postprocess internal timestamp tracker state. */
static int ts_stats_post(struct context *inc, gboolean ignore_terminal)
{
	struct ts_stats *stats;
	size_t min_idx;
	uint64_t delta, over_sample, over_sample_scaled, suggest_factor;
	enum sr_loglevel log_level;
	gboolean is_suspicious, has_downsample;

	stats = &inc->ts_stats;

	/*
	 * Lookup the smallest timestamp delta which was found during
	 * data import. Ignore the last delta if its timestamp was never
	 * followed by data, and this was the only occurance. Absence of
	 * result data is non-fatal here -- this code exclusively serves
	 * to raise users' awareness of potential pitfalls, but does not
	 * change behaviour of data processing.
	 *
	 * TODO Also filter by occurance count? To not emit warnings when
	 * captured signals only change slowly by design. Only warn when
	 * the sample rate and samples count product exceeds a threshold?
	 * See below for the necessity (and potential) to adjust the log
	 * message's severity and content.
	 */
	min_idx = 0;
	if (ignore_terminal) do {
		if (min_idx >= stats->min_count)
			break;
		delta = stats->last_ts_delta;
		if (stats->min_items[min_idx].delta != delta)
			break;
		if (stats->min_items[min_idx].count != 1)
			break;
		min_idx++;
	} while (0);
	if (min_idx >= stats->min_count)
		return SR_OK;

	/*
	 * TODO Refine the condition whether to notify the user, and
	 * which severity to use after having inspected all input data.
	 * Any detail could get involved which previously was gathered
	 * during data processing: total sample count, channel count
	 * including their data type and bits width, the oversampling
	 * factor (minimum observed "change rate"), or any combination
	 * thereof. The current check is rather simple (unconditional
	 * warning for ratios starting at 100, regardless of sample or
	 * channel count).
	 */
	over_sample = stats->min_items[min_idx].delta;
	over_sample_scaled = over_sample / inc->options.downsample;
	sr_dbg("TS post stats: oversample unscaled %" PRIu64 ", scaled %" PRIu64,
		over_sample, over_sample_scaled);
	if (over_sample_scaled < 10) {
		sr_dbg("TS post stats: Low oversampling ratio, good.");
		return SR_OK;
	}

	/*
	 * Avoid constructing the message from several tiny pieces by
	 * design, because this would be hard on translators. Stick with
	 * complete sentences instead, and accept the redundancy in the
	 * user's interest.
	 */
	log_level = (over_sample_scaled > 20) ? SR_LOG_WARN : SR_LOG_INFO;
	is_suspicious = over_sample_scaled > 20;
	if (is_suspicious) {
		sr_log(log_level, LOG_PREFIX ": "
			"Suspiciously low overall change rate (total min TS delta %" PRIu64 ").",
			over_sample_scaled);
	} else {
		sr_log(log_level, LOG_PREFIX ": "
			"Low overall change rate (total min TS delta %" PRIu64 ").",
			over_sample_scaled);
	}
	has_downsample = inc->options.downsample > 1;
	suggest_factor = inc->options.downsample;
	while (over_sample_scaled >= 10) {
		suggest_factor *= 10;
		over_sample_scaled /= 10;
	}
	if (has_downsample) {
		sr_log(log_level, LOG_PREFIX ": "
			"Suggest higher downsample value, like %" PRIu64 ".",
			suggest_factor);
	} else {
		sr_log(log_level, LOG_PREFIX ": "
			"Suggest to downsample, value like %" PRIu64 ".",
			suggest_factor);
	}

	return SR_OK;
}

static void check_remove_bom(GString *buf)
{
	static const char *bom_text = "\xef\xbb\xbf";

	if (buf->len < strlen(bom_text))
		return;
	if (strncmp(buf->str, bom_text, strlen(bom_text)) != 0)
		return;
	g_string_erase(buf, 0, strlen(bom_text));
}

/*
 * Reads a single VCD section from input file and parses it to name/contents.
 * e.g. $timescale 1ps $end => "timescale" "1ps"
 */
static gboolean parse_section(GString *buf, char **name, char **contents)
{
	static const char *end_text = "$end";

	gboolean status;
	size_t pos, len;
	const char *grab_start, *grab_end;
	GString *sname, *scontent;

	/* Preset falsy return values. Gets updated below. */
	*name = *contents = NULL;
	status = FALSE;

	/* Skip any initial white-space. */
	pos = 0;
	while (pos < buf->len && g_ascii_isspace(buf->str[pos]))
		pos++;

	/* Section tag should start with $. */
	if (buf->str[pos++] != '$')
		return FALSE;

	/* Read the section tag. */
	grab_start = &buf->str[pos];
	while (pos < buf->len && !g_ascii_isspace(buf->str[pos]))
		pos++;
	grab_end = &buf->str[pos];
	sname = g_string_new_len(grab_start, grab_end - grab_start);

	/* Skip whitespace before content. */
	while (pos < buf->len && g_ascii_isspace(buf->str[pos]))
		pos++;

	/* Read the content up to the '$end' marker. */
	scontent = g_string_sized_new(128);
	grab_start = &buf->str[pos];
	grab_end = g_strstr_len(grab_start, buf->len - pos, end_text);
	if (grab_end) {
		/* Advance 'pos' to after '$end' and more whitespace. */
		pos = grab_end - buf->str;
		pos += strlen(end_text);
		while (pos < buf->len && g_ascii_isspace(buf->str[pos]))
			pos++;

		/* Grab the (trimmed) content text. */
		while (grab_end > grab_start && g_ascii_isspace(grab_end[-1]))
			grab_end--;
		len = grab_end - grab_start;
		g_string_append_len(scontent, grab_start, len);
		if (sname->len)
			status = TRUE;

		/* Consume the input text which just was taken. */
		g_string_erase(buf, 0, pos);
	}

	/* Return section name and content if a section was seen. */
	*name = g_string_free(sname, !status);
	*contents = g_string_free(scontent, !status);

	return status;
}

/*
 * The glib routine which splits an input text into a list of words also
 * "provides empty strings" which application code then needs to remove.
 * And copies of the input text get allocated for all words.
 *
 * The repeated memory allocation is acceptable for small workloads like
 * parsing the header sections. But the heavy lifting for sample data is
 * done by DIY code to speedup execution. The use of glib routines would
 * severely hurt throughput. Allocated memory gets re-used while a strict
 * ping-pong pattern is assumed (each text line of input data enters and
 * leaves in a strict symmetrical manner, due to the organization of the
 * receive() routine and parse calls).
 */

/* Remove empty parts from an array returned by g_strsplit(). */
static void remove_empty_parts(gchar **parts)
{
	gchar **src, **dest;

	src = dest = parts;
	while (*src) {
		if (!**src) {
			g_free(*src);
		} else {
			if (dest != src)
				*dest = *src;
			dest++;
		}
		src++;
	}
	*dest = NULL;
}

static char **split_text_line(struct context *inc, char *text, size_t *count)
{
	struct split_state *state;
	size_t counted, alloced, wanted;
	char **words, *p, **new_words;

	state = &inc->split;

	if (count)
		*count = 0;

	if (state->in_use) {
		sr_dbg("coding error, split() called while \"in use\".");
		return NULL;
	}

	/*
	 * Seed allocation when invoked for the first time. Assume
	 * simple logic data, start with a few words per line. Will
	 * automatically adjust with subsequent use.
	 */
	if (!state->alloced) {
		alloced = 20;
		words = g_malloc(sizeof(words[0]) * alloced);
		if (!words)
			return NULL;
		state->alloced = alloced;
		state->words = words;
	}

	/* Start with most recently allocated word list space. */
	alloced = state->alloced;
	words = state->words;
	counted = 0;

	/* As long as more input text remains ... */
	p = text;
	while (*p) {
		/* Resize word list if needed. Just double the size. */
		if (counted + 1 >= alloced) {
			wanted = 2 * alloced;
			new_words = g_realloc(words, sizeof(words[0]) * wanted);
			if (!new_words) {
				return NULL;
			}
			words = new_words;
			alloced = wanted;
			state->words = words;
			state->alloced = alloced;
		}

		/* Skip leading spaces. */
		while (g_ascii_isspace(*p))
			p++;
		if (!*p)
			break;

		/* Add found word to word list. */
		words[counted++] = p;

		/* Find end of the word. Terminate loop upon EOS. */
		while (*p && !g_ascii_isspace(*p))
			p++;
		if (!*p)
			break;

		/* More text follows. Terminate the word. */
		*p++ = '\0';
	}

	/*
	 * NULL terminate the word list. Provide its length so that
	 * calling code need not re-iterate the list to get the count.
	 */
	words[counted] = NULL;
	if (count)
		*count = counted;
	state->in_use = TRUE;

	return words;
}

static void free_text_split(struct context *inc, char **words)
{
	struct split_state *state;

	state = &inc->split;

	if (words && words != state->words) {
		sr_dbg("coding error, free() arg differs from split() result.");
	}

	/* "Double free" finally releases the memory. */
	if (!state->in_use) {
		g_free(state->words);
		state->words = NULL;
		state->alloced = 0;
	}

	/* Mark as no longer in use. */
	state->in_use = FALSE;
}

static gboolean have_header(GString *buf)
{
	static const char *enddef_txt = "$enddefinitions";
	static const char *end_txt = "$end";

	char *p, *p_stop;

	/* Search for "end of definitions" section keyword. */
	p = g_strstr_len(buf->str, buf->len, enddef_txt);
	if (!p)
		return FALSE;
	p += strlen(enddef_txt);

	/* Search for end of section (content expected to be empty). */
	p_stop = &buf->str[buf->len];
	p_stop -= strlen(end_txt);
	while (p < p_stop && g_ascii_isspace(*p))
		p++;
	if (strncmp(p, end_txt, strlen(end_txt)) != 0)
		return FALSE;
	p += strlen(end_txt);

	return TRUE;
}

static int parse_timescale(struct context *inc, char *contents)
{
	uint64_t p, q;

	/*
	 * The standard allows for values 1, 10 or 100
	 * and units s, ms, us, ns, ps and fs.
	 */
	if (sr_parse_period(contents, &p, &q) != SR_OK) {
		sr_err("Parsing $timescale failed.");
		return SR_ERR_DATA;
	}

	inc->samplerate = q / p;
	sr_dbg("Samplerate: %" PRIu64, inc->samplerate);
	if (q % p != 0) {
		/* Does not happen unless time value is non-standard */
		sr_warn("Inexact rounding of samplerate, %" PRIu64 " / %" PRIu64 " to %" PRIu64 " Hz.",
			q, p, inc->samplerate);
	}

	return SR_OK;
}

/*
 * Handle '$scope' and '$upscope' sections in the input file. Assume that
 * input signals have a "base name", which may be ambiguous within the
 * file. These names get declared within potentially nested scopes, which
 * this implementation uses to create longer but hopefully unique and
 * thus more usable sigrok channel names.
 *
 * Track the currently effective scopes in a string variable to simplify
 * the channel name creation. Start from an empty string, then append the
 * scope name and a separator when a new scope opens, and remove the last
 * scope name when a scope closes. This allows to simply prefix basenames
 * with the current scope to get a full name.
 *
 * It's an implementation detail to keep the trailing NUL here in the
 * GString member, to simplify the g_strconcat() call in the channel name
 * creation.
 *
 * TODO
 * - Check whether scope types must get supported, this implementation
 *   does not distinguish between 'module' and 'begin' and what else
 *   may be seen. The first word simply gets ignored.
 * - Check the allowed alphabet for scope names. This implementation
 *   assumes "programming language identifier" style (alphanumeric with
 *   underscores, plus brackets since we've seen them in example files).
 */
static int parse_scope(struct context *inc, char *contents, gboolean is_up)
{
	char *sep_pos, *name_pos;
	char **parts;
	size_t length;

	/*
	 * The 'upscope' case, drop one scope level (if available). Accept
	 * excess 'upscope' calls, assume that a previous 'scope' section
	 * was ignored because it referenced our software package's name.
	 */
	if (is_up) {
		/*
		 * Check for a second right-most separator (and position
		 * right behind that, which is the start of the last
		 * scope component), or fallback to the start of string.
		 * g_string_erase() from that positon to the end to drop
		 * the last component.
		 */
		name_pos = inc->scope_prefix->str;
		do {
			sep_pos = strrchr(name_pos, SCOPE_SEP);
			if (!sep_pos)
				break;
			*sep_pos = '\0';
			sep_pos = strrchr(name_pos, SCOPE_SEP);
			if (!sep_pos)
				break;
			name_pos = ++sep_pos;
		} while (0);
		length = name_pos - inc->scope_prefix->str;
		g_string_truncate(inc->scope_prefix, length);
		g_string_append_c(inc->scope_prefix, '\0');
		sr_dbg("$upscope, prefix now: \"%s\"", inc->scope_prefix->str);
		return SR_OK;
	}

	/*
	 * The 'scope' case, add another scope level. But skip our own
	 * package name, assuming that this is an artificial node which
	 * was emitted by libsigrok's VCD output module.
	 */
	sr_spew("$scope, got: \"%s\"", contents);
	parts = g_strsplit_set(contents, " \r\n\t", 0);
	remove_empty_parts(parts);
	length = g_strv_length(parts);
	if (length != 2) {
		sr_err("Unsupported 'scope' syntax: %s", contents);
		g_strfreev(parts);
		return SR_ERR_DATA;
	}
	name_pos = parts[1];
	if (strcmp(name_pos, PACKAGE_NAME) == 0) {
		sr_info("Skipping scope with application's package name: %s",
			name_pos);
		*name_pos = '\0';
	}
	if (*name_pos) {
		/* Drop NUL, append scope name and separator, and re-add NUL. */
		g_string_truncate(inc->scope_prefix, inc->scope_prefix->len - 1);
		g_string_append_printf(inc->scope_prefix,
			"%s%c%c", name_pos, SCOPE_SEP, '\0');
	}
	g_strfreev(parts);
	sr_dbg("$scope, prefix now: \"%s\"", inc->scope_prefix->str);

	return SR_OK;
}

/**
 * Parse a $var section which describes a VCD signal ("variable").
 *
 * @param[in] inc Input module context.
 * @param[in] contents Input text, content of $var section.
 */
static int parse_header_var(struct context *inc, char *contents)
{
	char **parts;
	size_t length;
	char *type, *size_txt, *id, *ref, *idx;
	gboolean is_reg, is_wire, is_real, is_int;
	gboolean is_str;
	enum sr_channeltype ch_type;
	size_t size, next_size;
	struct vcd_channel *vcd_ch;

	/*
	 * Format of $var or $reg header specs:
	 * $var type size identifier reference [opt-index] $end
	 */
	parts = g_strsplit_set(contents, " \r\n\t", 0);
	remove_empty_parts(parts);
	length = g_strv_length(parts);
	if (length != 4 && length != 5) {
		sr_warn("$var section should have 4 or 5 items");
		g_strfreev(parts);
		return SR_ERR_DATA;
	}

	type = parts[0];
	size_txt = parts[1];
	id = parts[2];
	ref = parts[3];
	idx = parts[4];
	if (idx && !*idx)
		idx = NULL;
	is_reg = g_strcmp0(type, "reg") == 0;
	is_wire = g_strcmp0(type, "wire") == 0;
	is_real = g_strcmp0(type, "real") == 0;
	is_int = g_strcmp0(type, "integer") == 0;
	is_str = g_strcmp0(type, "string") == 0;

	if (is_reg || is_wire) {
		ch_type = SR_CHANNEL_LOGIC;
	} else if (is_real || is_int) {
		ch_type = SR_CHANNEL_ANALOG;
	} else if (is_str) {
		sr_warn("Skipping id %s, name '%s%s', unsupported type '%s'.",
			id, ref, idx ? idx : "", type);
		inc->ignored_signals = g_slist_append(inc->ignored_signals,
			g_strdup(id));
		g_strfreev(parts);
		return SR_OK;
	} else {
		sr_err("Unsupported signal type: '%s'", type);
		g_strfreev(parts);
		return SR_ERR_DATA;
	}

	size = strtol(size_txt, NULL, 10);
	if (ch_type == SR_CHANNEL_ANALOG) {
		if (is_real && size != 32 && size != 64) {
			/*
			 * The VCD input module does not depend on the
			 * specific width of the floating point value.
			 * This is just for information. Upon value
			 * changes, a mere string gets converted to
			 * float, so we may not care at all.
			 *
			 * Strictly speaking we might warn for 64bit
			 * (double precision) declarations, because
			 * sigrok internally uses single precision
			 * (32bit) only.
			 */
			sr_info("Unexpected real width: '%s'", size_txt);
		}
		/* Simplify code paths below, by assuming size 1. */
		size = 1;
	}
	if (!size) {
		sr_warn("Unsupported signal size: '%s'", size_txt);
		g_strfreev(parts);
		return SR_ERR_DATA;
	}
	if (inc->conv_bits.max_bits < size)
		inc->conv_bits.max_bits = size;
	next_size = inc->logic_count + inc->analog_count + size;
	if (inc->options.maxchannels && next_size > inc->options.maxchannels) {
		sr_warn("Skipping '%s%s', exceeds requested channel count %zu.",
			ref, idx ? idx : "", inc->options.maxchannels);
		inc->ignored_signals = g_slist_append(inc->ignored_signals,
			g_strdup(id));
		g_strfreev(parts);
		return SR_OK;
	}

	vcd_ch = g_malloc0(sizeof(*vcd_ch));
	vcd_ch->identifier = g_strdup(id);
	vcd_ch->name = g_strconcat(inc->scope_prefix->str, ref, idx, NULL);
	vcd_ch->size = size;
	vcd_ch->type = ch_type;
	switch (ch_type) {
	case SR_CHANNEL_LOGIC:
		vcd_ch->array_index = inc->logic_count;
		vcd_ch->byte_idx = vcd_ch->array_index / 8;
		vcd_ch->bit_mask = 1 << (vcd_ch->array_index % 8);
		inc->logic_count += size;
		break;
	case SR_CHANNEL_ANALOG:
		vcd_ch->array_index = inc->analog_count++;
		/* TODO: Use proper 'digits' value for this input module. */
		vcd_ch->submit_digits = is_real ? 2 : 0;
		break;
	}
	inc->vcdsignals++;
	sr_spew("VCD signal %zu '%s' ID '%s' (size %zu), sr type %s, idx %zu.",
		inc->vcdsignals, vcd_ch->name,
		vcd_ch->identifier, vcd_ch->size,
		vcd_ch->type == SR_CHANNEL_ANALOG ? "A" : "L",
		vcd_ch->array_index);
	inc->channels = g_slist_append(inc->channels, vcd_ch);
	g_strfreev(parts);

	return SR_OK;
}

/**
 * Construct the name of the nth sigrok channel for a VCD signal.
 *
 * Uses the VCD signal name for scalar types and single-bit signals.
 * Uses "signal.idx" for multi-bit VCD signals without a range spec in
 * their declaration. Uses "signal[idx]" when a range is known and was
 * verified.
 *
 * @param[in] vcd_ch The VCD signal's description.
 * @param[in] idx The sigrok channel's index within the VCD signal's group.
 *
 * @return An allocated text buffer which callers need to release, #NULL
 *   upon failure to create a sigrok channel name.
 */
static char *get_channel_name(struct vcd_channel *vcd_ch, size_t idx)
{
	char *open_pos, *close_pos, *check_pos, *endptr;
	gboolean has_brackets, has_range;
	size_t upper, lower, tmp;
	char *ch_name;

	/* Handle simple scalar types, and single-bit logic first. */
	if (vcd_ch->size <= 1)
		return g_strdup(vcd_ch->name);

	/*
	 * If not done before: Search for a matching pair of brackets in
	 * the right-most position at the very end of the string. Get the
	 * two colon separated numbers between the brackets, which are
	 * the range limits for array indices into the multi-bit signal.
	 * Grab the "base name" of the VCD signal.
	 *
	 * Notice that arrays can get nested. Earlier path components can
	 * be indexed as well, that's why we need the right-most range.
	 * This implementation does not handle bit vectors of size 1 here
	 * by explicit logic. The check for a [0:0] range would even fail.
	 * But the case of size 1 is handled above, and "happens to" give
	 * the expected result (just the VCD signal name).
	 *
	 * This implementation also deals with range limits in the reverse
	 * order, as well as ranges which are not 0-based (like "[4:7]").
	 */
	if (!vcd_ch->base_name) {
		has_range = TRUE;
		open_pos = strrchr(vcd_ch->name, '[');
		close_pos = strrchr(vcd_ch->name, ']');
		if (close_pos && close_pos[1])
			close_pos = NULL;
		has_brackets = open_pos && close_pos && close_pos > open_pos;
		if (!has_brackets)
			has_range = FALSE;
		if (has_range) {
			check_pos = &open_pos[1];
			endptr = NULL;
			upper = strtoul(check_pos, &endptr, 10);
			if (!endptr || *endptr != ':')
				has_range = FALSE;
		}
		if (has_range) {
			check_pos = &endptr[1];
			endptr = NULL;
			lower = strtoul(check_pos, &endptr, 10);
			if (!endptr || endptr != close_pos)
				has_range = FALSE;
		}
		if (has_range && lower > upper) {
			tmp = lower;
			lower = upper;
			upper = tmp;
		}
		if (has_range) {
			if (lower >= upper)
				has_range = FALSE;
			if (upper + 1 - lower != vcd_ch->size)
				has_range = FALSE;
		}
		if (has_range) {
			/* Temporarily patch the VCD channel's name. */
			*open_pos = '\0';
			vcd_ch->base_name = g_strdup(vcd_ch->name);
			*open_pos = '[';
			vcd_ch->range_lower = lower;
			vcd_ch->range_upper = upper;
		}
	}
	has_range = vcd_ch->range_lower + vcd_ch->range_upper;
	if (has_range && idx >= vcd_ch->size)
		has_range = FALSE;
	if (!has_range)
		return g_strdup_printf("%s.%zu", vcd_ch->name, idx);

	/*
	 * Create a sigrok channel name with just the bit's index in
	 * brackets. This avoids "name[7:0].3" results, instead results
	 * in "name[3]".
	 */
	ch_name = g_strdup_printf("%s[%zu]",
		vcd_ch->base_name, vcd_ch->range_lower + idx);
	return ch_name;
}

/*
 * Create (analog or logic) sigrok channels for the VCD signals. Create
 * multiple sigrok channels for vector input since sigrok has no concept
 * of multi-bit signals. Create a channel group for the vector's bits
 * though to reflect that they form a unit. This is beneficial when UIs
 * support optional "collapsed" displays of channel groups (like
 * "parallel bus, hex output").
 *
 * Defer channel creation until after completion of parsing the input
 * file header. Make sure to create all logic channels first before the
 * analog channels get created. This avoids issues with the mapping of
 * channel indices to bitmap positions in the sample buffer.
 */
static void create_channels(const struct sr_input *in,
	struct sr_dev_inst *sdi, enum sr_channeltype ch_type)
{
	struct context *inc;
	size_t ch_idx;
	GSList *l;
	struct vcd_channel *vcd_ch;
	size_t size_idx;
	char *ch_name;
	struct sr_channel_group *cg;
	struct sr_channel *ch;

	inc = in->priv;

	ch_idx = 0;
	if (ch_type > SR_CHANNEL_LOGIC)
		ch_idx += inc->logic_count;
	if (ch_type > SR_CHANNEL_ANALOG)
		ch_idx += inc->analog_count;
	for (l = inc->channels; l; l = l->next) {
		vcd_ch = l->data;
		if (vcd_ch->type != ch_type)
			continue;
		cg = NULL;
		if (vcd_ch->size != 1)
			cg = sr_channel_group_new(sdi, vcd_ch->name, NULL);
		for (size_idx = 0; size_idx < vcd_ch->size; size_idx++) {
			ch_name = get_channel_name(vcd_ch, size_idx);
			sr_dbg("sigrok channel idx %zu, name %s, type %s, en %d.",
				ch_idx, ch_name,
				ch_type == SR_CHANNEL_ANALOG ? "A" : "L", TRUE);
			ch = sr_channel_new(sdi, ch_idx, ch_type, TRUE, ch_name);
			g_free(ch_name);
			ch_idx++;
			if (cg)
				cg->channels = g_slist_append(cg->channels, ch);
		}
	}
}

static void create_feeds(const struct sr_input *in)
{
	struct context *inc;
	GSList *l;
	struct vcd_channel *vcd_ch;
	size_t ch_idx;
	struct sr_channel *ch;

	inc = in->priv;

	/* Create one feed for logic data. */
	if (inc->logic_count) {
		inc->unit_size = (inc->logic_count + 7) / 8;
		inc->feed_logic = feed_queue_logic_alloc(in->sdi,
			CHUNK_SIZE / inc->unit_size, inc->unit_size);
	}

	/* Create one feed per analog channel. */
	for (l = inc->channels; l; l = l->next) {
		vcd_ch = l->data;
		if (vcd_ch->type != SR_CHANNEL_ANALOG)
			continue;
		ch_idx = vcd_ch->array_index;
		ch_idx += inc->logic_count;
		ch = g_slist_nth_data(in->sdi->channels, ch_idx);
		vcd_ch->feed_analog = feed_queue_analog_alloc(in->sdi,
			CHUNK_SIZE / sizeof(float),
			vcd_ch->submit_digits, ch);
	}
}

/*
 * Keep track of a previously created channel list, in preparation of
 * re-reading the input file. Gets called from reset()/cleanup() paths.
 */
static void keep_header_for_reread(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	g_slist_free_full(inc->prev.sr_groups, sr_channel_group_free_cb);
	inc->prev.sr_groups = in->sdi->channel_groups;
	in->sdi->channel_groups = NULL;

	g_slist_free_full(inc->prev.sr_channels, sr_channel_free_cb);
	inc->prev.sr_channels = in->sdi->channels;
	in->sdi->channels = NULL;
}

/*
 * Check whether the input file is being re-read, and refuse operation
 * when essential parameters of the acquisition have changed in ways
 * that are unexpected to calling applications. Gets called after the
 * file header got parsed (again).
 *
 * Changing the channel list across re-imports of the same file is not
 * supported, by design and for valid reasons, see bug #1215 for details.
 * Users are expected to start new sessions when they change these
 * essential parameters in the acquisition's setup. When we accept the
 * re-read file, then make sure to keep using the previous channel list,
 * applications may still reference them.
 */
static gboolean check_header_in_reread(const struct sr_input *in)
{
	struct context *inc;

	if (!in)
		return FALSE;
	inc = in->priv;
	if (!inc)
		return FALSE;
	if (!inc->prev.sr_channels)
		return TRUE;

	if (sr_channel_lists_differ(inc->prev.sr_channels, in->sdi->channels)) {
		sr_err("Channel list change not supported for file re-read.");
		return FALSE;
	}

	g_slist_free_full(in->sdi->channel_groups, sr_channel_group_free_cb);
	in->sdi->channel_groups = inc->prev.sr_groups;
	inc->prev.sr_groups = NULL;

	g_slist_free_full(in->sdi->channels, sr_channel_free_cb);
	in->sdi->channels = inc->prev.sr_channels;
	inc->prev.sr_channels = NULL;

	return TRUE;
}

/* Parse VCD file header sections (rate and variables declarations). */
static int parse_header(const struct sr_input *in, GString *buf)
{
	struct context *inc;
	gboolean status;
	char *name, *contents;
	size_t size;
	int ret;

	inc = in->priv;

	/* Parse sections until complete header was seen. */
	status = FALSE;
	name = contents = NULL;
	inc->conv_bits.max_bits = 1;
	while (parse_section(buf, &name, &contents)) {
		sr_dbg("Section '%s', contents '%s'.", name, contents);

		if (g_strcmp0(name, "enddefinitions") == 0) {
			status = TRUE;
			goto done_section;
		}
		if (g_strcmp0(name, "timescale") == 0) {
			if (parse_timescale(inc, contents) != SR_OK)
				status = FALSE;
			goto done_section;
		}
		if (g_strcmp0(name, "scope") == 0) {
			if (parse_scope(inc, contents, FALSE) != SR_OK)
				status = FALSE;
			goto done_section;
		}
		if (g_strcmp0(name, "upscope") == 0) {
			if (parse_scope(inc, NULL, TRUE) != SR_OK)
				status = FALSE;
			goto done_section;
		}
		if (g_strcmp0(name, "var") == 0) {
			if (parse_header_var(inc, contents) != SR_OK)
				status = FALSE;
			goto done_section;
		}

done_section:
		g_free(name);
		name = NULL;
		g_free(contents);
		contents = NULL;

		if (status)
			break;
	}
	g_free(name);
	g_free(contents);

	inc->got_header = status;
	if (!status)
		return SR_ERR_DATA;

	/* Create sigrok channels here, late, logic before analog. */
	create_channels(in, in->sdi, SR_CHANNEL_LOGIC);
	create_channels(in, in->sdi, SR_CHANNEL_ANALOG);
	if (!check_header_in_reread(in))
		return SR_ERR_DATA;
	create_feeds(in);

	/*
	 * Allocate space for text to number conversion, and buffers to
	 * hold current sample values before submission to the session
	 * feed. Allocate one buffer for all logic bits, and another for
	 * all floating point values of all analog channels.
	 *
	 * The buffers get updated when the VCD input stream communicates
	 * value changes. Upon reception of VCD timestamps, the buffer can
	 * provide the previously received values, to "fill in the gaps"
	 * in the generation of a continuous stream of samples for the
	 * sigrok session.
	 */
	size = (inc->conv_bits.max_bits + 7) / 8;
	inc->conv_bits.unit_size = size;
	inc->conv_bits.value = g_malloc0(size);
	if (!inc->conv_bits.value)
		return SR_ERR_MALLOC;

	size = (inc->logic_count + 7) / 8;
	inc->unit_size = size;
	inc->current_logic = g_malloc0(size);
	if (inc->unit_size && !inc->current_logic)
		return SR_ERR_MALLOC;
	size = sizeof(inc->current_floats[0]) * inc->analog_count;
	inc->current_floats = g_malloc0(size);
	if (size && !inc->current_floats)
		return SR_ERR_MALLOC;
	for (size = 0; size < inc->analog_count; size++)
		inc->current_floats[size] = 0.;

	ret = ts_stats_prep(inc);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/*
 * Add N copies of previously received values to the session, before
 * subsequent value changes will update the data buffer. Locally buffer
 * sample data to minimize the number of send() calls.
 */
static void add_samples(const struct sr_input *in, size_t count, gboolean flush)
{
	struct context *inc;
	GSList *ch_list;
	struct vcd_channel *vcd_ch;
	struct feed_queue_analog *q;
	float value;

	inc = in->priv;

	if (inc->logic_count) {
		feed_queue_logic_submit(inc->feed_logic,
			inc->current_logic, count);
		if (flush)
			feed_queue_logic_flush(inc->feed_logic);
	}
	for (ch_list = inc->channels; ch_list; ch_list = ch_list->next) {
		vcd_ch = ch_list->data;
		if (vcd_ch->type != SR_CHANNEL_ANALOG)
			continue;
		q = vcd_ch->feed_analog;
		if (!q)
			continue;
		value = inc->current_floats[vcd_ch->array_index];
		feed_queue_analog_submit(q, value, count);
		if (flush)
			feed_queue_analog_flush(q);
	}
}

static gint vcd_compare_id(gconstpointer a, gconstpointer b)
{
	return strcmp((const char *)a, (const char *)b);
}

static gboolean is_ignored(struct context *inc, const char *id)
{
	GSList *ignored;

	ignored = g_slist_find_custom(inc->ignored_signals, id, vcd_compare_id);
	return ignored != NULL;
}

/*
 * Get an analog channel's value from a bit pattern (VCD 'integer' type).
 * The implementation assumes a maximum integer width (64bit), the API
 * doesn't (beyond the return data type). The use of SR_CHANNEL_ANALOG
 * channels may further constraint the number of significant digits
 * (current asumption: float -> 23bit).
 */
static float get_int_val(uint8_t *in_bits_data, size_t in_bits_count)
{
	uint64_t int_value;
	size_t byte_count, byte_idx;
	float flt_value; /* typeof(inc->current_floats[0]) */

	/* Convert bit pattern to integer number (limited range). */
	int_value = 0;
	byte_count = (in_bits_count + 7) / 8;
	for (byte_idx = 0; byte_idx < byte_count; byte_idx++) {
		if (byte_idx >= sizeof(int_value))
			break;
		int_value |= *in_bits_data++ << (byte_idx * 8);
	}
	flt_value = int_value;

	return flt_value;
}

/*
 * Set a logic channel's level depending on the VCD signal's identifier
 * and parsed value. Multi-bit VCD values will affect several sigrok
 * channels. One VCD signal name can translate to several sigrok channels.
 */
static void process_bits(struct context *inc, char *identifier,
	uint8_t *in_bits_data, size_t in_bits_count)
{
	size_t size;
	gboolean have_int;
	GSList *l;
	struct vcd_channel *vcd_ch;
	float int_val;
	size_t bit_idx;
	uint8_t *in_bit_ptr, in_bit_mask;
	uint8_t *out_bit_ptr, out_bit_mask;
	uint8_t bit_val;

	size = 0;
	have_int = FALSE;
	int_val = 0;
	for (l = inc->channels; l; l = l->next) {
		vcd_ch = l->data;
		if (g_strcmp0(identifier, vcd_ch->identifier) != 0)
			continue;
		if (vcd_ch->type == SR_CHANNEL_ANALOG) {
			/* Special case for 'integer' VCD signal types. */
			size = vcd_ch->size; /* Flag for "VCD signal found". */
			if (!have_int) {
				int_val = get_int_val(in_bits_data, in_bits_count);
				have_int = TRUE;
			}
			inc->current_floats[vcd_ch->array_index] = int_val;
			continue;
		}
		if (vcd_ch->type != SR_CHANNEL_LOGIC)
			continue;
		sr_spew("Processing %s data, id '%s', ch %zu sz %zu",
			(size == 1) ? "bit" : "vector",
			identifier, vcd_ch->array_index, vcd_ch->size);

		/* Found our (logic) channel. Setup in/out bit positions. */
		size = vcd_ch->size;
		in_bit_ptr = in_bits_data;
		in_bit_mask = 1 << 0;
		out_bit_ptr = &inc->current_logic[vcd_ch->byte_idx];
		out_bit_mask = vcd_ch->bit_mask;

		/*
		 * Pass VCD input bit(s) to sigrok logic bits. Conversion
		 * must be done repeatedly because one VCD signal name
		 * can translate to several sigrok channels, and shifting
		 * a previously computed bit field to another channel's
		 * position in the buffer would be nearly as expensive,
		 * and certain would increase complexity of the code.
		 */
		for (bit_idx = 0; bit_idx < size; bit_idx++) {
			/* Get the bit value from input data. */
			bit_val = 0;
			if (bit_idx < in_bits_count) {
				bit_val = *in_bit_ptr & in_bit_mask;
				in_bit_mask <<= 1;
				if (!in_bit_mask) {
					in_bit_mask = 1 << 0;
					in_bit_ptr++;
				}
			}
			/* Manipulate the sample buffer data image. */
			if (bit_val)
				*out_bit_ptr |= out_bit_mask;
			else
				*out_bit_ptr &= ~out_bit_mask;
			/* Update output position after bitmap update. */
			out_bit_mask <<= 1;
			if (!out_bit_mask) {
				out_bit_mask = 1 << 0;
				out_bit_ptr++;
			}
		}
	}
	if (!size && !is_ignored(inc, identifier))
		sr_warn("VCD signal not found for ID '%s'.", identifier);
}

/*
 * Set an analog channel's value from a floating point number. One
 * VCD signal name can translate to several sigrok channels.
 */
static void process_real(struct context *inc, char *identifier, float real_val)
{
	gboolean found;
	GSList *l;
	struct vcd_channel *vcd_ch;

	found = FALSE;
	for (l = inc->channels; l; l = l->next) {
		vcd_ch = l->data;
		if (vcd_ch->type != SR_CHANNEL_ANALOG)
			continue;
		if (g_strcmp0(identifier, vcd_ch->identifier) != 0)
			continue;

		/* Found our (analog) channel. */
		found = TRUE;
		sr_spew("Processing real data, id '%s', ch %zu, val %.16g",
			identifier, vcd_ch->array_index, real_val);
		inc->current_floats[vcd_ch->array_index] = real_val;
	}
	if (!found && !is_ignored(inc, identifier))
		sr_warn("VCD signal not found for ID '%s'.", identifier);
}

/*
 * Converts a bit position's text character to a number value.
 *
 * TODO Check for complete coverage of Verilog's standard logic values
 * (IEEE-1364). The set is said to be “01XZHUWL-”, which only a part of
 * is handled here. What would be the complete mapping?
 * - 0/L -> bit value 0
 * - 1/H -> bit value 1
 * - X "don't care" -> TODO
 * - Z "high impedance" -> TODO
 * - W "weak(?)" -> TODO
 * - U "undefined" -> TODO
 * - '-' "TODO" -> TODO
 *
 * For simplicity, this input module implementation maps "known low"
 * values to 0, and "known high" values to 1. All other values will
 * end up assuming "low" (return number 0), while callers might warn.
 * It's up to users to provide compatible input data, or accept the
 * warnings. Silently accepting unknown input data is not desirable.
 */
static uint8_t vcd_char_to_value(char bit_char, int *warn)
{

	bit_char = g_ascii_tolower(bit_char);

	/* Convert the "undisputed" variants. */
	if (bit_char == '0' || bit_char == 'l')
		return 0;
	if (bit_char == '1' || bit_char == 'h')
		return 1;

	/* Convert the "uncertain" variants. */
	if (warn)
		*warn = 1;
	if (bit_char == 'x' || bit_char == 'z')
		return 0;
	if (bit_char == 'u')
		return 0;
	if (bit_char == '-')
		return 0;

	/* Unhandled input text. */
	return ~0;
}

/*
 * Check the validity of a VCD string value. It's essential to reliably
 * accept valid data which the community uses in the field, yet robustly
 * reject invalid data for users' awareness. Since IEEE 1800-2017 would
 * not discuss the representation of this data type, it's assumed to not
 * be an official feature of the VCD file format. This implementation is
 * an educated guess after inspection of other arbitrary implementations,
 * not backed by any specification or public documentation.
 *
 * A quick summary of the implemented assumptions: Must be a sequence of
 * ASCII printables. Must not contain whitespace. Might contain escape
 * sequences: A backslash followed by a single character, like '\n' or
 * '\\'. Or a backslash and the letter x followed by two hex digits,
 * like '\x20'. Or a backslash followed by three octal digits, like
 * '\007'. As an exception also accepts a single digit '\0' but only at
 * the text end. The string value may be empty, but must not be NULL.
 *
 * This implementation assumes an ASCII based platform for simplicity
 * and readability. Should be a given on sigrok supported platforms.
 */
static gboolean vcd_string_valid(const char *s)
{
	char c;

	if (!s)
		return FALSE;

	while (*s) {
		c = *s++;
		/* Reject non-printable ASCII chars including DEL. */
		if (c < ' ')
			return FALSE;
		if (c > '~')
			return FALSE;
		/* Deeper inspection of escape sequences. */
		if (c == '\\') {
			c = *s++;
			switch (c) {
			case 'a': /* BEL, bell aka "alarm" */
			case 'b': /* BS, back space */
			case 't': /* TAB, tabulator */
			case 'n': /* NL, newline */
			case 'v': /* VT, vertical tabulator */
			case 'f': /* FF, form feed */
			case 'r': /* CR, carriage return */
			case '"': /* double quotes */
			case '\'': /* tick, single quote */
			case '?': /* question mark */
			case '\\': /* backslash */
				continue;
			case 'x': /* \xNN two hex digits */
				c = *s++;
				if (!g_ascii_isxdigit(c))
					return FALSE;
				c = *s++;
				if (!g_ascii_isxdigit(c))
					return FALSE;
				continue;
			case '0': /* \NNN three octal digits */
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				/* Special case '\0' at end of text. */
				if (c == '0' && !*s)
					return TRUE;
				/*
				 * First digit was covered by the outer
				 * switch(). Two more digits to check.
				 */
				c = *s++;
				if (!g_ascii_isdigit(c) || c > '7')
					return FALSE;
				c = *s++;
				if (!g_ascii_isdigit(c) || c > '7')
					return FALSE;
				continue;
			default:
				return FALSE;
			}
		}
	}

	return TRUE;
}

/* Parse one text line of the data section. */
static int parse_textline(const struct sr_input *in, char *lines)
{
	struct context *inc;
	int ret;
	char **words;
	size_t word_count, word_idx;
	char *curr_word, *next_word, curr_first;
	gboolean is_timestamp, is_section;
	gboolean is_real, is_multibit, is_singlebit, is_string;
	uint64_t timestamp;
	char *identifier, *endptr;
	size_t count;

	inc = in->priv;

	/*
	 * Split the caller's text lines into a list of space separated
	 * words. Note that some of the branches consume the very next
	 * words as well, and assume that both adjacent words will be
	 * available when the first word is seen. This constraint applies
	 * to bit vector data, multi-bit integers and real (float) data,
	 * as well as single-bit data with whitespace before its
	 * identifier (if that's valid in VCD, we'd accept it here).
	 * The fact that callers always pass complete text lines should
	 * make this assumption acceptable.
	 */
	ret = SR_OK;
	words = split_text_line(inc, lines, &word_count);
	for (word_idx = 0; word_idx < word_count; word_idx++) {
		/*
		 * Make the next two words available, to simpilify code
		 * paths below. The second word is optional here.
		 */
		curr_word = words[word_idx];
		if (!curr_word && !curr_word[0])
			continue;
		curr_first = g_ascii_tolower(curr_word[0]);
		next_word = words[word_idx + 1];
		if (next_word && !next_word[0])
			next_word = NULL;

		/*
		 * Optionally skip some sections that can be interleaved
		 * with data (and may or may not be supported by this
		 * input module). If the section is not skipped but the
		 * $end keyword needs to get tracked, specifically handle
		 * this case, for improved robustness (still reject files
		 * which happen to use invalid syntax).
		 */
		if (inc->skip_until_end) {
			if (strcmp(curr_word, "$end") == 0) {
				/* Done with unhandled/unknown section. */
				sr_dbg("done skipping until $end");
				inc->skip_until_end = FALSE;
			} else {
				sr_spew("skipping word: %s", curr_word);
			}
			continue;
		}
		if (inc->ignore_end_keyword) {
			if (strcmp(curr_word, "$end") == 0) {
				sr_dbg("done ignoring $end keyword");
				inc->ignore_end_keyword = FALSE;
				continue;
			}
		}

		/*
		 * There may be $keyword sections inside the data part of
		 * the input file. Do inspect some of the sections' content
		 * but ignore their surrounding keywords. Silently skip
		 * unsupported section types (which transparently covers
		 * $comment sections).
		 */
		is_section = curr_first == '$' && curr_word[1];
		if (is_section) {
			gboolean inspect_data;

			inspect_data = FALSE;
			inspect_data |= g_strcmp0(curr_word, "$dumpvars") == 0;
			inspect_data |= g_strcmp0(curr_word, "$dumpon") == 0;
			inspect_data |= g_strcmp0(curr_word, "$dumpoff") == 0;
			if (inspect_data) {
				/* Ignore keywords, yet parse contents. */
				sr_dbg("%s section, will parse content", curr_word);
				inc->ignore_end_keyword = TRUE;
			} else {
				/* Ignore section from here up to $end. */
				sr_dbg("%s section, will skip until $end", curr_word);
				inc->skip_until_end = TRUE;
			}
			continue;
		}

		/*
		 * Numbers prefixed by '#' are timestamps, which translate
		 * to sigrok sample numbers. Apply optional downsampling,
		 * and apply the 'skip' logic. Check the recent timestamp
		 * for plausibility. Submit the corresponding number of
		 * samples of previously accumulated data values to the
		 * session feed.
		 */
		is_timestamp = curr_first == '#' && g_ascii_isdigit(curr_word[1]);
		if (is_timestamp) {
			endptr = NULL;
			timestamp = strtoull(&curr_word[1], &endptr, 10);
			if (!endptr || *endptr) {
				sr_err("Invalid timestamp: %s.", curr_word);
				ret = SR_ERR_DATA;
				break;
			}
			sr_spew("Got timestamp: %" PRIu64, timestamp);
			ret = ts_stats_check(&inc->ts_stats, timestamp);
			if (ret != SR_OK)
				break;
			if (inc->options.downsample > 1) {
				timestamp /= inc->options.downsample;
				sr_spew("Downsampled timestamp: %" PRIu64, timestamp);
			}

			/*
			 * Skip < 0 => skip until first timestamp.
			 * Skip = 0 => don't skip
			 * Skip > 0 => skip until timestamp >= skip.
			 */
			if (inc->options.skip_specified && !inc->use_skip) {
				sr_dbg("Seeding skip from user spec %" PRIu64,
					inc->options.skip_starttime);
				inc->prev_timestamp = inc->options.skip_starttime;
				inc->use_skip = TRUE;
			}
			if (!inc->use_skip) {
				sr_dbg("Seeding skip from first timestamp");
				inc->options.skip_starttime = timestamp;
				inc->prev_timestamp = timestamp;
				inc->use_skip = TRUE;
				continue;
			}
			if (inc->options.skip_starttime && timestamp < inc->options.skip_starttime) {
				sr_spew("Timestamp skipped, before user spec");
				inc->prev_timestamp = inc->options.skip_starttime;
				continue;
			}
			if (timestamp == inc->prev_timestamp) {
				/*
				 * Ignore repeated timestamps (e.g. sigrok
				 * outputs these). Can also happen when
				 * downsampling makes distinct input values
				 * end up at the same scaled down value.
				 * Also transparently covers the initial
				 * timestamp.
				 */
				sr_spew("Timestamp is identical to previous timestamp");
				continue;
			}
			if (timestamp < inc->prev_timestamp) {
				sr_err("Invalid timestamp: %" PRIu64 " (leap backwards).", timestamp);
				ret = SR_ERR_DATA;
				break;
			}
			if (inc->options.compress) {
				/* Compress long idle periods */
				count = timestamp - inc->prev_timestamp;
				if (count > inc->options.compress) {
					sr_dbg("Long idle period, compressing");
					count = timestamp - inc->options.compress;
					inc->prev_timestamp = count;
				}
			}

			/* Generate samples from prev_timestamp up to timestamp - 1. */
			count = timestamp - inc->prev_timestamp;
			sr_spew("Got a new timestamp, feeding %zu samples", count);
			add_samples(in, count, FALSE);
			inc->prev_timestamp = timestamp;
			inc->data_after_timestamp = FALSE;
			continue;
		}
		inc->data_after_timestamp = TRUE;

		/*
		 * Data values come in different formats, are associated
		 * with channel identifiers, and correspond to the period
		 * of time from the most recent timestamp to the next
		 * timestamp.
		 *
		 * Supported input data formats are:
		 * - S<value> <sep> <id> (value not used, VCD type 'string').
		 * - R<value> <sep> <id> (analog channel, VCD type 'real').
		 * - B<value> <sep> <id> (analog channel, VCD type 'integer').
		 * - B<value> <sep> <id> (logic channels, VCD bit vectors).
		 * - <value> <id> (logic channel, VCD single-bit values).
		 *
		 * Input values can be:
		 * - Floating point numbers.
		 * - Bit strings (which covers multi-bit aka integers
		 *   as well as vectors).
		 * - Single bits.
		 *
		 * Things to note:
		 * - Individual bits can be 0/1 which is supported by
		 *   libsigrok, or x or z which is treated like 0 here
		 *   (sigrok lacks support for ternary logic, neither is
		 *   there support for the full IEEE set of values).
		 * - Single-bit values typically won't be separated from
		 *   the signal identifer, multi-bit values and floats
		 *   are separated (will reference the next word). This
		 *   implementation silently accepts separators for
		 *   single-bit values, too.
		 */
		is_real = curr_first == 'r' && curr_word[1];
		is_multibit = curr_first == 'b' && curr_word[1];
		is_singlebit = curr_first == '0' || curr_first == '1';
		is_singlebit |= curr_first == 'l' || curr_first == 'h';
		is_singlebit |= curr_first == 'x' || curr_first == 'z';
		is_singlebit |= curr_first == 'u' || curr_first == '-';
		is_string = curr_first == 's';
		if (is_real) {
			char *real_text;
			float real_val;

			real_text = &curr_word[1];
			identifier = next_word;
			word_idx++;
			if (!*real_text || !identifier || !*identifier) {
				sr_err("Unexpected real format.");
				ret = SR_ERR_DATA;
				break;
			}
			sr_spew("Got real data %s for id '%s'.",
				real_text, identifier);
			if (sr_atof_ascii(real_text, &real_val) != SR_OK) {
				sr_err("Cannot convert value: %s.", real_text);
				ret = SR_ERR_DATA;
				break;
			}
			process_real(inc, identifier, real_val);
			continue;
		}
		if (is_multibit) {
			char *bits_text_start;
			size_t bit_count;
			char *bits_text, bit_char;
			uint8_t bit_value;
			uint8_t *value_ptr, value_mask;
			GString *bits_val_text;

			/* TODO
			 * Fold in single-bit code path here? To re-use
			 * the X/Z support. Current redundancy is few so
			 * there is little pressure to unify code paths.
			 * Also multi-bit handling is often different
			 * from single-bit handling, so the "unified"
			 * path would often check for special cases. So
			 * we may never unify code paths at all here.
			 */
			bits_text = &curr_word[1];
			identifier = next_word;
			word_idx++;

			if (!*bits_text || !identifier || !*identifier) {
				sr_err("Unexpected integer/vector format.");
				ret = SR_ERR_DATA;
				break;
			}
			sr_spew("Got integer/vector data %s for id '%s'.",
				bits_text, identifier);

			/*
			 * Accept a bit string of arbitrary length (sort
			 * of, within the limits of the previously setup
			 * conversion buffer). The input text omits the
			 * leading zeroes, hence we convert from end to
			 * the start, to get the significant bits. There
			 * should only be errors for invalid input, or
			 * for input that is rather strange (data holds
			 * more bits than the signal's declaration in
			 * the header suggested). Silently accept data
			 * that fits in the conversion buffer, and has
			 * more significant bits than the signal's type
			 * (that'd be non-sence yet acceptable input).
			 */
			bits_text_start = bits_text;
			bits_text += strlen(bits_text);
			bit_count = bits_text - bits_text_start;
			if (bit_count > inc->conv_bits.max_bits) {
				sr_err("Value exceeds conversion buffer: %s",
					bits_text_start);
				ret = SR_ERR_DATA;
				break;
			}
			memset(inc->conv_bits.value, 0, inc->conv_bits.unit_size);
			value_ptr = &inc->conv_bits.value[0];
			value_mask = 1 << 0;
			inc->conv_bits.sig_count = 0;
			while (bits_text > bits_text_start) {
				inc->conv_bits.sig_count++;
				bit_char = *(--bits_text);
				bit_value = vcd_char_to_value(bit_char, NULL);
				if (bit_value == 0) {
					/* EMPTY */
				} else if (bit_value == 1) {
					*value_ptr |= value_mask;
				} else {
					inc->conv_bits.sig_count = 0;
					break;
				}
				value_mask <<= 1;
				if (!value_mask) {
					value_ptr++;
					value_mask = 1 << 0;
				}
			}
			if (!inc->conv_bits.sig_count) {
				sr_err("Unexpected vector format: %s",
					bits_text_start);
				ret = SR_ERR_DATA;
				break;
			}
			if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
				bits_val_text = sr_hexdump_new(inc->conv_bits.value,
					value_ptr - inc->conv_bits.value + 1);
				sr_spew("Vector value: %s.", bits_val_text->str);
				sr_hexdump_free(bits_val_text);
			}

			process_bits(inc, identifier,
				inc->conv_bits.value, inc->conv_bits.sig_count);
			continue;
		}
		if (is_singlebit) {
			char *bits_text, bit_char;
			uint8_t bit_value;

			/* Get the value text, and signal identifier. */
			bits_text = &curr_word[0];
			bit_char = *bits_text;
			if (!bit_char) {
				sr_err("Bit value missing.");
				ret = SR_ERR_DATA;
				break;
			}
			identifier = ++bits_text;
			if (!*identifier) {
				identifier = next_word;
				word_idx++;
			}
			if (!identifier || !*identifier) {
				sr_err("Identifier missing.");
				ret = SR_ERR_DATA;
				break;
			}

			/* Convert value text to single-bit number. */
			bit_value = vcd_char_to_value(bit_char, NULL);
			if (bit_value != 0 && bit_value != 1) {
				sr_err("Unsupported bit value '%c'.", bit_char);
				ret = SR_ERR_DATA;
				break;
			}
			inc->conv_bits.value[0] = bit_value;
			process_bits(inc, identifier, inc->conv_bits.value, 1);
			continue;
		}
		if (is_string) {
			const char *str_value;

			str_value = &curr_word[1];
			identifier = next_word;
			word_idx++;
			if (!vcd_string_valid(str_value)) {
				sr_err("Invalid string data: %s", str_value);
				ret = SR_ERR_DATA;
				break;
			}
			if (!identifier || !*identifier) {
				sr_err("String value without identifier.");
				ret = SR_ERR_DATA;
				break;
			}
			sr_spew("Got string data, id '%s', value \"%s\".",
				identifier, str_value);
			if (!is_ignored(inc, identifier)) {
				sr_err("String value for identifier '%s'.",
					identifier);
				ret = SR_ERR_DATA;
				break;
			}
			continue;
		}

		/* Design choice: Consider unsupported input fatal. */
		sr_err("Unknown token '%s'.", curr_word);
		ret = SR_ERR_DATA;
		break;
	}
	free_text_split(inc, words);

	return ret;
}

static int process_buffer(struct sr_input *in, gboolean is_eof)
{
	struct context *inc;
	uint64_t samplerate;
	GVariant *gvar;
	int ret;
	char *rdptr, *endptr, *trimptr;
	size_t rdlen;

	inc = in->priv;

	/* Send feed header and samplerate (once) before sample data. */
	if (!inc->started) {
		std_session_send_df_header(in->sdi);

		samplerate = inc->samplerate / inc->options.downsample;
		if (samplerate) {
			gvar = g_variant_new_uint64(samplerate);
			sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, gvar);
		}

		inc->started = TRUE;
	}

	/*
	 * Workaround broken generators which output incomplete text
	 * lines. Enforce the trailing line feed. Proper input is not
	 * harmed by another empty line of input data.
	 */
	if (is_eof)
		g_string_append_c(in->buf, '\n');

	/* Find and process complete text lines in the input data. */
	ret = SR_OK;
	rdptr = in->buf->str;
	while (TRUE) {
		rdlen = &in->buf->str[in->buf->len] - rdptr;
		endptr = g_strstr_len(rdptr, rdlen, "\n");
		if (!endptr)
			break;
		trimptr = endptr;
		*endptr++ = '\0';
		while (g_ascii_isspace(*rdptr))
			rdptr++;
		while (trimptr > rdptr && g_ascii_isspace(trimptr[-1]))
			*(--trimptr) = '\0';
		if (!*rdptr) {
			rdptr = endptr;
			continue;
		}
		ret = parse_textline(in, rdptr);
		rdptr = endptr;
		if (ret != SR_OK)
			break;
	}
	rdlen = rdptr - in->buf->str;
	g_string_erase(in->buf, 0, rdlen);

	return ret;
}

static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	GString *buf, *tmpbuf;
	gboolean status;
	char *name, *contents;

	buf = g_hash_table_lookup(metadata,
		GINT_TO_POINTER(SR_INPUT_META_HEADER));
	tmpbuf = g_string_new_len(buf->str, buf->len);

	/*
	 * If we can parse the first section correctly, then it is
	 * assumed that the input is in VCD format.
	 */
	check_remove_bom(tmpbuf);
	status = parse_section(tmpbuf, &name, &contents);
	g_string_free(tmpbuf, TRUE);
	g_free(name);
	g_free(contents);

	if (!status)
		return SR_ERR;

	*confidence = 1;
	return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	GVariant *data;

	inc = g_malloc0(sizeof(*inc));

	data = g_hash_table_lookup(options, "numchannels");
	inc->options.maxchannels = g_variant_get_uint32(data);

	data = g_hash_table_lookup(options, "downsample");
	inc->options.downsample = g_variant_get_uint64(data);
	if (inc->options.downsample < 1)
		inc->options.downsample = 1;

	data = g_hash_table_lookup(options, "compress");
	inc->options.compress = g_variant_get_uint64(data);
	inc->options.compress /= inc->options.downsample;

	data = g_hash_table_lookup(options, "skip");
	if (data) {
		inc->options.skip_specified = TRUE;
		inc->options.skip_starttime = g_variant_get_uint64(data);
		if (inc->options.skip_starttime == ~UINT64_C(0)) {
			inc->options.skip_specified = FALSE;
			inc->options.skip_starttime = 0;
		}
		inc->options.skip_starttime /= inc->options.downsample;
	}

	in->sdi = g_malloc0(sizeof(*in->sdi));
	in->priv = inc;

	inc->scope_prefix = g_string_new("\0");

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;

	inc = in->priv;

	/* Collect all input chunks, potential deferred processing. */
	g_string_append_len(in->buf, buf->str, buf->len);
	if (!inc->got_header && in->buf->len == buf->len)
		check_remove_bom(in->buf);

	/* Must complete reception of the VCD header first. */
	if (!inc->got_header) {
		if (!have_header(in->buf))
			return SR_OK;
		ret = parse_header(in, in->buf);
		if (ret != SR_OK)
			return ret;
		/* sdi is ready, notify frontend. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	/* Process sample data. */
	ret = process_buffer(in, FALSE);

	return ret;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;
	size_t count;

	inc = in->priv;

	/* Must complete processing of previously received chunks. */
	if (in->sdi_ready)
		ret = process_buffer(in, TRUE);
	else
		ret = SR_OK;

	/* Flush most recently queued sample data when EOF is seen. */
	count = inc->data_after_timestamp ? 1 : 0;
	add_samples(in, count, TRUE);

	/* Optionally suggest downsampling after all input data was seen. */
	(void)ts_stats_post(inc, !inc->data_after_timestamp);

	/* Must send DF_END when DF_HEADER was sent before. */
	if (inc->started)
		std_session_send_df_end(in->sdi);

	return ret;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	keep_header_for_reread(in);

	g_slist_free_full(inc->channels, free_channel);
	inc->channels = NULL;
	feed_queue_logic_free(inc->feed_logic);
	inc->feed_logic = NULL;
	g_free(inc->conv_bits.value);
	inc->conv_bits.value = NULL;
	g_free(inc->current_logic);
	inc->current_logic = NULL;
	g_free(inc->current_floats);
	inc->current_floats = NULL;
	g_string_free(inc->scope_prefix, TRUE);
	inc->scope_prefix = NULL;
	g_slist_free_full(inc->ignored_signals, g_free);
	inc->ignored_signals = NULL;
	free_text_split(inc, NULL);
}

static int reset(struct sr_input *in)
{
	struct context *inc;
	struct vcd_user_opt save;
	struct vcd_prev prev;

	inc = in->priv;

	/* Relase previously allocated resources. */
	cleanup(in);
	g_string_truncate(in->buf, 0);

	/* Restore part of the context, init() won't run again. */
	save = inc->options;
	prev = inc->prev;
	memset(inc, 0, sizeof(*inc));
	inc->options = save;
	inc->prev = prev;
	inc->scope_prefix = g_string_new("\0");

	return SR_OK;
}

enum vcd_option_t {
	OPT_NUM_CHANS,
	OPT_DOWN_SAMPLE,
	OPT_SKIP_COUNT,
	OPT_COMPRESS,
	OPT_MAX,
};

static struct sr_option options[] = {
	[OPT_NUM_CHANS] = {
		"numchannels", "Max number of sigrok channels",
		"The maximum number of sigrok channels to create for VCD input signals.",
		NULL, NULL,
	},
	[OPT_DOWN_SAMPLE] = {
		"downsample", "Downsampling factor",
		"Downsample the input file's samplerate, i.e. divide by the specified factor.",
		NULL, NULL,
	},
	[OPT_SKIP_COUNT] = {
		"skip", "Skip this many initial samples",
		"Skip samples until the specified timestamp. "
		"By default samples start at the first timestamp in the file. "
		"Value 0 creates samples starting at timestamp 0. "
		"Values above 0 only start processing at the given timestamp.",
		NULL, NULL,
	},
	[OPT_COMPRESS] = {
		"compress", "Compress idle periods",
		"Compress idle periods which are longer than the specified number of timescale ticks.",
		NULL, NULL,
	},
	[OPT_MAX] = ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[OPT_NUM_CHANS].def = g_variant_ref_sink(g_variant_new_uint32(0));
		options[OPT_DOWN_SAMPLE].def = g_variant_ref_sink(g_variant_new_uint64(1));
		options[OPT_SKIP_COUNT].def = g_variant_ref_sink(g_variant_new_uint64(~UINT64_C(0)));
		options[OPT_COMPRESS].def = g_variant_ref_sink(g_variant_new_uint64(0));
	}

	return options;
}

SR_PRIV struct sr_input_module input_vcd = {
	.id = "vcd",
	.name = "VCD",
	.desc = "Value Change Dump data",
	.exts = (const char*[]){"vcd", NULL},
	.metadata = { SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
