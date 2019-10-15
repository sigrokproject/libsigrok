/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marc Schink <sigrok-dev@marcschink.de>
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include "config.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"	/* String un-quote for channel name from header line. */

#define LOG_PREFIX "input/csv"

#define CHUNK_SIZE	(4 * 1024 * 1024)

/*
 * The CSV input module has the following options:
 *
 * single-column: Specifies the column number which stores the sample data for
 *                single column mode and enables single column mode. Multi
 *                column mode is used if this parameter is omitted.
 *
 * numchannels:   Specifies the number of channels to use. In multi column mode
 *                the number of channels are the number of columns and in single
 *                column mode the number of bits (LSB first) beginning at
 *                'first-channel'.
 *
 * delimiter:     Specifies the delimiter for columns. Must be at least one
 *                character. Comma is used as default delimiter.
 *
 * format:        Specifies the format of the sample data in single column mode.
 *                Available formats are: 'bin', 'hex' and 'oct'. The binary
 *                format is used by default. This option has no effect in multi
 *                column mode.
 *
 * comment:       Specifies the prefix character(s) for comments. No prefix
 *                characters are used by default which disables removing of
 *                comments.
 *
 * samplerate:    Samplerate which the sample data was captured with. Default
 *                value is 0.
 *
 * first-channel: Column number of the first channel in multi column mode and
 *                position of the bit for the first channel in single column mode.
 *                Default value is 0.
 *
 * header:        Determines if the first line should be treated as header
 *                and used for channel names in multi column mode. Empty header
 *                names will be replaced by the channel number. If enabled in
 *                single column mode the first line will be skipped. Usage of
 *                header is disabled by default.
 *
 * startline:     Line number to start processing sample data. Must be greater
 *                than 0. The default line number to start processing is 1.
 */

/*
 * TODO
 *
 * - Determine how the text line handling can get improved, regarding
 *   all of robustness and flexibility and correctness.
 *   - The current implementation splits on "any run of CR and LF". Which
 *     translates to: Line numbers are wrong in the presence of empty
 *     lines in the input stream. See below for an (expensive) fix.
 *   - Dropping support for CR style end-of-line markers could improve
 *     the situation a lot. Code could search for and split on LF, and
 *     trim optional trailing CR. This would result in proper support
 *     for CRLF (Windows) as well as LF (Unix), and allow for correct
 *     line number counts.
 *   - When support for CR-only line termination cannot get dropped,
 *     then the current implementation is inappropriate. Currently the
 *     input stream is scanned for the first occurance of either of the
 *     supported termination styles (which is good). For the remaining
 *     session a consistent encoding of the text lines is assumed (which
 *     is acceptable).
 *   - When line numbers need to be correct and reliable, _and_ the full
 *     set of previously supported line termination sequences are required,
 *     and potentially more are to get added for improved compatibility
 *     with more platforms or generators, then the current approach of
 *     splitting on runs of termination characters needs to get replaced,
 *     by the more expensive approach to scan for and count the initially
 *     determined termination sequence.
 *
 * - Add support for analog input data? (optional)
 *   - Needs a syntax first for user specs which channels (columns) are
 *     logic and which are analog. May need heuristics(?) to guess from
 *     input data in the absence of user provided specs.
 */

/* Single column formats. */
enum single_col_format {
	FORMAT_NONE,	/* Ignore this column. */
	FORMAT_BIN,	/* Bin digits for a set of bits (or just one bit). */
	FORMAT_HEX,	/* Hex digits for a set of bits. */
	FORMAT_OCT,	/* Oct digits for a set of bits. */
};

static const char *col_format_text[] = {
	[FORMAT_NONE] = "unknown",
	[FORMAT_BIN] = "binary",
	[FORMAT_HEX] = "hexadecimal",
	[FORMAT_OCT] = "octal",
};

struct column_details {
	size_t col_nr;
	enum single_col_format text_format;
	size_t channel_offset;
	size_t channel_count;
};

struct context {
	gboolean started;

	/* Current selected samplerate. */
	uint64_t samplerate;
	gboolean samplerate_sent;

	/* Number of logic channels. */
	size_t logic_channels;

	/* Column delimiter (actually separator), comment leader, EOL sequence. */
	GString *delimiter;
	GString *comment;
	char *termination;

	/*
	 * Determines if sample data is stored in multiple columns,
	 * which column to start at, and how many columns to expect.
	 */
	gboolean multi_column_mode;
	size_t first_column;
	size_t column_want_count;
	/* Parameters how to process the columns. */
	struct column_details *column_details;

	/* Line number to start processing. */
	size_t start_line;

	/*
	 * Determines if the first line should be treated as header and used for
	 * channel names in multi column mode.
	 */
	gboolean use_header;
	gboolean header_seen;

	size_t sample_unit_size;	/**!< Byte count for a single sample. */
	uint8_t *sample_buffer;		/**!< Buffer for a single sample. */

	uint8_t *datafeed_buffer;	/**!< Queue for datafeed submission. */
	size_t datafeed_buf_size;
	size_t datafeed_buf_fill;

	/* Current line number. */
	size_t line_number;

	/* List of previously created sigrok channels. */
	GSList *prev_sr_channels;
};

/*
 * Primitive operations to handle sample sets:
 * - Keep a buffer for datafeed submission, capable of holding many
 *   samples (reduces call overhead, improves throughput).
 * - Have a "current sample set" pointer reference one position in that
 *   large samples buffer.
 * - Clear the current sample set before text line inspection, then set
 *   the bits which are found active in the current line of text input.
 *   Phrase the API such that call sites can be kept simple. Advance to
 *   the next sample set between lines, flush the larger buffer as needed
 *   (when it is full, or upon EOF).
 */

static void clear_logic_samples(struct context *inc)
{
	inc->sample_buffer = &inc->datafeed_buffer[inc->datafeed_buf_fill];
	memset(inc->sample_buffer, 0, inc->sample_unit_size);
}

static void set_logic_level(struct context *inc, size_t ch_idx, int on)
{
	size_t byte_idx, bit_idx;
	uint8_t bit_mask;

	if (ch_idx >= inc->logic_channels)
		return;
	if (!on)
		return;

	byte_idx = ch_idx / 8;
	bit_idx = ch_idx % 8;
	bit_mask = 1 << bit_idx;
	inc->sample_buffer[byte_idx] |= bit_mask;
}

static int flush_logic_samples(const struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	uint64_t samplerate;
	struct sr_datafeed_logic logic;
	int rc;

	inc = in->priv;
	if (!inc->datafeed_buf_fill)
		return SR_OK;

	if (inc->samplerate && !inc->samplerate_sent) {
		packet.type = SR_DF_META;
		packet.payload = &meta;
		samplerate = inc->samplerate;
		src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(samplerate));
		meta.config = g_slist_append(NULL, src);
		sr_session_send(in->sdi, &packet);
		g_slist_free(meta.config);
		sr_config_free(src);
		inc->samplerate_sent = TRUE;
	}

	memset(&packet, 0, sizeof(packet));
	memset(&logic, 0, sizeof(logic));
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = inc->sample_unit_size;
	logic.length = inc->datafeed_buf_fill;
	logic.data = inc->datafeed_buffer;

	rc = sr_session_send(in->sdi, &packet);
	if (rc != SR_OK)
		return rc;

	inc->datafeed_buf_fill = 0;
	return SR_OK;
}

static int queue_logic_samples(const struct sr_input *in)
{
	struct context *inc;
	int rc;

	inc = in->priv;
	if (!inc->logic_channels)
		return SR_OK;

	inc->datafeed_buf_fill += inc->sample_unit_size;
	if (inc->datafeed_buf_fill == inc->datafeed_buf_size) {
		rc = flush_logic_samples(in);
		if (rc != SR_OK)
			return rc;
	}
	return SR_OK;
}

static int make_column_details_single(struct context *inc,
	size_t col_nr, size_t bit_count, enum single_col_format format)
{
	struct column_details *details;

	/*
	 * Need at least as many columns to also include the one with
	 * the single-column input data.
	 */
	inc->column_want_count = col_nr;

	/*
	 * Allocate the columns' processing details. Columns are counted
	 * from 1 (user's perspective), array items from 0 (programmer's
	 * perspective).
	 */
	inc->column_details = g_malloc0_n(col_nr, sizeof(inc->column_details[0]));
	details = &inc->column_details[col_nr - 1];
	details->col_nr = col_nr;

	/*
	 * In single-column mode this single column will hold all bits
	 * of all logic channels, in the user specified number format.
	 */
	details->text_format = format;
	details->channel_offset = 0;
	details->channel_count = bit_count;

	return SR_OK;
}

static int make_column_details_multi(struct context *inc,
	size_t first_col, size_t last_col)
{
	struct column_details *details;
	size_t col_nr;

	/*
	 * Need at least as many columns to also include the one with
	 * the last channel's data.
	 */
	inc->column_want_count = last_col;

	/*
	 * Allocate the columns' processing details. Columns are counted
	 * from 1, array items from 0.
	 * In multi-column mode each column will hold a single bit for
	 * the respective channel.
	 */
	inc->column_details = g_malloc0_n(last_col, sizeof(inc->column_details[0]));
	for (col_nr = first_col; col_nr <= last_col; col_nr++) {
		details = &inc->column_details[col_nr - 1];
		details->col_nr = col_nr;
		details->text_format = FORMAT_BIN;
		details->channel_offset = col_nr - first_col;
		details->channel_count = 1;
	}


	return SR_OK;
}

static const struct column_details *lookup_column_details(struct context *inc, size_t nr)
{
	if (!inc || !inc->column_details)
		return NULL;
	if (!nr || nr > inc->column_want_count)
		return NULL;
	return &inc->column_details[nr - 1];
}

/*
 * Primitive operations for text input: Strip comments off text lines.
 * Split text lines into columns. Process input text for individual
 * columns.
 */

static void strip_comment(char *buf, const GString *prefix)
{
	char *ptr;

	if (!prefix->len)
		return;

	if ((ptr = strstr(buf, prefix->str))) {
		*ptr = '\0';
		g_strstrip(buf);
	}
}

/**
 * @brief Splits a text line into a set of columns.
 *
 * @param[in] buf	The input text line to split.
 * @param[in] inc	The input module's context.
 *
 * @returns An array of strings, representing the columns' text.
 *
 * This routine splits a text line on previously determined separators.
 */
static char **split_line(char *buf, struct context *inc)
{
	return g_strsplit(buf, inc->delimiter->str, 0);
}

/**
 * @brief Parse a multi-bit field into several logic channels.
 *
 * @param[in] column	The input text, a run of bin/hex/oct digits.
 * @param[in] inc	The input module's context.
 * @param[in] details	The column processing details.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Invalid input data (empty, or format error).
 *
 * This routine modifies the logic levels in the current sample set,
 * based on the text input and a user provided format spec.
 */
static int parse_logic(const char *column, struct context *inc,
	const struct column_details *details)
{
	size_t length, ch_rem, ch_idx, ch_inc;
	const char *rdptr;
	char c;
	gboolean valid;
	const char *type_text;
	uint8_t bits;

	/*
	 * Prepare to read the digits from the text end towards the start.
	 * A digit corresponds to a variable number of channels (depending
	 * on the value's radix). Prepare the mapping of text digits to
	 * (a number of) logic channels.
	 */
	length = strlen(column);
	if (!length) {
		sr_err("Column %zu in line %zu is empty.", details->col_nr,
			inc->line_number);
		return SR_ERR;
	}
	rdptr = &column[length];
	ch_idx = details->channel_offset;
	ch_rem = details->channel_count;

	/*
	 * Get another digit and derive up to four logic channels' state from
	 * it. Make sure to not process more bits than the column has channels
	 * associated with it.
	 */
	while (rdptr > column && ch_rem) {
		/* Check for valid digits according to the input radix. */
		c = *(--rdptr);
		switch (details->text_format) {
		case FORMAT_BIN:
			valid = g_ascii_isxdigit(c) && c < '2';
			ch_inc = 1;
			break;
		case FORMAT_OCT:
			valid = g_ascii_isxdigit(c) && c < '8';
			ch_inc = 3;
			break;
		case FORMAT_HEX:
			valid = g_ascii_isxdigit(c);
			ch_inc = 4;
			break;
		default:
			valid = FALSE;
			break;
		}
		if (!valid) {
			type_text = col_format_text[details->text_format];
			sr_err("Invalid text '%s' in %s type column %zu in line %zu.",
				column, type_text, details->col_nr, inc->line_number);
			return SR_ERR;
		}
		/* Use the digit's bits for logic channels' data. */
		bits = g_ascii_xdigit_value(c);
		switch (details->text_format) {
		case FORMAT_HEX:
			if (ch_rem >= 4) {
				ch_rem--;
				set_logic_level(inc, ch_idx + 3, bits & (1 << 3));
			}
			/* FALLTHROUGH */
		case FORMAT_OCT:
			if (ch_rem >= 3) {
				ch_rem--;
				set_logic_level(inc, ch_idx + 2, bits & (1 << 2));
			}
			if (ch_rem >= 2) {
				ch_rem--;
				set_logic_level(inc, ch_idx + 1, bits & (1 << 1));
			}
			/* FALLTHROUGH */
		case FORMAT_BIN:
			ch_rem--;
			set_logic_level(inc, ch_idx + 0, bits & (1 << 0));
			break;
		case FORMAT_NONE:
			/* ShouldNotHappen(TM), but silences compiler warning. */
			return SR_ERR;
		}
		ch_idx += ch_inc;
	}
	/*
	 * TODO Determine whether the availability of extra input data
	 * for unhandled logic channels is worth warning here. In this
	 * implementation users are in control, and can have the more
	 * significant bits ignored (which can be considered a feature
	 * and not really a limitation).
	 */

	return SR_OK;
}

/**
 * @brief Parse routine which ignores the input text.
 *
 * This routine exists to unify dispatch code paths, mapping input file
 * columns' data types to their respective parse routines.
 */
static int parse_ignore(const char *column, struct context *inc,
	const struct column_details *details)
{
	(void)column;
	(void)inc;
	(void)details;
	return SR_OK;
}

typedef int (*col_parse_cb)(const char *column, struct context *inc,
	const struct column_details *details);

static const col_parse_cb col_parse_funcs[] = {
	[FORMAT_NONE] = parse_ignore,
	[FORMAT_BIN] = parse_logic,
	[FORMAT_OCT] = parse_logic,
	[FORMAT_HEX] = parse_logic,
};

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	size_t single_column;
	const char *s;
	enum single_col_format format;
	int ret;

	in->sdi = g_malloc0(sizeof(*in->sdi));
	in->priv = inc = g_malloc0(sizeof(*inc));

	single_column = g_variant_get_uint32(g_hash_table_lookup(options, "single-column"));
	inc->multi_column_mode = single_column == 0;

	inc->logic_channels = g_variant_get_uint32(g_hash_table_lookup(options, "numchannels"));

	inc->delimiter = g_string_new(g_variant_get_string(
			g_hash_table_lookup(options, "delimiter"), NULL));
	if (!inc->delimiter->len) {
		sr_err("Column delimiter cannot be empty.");
		return SR_ERR_ARG;
	}

	s = g_variant_get_string(g_hash_table_lookup(options, "format"), NULL);
	if (g_ascii_strncasecmp(s, "bin", 3) == 0) {
		format = FORMAT_BIN;
	} else if (g_ascii_strncasecmp(s, "hex", 3) == 0) {
		format = FORMAT_HEX;
	} else if (g_ascii_strncasecmp(s, "oct", 3) == 0) {
		format = FORMAT_OCT;
	} else {
		sr_err("Invalid format: '%s'", s);
		return SR_ERR_ARG;
	}

	inc->comment = g_string_new(g_variant_get_string(
			g_hash_table_lookup(options, "comment"), NULL));
	if (g_string_equal(inc->comment, inc->delimiter)) {
		/*
		 * Using the same sequence as comment leader and column
		 * delimiter won't work. The user probably specified ';'
		 * as the column delimiter but did not adjust the comment
		 * leader. Try DWIM, drop comment strippin support here.
		 */
		sr_warn("Comment leader and column delimiter conflict, disabling comment support.");
		g_string_truncate(inc->comment, 0);
	}

	inc->samplerate = g_variant_get_uint64(g_hash_table_lookup(options, "samplerate"));

	inc->first_column = g_variant_get_uint32(g_hash_table_lookup(options, "first-column"));

	inc->use_header = g_variant_get_boolean(g_hash_table_lookup(options, "header"));

	inc->start_line = g_variant_get_uint32(g_hash_table_lookup(options, "startline"));
	if (inc->start_line < 1) {
		sr_err("Invalid start line %zu.", inc->start_line);
		return SR_ERR_ARG;
	}

	/*
	 * Derive the set of columns to inspect and their respective
	 * formats from simple input specs. Remain close to the previous
	 * set of option keywords and their meaning. Exclusively support
	 * a single column with multiple bits in it, or an adjacent set
	 * of colums with one bit each. The latter may not know the total
	 * column count here (when the user omitted the spec), and will
	 * derive it from the first text line of the input file.
	 */
	if (single_column && inc->logic_channels) {
		sr_dbg("DIAG Got single column (%zu) and channels (%zu).",
			single_column, inc->logic_channels);
		sr_dbg("DIAG -> column %zu, %zu bits in %s format.",
			single_column, inc->logic_channels,
			col_format_text[format]);
		ret = make_column_details_single(inc,
			single_column, inc->logic_channels, format);
		if (ret != SR_OK)
			return ret;
	} else if (inc->multi_column_mode) {
		sr_dbg("DIAG Got multi-column, first column %zu, count %zu.",
			inc->first_column, inc->logic_channels);
		if (inc->logic_channels) {
			sr_dbg("DIAG -> columns %zu-%zu, 1 bit each.",
				inc->first_column,
				inc->first_column + inc->logic_channels - 1);
			ret = make_column_details_multi(inc, inc->first_column,
				inc->first_column + inc->logic_channels - 1);
			if (ret != SR_OK)
				return ret;
		} else {
			sr_dbg("DIAG -> incomplete spec, have to update later.");
		}
	} else {
		sr_err("Unknown or unsupported combination of option values.");
		return SR_ERR_ARG;
	}

	return SR_OK;
}

/*
 * Check the channel list for consistency across file re-import. See
 * the VCD input module for more details and motivation.
 */

static void keep_header_for_reread(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;
	g_slist_free_full(inc->prev_sr_channels, sr_channel_free_cb);
	inc->prev_sr_channels = in->sdi->channels;
	in->sdi->channels = NULL;
}

static int check_header_in_reread(const struct sr_input *in)
{
	struct context *inc;

	if (!in)
		return FALSE;
	inc = in->priv;
	if (!inc)
		return FALSE;
	if (!inc->prev_sr_channels)
		return TRUE;

	if (sr_channel_lists_differ(inc->prev_sr_channels, in->sdi->channels)) {
		sr_err("Channel list change not supported for file re-read.");
		return FALSE;
	}
	g_slist_free_full(in->sdi->channels, sr_channel_free_cb);
	in->sdi->channels = inc->prev_sr_channels;
	inc->prev_sr_channels = NULL;

	return TRUE;
}

static const char *delim_set = "\r\n";

static const char *get_line_termination(GString *buf)
{
	const char *term;

	term = NULL;
	if (g_strstr_len(buf->str, buf->len, "\r\n"))
		term = "\r\n";
	else if (memchr(buf->str, '\n', buf->len))
		term = "\n";
	else if (memchr(buf->str, '\r', buf->len))
		term = "\r";

	return term;
}

static int initial_parse(const struct sr_input *in, GString *buf)
{
	struct context *inc;
	GString *channel_name;
	size_t num_columns, ch_idx, ch_name_idx, col_idx, col_nr;
	size_t line_number, line_idx;
	int ret;
	char **lines, *line, **columns, *column;
	const char *col_caption;
	gboolean got_caption;
	const struct column_details *detail;

	ret = SR_OK;
	inc = in->priv;
	columns = NULL;

	line_number = 0;
	lines = g_strsplit_set(buf->str, delim_set, 0);
	for (line_idx = 0; (line = lines[line_idx]); line_idx++) {
		line_number++;
		if (inc->start_line > line_number) {
			sr_spew("Line %zu skipped (before start).", line_number);
			continue;
		}
		if (line[0] == '\0') {
			sr_spew("Blank line %zu skipped.", line_number);
			continue;
		}
		strip_comment(line, inc->comment);
		if (line[0] == '\0') {
			sr_spew("Comment-only line %zu skipped.", line_number);
			continue;
		}

		/* Reached first proper line. */
		break;
	}
	if (!line) {
		/* Not enough data for a proper line yet. */
		ret = SR_ERR_NA;
		goto out;
	}

	/* See how many columns the current line has. */
	columns = split_line(line, inc);
	if (!columns) {
		sr_err("Error while parsing line %zu.", line_number);
		ret = SR_ERR;
		goto out;
	}
	num_columns = g_strv_length(columns);
	if (!num_columns) {
		sr_err("Error while parsing line %zu.", line_number);
		ret = SR_ERR;
		goto out;
	}
	sr_dbg("DIAG Got %zu columns in text line: %s.", num_columns, line);

	/* Optionally update incomplete multi-column specs. */
	if (inc->multi_column_mode && !inc->logic_channels) {
		inc->logic_channels = num_columns - inc->first_column + 1;
		sr_dbg("DIAG -> multi-column update: columns %zu-%zu, 1 bit each.",
			inc->first_column,
			inc->first_column + inc->logic_channels - 1);
		ret = make_column_details_multi(inc, inc->first_column,
			inc->first_column + inc->logic_channels - 1);
		if (ret != SR_OK)
			goto out;
	}

	/*
	 * Assume all lines have equal length (column count). Bail out
	 * early on suspicious or insufficient input data (check input
	 * which became available here against previous user specs or
	 * auto-determined properties, regardless of layout variant).
	 */
	if (num_columns < inc->column_want_count) {
		sr_err("Insufficient input text width for desired data amount, got %zu but want %zu columns.",
			num_columns, inc->column_want_count);
		ret = SR_ERR;
		goto out;
	}

	/*
	 * Determine channel names. Optionally use text from a header
	 * line (when requested by the user, and only works in multi
	 * column mode). In the absence of header text, or in single
	 * column mode, channels are assigned rather generic names.
	 *
	 * Manipulation of the column's caption is acceptable here, the
	 * header line will never get processed another time.
	 */
	channel_name = g_string_sized_new(64);
	for (col_idx = 0; col_idx < inc->column_want_count; col_idx++) {

		col_nr = col_idx + 1;
		detail = lookup_column_details(inc, col_nr);
		if (detail->text_format == FORMAT_NONE)
			continue;
		column = columns[col_idx];
		col_caption = sr_scpi_unquote_string(column);
		got_caption = inc->use_header && *col_caption;
		sr_dbg("DIAG col %zu, ch count %zu, text %s.",
			col_nr, detail->channel_count, col_caption);
		for (ch_idx = 0; ch_idx < detail->channel_count; ch_idx++) {
			ch_name_idx = detail->channel_offset + ch_idx;
			if (got_caption && detail->channel_count == 1)
				g_string_assign(channel_name, col_caption);
			else if (got_caption)
				g_string_printf(channel_name, "%s[%zu]",
					col_caption, ch_idx);
			else
				g_string_printf(channel_name, "%zu", ch_name_idx);
			sr_dbg("DIAG ch idx %zu, name %s.", ch_name_idx, channel_name->str);
			sr_channel_new(in->sdi, ch_name_idx, SR_CHANNEL_LOGIC, TRUE,
				channel_name->str);
		}
	}
	g_string_free(channel_name, TRUE);
	if (!check_header_in_reread(in)) {
		ret = SR_ERR_DATA;
		goto out;
	}

	/*
	 * Calculate the minimum buffer size to store the set of samples
	 * of all channels (unit size). Determine a larger buffer size
	 * for datafeed submission that is a multiple of the unit size.
	 * Allocate the larger buffer, the "sample buffer" will point
	 * to a location within that large buffer later.
	 */
	inc->sample_unit_size = (inc->logic_channels + 7) / 8;
	inc->datafeed_buf_size = CHUNK_SIZE;
	inc->datafeed_buf_size *= inc->sample_unit_size;
	inc->datafeed_buffer = g_malloc(inc->datafeed_buf_size);
	inc->datafeed_buf_fill = 0;

out:
	if (columns)
		g_strfreev(columns);
	g_strfreev(lines);

	return ret;
}

/*
 * Gets called from initial_receive(), which runs until the end-of-line
 * encoding of the input stream could get determined. Assumes that this
 * routine receives enough buffered initial input data to either see the
 * BOM when there is one, or that no BOM will follow when a text line
 * termination sequence was seen. Silently drops the UTF-8 BOM sequence
 * from the input buffer if one was seen. Does not care to protect
 * against multiple execution or dropping the BOM multiple times --
 * there should be at most one in the input stream.
 */
static void initial_bom_check(const struct sr_input *in)
{
	static const char *utf8_bom = "\xef\xbb\xbf";

	if (in->buf->len < strlen(utf8_bom))
		return;
	if (strncmp(in->buf->str, utf8_bom, strlen(utf8_bom)) != 0)
		return;
	g_string_erase(in->buf, 0, strlen(utf8_bom));
}

static int initial_receive(const struct sr_input *in)
{
	struct context *inc;
	GString *new_buf;
	int len, ret;
	char *p;
	const char *termination;

	initial_bom_check(in);

	inc = in->priv;

	termination = get_line_termination(in->buf);
	if (!termination)
		/* Don't have a full line yet. */
		return SR_ERR_NA;

	p = g_strrstr_len(in->buf->str, in->buf->len, termination);
	if (!p)
		/* Don't have a full line yet. */
		return SR_ERR_NA;
	len = p - in->buf->str - 1;
	new_buf = g_string_new_len(in->buf->str, len);
	g_string_append_c(new_buf, '\0');

	inc->termination = g_strdup(termination);

	if (in->buf->str[0] != '\0')
		ret = initial_parse(in, new_buf);
	else
		ret = SR_OK;

	g_string_free(new_buf, TRUE);

	return ret;
}

static int process_buffer(struct sr_input *in, gboolean is_eof)
{
	struct context *inc;
	gsize num_columns;
	size_t line_idx, col_idx, col_nr;
	const struct column_details *details;
	col_parse_cb parse_func;
	int ret;
	char *p, **lines, *line, **columns, *column;

	inc = in->priv;
	if (!inc->started) {
		std_session_send_df_header(in->sdi);
		inc->started = TRUE;
	}

	/*
	 * Consider empty input non-fatal. Keep accumulating input until
	 * at least one full text line has become available. Grab the
	 * maximum amount of accumulated data that consists of full text
	 * lines, and process what has been received so far, leaving not
	 * yet complete lines for the next invocation.
	 *
	 * Enforce that all previously buffered data gets processed in
	 * the "EOF" condition. Do not insist in the presence of the
	 * termination sequence for the last line (may often be missing
	 * on Windows). A present termination sequence will just result
	 * in the "execution of an empty line", and does not harm.
	 */
	if (!in->buf->len)
		return SR_OK;
	if (is_eof) {
		p = in->buf->str + in->buf->len;
	} else {
		p = g_strrstr_len(in->buf->str, in->buf->len, inc->termination);
		if (!p)
			return SR_ERR;
		*p = '\0';
		p += strlen(inc->termination);
	}
	g_strstrip(in->buf->str);

	ret = SR_OK;
	lines = g_strsplit_set(in->buf->str, delim_set, 0);
	for (line_idx = 0; (line = lines[line_idx]); line_idx++) {
		inc->line_number++;
		if (line[0] == '\0') {
			sr_spew("Blank line %zu skipped.", inc->line_number);
			continue;
		}

		/* Remove trailing comment. */
		strip_comment(line, inc->comment);
		if (line[0] == '\0') {
			sr_spew("Comment-only line %zu skipped.", inc->line_number);
			continue;
		}

		/* Skip the header line, its content was used as the channel names. */
		if (inc->use_header && !inc->header_seen) {
			sr_spew("Header line %zu skipped.", inc->line_number);
			inc->header_seen = TRUE;
			continue;
		}

		/* Split the line into columns, check for minimum length. */
		columns = split_line(line, inc);
		if (!columns) {
			sr_err("Error while parsing line %zu.", inc->line_number);
			g_strfreev(lines);
			return SR_ERR;
		}
		num_columns = g_strv_length(columns);
		if (num_columns < inc->column_want_count) {
			sr_err("Insufficient column count %zu in line %zu.",
				num_columns, inc->line_number);
			g_strfreev(columns);
			g_strfreev(lines);
			return SR_ERR;
		}

		/* Have the columns of the current text line processed. */
		clear_logic_samples(inc);
		for (col_idx = 0; col_idx < inc->column_want_count; col_idx++) {
			column = columns[col_idx];
			col_nr = col_idx + 1;
			details = lookup_column_details(inc, col_nr);
			if (!details || !details->text_format)
				continue;
			parse_func = col_parse_funcs[details->text_format];
			if (!parse_func)
				continue;
			ret = parse_func(column, inc, details);
			if (ret != SR_OK) {
				g_strfreev(columns);
				g_strfreev(lines);
				return SR_ERR;
			}
		}

		/* Send sample data to the session bus (buffered). */
		ret = queue_logic_samples(in);
		if (ret != SR_OK) {
			sr_err("Sending samples failed.");
			g_strfreev(columns);
			g_strfreev(lines);
			return SR_ERR;
		}

		g_strfreev(columns);
	}
	g_strfreev(lines);
	g_string_erase(in->buf, 0, p - in->buf->str);

	return ret;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;

	g_string_append_len(in->buf, buf->str, buf->len);

	inc = in->priv;
	if (!inc->termination) {
		ret = initial_receive(in);
		if (ret == SR_ERR_NA)
			/* Not enough data yet. */
			return SR_OK;
		else if (ret != SR_OK)
			return SR_ERR;

		/* sdi is ready, notify frontend. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	ret = process_buffer(in, FALSE);

	return ret;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	if (in->sdi_ready)
		ret = process_buffer(in, TRUE);
	else
		ret = SR_OK;
	if (ret != SR_OK)
		return ret;

	ret = flush_logic_samples(in);
	if (ret != SR_OK)
		return ret;

	inc = in->priv;
	if (inc->started)
		std_session_send_df_end(in->sdi);

	return ret;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;

	keep_header_for_reread(in);

	inc = in->priv;

	g_free(inc->termination);
	inc->termination = NULL;
	g_free(inc->datafeed_buffer);
	inc->datafeed_buffer = NULL;
}

static int reset(struct sr_input *in)
{
	struct context *inc = in->priv;

	cleanup(in);
	inc->started = FALSE;
	g_string_truncate(in->buf, 0);

	return SR_OK;
}

enum option_index {
	OPT_SINGLE_COL,
	OPT_NUM_LOGIC,
	OPT_DELIM,
	OPT_FORMAT,
	OPT_COMMENT,
	OPT_RATE,
	OPT_FIRST_COL,
	OPT_HEADER,
	OPT_START,
	OPT_MAX,
};

static struct sr_option options[] = {
	[OPT_SINGLE_COL] = { "single-column", "Single column", "Enable single-column mode, using the specified column (>= 1); 0: multi-col. mode", NULL, NULL },
	[OPT_NUM_LOGIC] = { "numchannels", "Number of logic channels", "The number of (logic) channels (single-col. mode: number of bits beginning at 'first channel', LSB-first)", NULL, NULL },
	[OPT_DELIM] = { "delimiter", "Column delimiter", "The column delimiter (>= 1 characters)", NULL, NULL },
	[OPT_FORMAT] = { "format", "Data format (single-col. mode)", "The numeric format of the data (single-col. mode): bin, hex, oct", NULL, NULL },
	[OPT_COMMENT] = { "comment", "Comment character(s)", "The comment prefix character(s)", NULL, NULL },
	[OPT_RATE] = { "samplerate", "Samplerate (Hz)", "The sample rate (used during capture) in Hz", NULL, NULL },
	[OPT_FIRST_COL] = { "first-column", "First column", "The column number of the first channel (multi-col. mode)", NULL, NULL },
	[OPT_HEADER] = { "header", "Interpret first line as header (multi-col. mode)", "Treat the first line as header with channel names (multi-col. mode)", NULL, NULL },
	[OPT_START] = { "startline", "Start line", "The line number at which to start processing samples (>= 1)", NULL, NULL },
	[OPT_MAX] = ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	GSList *l;

	if (!options[0].def) {
		options[OPT_SINGLE_COL].def = g_variant_ref_sink(g_variant_new_uint32(0));
		options[OPT_NUM_LOGIC].def = g_variant_ref_sink(g_variant_new_uint32(0));
		options[OPT_DELIM].def = g_variant_ref_sink(g_variant_new_string(","));
		options[OPT_FORMAT].def = g_variant_ref_sink(g_variant_new_string("bin"));
		l = NULL;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("bin")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("hex")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("oct")));
		options[OPT_FORMAT].values = l;
		options[OPT_COMMENT].def = g_variant_ref_sink(g_variant_new_string(";"));
		options[OPT_RATE].def = g_variant_ref_sink(g_variant_new_uint64(0));
		options[OPT_FIRST_COL].def = g_variant_ref_sink(g_variant_new_uint32(1));
		options[OPT_HEADER].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[OPT_START].def = g_variant_ref_sink(g_variant_new_uint32(1));
	}

	return options;
}

SR_PRIV struct sr_input_module input_csv = {
	.id = "csv",
	.name = "CSV",
	.desc = "Comma-separated values",
	.exts = (const char*[]){"csv", NULL},
	.options = get_options,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
