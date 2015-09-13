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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/csv"

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

/* Single column formats. */
enum {
	FORMAT_BIN,
	FORMAT_HEX,
	FORMAT_OCT
};

struct context {
	gboolean started;

	/* Current selected samplerate. */
	uint64_t samplerate;

	/* Number of channels. */
	unsigned int num_channels;

	/* Column delimiter character(s). */
	GString *delimiter;

	/* Comment prefix character(s). */
	GString *comment;

	/* Termination  character(s) used in current stream. */
	char *termination;

	/* Determines if sample data is stored in multiple columns. */
	gboolean multi_column_mode;

	/* Column number of the sample data in single column mode. */
	unsigned int single_column;

	/*
	 * Number of the first column to parse. Equivalent to the number of the
	 * first channel in multi column mode and the single column number in
	 * single column mode.
	 */
	unsigned int first_column;

	/*
	 * Column number of the first channel in multi column mode and position of
	 * the bit for the first channel in single column mode.
	 */
	unsigned int first_channel;

	/* Line number to start processing. */
	size_t start_line;

	/*
	 * Determines if the first line should be treated as header and used for
	 * channel names in multi column mode.
	 */
	gboolean header;

	/* Format sample data is stored in single column mode. */
	int format;

	/* Size of the sample buffer. */
	size_t sample_buffer_size;

	/* Buffer to store sample data. */
	uint8_t *sample_buffer;

	/* Current line number. */
	size_t line_number;
};

static int format_match(GHashTable *metadata)
{
	char *buf;

	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_MIMETYPE));
	if (!strcmp(buf, "text/csv"))
		return SR_OK;

	return SR_ERR;
}

static void strip_comment(char *buf, const GString *prefix)
{
	char *ptr;

	if (!prefix->len)
		return;

	if ((ptr = strstr(buf, prefix->str)))
		*ptr = '\0';
}

static int parse_binstr(const char *str, struct context *inc)
{
	gsize i, j, length;

	length = strlen(str);

	if (!length) {
		sr_err("Column %u in line %zu is empty.", inc->single_column,
			inc->line_number);
		return SR_ERR;
	}

	/* Clear buffer in order to set bits only. */
	memset(inc->sample_buffer, 0, (inc->num_channels + 7) >> 3);

	i = inc->first_channel;

	for (j = 0; i < length && j < inc->num_channels; i++, j++) {
		if (str[length - i - 1] == '1') {
			inc->sample_buffer[j / 8] |= (1 << (j % 8));
		} else if (str[length - i - 1] != '0') {
			sr_err("Invalid value '%s' in column %u in line %zu.",
				str, inc->single_column, inc->line_number);
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int parse_hexstr(const char *str, struct context *inc)
{
	gsize i, j, k, length;
	uint8_t value;
	char c;

	length = strlen(str);

	if (!length) {
		sr_err("Column %u in line %zu is empty.", inc->single_column,
			inc->line_number);
		return SR_ERR;
	}

	/* Clear buffer in order to set bits only. */
	memset(inc->sample_buffer, 0, (inc->num_channels + 7) >> 3);

	/* Calculate the position of the first hexadecimal digit. */
	i = inc->first_channel / 4;

	for (j = 0; i < length && j < inc->num_channels; i++) {
		c = str[length - i - 1];

		if (!g_ascii_isxdigit(c)) {
			sr_err("Invalid value '%s' in column %u in line %zu.",
				str, inc->single_column, inc->line_number);
			return SR_ERR;
		}

		value = g_ascii_xdigit_value(c);

		k = (inc->first_channel + j) % 4;

		for (; j < inc->num_channels && k < 4; k++) {
			if (value & (1 << k))
				inc->sample_buffer[j / 8] |= (1 << (j % 8));

			j++;
		}
	}

	return SR_OK;
}

static int parse_octstr(const char *str, struct context *inc)
{
	gsize i, j, k, length;
	uint8_t value;
	char c;

	length = strlen(str);

	if (!length) {
		sr_err("Column %u in line %zu is empty.", inc->single_column,
			inc->line_number);
		return SR_ERR;
	}

	/* Clear buffer in order to set bits only. */
	memset(inc->sample_buffer, 0, (inc->num_channels + 7) >> 3);

	/* Calculate the position of the first octal digit. */
	i = inc->first_channel / 3;

	for (j = 0; i < length && j < inc->num_channels; i++) {
		c = str[length - i - 1];

		if (c < '0' || c > '7') {
			sr_err("Invalid value '%s' in column %u in line %zu.",
				str, inc->single_column, inc->line_number);
			return SR_ERR;
		}

		value = g_ascii_xdigit_value(c);

		k = (inc->first_channel + j) % 3;

		for (; j < inc->num_channels && k < 3; k++) {
			if (value & (1 << k))
				inc->sample_buffer[j / 8] |= (1 << (j % 8));

			j++;
		}
	}

	return SR_OK;
}

static char **parse_line(char *buf, struct context *inc, int max_columns)
{
	const char *str, *remainder;
	GSList *list, *l;
	char **columns;
	char *column;
	gsize n, k;

	n = 0;
	k = 0;
	list = NULL;

	remainder = buf;
	str = strstr(remainder, inc->delimiter->str);

	while (str && max_columns) {
		if (n >= inc->first_column) {
			column = g_strndup(remainder, str - remainder);
			list = g_slist_prepend(list, g_strstrip(column));

			max_columns--;
			k++;
		}

		remainder = str + inc->delimiter->len;
		str = strstr(remainder, inc->delimiter->str);
		n++;
	}

	if (buf[0] && max_columns && n >= inc->first_column) {
		column = g_strdup(remainder);
		list = g_slist_prepend(list, g_strstrip(column));
		k++;
	}

	if (!(columns = g_try_new(char *, k + 1)))
		return NULL;

	columns[k--] = NULL;

	for (l = list; l; l = l->next)
		columns[k--] = l->data;

	g_slist_free(list);

	return columns;
}

static int parse_multi_columns(char **columns, struct context *inc)
{
	gsize i;

	/* Clear buffer in order to set bits only. */
	memset(inc->sample_buffer, 0, (inc->num_channels + 7) >> 3);

	for (i = 0; i < inc->num_channels; i++) {
		if (columns[i][0] == '1') {
			inc->sample_buffer[i / 8] |= (1 << (i % 8));
		} else if (!strlen(columns[i])) {
			sr_err("Column %zu in line %zu is empty.",
				inc->first_channel + i, inc->line_number);
			return SR_ERR;
		} else if (columns[i][0] != '0') {
			sr_err("Invalid value '%s' in column %zu in line %zu.",
				columns[i], inc->first_channel + i,
				inc->line_number);
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int parse_single_column(const char *column, struct context *inc)
{
	int res;

	res = SR_ERR;

	switch (inc->format) {
	case FORMAT_BIN:
		res = parse_binstr(column, inc);
		break;
	case FORMAT_HEX:
		res = parse_hexstr(column, inc);
		break;
	case FORMAT_OCT:
		res = parse_octstr(column, inc);
		break;
	}

	return res;
}

static int send_samples(const struct sr_dev_inst *sdi, uint8_t *buffer,
		gsize buffer_size, gsize count)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	int res;
	gsize i;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = buffer_size;
	logic.length = buffer_size;
	logic.data = buffer;

	for (i = 0; i < count; i++) {
		if ((res = sr_session_send(sdi, &packet)) != SR_OK)
			return res;
	}

	return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	const char *s;

	in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
	in->priv = inc = g_malloc0(sizeof(struct context));

	inc->single_column = g_variant_get_int32(g_hash_table_lookup(options, "single-column"));
	inc->multi_column_mode = inc->single_column == 0;

	inc->num_channels = g_variant_get_int32(g_hash_table_lookup(options, "numchannels"));

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

	inc->first_channel = g_variant_get_int32(g_hash_table_lookup(options, "first-channel"));

	inc->header = g_variant_get_boolean(g_hash_table_lookup(options, "header"));

	inc->start_line = g_variant_get_int32(g_hash_table_lookup(options, "startline"));
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
	unsigned int num_columns, i;
	size_t line_number, l;
	int ret;
	char **lines, **columns;

	ret = SR_OK;
	inc = in->priv;
	columns = NULL;

	line_number = 0;
	lines = g_strsplit_set(buf->str, "\r\n", 0);
	for (l = 0; lines[l]; l++) {
		line_number++;
		if (inc->start_line > line_number) {
			sr_spew("Line %zu skipped.", line_number);
			continue;
		}
		if (lines[l][0] == '\0') {
			sr_spew("Blank line %zu skipped.", line_number);
			continue;
		}
		strip_comment(lines[l], inc->comment);
		if (lines[l][0] == '\0') {
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
	if (!(columns = parse_line(lines[l], inc, -1))) {
		sr_err("Error while parsing line %zu.", line_number);
		ret = SR_ERR;
		goto out;
	}
	num_columns = g_strv_length(columns);

	/* Ensure that the first column is not out of bounds. */
	if (!num_columns) {
		sr_err("Column %u in line %zu is out of bounds.",
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
			sr_dbg("Number of auto-detected channels: %u.",
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
		if (inc->header && inc->multi_column_mode && columns[i][0] != '\0')
			g_string_assign(channel_name, columns[i]);
		else
			g_string_printf(channel_name, "%u", i);
		sr_channel_new(in->sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name->str);
	}
	g_string_free(channel_name, TRUE);

	/*
	 * Calculate the minimum buffer size to store the sample data of the
	 * channels.
	 */
	inc->sample_buffer_size = (inc->num_channels + 7) >> 3;
	inc->sample_buffer = g_malloc(inc->sample_buffer_size);

out:
	if (columns)
		g_strfreev(columns);
	g_strfreev(lines);

	return ret;
}

static int initial_receive(const struct sr_input *in)
{
	struct context *inc;
	GString *new_buf;
	int len, ret;
	char *p;
	const char *termination;

	inc = in->priv;

	if (!(termination = get_line_termination(in->buf)))
		/* Don't have a full line yet. */
		return SR_ERR_NA;

	if (!(p = g_strrstr_len(in->buf->str, in->buf->len, termination)))
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

static int process_buffer(struct sr_input *in)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct context *inc;
	gsize num_columns;
	uint64_t samplerate;
	int max_columns, ret, l;
	char *p, **lines, **columns;

	inc = in->priv;
	if (!inc->started) {
		std_session_send_df_header(in->sdi, LOG_PREFIX);

		if (inc->samplerate) {
			packet.type = SR_DF_META;
			packet.payload = &meta;
			samplerate = inc->samplerate;
			src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(samplerate));
			meta.config = g_slist_append(NULL, src);
			sr_session_send(in->sdi, &packet);
			sr_config_free(src);
		}

		inc->started = TRUE;
	}

	if (!(p = g_strrstr_len(in->buf->str, in->buf->len, inc->termination)))
		/* Don't have a full line. */
		return SR_ERR;

	*p = '\0';
	g_strstrip(in->buf->str);

	/* Limit the number of columns to parse. */
	if (inc->multi_column_mode)
		max_columns = inc->num_channels;
	else
		max_columns = 1;

	ret = SR_OK;
	lines = g_strsplit_set(in->buf->str, "\r\n", 0);
	for (l = 0; lines[l]; l++) {
		inc->line_number++;
		if (lines[l][0] == '\0') {
			sr_spew("Blank line %zu skipped.", inc->line_number);
			continue;
		}

		/* Remove trailing comment. */
		strip_comment(lines[l], inc->comment);
		if (lines[l][0] == '\0') {
			sr_spew("Comment-only line %zu skipped.", inc->line_number);
			continue;
		}

		/* Skip the header line, its content was used as the channel names. */
		if (inc->header) {
			sr_spew("Header line %zu skipped.", inc->line_number);
			inc->header = FALSE;
			continue;
		}

		if (!(columns = parse_line(lines[l], inc, max_columns))) {
			sr_err("Error while parsing line %zu.", inc->line_number);
			return SR_ERR;
		}
		num_columns = g_strv_length(columns);
		if (!num_columns) {
			sr_err("Column %u in line %zu is out of bounds.",
				inc->first_column, inc->line_number);
			g_strfreev(columns);
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
			return SR_ERR;
		}

		if (inc->multi_column_mode)
			ret = parse_multi_columns(columns, inc);
		else
			ret = parse_single_column(columns[0], inc);
		if (ret != SR_OK) {
			g_strfreev(columns);
			return SR_ERR;
		}

		/* Send sample data to the session bus. */
		ret = send_samples(in->sdi, inc->sample_buffer,
			inc->sample_buffer_size, 1);
		if (ret != SR_OK) {
			sr_err("Sending samples failed.");
			return SR_ERR;
		}
		g_strfreev(columns);
	}
	g_strfreev(lines);
	g_string_erase(in->buf, 0, p - in->buf->str + 1);

	return ret;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;

	g_string_append_len(in->buf, buf->str, buf->len);

	inc = in->priv;
	if (!inc->termination) {
		if ((ret = initial_receive(in)) == SR_ERR_NA)
			/* Not enough data yet. */
			return SR_OK;
		else if (ret != SR_OK)
			return SR_ERR;

		/* sdi is ready, notify frontend. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	ret = process_buffer(in);

	return ret;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	int ret;

	if (in->sdi_ready)
		ret = process_buffer(in);
	else
		ret = SR_OK;

	inc = in->priv;
	if (inc->started) {
		/* End of stream. */
		packet.type = SR_DF_END;
		sr_session_send(in->sdi, &packet);
	}

	return ret;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	if (inc->delimiter)
		g_string_free(inc->delimiter, TRUE);

	if (inc->comment)
		g_string_free(inc->comment, TRUE);

	g_free(inc->termination);
	g_free(inc->sample_buffer);
}

static struct sr_option options[] = {
	{ "single-column", "Single column", "Enable/specify single column", NULL, NULL },
	{ "numchannels", "Max channels", "Number of channels", NULL, NULL },
	{ "delimiter", "Delimiter", "Column delimiter", NULL, NULL },
	{ "format", "Format", "Numeric format", NULL, NULL },
	{ "comment", "Comment", "Comment prefix character", NULL, NULL },
	{ "samplerate", "Samplerate", "Samplerate used during capture", NULL, NULL },
	{ "first-channel", "First channel", "Column number of first channel", NULL, NULL },
	{ "header", "Header", "Treat first line as header with channel names", NULL, NULL },
	{ "startline", "Start line", "Line number at which to start processing samples", NULL, NULL },
	ALL_ZERO
};

static struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[1].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[2].def = g_variant_ref_sink(g_variant_new_string(","));
		options[3].def = g_variant_ref_sink(g_variant_new_string("bin"));
		options[4].def = g_variant_ref_sink(g_variant_new_string(";"));
		options[5].def = g_variant_ref_sink(g_variant_new_uint64(0));
		options[6].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[7].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[8].def = g_variant_ref_sink(g_variant_new_int32(1));
	}

	return options;
}

SR_PRIV struct sr_input_module input_csv = {
	.id = "csv",
	.name = "CSV",
	.desc = "Comma-separated values",
	.exts = (const char*[]){"csv", NULL},
	.metadata = { SR_INPUT_META_MIMETYPE },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
};
