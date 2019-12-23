/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Marc Jacobi <obiwanjacobi@hotmail.com>
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
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/wavedrom"

struct context {
	uint32_t channel_count;
	struct sr_channel **channels;
	GString **channel_outputs; /* output strings */
};

/* Converts accumulated output data to a JSON string. */
static GString *wavedrom_render(const struct context *ctx)
{
	GString *output;
	size_t ch, i;
	char last_char, curr_char;

	output = g_string_new("{ \"signal\": [");
	for (ch = 0; ch < ctx->channel_count; ch++) {
		if (!ctx->channel_outputs[ch])
			continue;

		/* Channel strip. */
		g_string_append_printf(output,
			"{ \"name\": \"%s\", \"wave\": \"", ctx->channels[ch]->name);

		last_char = 0;
		for (i = 0; i < ctx->channel_outputs[ch]->len; i++) {
			curr_char = ctx->channel_outputs[ch]->str[i];
			/* Data point. */
			if (curr_char == last_char) {
				g_string_append_c(output, '.');
			} else {
				g_string_append_c(output, curr_char);
				last_char = curr_char;
			}
		}
		if (ch < ctx->channel_count - 1) {
			g_string_append(output, "\" },");
		} else {
			/* Last channel, no comma. */
			g_string_append(output, "\" }");
		}
	}
	g_string_append(output, "], \"config\": { \"skin\": \"narrow\" }}");

	return output;
}

static void process_logic(const struct context *ctx,
	const struct sr_datafeed_logic *logic)
{
	size_t sample_count, ch, i;
	uint8_t *sample, bit;
	GString *accu;

	if (!ctx->channel_count)
		return;

	/*
	 * Extract the logic bits for each channel and store them
	 * as wavedrom letters (1/0) in each channel's text string.
	 * This transforms the input which consists of sample sets
	 * that span multiple channels into output stripes per logic
	 * channel which consist of bits for that individual channel.
	 *
	 * TODO Reduce memory consumption during accumulation of
	 * output data.
	 *
	 * Ideally we'd accumulate binary chunks, and defer conversion
	 * to the text format. Analog data already won't get here, only
	 * logic data does. When the per-channel transformation also
	 * gets deferred until later, then the only overhead would be
	 * for disabled logic channels. Which may be acceptable or even
	 * negligable.
	 *
	 * An optional addition to the above mentioned accumulation of
	 * binary data is RLE compression. Mark both the position in the
	 * accumulated data as well as a repetition counter, instead of
	 * repeatedly storing the same sample set. The efficiency of
	 * this approach of course depends on the change rate of input
	 * data. But the approach perfectly matches the WaveDrom syntax
	 * for repeated bit patterns, and thus is easily handled in the
	 * text rendering stage of the output module.
	 */
	sample_count = logic->length / logic->unitsize;
	for (i = 0; i < sample_count; i++) {
		sample = logic->data + i * logic->unitsize;
		for (ch = 0; ch < ctx->channel_count; ch++) {
			accu = ctx->channel_outputs[ch];
			if (!accu)
				continue;
			bit = sample[ch / 8] & (1 << (ch % 8));
			g_string_append_c(accu, bit ? '1' : '0');
		}
	}
}

static int receive(const struct sr_output *o,
	const struct sr_datafeed_packet *packet, GString **out)
{
	struct context *ctx;

	*out = NULL;

	if (!o || !o->sdi || !o->priv)
		return SR_ERR_ARG;

	ctx = o->priv;

	switch (packet->type) {
	case SR_DF_LOGIC:
		process_logic(ctx, packet->payload);
		break;
	case SR_DF_END:
		*out = wavedrom_render(ctx);
		break;
	}

	return SR_OK;
}

static int init(struct sr_output *o, GHashTable *options)
{
	struct context *ctx;
	struct sr_channel *channel;
	GSList *l;
	size_t i;

	(void)options;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	o->priv = ctx = g_malloc0(sizeof(*ctx));

	ctx->channel_count = g_slist_length(o->sdi->channels);
	ctx->channels = g_malloc0(
		sizeof(ctx->channels[0]) * ctx->channel_count);
	ctx->channel_outputs = g_malloc0(
		sizeof(ctx->channel_outputs[0]) * ctx->channel_count);

	for (i = 0, l = o->sdi->channels; l; l = l->next, i++) {
		channel = l->data;
		if (channel->enabled && channel->type == SR_CHANNEL_LOGIC) {
			ctx->channels[i] = channel;
			ctx->channel_outputs[i] = g_string_new(NULL);
		}
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;
	GString *s;

	if (!o)
		return SR_ERR_ARG;

	ctx = o->priv;
	o->priv = NULL;

	if (ctx) {
		while (--ctx->channel_count) {
			s = ctx->channel_outputs[ctx->channel_count];
			if (s)
				g_string_free(s, TRUE);
		}
		g_free(ctx->channel_outputs);
		g_free(ctx->channels);
		g_free(ctx);
	}

	return SR_OK;
}

SR_PRIV struct sr_output_module output_wavedrom = {
	.id = "wavedrom",
	.name = "WaveDrom",
	.desc = "WaveDrom.com file format",
	.exts = (const char *[]){"wavedrom", "json", NULL},
	.flags = 0,
	.options = NULL,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
