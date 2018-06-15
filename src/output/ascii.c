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

#define margin_chan "%8s:"
#define margin_trig "%9s"

struct context {
	unsigned int num_enabled_channels;
	int spl;
	int bit_cnt;
	int spl_cnt;
	int trigger;
	uint64_t samplerate;
	int *channel_index;
	char **channel_names;
	char **line_values;
	uint8_t *prev_sample;
	gboolean header_done;
	GString **lines;
	GString *header;
	const char *charset;
	gboolean edges;
};

static int init(struct sr_output *o, GHashTable *options)
{
	struct context *ctx;
	struct sr_channel *ch;
	GSList *l;
	unsigned int i, j;

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
	ctx->channel_index = g_malloc(sizeof(int) * ctx->num_enabled_channels);
	ctx->channel_names = g_malloc(sizeof(char *) * ctx->num_enabled_channels);
	ctx->lines = g_malloc(sizeof(GString *) * ctx->num_enabled_channels);
	ctx->prev_sample = g_malloc(g_slist_length(o->sdi->channels));

	j = 0;
	for (i = 0, l = o->sdi->channels; l; l = l->next, i++) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		ctx->channel_index[j] = ch->index;
		ctx->channel_names[j] = ch->name;
		ctx->lines[j] = g_string_sized_new(80);
		g_string_printf(ctx->lines[j], margin_chan, ch->name);
		j++;
	}

	return SR_OK;
}

static GString *gen_header(const struct sr_output *o)
{
	struct context *ctx;
	GVariant *gvar;
	GString *header;
	int num_channels;
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
	g_string_printf(header, "%s %s\n", PACKAGE_NAME, SR_PACKAGE_VERSION_STRING);
	num_channels = g_slist_length(o->sdi->channels);
	g_string_append_printf(header, "Acquisition with %d/%d channels",
			ctx->num_enabled_channels, num_channels);
	if (ctx->samplerate != 0) {
		samplerate_s = sr_samplerate_string(ctx->samplerate);
		g_string_append_printf(header, " at %s", samplerate_s);
		g_free(samplerate_s);
	}
	g_string_append_printf(header, "\n");

	return header;
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_config *src;
	GSList *l;
	struct context *ctx;
	int idx, offset, curbit, prevbit;
	uint64_t i, j;
	gchar *p, c;
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
		} else
			*out = g_string_sized_new(512);

		logic = packet->payload;
		for (i = 0; i <= logic->length - logic->unitsize; i += logic->unitsize) {
			ctx->spl_cnt++;
			for (j = 0; j < ctx->num_enabled_channels; j++) {
				idx = ctx->channel_index[j];
				p = logic->data + i + idx / 8;
				curbit = *p & (1 << (idx % 8));
				prevbit = (ctx->prev_sample[idx / 8] & ((uint8_t) 1 << (idx % 8)));

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
					if (j == ctx->num_enabled_channels - 1 && ctx->trigger > -1) {
						offset = ctx->trigger;
						g_string_append_printf(*out, margin_trig, "T:");
						g_string_append_printf(*out, " %*s^ %d\n", offset - 1, "", ctx->trigger + 1);
						ctx->trigger = -1;
					}
					g_string_printf(ctx->lines[j], margin_chan, ctx->channel_names[j]);
				}
			}
			if (ctx->spl_cnt == ctx->spl)
				/* Line buffers were already flushed. */
				ctx->spl_cnt = 0;
			memcpy(ctx->prev_sample, logic->data + i, logic->unitsize);
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
		}
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;
	unsigned int i;

	if (!o)
		return SR_ERR_ARG;

	if (!(ctx = o->priv))
		return SR_OK;

	g_free(ctx->channel_index);
	g_free(ctx->prev_sample);
	g_free(ctx->channel_names);
	for (i = 0; i < ctx->num_enabled_channels; i++)
		g_string_free(ctx->lines[i], TRUE);
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
