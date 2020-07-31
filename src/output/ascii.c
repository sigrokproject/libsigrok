/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Håvard Espeland <gus@ping.uio.no>
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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/hex"

#define DEFAULT_SAMPLES_PER_LINE 74

/*
 * The string looks ugly with escape characters, here is the readable
 * version: Use . and " for low and high bits, use \ and / to draw
 * falling and rising edges respectively.
 */
#define DEFAULT_ASCII_CHARS ".\"\\/"

struct context {
	size_t num_enabled_channels;
	size_t spl;
	size_t spl_cnt;
	int trigger;
	uint64_t samplerate;
	int *channel_index;
	char **aligned_names;
	size_t max_namelen;
	char **line_values;
	uint8_t *prev_sample;
	gboolean header_done;
	GString **lines;
	const char *charset;
	gboolean edges;
};

static int init(struct sr_output *o, GHashTable *options)
{
	struct context *ctx;
	struct sr_channel *ch;
	GSList *l;
	size_t j, max_namelen, alloc_line_len;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	ctx = g_malloc0(sizeof(struct context));
	o->priv = ctx;
	ctx->trigger = -1;
	ctx->spl = g_variant_get_uint32(g_hash_table_lookup(options, "width"));
	ctx->charset = g_strdup(g_variant_get_string(
		g_hash_table_lookup(options, "charset"), NULL));
	if (!ctx->charset || strlen(ctx->charset) < 2) {
		g_free((gpointer)ctx->charset);
		ctx->charset = g_strdup(DEFAULT_ASCII_CHARS);
	}
	ctx->edges = (strlen(ctx->charset) >= 4) ? TRUE : FALSE;

	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		ctx->num_enabled_channels++;
	}
	ctx->channel_index = g_malloc0(sizeof(ctx->channel_index[0]) * ctx->num_enabled_channels);
	ctx->aligned_names = g_malloc0(sizeof(ctx->aligned_names[0]) * ctx->num_enabled_channels);
	ctx->lines = g_malloc0(sizeof(ctx->lines[0]) * ctx->num_enabled_channels);
	ctx->prev_sample = g_malloc0(g_slist_length(o->sdi->channels));

	/* Get the maximum length across all active logic channels. */
	max_namelen = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		max_namelen = MAX(max_namelen, strlen(ch->name));
	}
	ctx->max_namelen = max_namelen;

	alloc_line_len = ctx->max_namelen + 8 + ctx->spl;
	j = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;

		ctx->channel_index[j] = ch->index;
		ctx->aligned_names[j] = g_strdup_printf("%*s", (int)max_namelen, ch->name);

		ctx->lines[j] = g_string_sized_new(alloc_line_len);
		g_string_printf(ctx->lines[j], "%s:", ctx->aligned_names[j]);

		j++;
	}

	return SR_OK;
}

static GString *gen_header(const struct sr_output *o)
{
	struct context *ctx;
	GVariant *gvar;
	GString *header;
	size_t num_channels;
	char *samplerate_s;

	ctx = o->priv;
	if (ctx->samplerate == 0) {
		if (sr_config_get(o->sdi->driver, o->sdi, NULL, SR_CONF_SAMPLERATE,
				&gvar) == SR_OK) {
			ctx->samplerate = g_variant_get_uint64(gvar);
			g_variant_unref(gvar);
		}
	}

	header = g_string_sized_new(512);
	g_string_printf(header, "%s %s\n", PACKAGE_NAME, sr_package_version_string_get());
	num_channels = g_slist_length(o->sdi->channels);
	g_string_append_printf(header, "Acquisition with %zu/%zu channels",
			ctx->num_enabled_channels, num_channels);
	if (ctx->samplerate != 0) {
		samplerate_s = sr_samplerate_string(ctx->samplerate);
		g_string_append_printf(header, " at %s", samplerate_s);
		g_free(samplerate_s);
	}
	g_string_append_printf(header, "\n");

	return header;
}

static void maybe_add_trigger(struct context *ctx, GString *out)
{
	int offset;

	if (ctx->trigger < 0)
		return;
	offset = ctx->trigger;
	ctx->trigger = -1;

	/*
	 * Sample data lines have one character per bit and
	 * no separator between bytes. Align trigger marker
	 * to this layout.
	 */
	g_string_append_printf(out, "%*s:%*s %d\n",
		(int)ctx->max_namelen, "T",
		offset + 1, "^", offset);
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_config *src;
	GSList *l;
	struct context *ctx;
	size_t idx, i, j;
	size_t num_samples;
	const uint8_t *curr_sample;
	size_t bytepos;
	uint8_t bitmask, curbit, prevbit;
	char c;
	size_t charidx;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	if (!(ctx = o->priv))
		return SR_ERR_ARG;

	switch (packet->type) {
	case SR_DF_META:
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			if (src->key != SR_CONF_SAMPLERATE)
				continue;
			ctx->samplerate = g_variant_get_uint64(src->data);
		}
		break;
	case SR_DF_TRIGGER:
		ctx->trigger = ctx->spl_cnt;
		break;
	case SR_DF_LOGIC:
		if (!ctx->header_done) {
			*out = gen_header(o);
			ctx->header_done = TRUE;
		} else {
			*out = g_string_sized_new(512);
		}

		logic = packet->payload;
		num_samples = logic->length / logic->unitsize;
		curr_sample = logic->data;
		while (num_samples--) {
			ctx->spl_cnt++;
			for (j = 0; j < ctx->num_enabled_channels; j++) {
				idx = ctx->channel_index[j];
				bytepos = idx / 8;
				bitmask = 1U << (idx % 8);
				curbit = curr_sample[bytepos] & bitmask;
				prevbit = ctx->prev_sample[bytepos] & bitmask;

				charidx = curbit ? 1 : 0;
				if (ctx->edges && ctx->spl_cnt > 1) {
					if (curbit != prevbit)
						charidx += 2;
				}
				c = ctx->charset[charidx];
				g_string_append_c(ctx->lines[j], c);

				if (ctx->spl_cnt == ctx->spl) {
					/* Flush line buffers. */
					g_string_append_len(*out, ctx->lines[j]->str, ctx->lines[j]->len);
					g_string_append_c(*out, '\n');
					if (j + 1 == ctx->num_enabled_channels)
						maybe_add_trigger(ctx, *out);
					g_string_printf(ctx->lines[j], "%s:", ctx->aligned_names[j]);
				}
			}
			if (ctx->spl_cnt == ctx->spl)
				/* Line buffers were already flushed. */
				ctx->spl_cnt = 0;
			memcpy(ctx->prev_sample, curr_sample, logic->unitsize);
			curr_sample += logic->unitsize;
		}
		break;
	case SR_DF_END:
		if (ctx->spl_cnt) {
			/* Line buffers need flushing. */
			*out = g_string_sized_new(512);
			for (i = 0; i < ctx->num_enabled_channels; i++) {
				g_string_append_len(*out, ctx->lines[i]->str, ctx->lines[i]->len);
				g_string_append_c(*out, '\n');
			}
			maybe_add_trigger(ctx, *out);
		}
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;
	size_t i;

	if (!o)
		return SR_ERR_ARG;

	if (!(ctx = o->priv))
		return SR_OK;

	g_free(ctx->channel_index);
	g_free(ctx->prev_sample);
	for (i = 0; i < ctx->num_enabled_channels; i++) {
		g_free(ctx->aligned_names[i]);
		g_string_free(ctx->lines[i], TRUE);
	}
	g_free(ctx->aligned_names);
	g_free(ctx->lines);
	g_free((gpointer)ctx->charset);
	g_free(ctx);
	o->priv = NULL;

	return SR_OK;
}

static struct sr_option options[] = {
	{ "width", "Width", "Number of samples per line", NULL, NULL },
	{ "charset", "Charset", "Characters for 0/1 bits (and fall/rise edges)", NULL, NULL },
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_new_uint32(DEFAULT_SAMPLES_PER_LINE);
		g_variant_ref_sink(options[0].def);
		options[1].def = g_variant_new_string(DEFAULT_ASCII_CHARS);
		g_variant_ref_sink(options[1].def);
	}

	return options;
}

SR_PRIV struct sr_output_module output_ascii = {
	.id = "ascii",
	.name = "ASCII",
	.desc = "ASCII art logic data",
	.exts = (const char*[]){"txt", NULL},
	.flags = 0,
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
