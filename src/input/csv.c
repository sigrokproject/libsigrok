/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marc Schink <sigrok-dev@marcschink.de>
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
	FORMAT_BIN,
	FORMAT_HEX,
	FORMAT_OCT,
};

struct context {
	gboolean started;

	/* Current selected samplerate. */
	uint64_t samplerate;

	/* Number of channels. */
	size_t num_channels;

	/* Column delimiter character(s). */
	GString *delimiter;

	/* Comment prefix character(s). */
	GString *comment;

	/* Termination character(s) used in current stream. */
	char *termination;

	/* Determines if sample data is stored in multiple columns. */
	gboolean multi_column_mode;

	/* Column number of the sample data in single column mode. */
	size_t single_column;

	/*
	 * Number of the first column to parse. Equivalent to the number of the
	 * first channel in multi column mode and the single column number in
	 * single column mode.
	 */
	size_t first_column;

	/*
	 * Column number of the first channel in multi column mode and position of
	 * the bit for the first channel in single column mode.
	 */
	size_t first_channel;

	/* Line number to start processing. */
	size_t start_line;

	/*
	 * Determines if the first line should be treated as header and used for
	 * channel names in multi column mode.
	 */
	gboolean header;

	/* Format sample data is stored in single column mode. */
	enum single_col_format format;

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

	if (ch_idx >= inc->num_channels)
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
	struct sr_datafeed_logic logic;
	int rc;

	inc = in->priv;
	if (!inc->datafeed_buf_fill)
		return SR_OK;

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

	inc->datafeed_buf_fill += inc->sample_unit_size;
	if (inc->datafeed_buf_fill == inc->datafeed_buf_size) {
		rc = flush_logic_samples(in);
		if (rc != SR_OK)
			return rc;
	}
	return SR_OK;
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

/* TODO Move parse_line() here. */

/**
 * @brief Parse a text field into multiple bits, binary presentation.
 *
 * @param[in] str	The input text, a run of 0/1 digits.
 * @param[in] inc	The input module's context.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Invalid input data (empty, or format error).
 *
 * This routine modifies the logic levels in the current sample set,
 * based on the text input which consists of binary digits.
 */
static int parse_binstr(const char *str, struct context *inc)
{
	gsize i, j, length;
	char c;

	length = strlen(str);
	if (!length) {
		sr_err("Column %zu in line %zu is empty.", inc->single_column,
			inc->line_number);
		return SR_ERR;
	}

	i = inc->first_channel;
	for (j = 0; i < length && j < inc->num_channels; i++, j++) {
		c = str[length - i - 1];
		if (c == '1') {
			set_logic_level(inc, j, 1);
		} else if (c != '0') {
			sr_err("Invalid text '%s' in binary column %zu in line %zu.",
				str, inc->single_column, inc->line_number);
			return SR_ERR;
		}
	}

	return SR_OK;
}

/**
 * @brief Parse a text field into multiple bits, hexadecimal presentation.
 *
 * @param[in] str	The input text, a run of hex digits.
 * @param[in] inc	The input module's context.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Invalid input data (empty, or format error).
 *
 * This routine modifies the logic levels in the current sample set,
 * based on the text input which consists of hexadecimal digits.
 */
static int parse_hexstr(const char *str, struct context *inc)
{
	gsize i, j, k, length;
	uint8_t value;
	char c;

	length = strlen(str);
	if (!length) {
		sr_err("Column %zu in line %zu is empty.", inc->single_column,
			inc->line_number);
		return SR_ERR;
	}

	/* Calculate the position of the first hexadecimal digit. */
	i = inc->first_channel / 4;
	for (j = 0; i < length && j < inc->num_channels; i++) {
		c = str[length - i - 1];
		if (!g_ascii_isxdigit(c)) {
			sr_err("Invalid text '%s' in hex column %zu in line %zu.",
				str, inc->single_column, inc->line_number);
			return SR_ERR;
		}

		value = g_ascii_xdigit_value(c);
		k = (inc->first_channel + j) % 4;
		for (; j < inc->num_channels && k < 4; j++, k++) {
			set_logic_level(inc, j, value & (1 << k));
		}
	}

	return SR_OK;
}

/**
 * @brief Parse a text field into multiple bits, octal presentation.
 *
 * @param[in] str	The input text, a run of oct digits.
 * @param[in] inc	The input module's context.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Invalid input data (empty, or format error).
 *
 * This routine modifies the logic levels in the current sample set,
 * based on the text input which consists of octal digits.
 */
static int parse_octstr(const char *str, struct context *inc)
{
	gsize i, j, k, length;
	uint8_t value;
	char c;

	length = strlen(str);
	if (!length) {
		sr_err("Column %zu in line %zu is empty.", inc->single_column,
			inc->line_number);
		return SR_ERR;
	}

	/* Calculate the position of the first octal digit. */
	i = inc->first_channel / 3;
	for (j = 0; i < length && j < inc->num_channels; i++) {
		c = str[length - i - 1];
		if (c < '0' || c > '7') {
			sr_err("Invalid text '%s' in oct column %zu in line %zu.",
				str, inc->single_column, inc->line_number);
			return SR_ERR;
		}

		value = g_ascii_xdigit_value(c);
		k = (inc->first_channel + j) % 3;
		for (; j < inc->num_channels && k < 3; j++, k++) {
			set_logic_level(inc, j, value & (1 << k));
		}
	}

	return SR_OK;
}

static int parse_single_column(const char *column, struct context *inc)
{
	switch (inc->format) {
	case FORMAT_BIN:
		return parse_binstr(column, inc);
	case FORMAT_HEX:
		return parse_hexstr(column, inc);
	case FORMAT_OCT:
		return parse_octstr(column, inc);
	}

	return SR_ERR;
}

/**
 * @brief Splits a text line into a set of columns.
 *
 * @param[in] buf	The input text line to split.
 * @param[in] inc	The input module's context.
 * @param[in] max_cols	The maximum column count, negative to get all of them.
 *
 * @returns An array of strings, representing the columns' text.
 *
 * This routine splits a text line on previously determined separators.
 * A previously determined set of columns gets isolated (starting at a
 * first position and spanning a given number of columns). A negative
 * value for the maximum number of columns results in no restriction on
 * the result set's length (the first columns still get trimmed off).
 */
static char **parse_line(char *buf, struct context *inc, ssize_t max_cols)
{
	const char *str, *remainder;
	GSList *list, *l;
	char **columns;
	char *column;
	gsize seen, taken;

	seen = 0;
	taken = 0;
	list = NULL;

	remainder = buf;
	str = strstr(remainder, inc->delimiter->str);
	while (str && max_cols) {
		if (seen >= inc->first_column) {
			column = g_strndup(remainder, str - remainder);
			list = g_slist_prepend(list, g_strstrip(column));

			max_cols--;
			taken++;
		}

		remainder = str + inc->delimiter->len;
		str = strstr(remainder, inc->delimiter->str);
		seen++;
	}

	if (buf[0] && max_cols && seen >= inc->first_column) {
		column = g_strdup(remainder);
		list = g_slist_prepend(list, g_strstrip(column));
		taken++;
	}

	if (!(columns = g_try_new(char *, taken + 1)))
		return NULL;
	columns[taken--] = NULL;
	for (l = list; l; l = l->next)
		columns[taken--] = l->data;

	g_slist_free(list);

	return columns;
}

/**
 * @brief Picks logic levels from multiple binary colomns, one channel per column.
 *
 * @param[in] columns	The text fields which are kept in the columns.
 * @param[in] inc	The input module's context.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Insufficient input, or syntax errors.
 *
 * This routine exclusively handles binary input where one logic channel
 * occupies one column each. All channels are expected to reside in one
 * consequtive run of columns.
 */
static int parse_multi_columns(char **columns, struct context *inc)
{
	gsize i;
	char *column;

	for (i = 0; i < inc->num_channels; i++) {
		column = columns[i];
		if (strcmp(column, "1") == 0) {
			set_logic_level(inc, i, 1);
		} else if (!strlen(column)) {
			sr_err("Column %zu in line %zu is empty.",
				inc->first_channel + i, inc->line_number);
			return SR_ERR;
		} else if (strcmp(column, "0") != 0) {
			sr_err("Invalid text '%s' in bit column %zu in line %zu.",
				column, inc->first_channel + i,
				inc->line_number);
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	const char *s;

	in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
	in->priv = inc = g_malloc0(sizeof(struct context));

	inc->single_column = g_variant_get_uint32(g_hash_table_lookup(options, "single-column"));
	inc->multi_column_mode = inc->single_column == 0;

	inc->num_channels = g_variant_get_uint32(g_hash_table_lookup(options, "numchannels"));

	inc->delimiter = g_string_new(g_variant_get_string(
			g_hash_table_lookup(options, "delimiter"), NULL));
	if (inc->delimiter->len == 0) {
		sr_err("Delimiter must be at least one character.");
		return SR_ERR_ARG;
	}

	s = g_variant_get_string(g_hash_table_lookup(options, "format"), NULL);
	if (!g_ascii_strncasecmp(s, "bin", 3)) {
		inc->format = FORMAT_BIN;
	} else if (!g_ascii_strncasecmp(s, "hex", 3)) {
		inc->format = FORMAT_HEX;
	} else if (!g_ascii_strncasecmp(s, "oct", 3)) {
		inc->format = FORMAT_OCT;
	} else {
		sr_err("Invalid format: '%s'", s);
		return SR_ERR_ARG;
	}

	inc->comment = g_string_new(g_variant_get_string(
			g_hash_table_lookup(options, "comment"), NULL));
	if (g_string_equal(inc->comment, inc->delimiter)) {
		/* That's never going to work. Likely the result of the user
		 * setting the delimiter to ; -- the default comment. Clearing
		 * the comment setting will work in that case. */
		g_string_truncate(inc->comment, 0);
	}

	inc->samplerate = g_variant_get_uint64(g_hash_table_lookup(options, "samplerate"));

	inc->first_channel = g_variant_get_uint32(g_hash_table_lookup(options, "first-channel"));

	inc->header = g_variant_get_boolean(g_hash_table_lookup(options, "header"));

	inc->start_line = g_variant_get_uint32(g_hash_table_lookup(options, "startline"));
	if (inc->start_line < 1) {
		sr_err("Invalid start line %zu.", inc->start_line);
		return SR_ERR_ARG;
	}

	if (inc->multi_column_mode)
		inc->first_column = inc->first_channel;
	else
		inc->first_column = inc->single_column;

	if (!inc->multi_column_mode && !inc->num_channels) {
		sr_err("Number of channels needs to be specified in single column mode.");
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
	size_t num_columns, i;
	size_t line_number, l;
	int ret;
	char **lines, *line, **columns, *column;

	ret = SR_OK;
	inc = in->priv;
	columns = NULL;

	line_number = 0;
	lines = g_strsplit_set(buf->str, delim_set, 0);
	for (l = 0; lines[l]; l++) {
		line_number++;
		line = lines[l];
		if (inc->start_line > line_number) {
			sr_spew("Line %zu skipped.", line_number);
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
	if (!lines[l]) {
		/* Not enough data for a proper line yet. */
		ret = SR_ERR_NA;
		goto out;
	}

	/*
	 * In order to determine the number of columns parse the current line
	 * without limiting the number of columns.
	 */
	columns = parse_line(line, inc, -1);
	if (!columns) {
		sr_err("Error while parsing line %zu.", line_number);
		ret = SR_ERR;
		goto out;
	}
	num_columns = g_strv_length(columns);

	/* Ensure that the first column is not out of bounds. */
	if (!num_columns) {
		sr_err("Column %zu in line %zu is out of bounds.",
			inc->first_column, line_number);
		ret = SR_ERR;
		goto out;
	}

	if (inc->multi_column_mode) {
		/*
		 * Detect the number of channels in multi column mode
		 * automatically if not specified.
		 */
		if (!inc->num_channels) {
			inc->num_channels = num_columns;
			sr_dbg("Number of auto-detected channels: %zu.",
				inc->num_channels);
		}

		/*
		 * Ensure that the number of channels does not exceed the number
		 * of columns in multi column mode.
		 */
		if (num_columns < inc->num_channels) {
			sr_err("Not enough columns for desired number of channels in line %zu.",
				line_number);
			ret = SR_ERR;
			goto out;
		}
	}

	channel_name = g_string_sized_new(64);
	for (i = 0; i < inc->num_channels; i++) {
		column = columns[i];
		if (inc->header && inc->multi_column_mode && column[0] != '\0')
			g_string_assign(channel_name, column);
		else
			g_string_printf(channel_name, "%zu", i);
		sr_channel_new(in->sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name->str);
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
	inc->sample_unit_size = (inc->num_channels + 7) / 8;
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
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct context *inc;
	gsize num_columns;
	uint64_t samplerate;
	size_t max_columns, l;
	int ret;
	char *p, **lines, *line, **columns;

	inc = in->priv;
	if (!inc->started) {
		std_session_send_df_header(in->sdi);

		if (inc->samplerate) {
			packet.type = SR_DF_META;
			packet.payload = &meta;
			samplerate = inc->samplerate;
			src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(samplerate));
			meta.config = g_slist_append(NULL, src);
			sr_session_send(in->sdi, &packet);
			g_slist_free(meta.config);
			sr_config_free(src);
		}

		inc->started = TRUE;
	}

	/* Limit the number of columns to parse. */
	if (inc->multi_column_mode)
		max_columns = inc->num_channels;
	else
		max_columns = 1;

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
	for (l = 0; lines[l]; l++) {
		inc->line_number++;
		line = lines[l];
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
		if (inc->header) {
			sr_spew("Header line %zu skipped.", inc->line_number);
			inc->header = FALSE;
			continue;
		}

		columns = parse_line(line, inc, max_columns);
		if (!columns) {
			sr_err("Error while parsing line %zu.", inc->line_number);
			g_strfreev(lines);
			return SR_ERR;
		}
		num_columns = g_strv_length(columns);
		if (!num_columns) {
			sr_err("Column %zu in line %zu is out of bounds.",
				inc->first_column, inc->line_number);
			g_strfreev(columns);
			g_strfreev(lines);
			return SR_ERR;
		}
		/*
		 * Ensure that the number of channels does not exceed the number
		 * of columns in multi column mode.
		 */
		if (inc->multi_column_mode && num_columns < inc->num_channels) {
			sr_err("Not enough columns for desired number of channels in line %zu.",
				inc->line_number);
			g_strfreev(columns);
			g_strfreev(lines);
			return SR_ERR;
		}

		clear_logic_samples(inc);

		if (inc->multi_column_mode)
			ret = parse_multi_columns(columns, inc);
		else
			ret = parse_single_column(columns[0], inc);
		if (ret != SR_OK) {
			g_strfreev(columns);
			g_strfreev(lines);
			return SR_ERR;
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
	OPT_FIRST_LOGIC,
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
	[OPT_FIRST_LOGIC] = { "first-channel", "First channel", "The column number of the first channel (multi-col. mode); bit position for the first channel (single-col. mode)", NULL, NULL },
	[OPT_HEADER] = { "header", "Interpret first line as header (multi-col. mode)", "Treat the first line as header with channel names (multi-col. mode)", NULL, NULL },
	[OPT_START] = { "startline", "Start line", "The line number at which to start processing samples (>= 1)", NULL, NULL },
	[OPT_MAX] = ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	GSList *l;

	if (!options[0].def) {
		options[OPT_SINGLE_COL].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[OPT_NUM_LOGIC].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[OPT_DELIM].def = g_variant_ref_sink(g_variant_new_string(","));
		options[OPT_FORMAT].def = g_variant_ref_sink(g_variant_new_string("bin"));
		l = NULL;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("bin")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("hex")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("oct")));
		options[OPT_FORMAT].values = l;
		options[OPT_COMMENT].def = g_variant_ref_sink(g_variant_new_string(";"));
		options[OPT_RATE].def = g_variant_ref_sink(g_variant_new_uint64(0));
		options[OPT_FIRST_LOGIC].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[OPT_HEADER].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[OPT_START].def = g_variant_ref_sink(g_variant_new_int32(1));
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
