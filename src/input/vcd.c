/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Petteri Aimonen <jpa@sr.mail.kapsi.fi>
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

/* The VCD input module has the following options:
 *
 * numchannels: Maximum number of channels to use. The channels are
 *              detected in the same order as they are listed
 *              in the $var sections of the VCD file.
 *
 * skip:        Allows skipping until given timestamp in the file.
 *              This can speed up analyzing of long captures.
 *
 *              Value < 0: Skip until first timestamp listed in
 *              the file. (default)
 *
 *              Value = 0: Do not skip, instead generate samples
 *              beginning from timestamp 0.
 *
 *              Value > 0: Start at the given timestamp.
 *
 * downsample:  Divide the samplerate by the given factor.
 *              This can speed up analyzing of long captures.
 *
 * compress:    Compress idle periods longer than this value.
 *              This can speed up analyzing of long captures.
 *              Default 0 = don't compress.
 *
 * Based on Verilog standard IEEE Std 1364-2001 Version C
 *
 * Supported features:
 * - $var with 'wire' and 'reg' types of scalar variables
 * - $timescale definition for samplerate
 * - multiple character variable identifiers
 *
 * Most important unsupported features:
 * - vector variables (bit vectors etc.)
 * - analog, integer and real number variables
 * - $dumpvars initial value declaration
 * - $scope namespaces
 * - more than 64 channels
 */

#include <config.h>
#include <stdlib.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/vcd"

#define CHUNKSIZE (1024 * 1024)

struct context {
	gboolean started;
	gboolean got_header;
	uint64_t samplerate;
	unsigned int maxchannels;
	unsigned int channelcount;
	int downsample;
	unsigned compress;
	int64_t skip;
	gboolean skip_until_end;
	GSList *channels;
	size_t bytes_per_sample;
	size_t samples_in_buffer;
	uint8_t *buffer;
	uint8_t *current_levels;
};

struct vcd_channel {
	gchar *name;
	gchar *identifier;
};

/*
 * Reads a single VCD section from input file and parses it to name/contents.
 * e.g. $timescale 1ps $end => "timescale" "1ps"
 */
static gboolean parse_section(GString *buf, gchar **name, gchar **contents)
{
	GString *sname, *scontent;
	gboolean status;
	unsigned int pos;

	*name = *contents = NULL;
	status = FALSE;
	pos = 0;

	/* Skip UTF8 BOM */
	if (buf->len >= 3 && !strncmp(buf->str, "\xef\xbb\xbf", 3))
		pos = 3;

	/* Skip any initial white-space. */
	while (pos < buf->len && g_ascii_isspace(buf->str[pos]))
		pos++;

	/* Section tag should start with $. */
	if (buf->str[pos++] != '$')
		return FALSE;

	sname = g_string_sized_new(32);
	scontent = g_string_sized_new(128);

	/* Read the section tag. */
	while (pos < buf->len && !g_ascii_isspace(buf->str[pos]))
		g_string_append_c(sname, buf->str[pos++]);

	/* Skip whitespace before content. */
	while (pos < buf->len && g_ascii_isspace(buf->str[pos]))
		pos++;

	/* Read the content. */
	while (pos < buf->len - 4 && strncmp(buf->str + pos, "$end", 4))
		g_string_append_c(scontent, buf->str[pos++]);

	if (sname->len && pos < buf->len - 4 && !strncmp(buf->str + pos, "$end", 4)) {
		status = TRUE;
		pos += 4;
		while (pos < buf->len && g_ascii_isspace(buf->str[pos]))
			pos++;
		g_string_erase(buf, 0, pos);
	}

	*name = g_string_free(sname, !status);
	*contents = g_string_free(scontent, !status);
	if (*contents)
		g_strchomp(*contents);

	return status;
}

static void free_channel(void *data)
{
	struct vcd_channel *vcd_ch = data;
	g_free(vcd_ch->name);
	g_free(vcd_ch->identifier);
	g_free(vcd_ch);
}

/* Remove empty parts from an array returned by g_strsplit. */
static void remove_empty_parts(gchar **parts)
{
	gchar **src = parts;
	gchar **dest = parts;
	while (*src != NULL) {
		if (**src != '\0')
			*dest++ = *src;
		src++;
	}

	*dest = NULL;
}

/*
 * Parse VCD header to get values for context structure.
 * The context structure should be zeroed before calling this.
 */
static gboolean parse_header(const struct sr_input *in, GString *buf)
{
	struct vcd_channel *vcd_ch;
	uint64_t p, q;
	struct context *inc;
	gboolean status;
	gchar *name, *contents, **parts;

	inc = in->priv;
	name = contents = NULL;
	status = FALSE;
	while (parse_section(buf, &name, &contents)) {
		sr_dbg("Section '%s', contents '%s'.", name, contents);

		if (g_strcmp0(name, "enddefinitions") == 0) {
			status = TRUE;
			break;
		} else if (g_strcmp0(name, "timescale") == 0) {
			/*
			 * The standard allows for values 1, 10 or 100
			 * and units s, ms, us, ns, ps and fs.
			 */
			if (sr_parse_period(contents, &p, &q) == SR_OK) {
				inc->samplerate = q / p;
				if (q % p != 0) {
					/* Does not happen unless time value is non-standard */
					sr_warn("Inexact rounding of samplerate, %" PRIu64 " / %" PRIu64 " to %" PRIu64 " Hz.",
						q, p, inc->samplerate);
				}

				sr_dbg("Samplerate: %" PRIu64, inc->samplerate);
			} else {
				sr_err("Parsing timescale failed.");
			}
		} else if (g_strcmp0(name, "var") == 0) {
			/* Format: $var type size identifier reference [opt. index] $end */
			unsigned int length;

			parts = g_strsplit_set(contents, " \r\n\t", 0);
			remove_empty_parts(parts);
			length = g_strv_length(parts);

			if (length != 4 && length != 5)
				sr_warn("$var section should have 4 or 5 items");
			else if (g_strcmp0(parts[0], "reg") != 0 && g_strcmp0(parts[0], "wire") != 0)
				sr_info("Unsupported signal type: '%s'", parts[0]);
			else if (strtol(parts[1], NULL, 10) != 1)
				sr_info("Unsupported signal size: '%s'", parts[1]);
			else if (inc->maxchannels && inc->channelcount >= inc->maxchannels)
				sr_warn("Skipping '%s%s' because only %d channels requested.",
					parts[3], parts[4] ? : "", inc->maxchannels);
			else {
				vcd_ch = g_malloc(sizeof(struct vcd_channel));
				vcd_ch->identifier = g_strdup(parts[2]);
				if (length == 4)
					vcd_ch->name = g_strdup(parts[3]);
				else
					vcd_ch->name = g_strconcat(parts[3], parts[4], NULL);

				sr_info("Channel %d is '%s' identified by '%s'.",
						inc->channelcount, vcd_ch->name, vcd_ch->identifier);

				sr_channel_new(in->sdi, inc->channelcount++, SR_CHANNEL_LOGIC, TRUE, vcd_ch->name);
				inc->channels = g_slist_append(inc->channels, vcd_ch);
			}

			g_strfreev(parts);
		}

		g_free(name);
		name = NULL;
		g_free(contents);
		contents = NULL;
	}
	g_free(name);
	g_free(contents);

	/*
	 * Compute how many bytes each sample will have and initialize the
	 * current levels. The current levels will be updated whenever VCD
	 * has changes.
	 */
	inc->bytes_per_sample = (inc->channelcount + 7) / 8;
	inc->current_levels = g_malloc0(inc->bytes_per_sample);

	inc->got_header = status;

	return status;
}

static int format_match(GHashTable *metadata)
{
	GString *buf, *tmpbuf;
	gboolean status;
	gchar *name, *contents;

	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
	tmpbuf = g_string_new_len(buf->str, buf->len);

	/*
	 * If we can parse the first section correctly,
	 * then it is assumed to be a VCD file.
	 */
	status = parse_section(tmpbuf, &name, &contents);
	g_string_free(tmpbuf, TRUE);
	g_free(name);
	g_free(contents);

	return status ? SR_OK : SR_ERR;
}

/* Send all accumulated bytes from inc->buffer. */
static void send_buffer(const struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	inc = in->priv;

	if (inc->samples_in_buffer == 0)
		return;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = inc->bytes_per_sample;
	logic.data = inc->buffer;
	logic.length = inc->bytes_per_sample * inc->samples_in_buffer;
	sr_session_send(in->sdi, &packet);
	inc->samples_in_buffer = 0;
}

/*
 * Add N copies of the current sample to buffer.
 * When the buffer fills up, automatically send it.
 */
static void add_samples(const struct sr_input *in, size_t count)
{
	struct context *inc;
	size_t samples_per_chunk;
	size_t space_left, i;
	uint8_t *p;

	inc = in->priv;
	samples_per_chunk = CHUNKSIZE / inc->bytes_per_sample;

	while (count) {
		space_left = samples_per_chunk - inc->samples_in_buffer;

		if (space_left > count)
			space_left = count;

		p = inc->buffer + inc->samples_in_buffer * inc->bytes_per_sample;
		for (i = 0; i < space_left; i++) {
			memcpy(p, inc->current_levels, inc->bytes_per_sample);
			p += inc->bytes_per_sample;
			inc->samples_in_buffer++;
			count--;
		}

		if (inc->samples_in_buffer == samples_per_chunk)
			send_buffer(in);
	}
}

/* Set the channel level depending on the identifier and parsed value. */
static void process_bit(struct context *inc, char *identifier, unsigned int bit)
{
	GSList *l;
	struct vcd_channel *vcd_ch;
	unsigned int j;

	for (j = 0, l = inc->channels; j < inc->channelcount && l; j++, l = l->next) {
		vcd_ch = l->data;
		if (g_strcmp0(identifier, vcd_ch->identifier) == 0) {
			/* Found our channel. */
			size_t byte_idx = (j / 8);
			size_t bit_idx = j - 8 * byte_idx;
			if (bit)
				inc->current_levels[byte_idx] |= (uint8_t)1 << bit_idx;
			else
				inc->current_levels[byte_idx] &= ~((uint8_t)1 << bit_idx);
			break;
		}
	}
	if (j == inc->channelcount)
		sr_dbg("Did not find channel for identifier '%s'.", identifier);
}

/* Parse a set of lines from the data section. */
static void parse_contents(const struct sr_input *in, char *data)
{
	struct context *inc;
	uint64_t timestamp, prev_timestamp;
	unsigned int bit, i;
	char **tokens;

	inc = in->priv;
	prev_timestamp = 0;

	/* Read one space-delimited token at a time. */
	tokens = g_strsplit_set(data, " \t\r\n", 0);
	remove_empty_parts(tokens);
	for (i = 0; tokens[i]; i++) {
		if (inc->skip_until_end) {
			if (!strcmp(tokens[i], "$end")) {
				/* Done with unhandled/unknown section. */
				inc->skip_until_end = FALSE;
				break;
			}
		}
		if (tokens[i][0] == '#' && g_ascii_isdigit(tokens[i][1])) {
			/* Numeric value beginning with # is a new timestamp value */
			timestamp = strtoull(tokens[i] + 1, NULL, 10);

			if (inc->downsample > 1)
				timestamp /= inc->downsample;

			/*
			 * Skip < 0 => skip until first timestamp.
			 * Skip = 0 => don't skip
			 * Skip > 0 => skip until timestamp >= skip.
			 */
			if (inc->skip < 0) {
				inc->skip = timestamp;
				prev_timestamp = timestamp;
			} else if (inc->skip > 0 && timestamp < (uint64_t)inc->skip) {
				prev_timestamp = inc->skip;
			} else if (timestamp == prev_timestamp) {
				/* Ignore repeated timestamps (e.g. sigrok outputs these) */
			} else {
				if (inc->compress != 0 && timestamp - prev_timestamp > inc->compress) {
					/* Compress long idle periods */
					prev_timestamp = timestamp - inc->compress;
				}

				sr_dbg("New timestamp: %" PRIu64, timestamp);

				/* Generate samples from prev_timestamp up to timestamp - 1. */
				add_samples(in, timestamp - prev_timestamp);
				prev_timestamp = timestamp;
			}
		} else if (tokens[i][0] == '$' && tokens[i][1] != '\0') {
			/*
			 * This is probably a $dumpvars, $comment or similar.
			 * $dump* contain useful data.
			 */
			if (g_strcmp0(tokens[i], "$dumpvars") == 0
					|| g_strcmp0(tokens[i], "$dumpon") == 0
					|| g_strcmp0(tokens[i], "$dumpoff") == 0
					|| g_strcmp0(tokens[i], "$end") == 0) {
				/* Ignore, parse contents as normally. */
			} else {
				/* Ignore this and future lines until $end. */
				inc->skip_until_end = TRUE;
				break;
			}
		} else if (strchr("rR", tokens[i][0]) != NULL) {
			sr_dbg("Real type vector values not supported yet!");
			if (!tokens[++i])
				/* No tokens left, bail out */
				break;
			else
				/* Process next token */
				continue;
		} else if (strchr("bB", tokens[i][0]) != NULL) {
			bit = (tokens[i][1] == '1');

			/*
			 * Bail out if a) char after 'b' is NUL, or b) there is
			 * a second character after 'b', or c) there is no
			 * identifier.
			 */
			if (!tokens[i][1] || tokens[i][2] || !tokens[++i]) {
				sr_dbg("Unexpected vector format!");
				break;
			}

			process_bit(inc, tokens[i], bit);
		} else if (strchr("01xXzZ", tokens[i][0]) != NULL) {
			char *identifier;

			/* A new 1-bit sample value */
			bit = (tokens[i][0] == '1');

			/*
			 * The identifier is either the next character, or, if
			 * there was whitespace after the bit, the next token.
			 */
			if (tokens[i][1] == '\0') {
				if (!tokens[++i]) {
					sr_dbg("Identifier missing!");
					break;
				}
				identifier = tokens[i];
			} else {
				identifier = tokens[i] + 1;
			}
			process_bit(inc, identifier, bit);
		} else {
			sr_warn("Skipping unknown token '%s'.", tokens[i]);
		}
	}
	g_strfreev(tokens);
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;

	inc = in->priv = g_malloc0(sizeof(struct context));

	inc->maxchannels = g_variant_get_int32(g_hash_table_lookup(options, "numchannels"));
	inc->downsample = g_variant_get_int32(g_hash_table_lookup(options, "downsample"));
	if (inc->downsample < 1)
		inc->downsample = 1;

	inc->compress = g_variant_get_int32(g_hash_table_lookup(options, "compress"));
	inc->skip = g_variant_get_int32(g_hash_table_lookup(options, "skip"));
	inc->skip /= inc->downsample;

	in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
	in->priv = inc;

	inc->buffer = g_malloc(CHUNKSIZE);

	return SR_OK;
}

static gboolean have_header(GString *buf)
{
	unsigned int pos;
	char *p;

	if (!(p = g_strstr_len(buf->str, buf->len, "$enddefinitions")))
		return FALSE;
	pos = p - buf->str + 15;
	while (pos < buf->len - 4 && g_ascii_isspace(buf->str[pos]))
		pos++;
	if (!strncmp(buf->str + pos, "$end", 4))
		return TRUE;

	return FALSE;
}

static int process_buffer(struct sr_input *in)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct context *inc;
	uint64_t samplerate;
	char *p;

	inc = in->priv;
	if (!inc->started) {
		std_session_send_df_header(in->sdi);

		packet.type = SR_DF_META;
		packet.payload = &meta;
		samplerate = inc->samplerate / inc->downsample;
		src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(samplerate));
		meta.config = g_slist_append(NULL, src);
		sr_session_send(in->sdi, &packet);
		g_slist_free(meta.config);
		sr_config_free(src);

		inc->started = TRUE;
	}

	while ((p = g_strrstr_len(in->buf->str, in->buf->len, "\n"))) {
		*p = '\0';
		g_strstrip(in->buf->str);
		if (in->buf->str[0] != '\0')
			parse_contents(in, in->buf->str);
		g_string_erase(in->buf, 0, p - in->buf->str + 1);
	}

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;

	g_string_append_len(in->buf, buf->str, buf->len);

	inc = in->priv;
	if (!inc->got_header) {
		if (!have_header(in->buf))
			return SR_OK;
		if (!parse_header(in, in->buf))
			/* There was a header in there, but it was malformed. */
			return SR_ERR;

		in->sdi_ready = TRUE;
		/* sdi is ready, notify frontend. */
		return SR_OK;
	}

	ret = process_buffer(in);

	return ret;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	inc = in->priv;

	if (in->sdi_ready)
		ret = process_buffer(in);
	else
		ret = SR_OK;

	/* Send any samples that haven't been sent yet. */
	send_buffer(in);

	if (inc->started)
		std_session_send_df_end(in->sdi);

	return ret;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;
	g_slist_free_full(inc->channels, free_channel);
	g_free(inc->buffer);
	inc->buffer = NULL;
	g_free(inc->current_levels);
	inc->current_levels = NULL;
}

static int reset(struct sr_input *in)
{
	struct context *inc = in->priv;

	cleanup(in);
	inc->started = FALSE;
	g_string_truncate(in->buf, 0);

	return SR_OK;
}

static struct sr_option options[] = {
	{ "numchannels", "Number of channels", "Number of channels", NULL, NULL },
	{ "skip", "Skip", "Skip until timestamp", NULL, NULL },
	{ "downsample", "Downsample", "Divide samplerate by factor", NULL, NULL },
	{ "compress", "Compress", "Compress idle periods longer than this value", NULL, NULL },
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_int32(0));
		options[1].def = g_variant_ref_sink(g_variant_new_int32(-1));
		options[2].def = g_variant_ref_sink(g_variant_new_int32(1));
		options[3].def = g_variant_ref_sink(g_variant_new_int32(0));
	}

	return options;
}

SR_PRIV struct sr_input_module input_vcd = {
	.id = "vcd",
	.name = "VCD",
	.desc = "Value Change Dump",
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
