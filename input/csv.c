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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "input/csv: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

/*
 * The CSV input module has the following options:
 *
 * single-column: Specifies the column number which stores the sample data for
 *                single column mode and enables single column mode. Multi
 *                column mode is used if this parameter is omitted.
 *
 * numprobes:     Specifies the number of probes to use. In multi column mode
 *                the number of probes are the number of columns and in single
 *                column mode the number of bits (LSB first) beginning at
 *                'first-probe'.
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
 * first-probe:   Column number of the first probe in multi column mode and
 *                position of the bit for the first probe in single column mode.
 *                Default value is 0.
 *
 * header:        Determines if the first line should be treated as header
 *                and used for probe names in multi column mode. Empty header
 *                names will be replaced by the probe number. If enabled in
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
	/* Current selected samplerate. */
	uint64_t samplerate;

	/* Number of probes. */
	gsize num_probes;

	/* Column delimiter character(s). */
	GString *delimiter;

	/* Comment prefix character(s). */
	GString *comment;

	/* Determines if sample data is stored in multiple columns. */
	gboolean multi_column_mode;

	/* Column number of the sample data in single column mode. */
	gsize single_column;

	/*
	 * Number of the first column to parse. Equivalent to the number of the
	 * first probe in multi column mode and the single column number in
	 * single column mode.
	 */
	gsize first_column;

	/*
	 * Column number of the first probe in multi column mode and position of
	 * the bit for the first probe in single column mode.
	 */
	gsize first_probe;

	/* Line number to start processing. */
	gsize start_line;

	/*
	 * Determines if the first line should be treated as header and used for
	 * probe names in multi column mode.
	 */
	gboolean header;

	/* Format sample data is stored in single column mode. */
	int format;

	/* Size of the sample buffer. */
	gsize sample_buffer_size;

	/* Buffer to store sample data. */
	uint8_t *sample_buffer;

	GIOChannel *channel;

	/* Buffer for the current line. */
	GString *buffer;

	/* Current line number. */
	gsize line_number;
};

static int format_match(const char *filename)
{
	if (!filename) {
		sr_err("%s: filename was NULL.", __func__);
		return FALSE;
	}

	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		sr_err("Input file '%s' does not exist.", filename);
		return FALSE;
	}

	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
		sr_err("Input file '%s' not a regular file.", filename);
		return FALSE;
	}

	return TRUE;
}

static void free_context(struct context *ctx)
{
	if (!ctx)
		return;

	if (ctx->delimiter)
		g_string_free(ctx->delimiter, TRUE);

	if (ctx->comment)
		g_string_free(ctx->comment, TRUE);

	if (ctx->channel) {
		g_io_channel_shutdown(ctx->channel, FALSE, NULL);
		g_io_channel_unref(ctx->channel);
	}

	if (ctx->sample_buffer)
		g_free(ctx->sample_buffer);

	if (ctx->buffer)
		g_string_free(ctx->buffer, TRUE);

	g_free(ctx);
}

static void strip_comment(GString *string, const GString *prefix)
{
	char *ptr;

	if (!prefix->len)
		return;

	if (!(ptr = strstr(string->str, prefix->str)))
		return;

	g_string_truncate(string, ptr - string->str);
}

static int parse_binstr(const char *str, struct context *ctx)
{
	gsize i, j, length;

	length = strlen(str);

	if (!length) {
		sr_err("Column %zu in line %zu is empty.", ctx->single_column,
			ctx->line_number);
		return SR_ERR;
	}

	/* Clear buffer in order to set bits only. */
	memset(ctx->sample_buffer, 0, (ctx->num_probes + 7) >> 3);

	i = ctx->first_probe;

	for (j = 0; i < length && j < ctx->num_probes; i++, j++) {
		if (str[length - i - 1] == '1') {
			ctx->sample_buffer[j / 8] |= (1 << (j % 8));
		} else if (str[length - i - 1] != '0') {
			sr_err("Invalid value '%s' in column %zu in line %zu.",
				str, ctx->single_column, ctx->line_number);
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int parse_hexstr(const char *str, struct context *ctx)
{
	gsize i, j, k, length;
	uint8_t value;
	char c;

	length = strlen(str);

	if (!length) {
		sr_err("Column %zu in line %zu is empty.", ctx->single_column,
			ctx->line_number);
		return SR_ERR;
	}

	/* Clear buffer in order to set bits only. */
	memset(ctx->sample_buffer, 0, (ctx->num_probes + 7) >> 3);

	/* Calculate the position of the first hexadecimal digit. */
	i = ctx->first_probe / 4;

	for (j = 0; i < length && j < ctx->num_probes; i++) {
		c = str[length - i - 1];

		if (!g_ascii_isxdigit(c)) {
			sr_err("Invalid value '%s' in column %zu in line %zu.",
				str, ctx->single_column, ctx->line_number);
			return SR_ERR;
		}

		value = g_ascii_xdigit_value(c);

		k = (ctx->first_probe + j) % 4;

		for (; j < ctx->num_probes && k < 4; k++) {
			if (value & (1 << k))
				ctx->sample_buffer[j / 8] |= (1 << (j % 8));

			j++;
		}
	}

	return SR_OK;
}

static int parse_octstr(const char *str, struct context *ctx)
{
	gsize i, j, k, length;
	uint8_t value;
	char c;

	length = strlen(str);

	if (!length) {
		sr_err("Column %zu in line %zu is empty.", ctx->single_column,
			ctx->line_number);
		return SR_ERR;
	}

	/* Clear buffer in order to set bits only. */
	memset(ctx->sample_buffer, 0, (ctx->num_probes + 7) >> 3);

	/* Calculate the position of the first octal digit. */
	i = ctx->first_probe / 3;

	for (j = 0; i < length && j < ctx->num_probes; i++) {
		c = str[length - i - 1];

		if (c < '0' || c > '7') {
			sr_err("Invalid value '%s' in column %zu in line %zu.",
				str, ctx->single_column, ctx->line_number);
			return SR_ERR;
		}

		value = g_ascii_xdigit_value(c);

		k = (ctx->first_probe + j) % 3;

		for (; j < ctx->num_probes && k < 3; k++) {
			if (value & (1 << k))
				ctx->sample_buffer[j / 8] |= (1 << (j % 8));

			j++;
		}
	}

	return SR_OK;
}

static char **parse_line(const struct context *ctx, int max_columns)
{
	const char *str, *remainder;
	GSList *list, *l;
	char **columns;
	char *column;
	gsize n, k;

	n = 0;
	k = 0;
	list = NULL;

	remainder = ctx->buffer->str;
	str = strstr(remainder, ctx->delimiter->str);

	while (str && max_columns) {
		if (n >= ctx->first_column) {
			column = g_strndup(remainder, str - remainder);
			list = g_slist_prepend(list, g_strstrip(column));

			max_columns--;
			k++;
		}

		remainder = str + ctx->delimiter->len;
		str = strstr(remainder, ctx->delimiter->str);
		n++;
	}

	if (ctx->buffer->len && max_columns && n >= ctx->first_column) {
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

static int parse_multi_columns(char **columns, struct context *ctx)
{
	gsize i;

	/* Clear buffer in order to set bits only. */
	memset(ctx->sample_buffer, 0, (ctx->num_probes + 7) >> 3);

	for (i = 0; i < ctx->num_probes; i++) {
		if (columns[i][0] == '1') {
			ctx->sample_buffer[i / 8] |= (1 << (i % 8));
		} else if (!strlen(columns[i])) {
			sr_err("Column %zu in line %zu is empty.",
				ctx->first_probe + i, ctx->line_number);
			return SR_ERR;
		} else if (columns[i][0] != '0') {
			sr_err("Invalid value '%s' in column %zu in line %zu.",
				columns[i], ctx->first_probe + i,
				ctx->line_number);
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int parse_single_column(const char *column, struct context *ctx)
{
	int res;

	res = SR_ERR;

	switch(ctx->format) {
	case FORMAT_BIN:
		res = parse_binstr(column, ctx);
		break;
	case FORMAT_HEX:
		res = parse_hexstr(column, ctx);
		break;
	case FORMAT_OCT:
		res = parse_octstr(column, ctx);
		break;
	}

	return res;
}

static int send_samples(const struct sr_dev_inst *sdi, uint8_t *buffer,
			gsize buffer_size, gsize count)
{
	int res;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
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

static int init(struct sr_input *in, const char *filename)
{
	int res;
	struct context *ctx;
	const char *param;
	GIOStatus status;
	gsize i, term_pos;
	char probe_name[SR_MAX_PROBENAME_LEN + 1];
	struct sr_probe *probe;
	char **columns;
	gsize num_columns;
	char *ptr;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("Context malloc failed.");
		return SR_ERR_MALLOC;
	}

	/* Create a virtual device. */
	in->sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, NULL, NULL, NULL);
	in->internal = ctx;

	/* Set default samplerate. */
	ctx->samplerate = 0;

	/*
	 * Enable auto-detection of the number of probes in multi column mode
	 * and enforce the specification of the number of probes in single
	 * column mode.
	 */
	ctx->num_probes = 0;

	/* Set default delimiter. */
	if (!(ctx->delimiter = g_string_new(","))) {
		sr_err("Delimiter malloc failed.");
		free_context(ctx);
		return SR_ERR_MALLOC;
	}

	/*
	 * Set default comment prefix. Note that an empty comment prefix
	 * disables removing of comments.
	 */
	if (!(ctx->comment = g_string_new(""))) {
		sr_err("Comment malloc failed.");
		free_context(ctx);
		return SR_ERR_MALLOC;
	}

	/* Enable multi column mode by default. */
	ctx->multi_column_mode = TRUE;

	/* Use first column as default single column number. */
	ctx->single_column = 0;

	/*
	 * In multi column mode start parsing sample data at the first column
	 * and in single column mode at the first bit.
	 */
	ctx->first_probe = 0;

	/* Start at the beginning of the file. */
	ctx->start_line = 1;

	/* Disable the usage of the first line as header by default. */
	ctx->header = FALSE;

	/* Set default format for single column mode. */
	ctx->format = FORMAT_BIN;

	if (!(ctx->buffer = g_string_new(""))) {
		sr_err("Line buffer malloc failed.");
		free_context(ctx);
		return SR_ERR_MALLOC;
	}

	if (in->param) {
		if ((param = g_hash_table_lookup(in->param, "samplerate"))) {
			res = sr_parse_sizestring(param, &ctx->samplerate);

			if (res != SR_OK) {
				sr_err("Invalid samplerate: %s.", param);
				free_context(ctx);
				return SR_ERR_ARG;
			}
		}

		if ((param = g_hash_table_lookup(in->param, "numprobes")))
			ctx->num_probes = g_ascii_strtoull(param, NULL, 10);

		if ((param = g_hash_table_lookup(in->param, "delimiter"))) {
			if (!strlen(param)) {
				sr_err("Delimiter must be at least one character.");
				free_context(ctx);
				return SR_ERR_ARG;
			}

			if (!g_ascii_strcasecmp(param, "\\t"))
				g_string_assign(ctx->delimiter, "\t");
			else
				g_string_assign(ctx->delimiter, param);
		}

		if ((param = g_hash_table_lookup(in->param, "comment")))
			g_string_assign(ctx->comment, param);

		if ((param = g_hash_table_lookup(in->param, "single-column"))) {
			ctx->single_column = g_ascii_strtoull(param, &ptr, 10);
			ctx->multi_column_mode = FALSE;

			if (param == ptr) {
				sr_err("Invalid single-colum number: %s.",
					param);
				free_context(ctx);
				return SR_ERR_ARG;
			}
		}

		if ((param = g_hash_table_lookup(in->param, "first-probe")))
			ctx->first_probe = g_ascii_strtoull(param, NULL, 10);

		if ((param = g_hash_table_lookup(in->param, "startline"))) {
			ctx->start_line = g_ascii_strtoull(param, NULL, 10);

			if (ctx->start_line < 1) {
				sr_err("Invalid start line: %s.", param);
				free_context(ctx);
				return SR_ERR_ARG;
			}
		}

		if ((param = g_hash_table_lookup(in->param, "header")))
			ctx->header = sr_parse_boolstring(param);

		if ((param = g_hash_table_lookup(in->param, "format"))) {
			if (!g_ascii_strncasecmp(param, "bin", 3)) {
				ctx->format = FORMAT_BIN;
			} else if (!g_ascii_strncasecmp(param, "hex", 3)) {
				ctx->format = FORMAT_HEX;
			} else if (!g_ascii_strncasecmp(param, "oct", 3)) {
				ctx->format = FORMAT_OCT;
			} else {
				sr_err("Invalid format: %s.", param);
				free_context(ctx);
				return SR_ERR;
			}
		}
	}

	if (ctx->multi_column_mode)
		ctx->first_column = ctx->first_probe;
	else
		ctx->first_column = ctx->single_column;

	if (!ctx->multi_column_mode && !ctx->num_probes) {
		sr_err("Number of probes needs to be specified in single column mode.");
		free_context(ctx);
		return SR_ERR;
	}

	if (!(ctx->channel = g_io_channel_new_file(filename, "r", NULL))) {
		sr_err("Input file '%s' could not be opened.", filename);
		free_context(ctx);
		return SR_ERR;
	}

	while (TRUE) {
		ctx->line_number++;
		status = g_io_channel_read_line_string(ctx->channel,
			ctx->buffer, &term_pos, NULL);

		if (status == G_IO_STATUS_EOF) {
			sr_err("Input file is empty.");
			free_context(ctx);
			return SR_ERR;
		}

		if (status != G_IO_STATUS_NORMAL) {
			sr_err("Error while reading line %zu.",
				ctx->line_number);
			free_context(ctx);
			return SR_ERR;
		}

		if (ctx->start_line > ctx->line_number) {
			sr_spew("Line %zu skipped.", ctx->line_number);
			continue;
		}

		/* Remove line termination character(s). */
		g_string_truncate(ctx->buffer, term_pos);

		if (!ctx->buffer->len) {
			sr_spew("Blank line %zu skipped.", ctx->line_number);
			continue;
		}

		/* Remove trailing comment. */
		strip_comment(ctx->buffer, ctx->comment);

		if (ctx->buffer->len)
			break;

		sr_spew("Comment-only line %zu skipped.", ctx->line_number);
	}

	/*
	 * In order to determine the number of columns parse the current line
	 * without limiting the number of columns.
	 */
	if (!(columns = parse_line(ctx, -1))) {
		sr_err("Error while parsing line %zu.", ctx->line_number);
		free_context(ctx);
		return SR_ERR;
	}

	num_columns = g_strv_length(columns);

	/* Ensure that the first column is not out of bounds. */
	if (!num_columns) {
		sr_err("Column %zu in line %zu is out of bounds.",
			ctx->first_column, ctx->line_number);
		g_strfreev(columns);
		free_context(ctx);
		return SR_ERR;
	}

	if (ctx->multi_column_mode) {
		/*
		 * Detect the number of probes in multi column mode
		 * automatically if not specified.
		 */
		if (!ctx->num_probes) {
			ctx->num_probes = num_columns;
			sr_info("Number of auto-detected probes: %zu.",
				ctx->num_probes);
		}

		/*
		 * Ensure that the number of probes does not exceed the number
		 * of columns in multi column mode.
		 */
		if (num_columns < ctx->num_probes) {
			sr_err("Not enough columns for desired number of probes in line %zu.",
				ctx->line_number);
			g_strfreev(columns);
			free_context(ctx);
			return SR_ERR;
		}
	}

	for (i = 0; i < ctx->num_probes; i++) {
		if (ctx->header && ctx->multi_column_mode && strlen(columns[i]))
			snprintf(probe_name, sizeof(probe_name), "%s",
				columns[i]);
		else
			snprintf(probe_name, sizeof(probe_name), "%zu", i);

		probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE, probe_name);

		if (!probe) {
			sr_err("Probe creation failed.");
			free_context(ctx);
			g_strfreev(columns);
			return SR_ERR;
		}

		in->sdi->probes = g_slist_append(in->sdi->probes, probe);
	}

	g_strfreev(columns);

	/*
	 * Calculate the minimum buffer size to store the sample data of the
	 * probes.
	 */
	ctx->sample_buffer_size = (ctx->num_probes + 7) >> 3;

	if (!(ctx->sample_buffer = g_try_malloc(ctx->sample_buffer_size))) {
		sr_err("Sample buffer malloc failed.");
		free_context(ctx);
		return SR_ERR_MALLOC;
	}

	return SR_OK;
}

static int loadfile(struct sr_input *in, const char *filename)
{
	int res;
	struct context *ctx;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *cfg;
	GIOStatus status;
	gboolean read_new_line;
	gsize term_pos;
	char **columns;
	gsize num_columns;
	int max_columns;

	(void)filename;

	ctx = in->internal;

	/* Send header packet to the session bus. */
	std_session_send_df_header(in->sdi, LOG_PREFIX);

	if (ctx->samplerate) {
		packet.type = SR_DF_META;
		packet.payload = &meta;
		cfg = sr_config_new(SR_CONF_SAMPLERATE,
			g_variant_new_uint64(ctx->samplerate));
		meta.config = g_slist_append(NULL, cfg);
		sr_session_send(in->sdi, &packet);
		sr_config_free(cfg);
	}

	read_new_line = FALSE;

	/* Limit the number of columns to parse. */
	if (ctx->multi_column_mode)
		max_columns = ctx->num_probes;
	else
		max_columns = 1;

	while (TRUE) {
		/*
		 * Skip reading a new line for the first time if the last read
		 * line was not a header because the sample data is not parsed
		 * yet.
		 */
		if (read_new_line || ctx->header) {
			ctx->line_number++;
			status = g_io_channel_read_line_string(ctx->channel,
				ctx->buffer, &term_pos, NULL);

			if (status == G_IO_STATUS_EOF)
				break;

			if (status != G_IO_STATUS_NORMAL) {
				sr_err("Error while reading line %zu.",
					ctx->line_number);
				free_context(ctx);
				return SR_ERR;
			}

			/* Remove line termination character(s). */
			g_string_truncate(ctx->buffer, term_pos);
		}

		read_new_line = TRUE;

		if (!ctx->buffer->len) {
			sr_spew("Blank line %zu skipped.", ctx->line_number);
			continue;
		}

		/* Remove trailing comment. */
		strip_comment(ctx->buffer, ctx->comment);

		if (!ctx->buffer->len) {
			sr_spew("Comment-only line %zu skipped.",
				ctx->line_number);
			continue;
		}

		if (!(columns = parse_line(ctx, max_columns))) {
			sr_err("Error while parsing line %zu.",
				ctx->line_number);
			free_context(ctx);
			return SR_ERR;
		}

		num_columns = g_strv_length(columns);

		/* Ensure that the first column is not out of bounds. */
		if (!num_columns) {
			sr_err("Column %zu in line %zu is out of bounds.",
				ctx->first_column, ctx->line_number);
			g_strfreev(columns);
			free_context(ctx);
			return SR_ERR;
		}

		/*
		 * Ensure that the number of probes does not exceed the number
		 * of columns in multi column mode.
		 */
		if (ctx->multi_column_mode && num_columns < ctx->num_probes) {
			sr_err("Not enough columns for desired number of probes in line %zu.",
				ctx->line_number);
			g_strfreev(columns);
			free_context(ctx);
			return SR_ERR;
		}

		if (ctx->multi_column_mode)
			res = parse_multi_columns(columns, ctx);
		else
			res = parse_single_column(columns[0], ctx);

		if (res != SR_OK) {
			g_strfreev(columns);
			free_context(ctx);
			return SR_ERR;
		}

		g_strfreev(columns);

		/*
		 * TODO: Parse sample numbers / timestamps and use it for
		 * decompression.
		 */

		/* Send sample data to the session bus. */
		res = send_samples(in->sdi, ctx->sample_buffer,
			ctx->sample_buffer_size, 1);

		if (res != SR_OK) {
			sr_err("Sending samples failed.");
			free_context(ctx);
			return SR_ERR;
		}
	}

	/* Send end packet to the session bus. */
	packet.type = SR_DF_END;
	sr_session_send(in->sdi, &packet);

	free_context(ctx);

	return SR_OK;
}

SR_PRIV struct sr_input_format input_csv = {
	.id = "csv",
	.description = "Comma-separated values (CSV)",
	.format_match = format_match,
	.init = init,
	.loadfile = loadfile,
};
