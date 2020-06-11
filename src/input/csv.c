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

#include <ctype.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"	/* String un-quote for channel name from header line. */

#define LOG_PREFIX "input/csv"

#define CHUNK_SIZE	(4 * 1024 * 1024)

/*
 * The CSV input module has the following options:
 *
 * column_formats: Specifies the data formats and channel counts for the
 *     input file's text columns. Accepts a comma separated list of tuples
 *     with: an optional column repeat count ('*' as a wildcard meaning
 *     "all remaining columns", only applicable to the last field), a format
 *     specifying character ('x' hexadecimal, 'o' octal, 'b' binary, 'l'
 *     single-bit logic), and an optional bit count (translating to: logic
 *     channels communicated in that column). The 'a' format marks analog
 *     data, an optionally following number is the digits count (resolution).
 *     The 't' format marks timestamp values, which could help in automatic
 *     determination of the input stream's samplerate. This "column_formats"
 *     option is most versatile, other forms of specifying the column layout
 *     only exist for backwards compatibility, and are rather limited. They
 *     exclusively support logic input data in strictly adjacent columns,
 *     with further constraints on column layout for multi-bit data.
 *
 * single_column: Specifies the column number which contains the logic data
 *     for single-column mode. All logic data is taken from several bits
 *     which all are kept within that one column. Only exists for backwards
 *     compatibility, see "column_formats" for more flexibility.
 *
 * first_column: Specifies the number of the first column with logic data
 *     in simple multi-column mode. Only exists for backwards compatibility,
 *     see "column_formats" for more flexibility.
 *
 * logic_channels: Specifies the number of logic channels. Is required in
 *     simple single-column mode. Is optional in simple multi-column mode
 *     (and defaults to all remaining columns). Only exists for backwards
 *     compatibility, see "column_formats" for more flexibility.
 *
 * single_format: Specifies the format of the input text in simple single-
 *     column mode. Available formats are: 'bin' (default), 'hex' and 'oct'.
 *     Simple multi-column mode always uses single-bit data per column.
 *     Only exists for backwards compatibility, see "column_formats" for
 *     more flexibility.
 *
 * start_line: Specifies at which line to start processing the input file.
 *     Allows to skip leading lines which neither are header nor data lines.
 *     By default all of the input file gets processed.
 *
 * header: Boolean option, controls whether the first processed line is used
 *     to determine channel names. Off by default. Generic channel names are
 *     used in the absence of header line content.
 *
 * samplerate: Specifies the samplerate of the input data. Defaults to 0.
 *     User specs take precedence over data which optionally gets derived
 *     from input data.
 *
 * column_separator: Specifies the sequence which separates the text file
 *     columns. Cannot be empty. Defaults to comma.
 *
 * comment_leader: Specifies the sequence which starts comments that run
 *     up to the end of the current text line. Can be empty to disable
 *     comment support. Defaults to semicolon.
 *
 * Typical examples of using these options:
 * - ... -I csv:column_formats=*l ...
 *   All columns are single-bit logic data. Identical to the previous
 *   multi-column mode (the default when no options were given at all).
 * - ... -I csv:column_formats=3-,*l ...
 *   Ignore the first three columns, get single-bit logic data from all
 *   remaining lines (multi-column mode with first-column above 1).
 * - ... -I csv:column_formats=3-,4l,x8 ...
 *   Ignore the first three columns, get single-bit logic data from the
 *   next four columns, then eight-bit data in hex format from the next
 *   column. More columns may follow in the input text but won't get
 *   processed. (Mix of previous multi-column as well as single-column
 *   modes.)
 * - ... -I csv:column_formats=4x8,b16,5l ...
 *   Get eight-bit data in hex format from the first four columns, then
 *   sixteen-bit data in binary format, then five times single-bit data.
 * - ... -I csv:single_column=2:single_format=bin:logic_channels=8 ...
 *   Get eight logic bits in binary format from column 2. (Simple
 *   single-column mode, corresponds to the "-,b8" format.)
 * - ... -I csv:first_column=6:logic_channels=4 ...
 *   Get four single-bit logic channels from columns 6 to 9 respectively.
 *   (Simple multi-column mode, corresponds to the "5-,4b" format.)
 * - ... -I csv:start_line=20:header=yes:...
 *   Skip the first 19 text lines. Use line 20 to derive channel names.
 *   Data starts at line 21.
 * - ... -I csv:column_formats=*a6 ...
 *   Each column contains an analog value with six significant digits
 *   after the decimal period.
 * - ... -I csv:column_formats=t,2a ...
 *   The first column contains timestamps, the next two columns contain
 *   analog values. The capture's samplerate could get determined from
 *   the timestamp values if not provided by the user by means of the
 *   'samplerate' option. This assumes a mere number in units of seconds,
 *   and equidistant rows, there is no fancy support for textual unit
 *   suffixes nor gaps in the stream of samples nor other non-linearity,
 *   just '-' ignore the column if the format is not supported).
 *
 * IMPORTANT! Make sure the .format_match() logic matches the default
 * values for the input module's options. Ideally the format match test
 * shall pass for all input data that is supported by default.
 */

/*
 * TODO
 *
 * - Unbreak analog data when submitted in the 'double' data type. This
 *   was observed with sigrok-cli screen output. Is analog.encoding->unitsize
 *   not handled appropriately? Is it a sigrok-cli or libsigrok issue?
 * - Add a test suite for input modules in general, and CSV in specific?
 *   Becomes more important with the multitude of options and their
 *   interaction. Could cover edge cases (BOM presence, line termination
 *   absence, etc) and auto-stuff as well (channel names, channel counts,
 *   samplerates, etc).
 */

typedef float csv_analog_t;	/* 'double' currently is flawed. */

/* Single column formats. */
enum single_col_format {
	FORMAT_NONE,	/* Ignore this column. */
	FORMAT_BIN,	/* Bin digits for a set of bits (or just one bit). */
	FORMAT_HEX,	/* Hex digits for a set of bits. */
	FORMAT_OCT,	/* Oct digits for a set of bits. */
	FORMAT_ANALOG,	/* Floating point number for an analog channel. */
	FORMAT_TIME,	/* Timestamps. */
};

static const char *col_format_text[] = {
	[FORMAT_NONE] = "unknown",
	[FORMAT_BIN] = "binary",
	[FORMAT_HEX] = "hexadecimal",
	[FORMAT_OCT] = "octal",
	[FORMAT_ANALOG] = "analog",
	[FORMAT_TIME] = "timestamp",
};

static const char col_format_char[] = {
	[FORMAT_NONE] = '?',
	[FORMAT_BIN] = 'b',
	[FORMAT_HEX] = 'x',
	[FORMAT_OCT] = 'o',
	[FORMAT_ANALOG] = 'a',
	[FORMAT_TIME] = 't',
};

static gboolean format_is_ignore(enum single_col_format fmt)
{
	return fmt == FORMAT_NONE;
}

static gboolean format_is_logic(enum single_col_format fmt)
{
	return fmt >= FORMAT_BIN && fmt <= FORMAT_OCT;
}

static gboolean format_is_analog(enum single_col_format fmt)
{
	return fmt == FORMAT_ANALOG;
}

static gboolean format_is_timestamp(enum single_col_format fmt)
{
	return fmt == FORMAT_TIME;
}

struct column_details {
	size_t col_nr;
	enum single_col_format text_format;
	size_t channel_offset;
	size_t channel_count;
	int analog_digits;
	GString **channel_names;
};

struct context {
	gboolean started;

	/* Current samplerate, optionally determined from input data. */
	uint64_t samplerate;
	uint64_t calc_samplerate;
	double prev_timestamp;
	gboolean samplerate_sent;

	/* Number of channels. */
	size_t logic_channels;
	size_t analog_channels;

	/* Column delimiter (actually separator), comment leader, EOL sequence. */
	GString *delimiter;
	GString *comment;
	char *termination;

	/* Format specs for input columns, and processing state. */
	size_t column_seen_count;
	const char *column_formats;
	size_t column_want_count;
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
	csv_analog_t *analog_sample_buffer;	/**!< Buffer for one set of analog values. */

	uint8_t *datafeed_buffer;	/**!< Queue for datafeed submission. */
	size_t datafeed_buf_size;
	size_t datafeed_buf_fill;
	/* "Striped" layout, M samples for N channels each. */
	csv_analog_t *analog_datafeed_buffer;	/**!< Queue for analog datafeed. */
	size_t analog_datafeed_buf_size;
	size_t analog_datafeed_buf_fill;
	int *analog_datafeed_digits;
	GSList **analog_datafeed_channels;

	/* Current line number. */
	size_t line_number;

	/* List of previously created sigrok channels. */
	GSList *prev_sr_channels;
	GSList **prev_df_channels;
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

static int flush_samplerate(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;
	if (!inc->calc_samplerate && inc->samplerate)
		inc->calc_samplerate = inc->samplerate;
	if (inc->calc_samplerate && !inc->samplerate_sent) {
		(void)sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE,
			g_variant_new_uint64(inc->calc_samplerate));
		inc->samplerate_sent = TRUE;
	}

	return SR_OK;
}

static void clear_logic_samples(struct context *inc)
{
	if (!inc->logic_channels)
		return;
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
	struct sr_datafeed_logic logic;
	int rc;

	inc = in->priv;
	if (!inc->datafeed_buf_fill)
		return SR_OK;

	rc = flush_samplerate(in);
	if (rc != SR_OK)
		return rc;

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

static void set_analog_value(struct context *inc, size_t ch_idx, csv_analog_t value);

static void clear_analog_samples(struct context *inc)
{
	size_t idx;

	if (!inc->analog_channels)
		return;
	inc->analog_sample_buffer = &inc->analog_datafeed_buffer[inc->analog_datafeed_buf_fill];
	for (idx = 0; idx < inc->analog_channels; idx++)
		set_analog_value(inc, idx, 0.0);
}

static void set_analog_value(struct context *inc, size_t ch_idx, csv_analog_t value)
{
	if (ch_idx >= inc->analog_channels)
		return;
	if (!value)
		return;
	inc->analog_sample_buffer[ch_idx * inc->analog_datafeed_buf_size] = value;
}

static int flush_analog_samples(const struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	csv_analog_t *samples;
	size_t ch_idx;
	int digits;
	int rc;

	inc = in->priv;
	if (!inc->analog_datafeed_buf_fill)
		return SR_OK;

	rc = flush_samplerate(in);
	if (rc != SR_OK)
		return rc;

	samples = inc->analog_datafeed_buffer;
	for (ch_idx = 0; ch_idx < inc->analog_channels; ch_idx++) {
		digits = inc->analog_datafeed_digits[ch_idx];
		sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
		memset(&packet, 0, sizeof(packet));
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.num_samples = inc->analog_datafeed_buf_fill;
		analog.data = samples;
		analog.meaning->channels = inc->analog_datafeed_channels[ch_idx];
		analog.meaning->mq = 0;
		analog.meaning->mqflags = 0;
		analog.meaning->unit = 0;
		analog.encoding->unitsize = sizeof(samples[0]);
		analog.encoding->is_signed = TRUE;
		analog.encoding->is_float = TRUE;
#ifdef WORDS_BIGENDIAN
		analog.encoding->is_bigendian = TRUE;
#else
		analog.encoding->is_bigendian = FALSE;
#endif
		analog.encoding->digits = spec.spec_digits;
		rc = sr_session_send(in->sdi, &packet);
		if (rc != SR_OK)
			return rc;
		samples += inc->analog_datafeed_buf_size;
	}

	inc->analog_datafeed_buf_fill = 0;

	return SR_OK;
}

static int queue_analog_samples(const struct sr_input *in)
{
	struct context *inc;
	int rc;

	inc = in->priv;
	if (!inc->analog_channels)
		return SR_OK;

	inc->analog_datafeed_buf_fill++;
	if (inc->analog_datafeed_buf_fill == inc->analog_datafeed_buf_size) {
		rc = flush_analog_samples(in);
		if (rc != SR_OK)
			return rc;
	}

	return SR_OK;
}

/* Helpers for "column processing". */

static int split_column_format(const char *spec,
	size_t *column_count, enum single_col_format *format, size_t *bit_count)
{
	size_t count;
	char *endp, format_char;
	enum single_col_format format_code;

	if (!spec || !*spec)
		return SR_ERR_ARG;

	/* Get the (optional, decimal, default 1) column count. Accept '*'. */
	endp = NULL;
	if (*spec == '*') {
		/* Workaround, strtoul("*") won't always yield expected endp. */
		count = 0;
		endp = (char *)&spec[1];
	} else {
		count = strtoul(spec, &endp, 10);
	}
	if (!endp)
		return SR_ERR_ARG;
	if (endp == spec)
		count = 1;
	if (column_count)
		*column_count = count;
	spec = endp;

	/* Get the (mandatory, single letter) type spec (-/xob/l). */
	format_char = *spec++;
	switch (format_char) {
	case '-':
	case '/':
		format_char = '-';
		format_code = FORMAT_NONE;
		break;
	case 'x':
		format_code = FORMAT_HEX;
		break;
	case 'o':
		format_code = FORMAT_OCT;
		break;
	case 'b':
	case 'l':
		format_code = FORMAT_BIN;
		break;
	case 'a':
		format_code = FORMAT_ANALOG;
		break;
	case 't':
		format_code = FORMAT_TIME;
		break;
	default:	/* includes NUL */
		return SR_ERR_ARG;
	}
	if (format)
		*format = format_code;

	/* Get the (optional, decimal, default 1) bit count. */
	endp = NULL;
	count = strtoul(spec, &endp, 10);
	if (!endp)
		return SR_ERR_ARG;
	if (endp == spec)
		count = format_is_analog(format_code) ? 3 : 1;
	if (format_is_ignore(format_code))
		count = 0;
	if (format_char == 'l')
		count = 1;
	if (bit_count)
		*bit_count = count;
	spec = endp;

	/* Input spec must have been exhausted. */
	if (*spec)
		return SR_ERR_ARG;

	return SR_OK;
}

static int make_column_details_from_format(const struct sr_input *in,
	const char *column_format, char **column_texts)
{
	struct context *inc;
	char **formats, *format;
	size_t format_count, column_count, logic_count, analog_count;
	size_t auto_column_count;
	size_t format_idx, c, b, column_idx, channel_idx, analog_idx;
	enum single_col_format f;
	struct column_details *detail;
	GString *channel_name;
	size_t create_idx;
	char *column;
	const char *caption;
	int channel_type, channel_sdi_nr;
	void *channel;
	int ret;

	inc = in->priv;
	inc->column_seen_count = g_strv_length(column_texts);

	/* Split the input spec, count involved columns and channels. */
	formats = g_strsplit(column_format, ",", 0);
	if (!formats) {
		sr_err("Cannot parse columns format %s (comma split).", column_format);
		return SR_ERR_ARG;
	}
	format_count = g_strv_length(formats);
	if (!format_count) {
		sr_err("Cannot parse columns format %s (field count).", column_format);
		g_strfreev(formats);
		return SR_ERR_ARG;
	}
	column_count = logic_count = analog_count = 0;
	auto_column_count = 0;
	for (format_idx = 0; format_idx < format_count; format_idx++) {
		format = formats[format_idx];
		ret = split_column_format(format, &c, &f, &b);
		sr_dbg("fmt %s -> %zu cols, %s fmt, %zu bits, rc %d", format, c, col_format_text[f], b, ret);
		if (ret != SR_OK) {
			sr_err("Cannot parse columns format %s (field split, %s).", column_format, format);
			g_strfreev(formats);
			return SR_ERR_ARG;
		}
		if (f && !c) {
			/* User requested "auto-count", must be last format. */
			if (formats[format_idx + 1]) {
				sr_err("Auto column count must be last format field.");
				g_strfreev(formats);
				return SR_ERR_ARG;
			}
			auto_column_count = inc->column_seen_count - column_count;
			c = auto_column_count;
		}
		column_count += c;
		if (format_is_analog(f))
			analog_count += c;
		else if (format_is_logic(f))
			logic_count += c * b;
	}
	sr_dbg("Column format %s -> %zu columns, %zu logic, %zu analog channels.",
		column_format, column_count, logic_count, analog_count);

	/* Allocate and fill in "column processing" details. */
	inc->column_want_count = column_count;
	if (inc->column_seen_count < inc->column_want_count) {
		sr_err("Insufficient input text width for desired data amount, got %zu but want %zu columns.",
			inc->column_seen_count, inc->column_want_count);
		g_strfreev(formats);
		return SR_ERR_ARG;
	}
	inc->logic_channels = logic_count;
	inc->analog_channels = analog_count;
	inc->analog_datafeed_digits = g_malloc0(inc->analog_channels * sizeof(inc->analog_datafeed_digits[0]));
	inc->analog_datafeed_channels = g_malloc0(inc->analog_channels * sizeof(inc->analog_datafeed_channels[0]));
	inc->column_details = g_malloc0_n(column_count, sizeof(inc->column_details[0]));
	column_idx = channel_idx = analog_idx = 0;
	channel_name = g_string_sized_new(64);
	for (format_idx = 0; format_idx < format_count; format_idx++) {
		/* Process a format field, which can span multiple columns. */
		format = formats[format_idx];
		(void)split_column_format(format, &c, &f, &b);
		if (f && !c)
			c = auto_column_count;
		while (c-- > 0) {
			/* Fill in a column's processing details. */
			detail = &inc->column_details[column_idx++];
			detail->col_nr = column_idx;
			detail->text_format = f;
			if (format_is_analog(detail->text_format)) {
				detail->channel_offset = analog_idx;
				detail->channel_count = 1;
				detail->analog_digits = b;
				analog_idx += detail->channel_count;
			} else if (format_is_logic(detail->text_format)) {
				detail->channel_offset = channel_idx;
				detail->channel_count = b;
				channel_idx += detail->channel_count;
			} else if (format_is_ignore(detail->text_format)) {
				/* EMPTY */
				continue;
			} else {
				/*
				 * Neither logic nor analog data, nor ignore.
				 * Format was noted. No channel creation involved.
				 */
				continue;
			}
			/*
			 * Pick most appropriate channel names. Optionally
			 * use text from a header line (when requested by the
			 * user). In the absence of header text, channels are
			 * assigned rather generic names.
			 *
			 * Manipulation of the column's caption (when a header
			 * line is seen) is acceptable, because this header
			 * line won't get processed another time.
			 */
			column = column_texts[detail->col_nr - 1];
			if (inc->use_header && column && *column)
				caption = sr_scpi_unquote_string(column);
			else
				caption = NULL;
			if (!caption || !*caption)
				caption = NULL;
			/*
			 * Collect channel creation details here, but defer
			 * actual creation of the channels such that all
			 * logic channels can get created first and analog
			 * channels only get created afterwards.
			 */
			detail->channel_names = g_malloc0(detail->channel_count * sizeof(detail->channel_names[0]));
			for (create_idx = 0; create_idx < detail->channel_count; create_idx++) {
				if (caption && detail->channel_count == 1) {
					g_string_assign(channel_name, caption);
				} else if (caption) {
					g_string_printf(channel_name, "%s[%zu]",
						caption, create_idx);
				} else {
					g_string_printf(channel_name, "%zu",
						detail->channel_offset + create_idx);
				}
				detail->channel_names[create_idx] = g_string_new_len(channel_name->str, channel_name->len);
			}
		}
	}
	g_string_free(channel_name, TRUE);
	g_strfreev(formats);

	/* Create channels in strict logic to analog order. */
	channel_type = SR_CHANNEL_LOGIC;
	for (column_idx = 0; column_idx < inc->column_want_count; column_idx++) {
		detail = &inc->column_details[column_idx];
		if (!format_is_logic(detail->text_format))
			continue;
		for (create_idx = 0; create_idx < detail->channel_count; create_idx++) {
			caption = detail->channel_names[create_idx]->str;
			channel_sdi_nr = g_slist_length(in->sdi->channels);
			sr_channel_new(in->sdi, channel_sdi_nr, channel_type, TRUE, caption);
		}
	}
	channel_type = SR_CHANNEL_ANALOG;
	for (column_idx = 0; column_idx < inc->column_want_count; column_idx++) {
		detail = &inc->column_details[column_idx];
		if (!format_is_analog(detail->text_format))
			continue;
		caption = detail->channel_names[0]->str;
		channel_sdi_nr = g_slist_length(in->sdi->channels);
		channel = sr_channel_new(in->sdi, channel_sdi_nr, channel_type, TRUE, caption);
		channel_idx = channel_sdi_nr - inc->logic_channels;
		inc->analog_datafeed_digits[channel_idx] = detail->analog_digits;
		inc->analog_datafeed_channels[channel_idx] = g_slist_append(NULL, channel);
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
 * Splits a text line into a set of columns.
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
 * Parse a multi-bit field into several logic channels.
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
		default:
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
 * Parse a floating point text into an analog value.
 *
 * @param[in] column	The input text, a floating point number.
 * @param[in] inc	The input module's context.
 * @param[in] details	The column processing details.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Invalid input data (empty, or format error).
 *
 * This routine modifies the analog values in the current sample set,
 * based on the text input and a user provided format spec.
 */
static int parse_analog(const char *column, struct context *inc,
	const struct column_details *details)
{
	size_t length;
	double dvalue; float fvalue;
	csv_analog_t value;
	int ret;

	if (!format_is_analog(details->text_format))
		return SR_ERR_BUG;

	length = strlen(column);
	if (!length) {
		sr_err("Column %zu in line %zu is empty.", details->col_nr,
			inc->line_number);
		return SR_ERR;
	}
	if (sizeof(value) == sizeof(double)) {
		ret = sr_atod_ascii(column, &dvalue);
		value = dvalue;
	} else if (sizeof(value) == sizeof(float)) {
		ret = sr_atof_ascii(column, &fvalue);
		value = fvalue;
	} else {
		ret = SR_ERR_BUG;
	}
	if (ret != SR_OK) {
		sr_err("Cannot parse analog text %s in column %zu in line %zu.",
			column, details->col_nr, inc->line_number);
		return SR_ERR_DATA;
	}
	set_analog_value(inc, details->channel_offset, value);

	return SR_OK;
}

/**
 * Parse a timestamp text, auto-determine samplerate.
 *
 * @param[in] column	The input text, a floating point number.
 * @param[in] inc	The input module's context.
 * @param[in] details	The column processing details.
 *
 * @retval SR_OK	Success.
 * @retval SR_ERR	Invalid input data (empty, or format error).
 *
 * This routine attempts to automatically determine the input data's
 * samplerate from text rows' timestamp values. Only simple formats are
 * supported, user provided values always take precedence.
 */
static int parse_timestamp(const char *column, struct context *inc,
	const struct column_details *details)
{
	double ts, rate;
	int ret;

	if (!format_is_timestamp(details->text_format))
		return SR_ERR_BUG;

	/*
	 * Implementor's notes on timestamp interpretation. Use a simple
	 * approach for improved maintainability which covers most cases
	 * of input data. There is not much gain in adding complexity,
	 * users can easily provide the rate when auto-detection fails.
	 * - Bail out if samplerate is known already.
	 * - Try to interpret the timestamp (simple float conversion).
	 *   If conversion fails then clear all previous knowledge and
	 *   bail out (non-fatal, perhaps warn). Silently ignore values
	 *   of zero since those could be silent fails -- assume that
	 *   genuine data contains at least two adjacent rows with useful
	 *   timestamps for the feature to work reliably. Annoying users
	 *   with "failed to detect" messages is acceptable here, since
	 *   users expecting the feature to work should provide useful
	 *   data, and there are easy ways to disable the detection or
	 *   ignore the column.
	 * - If there is no previous timestamp, keep the current value
	 *   for later reference and bail out.
	 * - If a previous timestamp was seen, determine the difference
	 *   between them, and derive the samplerate. Update internal
	 *   state (the value automatically gets sent to the datafeed),
	 *   and clear previous knowledge. Subsequent calls will ignore
	 *   following input data (see above, rate is known).
	 *
	 * TODO Potential future improvements:
	 * - Prefer rationals over floats for improved precision and
	 *   reduced rounding errors which result in odd rates.
	 * - Support other formats ("2 ms" or similar)?
	 */
	if (inc->calc_samplerate)
		return SR_OK;
	ret = sr_atod_ascii(column, &ts);
	if (ret != SR_OK)
		ts = 0.0;
	if (!ts) {
		sr_info("Cannot convert timestamp text %s in line %zu (or zero value).",
			column, inc->line_number);
		inc->prev_timestamp = 0.0;
		return SR_OK;
	}
	if (!inc->prev_timestamp) {
		sr_dbg("First timestamp value %g in line %zu.",
			ts, inc->line_number);
		inc->prev_timestamp = ts;
		return SR_OK;
	}
	sr_dbg("Second timestamp value %g in line %zu.", ts, inc->line_number);
	ts -= inc->prev_timestamp;
	sr_dbg("Timestamp difference %g in line %zu.",
		ts, inc->line_number);
	if (!ts) {
		sr_warn("Zero timestamp difference in line %zu.",
			inc->line_number);
		inc->prev_timestamp = ts;
		return SR_OK;
	}
	rate = 1.0 / ts;
	rate += 0.5;
	rate = (uint64_t)rate;
	sr_dbg("Rate from timestamp %g in line %zu.", rate, inc->line_number);
	inc->calc_samplerate = rate;
	inc->prev_timestamp = 0.0;

	return SR_OK;
}

/**
 * Parse routine which ignores the input text.
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
	[FORMAT_ANALOG] = parse_analog,
	[FORMAT_TIME] = parse_timestamp,
};

/*
 * BEWARE! Implementor's notes. Sync with feature set and default option
 * values required during maintenance of the input module implementation.
 *
 * When applications invoke .format_match() routines, trying automatic
 * determination of an input file's format handler, then no options are
 * in effect. Because specifying options requires selection of an input
 * module to pass the options to, which obsoletes the format-match check.
 *
 * Which means that we only need to deal with the default format here,
 * which happens to be the simple multi-column format without header
 * lines or leading garbage. Which means that the check can be rather
 * strict, resulting in high levels of confidence upon match, never
 * "accidently" winning for unreadable or unsupported-by-default formats.
 *
 * This .format_match() logic only needs to become more involved when
 * default option values change, or when automatic detection of column
 * data types improves. Then the supported-by-default types of input
 * data must be considered acceptable here in the format-match check
 * as well.
 *
 * Notice that the format check cannot re-use regular processing logic
 * when their implementation assumes proper input data and wll generate
 * diagnostics for unexpected input data. Failure to match the format is
 * non-fatal here, mismatch must remain silent. It's up to applications
 * how large a chunk of data gets passed here (start of the file's
 * content). But inspection of the first few hundred bytes will usually
 * be GoodEnough(TM) for the format-match purpose. Notice that filenames
 * need not necessarily be available to the format-match routine.
 *
 * This implementation errs on the safe side. Users can always select
 * the CSV input module when automatic format detection fails.
 */
static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	const int match_confidence = 100;
	const char *default_extension = ".csv";
	const char *line_termination = "\n";
	const char *comment_leader = ";";
	const char *column_separator = ",";
	const char *binary_charset = "01";

	const char *fn;
	GString *buf;
	size_t fn_len;
	GString *tmpbuf;
	gboolean status;
	size_t line_idx, col_idx;
	char *rdptr, **lines, *line;
	char **cols, *col;

	/* Get the application provided input data properties. */
	fn = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_FILENAME));
	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));

	/* Filenames are a strong hint. Use then when available. */
	if (fn && *fn && (fn_len = strlen(fn)) >= strlen(default_extension)) {
		if (strcasecmp(&fn[fn_len - strlen(default_extension)], default_extension) == 0) {
			*confidence = 10;
			return SR_OK;
		}
	}

	/*
	 * Check file content for compatibility with the input module's
	 * default format. Which translates to:
	 * - Must be at least one text line worth of input data. Ignore
	 *   incomplete lines at the end of the available buffer.
	 * - Must be LF terminated text lines, optional CR-LF sequence.
	 *   (Drop CR-only for simplicity since that's rare and users
	 *   can override the automatic detection.)
	 * - Strip comments and skip empty lines.
	 * - Data lines must be binary input (potentially multiple bits
	 *   per column which then get ignored). Presence of comma is
	 *   optional but then must be followed by another data column.
	 * - No other content is acceptable, there neither are ignored
	 *   columns nor analog data nor timestamps in the default layout.
	 *   (See the above "sync format match with default options"
	 *   comment though during maintenance!)
	 * Run the check on a copy to not affect the caller's buffer.
	 */
	if (!buf || !buf->len || !buf->str || !*buf->str)
		return SR_ERR;
	rdptr = g_strstr_len(buf->str, buf->len, line_termination);
	if (!rdptr)
		return SR_ERR;
	tmpbuf = g_string_new_len(buf->str, rdptr + 1 - buf->str);
	tmpbuf->str[tmpbuf->len - 1] = '\0';
	status = TRUE;
	*confidence = match_confidence;
	lines = g_strsplit(tmpbuf->str, line_termination, 0);
	for (line_idx = 0; status && (line = lines[line_idx]); line_idx++) {
		rdptr = strstr(line, comment_leader);
		if (rdptr)
			*rdptr = '\0';
		line = g_strstrip(line);
		if (!line || !*line)
			continue;
		cols = g_strsplit(line, column_separator, 0);
		if (!cols) {
			status = FALSE;
			break;
		}
		for (col_idx = 0; status && (col = cols[col_idx]); col_idx++) {
			if (strspn(col, binary_charset) != strlen(col)) {
				status = FALSE;
				break;
			}
		}
		g_strfreev(cols);
	}
	g_strfreev(lines);
	g_string_free(tmpbuf, TRUE);

	if (!status)
		return SR_ERR;

	return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	size_t single_column, first_column, logic_channels;
	const char *s;
	enum single_col_format format;
	char format_char;

	in->sdi = g_malloc0(sizeof(*in->sdi));
	in->priv = inc = g_malloc0(sizeof(*inc));

	single_column = g_variant_get_uint32(g_hash_table_lookup(options, "single_column"));
	logic_channels = g_variant_get_uint32(g_hash_table_lookup(options, "logic_channels"));
	inc->delimiter = g_string_new(g_variant_get_string(
			g_hash_table_lookup(options, "column_separator"), NULL));
	if (!inc->delimiter->len) {
		sr_err("Column separator cannot be empty.");
		return SR_ERR_ARG;
	}
	s = g_variant_get_string(g_hash_table_lookup(options, "single_format"), NULL);
	if (g_ascii_strncasecmp(s, "bin", 3) == 0) {
		format = FORMAT_BIN;
	} else if (g_ascii_strncasecmp(s, "hex", 3) == 0) {
		format = FORMAT_HEX;
	} else if (g_ascii_strncasecmp(s, "oct", 3) == 0) {
		format = FORMAT_OCT;
	} else {
		sr_err("Invalid single-column format: '%s'", s);
		return SR_ERR_ARG;
	}
	inc->comment = g_string_new(g_variant_get_string(
			g_hash_table_lookup(options, "comment_leader"), NULL));
	if (g_string_equal(inc->comment, inc->delimiter)) {
		/*
		 * Using the same sequence as comment leader and column
		 * separator won't work. The user probably specified ';'
		 * as the column separator but did not adjust the comment
		 * leader. Try DWIM, drop comment strippin support here.
		 */
		sr_warn("Comment leader and column separator conflict, disabling comment support.");
		g_string_truncate(inc->comment, 0);
	}
	inc->samplerate = g_variant_get_uint64(g_hash_table_lookup(options, "samplerate"));
	first_column = g_variant_get_uint32(g_hash_table_lookup(options, "first_column"));
	inc->use_header = g_variant_get_boolean(g_hash_table_lookup(options, "header"));
	inc->start_line = g_variant_get_uint32(g_hash_table_lookup(options, "start_line"));
	if (inc->start_line < 1) {
		sr_err("Invalid start line %zu.", inc->start_line);
		return SR_ERR_ARG;
	}

	/*
	 * Scan flexible, to get prefered format specs which describe
	 * the input file's data formats. As well as some simple specs
	 * for backwards compatibility and user convenience.
	 *
	 * This logic ends up with a copy of the format string, either
	 * user provided or internally derived. Actual creation of the
	 * column processing details gets deferred until the first line
	 * of input data was seen. To support automatic determination of
	 * e.g. channel counts from column counts.
	 */
	s = g_variant_get_string(g_hash_table_lookup(options, "column_formats"), NULL);
	if (s && *s) {
		inc->column_formats = g_strdup(s);
		sr_dbg("User specified column_formats: %s.", s);
	} else if (single_column && logic_channels) {
		format_char = col_format_char[format];
		if (single_column == 1) {
			inc->column_formats = g_strdup_printf("%c%zu",
				format_char, logic_channels);
		} else {
			inc->column_formats = g_strdup_printf("%zu-,%c%zu",
				single_column - 1,
				format_char, logic_channels);
		}
		sr_dbg("Backwards compat single_column, col %zu, fmt %s, bits %zu -> %s.",
			single_column, col_format_text[format], logic_channels,
			inc->column_formats);
	} else if (!single_column) {
		if (first_column > 1) {
			inc->column_formats = g_strdup_printf("%zu-,%zul",
				first_column - 1, logic_channels);
		} else {
			inc->column_formats = g_strdup_printf("%zul",
				logic_channels);
		}
		sr_dbg("Backwards compat multi-column, col %zu, chans %zu -> %s.",
			first_column, logic_channels,
			inc->column_formats);
	} else {
		sr_warn("Unknown or unsupported columns layout spec, assuming simple multi-column mode.");
		inc->column_formats = g_strdup("*l");
	}

	return SR_OK;
}

/*
 * Check the channel list for consistency across file re-import. See
 * the VCD input module for more details and motivation.
 */
static void release_df_channels(struct context *inc, GSList **l)
{
	size_t idx;

	if (!inc->analog_channels || !l)
		return;
	for (idx = 0; idx < inc->analog_channels; idx++)
		g_slist_free(l[idx]);
	g_free(l);
}

static void keep_header_for_reread(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	g_slist_free_full(inc->prev_sr_channels, sr_channel_free_cb);
	inc->prev_sr_channels = in->sdi->channels;
	in->sdi->channels = NULL;

	release_df_channels(inc, inc->prev_df_channels);
	inc->prev_df_channels = inc->analog_datafeed_channels;
	inc->analog_datafeed_channels = NULL;
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

	release_df_channels(inc, inc->analog_datafeed_channels);
	inc->analog_datafeed_channels = inc->prev_df_channels;
	inc->prev_df_channels = NULL;

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
	size_t num_columns;
	size_t line_number, line_idx;
	int ret;
	char **lines, *line, **columns;

	ret = SR_OK;
	inc = in->priv;
	columns = NULL;

	/* Search for the first line to process (header or data). */
	line_number = 0;
	if (inc->termination)
		lines = g_strsplit(buf->str, inc->termination, 0);
	else
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

	/* Get the number of columns in the line. */
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
	sr_dbg("Got %zu columns in text line: %s.", num_columns, line);

	/*
	 * Interpret the user provided column format specs. This might
	 * involve inspection of the now received input text, to support
	 * e.g. automatic detection of channel counts in the absence of
	 * user provided specs. Optionally a header line is used to get
	 * channels' names.
	 *
	 * Check the then created channels for consistency across .reset
	 * and .receive sequences (file re-load).
	 */
	ret = make_column_details_from_format(in, inc->column_formats, columns);
	if (ret != SR_OK) {
		sr_err("Cannot parse columns format using line %zu.", line_number);
		goto out;
	}
	if (!check_header_in_reread(in)) {
		ret = SR_ERR_DATA;
		goto out;
	}

	/*
	 * Allocate buffer memory for datafeed submission of sample data.
	 * Calculate the minimum buffer size to store the set of samples
	 * of all channels (unit size). Determine a larger buffer size
	 * for datafeed submission that is a multiple of the unit size.
	 * Allocate the larger buffer, the "sample buffer" will point
	 * to a location within that large buffer later.
	 *
	 * TODO Move channel creation here, and just store required
	 * parameters in the format parser above? Could simplify the
	 * arrangement that logic and analog channels get created in
	 * strict sequence in their respective groups.
	 */
	if (inc->logic_channels) {
		inc->sample_unit_size = (inc->logic_channels + 7) / 8;
		inc->datafeed_buf_size = CHUNK_SIZE;
		inc->datafeed_buf_size *= inc->sample_unit_size;
		inc->datafeed_buffer = g_malloc(inc->datafeed_buf_size);
		if (!inc->datafeed_buffer) {
			sr_err("Cannot allocate datafeed send buffer (logic).");
			ret = SR_ERR_MALLOC;
			goto out;
		}
		inc->datafeed_buf_fill = 0;
	}

	if (inc->analog_channels) {
		size_t sample_size, sample_count;
		sample_size = sizeof(inc->analog_datafeed_buffer[0]);
		inc->analog_datafeed_buf_size = CHUNK_SIZE;
		inc->analog_datafeed_buf_size /= sample_size;
		inc->analog_datafeed_buf_size /= inc->analog_channels;
		sample_count = inc->analog_channels * inc->analog_datafeed_buf_size;
		inc->analog_datafeed_buffer = g_malloc0(sample_count * sample_size);
		if (!inc->analog_datafeed_buffer) {
			sr_err("Cannot allocate datafeed send buffer (analog).");
			ret = SR_ERR_MALLOC;
			goto out;
		}
		inc->analog_datafeed_buf_fill = 0;
	}

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
	char *processed_up_to;
	char **lines, *line, **columns, *column;

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
		processed_up_to = in->buf->str + in->buf->len;
	} else {
		processed_up_to = g_strrstr_len(in->buf->str, in->buf->len,
			inc->termination);
		if (!processed_up_to)
			return SR_OK;
		*processed_up_to = '\0';
		processed_up_to += strlen(inc->termination);
	}

	/* Split input text lines and process their columns. */
	ret = SR_OK;
	lines = g_strsplit(in->buf->str, inc->termination, 0);
	for (line_idx = 0; (line = lines[line_idx]); line_idx++) {
		inc->line_number++;
		if (inc->line_number < inc->start_line) {
			sr_spew("Line %zu skipped (before start).", inc->line_number);
			continue;
		}
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
		clear_analog_samples(inc);
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
		ret += queue_analog_samples(in);
		if (ret != SR_OK) {
			sr_err("Sending samples failed.");
			g_strfreev(columns);
			g_strfreev(lines);
			return SR_ERR;
		}

		g_strfreev(columns);
	}
	g_strfreev(lines);
	g_string_erase(in->buf, 0, processed_up_to - in->buf->str);

	return ret;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;

	g_string_append_len(in->buf, buf->str, buf->len);

	inc = in->priv;
	if (!inc->column_seen_count) {
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
	ret += flush_analog_samples(in);
	if (ret != SR_OK)
		return ret;

	inc = in->priv;
	if (inc->started)
		std_session_send_df_end(in->sdi);

	return ret;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc, save_ctx;

	/* Keep channel references between file re-imports. */
	keep_header_for_reread(in);

	/* Release dynamically allocated resources. */
	inc = in->priv;

	g_free(inc->termination);
	inc->termination = NULL;
	g_free(inc->datafeed_buffer);
	inc->datafeed_buffer = NULL;
	g_free(inc->analog_datafeed_buffer);
	inc->analog_datafeed_buffer = NULL;
	g_free(inc->analog_datafeed_digits);
	inc->analog_datafeed_digits = NULL;
	/* analog_datafeed_channels was released in keep_header_for_reread() */
	/* TODO Release channel names (before releasing details). */
	g_free(inc->column_details);
	inc->column_details = NULL;

	/* Clear internal state, but keep what .init() has provided. */
	save_ctx = *inc;
	memset(inc, 0, sizeof(*inc));
	inc->samplerate = save_ctx.samplerate;
	inc->delimiter = save_ctx.delimiter;
	inc->comment = save_ctx.comment;
	inc->column_formats = save_ctx.column_formats;
	inc->start_line = save_ctx.start_line;
	inc->use_header = save_ctx.use_header;
	inc->prev_sr_channels = save_ctx.prev_sr_channels;
	inc->prev_df_channels = save_ctx.prev_df_channels;
}

static int reset(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;
	cleanup(in);
	inc->started = FALSE;
	g_string_truncate(in->buf, 0);

	return SR_OK;
}

enum option_index {
	OPT_COL_FMTS,
	OPT_SINGLE_COL,
	OPT_FIRST_COL,
	OPT_NUM_LOGIC,
	OPT_SINGLE_FMT,
	OPT_START_LINE,
	OPT_HEADER,
	OPT_SAMPLERATE,
	OPT_COL_SEP,
	OPT_COMMENT,
	OPT_MAX,
};

static struct sr_option options[] = {
	[OPT_COL_FMTS] = {
		"column_formats", "Column format specs",
		"Text columns data types. A comma separated list of [<cols>]<fmt>[<bits>] items. * for all remaining columns. - ignores columns, x/o/b/l logic data, a (and digits) analog data, t timestamps.",
		NULL, NULL,
	},
	[OPT_SINGLE_COL] = {
		"single_column", "Single column",
		"Simple single-column mode, exclusively use text from the specified column (number starting at 1). Obsoleted by 'column_formats=4-,x16'.",
		NULL, NULL,
	},
	[OPT_FIRST_COL] = {
		"first_column", "First column",
		"First column with logic data in simple multi-column mode (number starting at 1, default 1). Obsoleted by 'column_formats=4-,*l'.",
		NULL, NULL,
	},
	[OPT_NUM_LOGIC] = {
		"logic_channels", "Number of logic channels",
		"Logic channel count, required in simple single-column mode, defaults to \"all remaining columns\" in simple multi-column mode. Obsoleted by 'column_formats=8l'.",
		NULL, NULL,
	},
	[OPT_SINGLE_FMT] = {
		"single_format", "Data format for simple single-column mode.",
		"The input text number format of simple single-column mode: bin, hex, oct. Obsoleted by 'column_formats=x8'.",
		NULL, NULL,
	},
	[OPT_START_LINE] = {
		"start_line", "Start line",
		"The line number at which to start processing input text (default: 1).",
		NULL, NULL,
	},
	[OPT_HEADER] = {
		"header", "Get channel names from first line.",
		"Use the first processed line's column captions (when available) as channel names. Enabled by default.",
		NULL, NULL,
	},
	[OPT_SAMPLERATE] = {
		"samplerate", "Samplerate (Hz)",
		"The input data's sample rate in Hz. No default value.",
		NULL, NULL,
	},
	[OPT_COL_SEP] = {
		"column_separator", "Column separator",
		"The sequence which separates text columns. Non-empty text, comma by default.",
		NULL, NULL,
	},
	[OPT_COMMENT] = {
		"comment_leader", "Comment leader character",
		"The text which starts comments at the end of text lines, semicolon by default.",
		NULL, NULL,
	},
	[OPT_MAX] = ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	GSList *l;

	if (!options[0].def) {
		options[OPT_COL_FMTS].def = g_variant_ref_sink(g_variant_new_string(""));
		options[OPT_SINGLE_COL].def = g_variant_ref_sink(g_variant_new_uint32(0));
		options[OPT_FIRST_COL].def = g_variant_ref_sink(g_variant_new_uint32(1));
		options[OPT_NUM_LOGIC].def = g_variant_ref_sink(g_variant_new_uint32(0));
		options[OPT_SINGLE_FMT].def = g_variant_ref_sink(g_variant_new_string("bin"));
		l = NULL;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("bin")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("hex")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("oct")));
		options[OPT_SINGLE_FMT].values = l;
		options[OPT_START_LINE].def = g_variant_ref_sink(g_variant_new_uint32(1));
		options[OPT_HEADER].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[OPT_SAMPLERATE].def = g_variant_ref_sink(g_variant_new_uint64(0));
		options[OPT_COL_SEP].def = g_variant_ref_sink(g_variant_new_string(","));
		options[OPT_COMMENT].def = g_variant_ref_sink(g_variant_new_string(";"));
	}

	return options;
}

SR_PRIV struct sr_input_module input_csv = {
	.id = "csv",
	.name = "CSV",
	.desc = "Comma-separated values",
	.exts = (const char*[]){"csv", NULL},
	.metadata = { SR_INPUT_META_FILENAME, SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
