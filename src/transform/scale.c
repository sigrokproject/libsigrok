/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "transform/scale"

struct context {
	struct sr_rational factor;
};

static int init(struct sr_transform *t, GHashTable *options)
{
	struct context *ctx;

	if (!t || !t->sdi || !options)
		return SR_ERR_ARG;

	t->priv = ctx = g_malloc0(sizeof(struct context));

	g_variant_get(g_hash_table_lookup(options, "factor"), "(xt)",
			&ctx->factor.p, &ctx->factor.q);

	return SR_OK;
}

static int receive(const struct sr_transform *t,
		struct sr_datafeed_packet *packet_in,
		struct sr_datafeed_packet **packet_out)
{
	struct context *ctx;
	const struct sr_datafeed_analog *analog;
	const struct sr_datafeed_analog2 *analog2;
	struct sr_channel *ch;
	GSList *l;
	float *fdata;
	float factor;
	int i, num_channels, c;

	if (!t || !t->sdi || !packet_in || !packet_out)
		return SR_ERR_ARG;
	ctx = t->priv;

	switch (packet_in->type) {
	case SR_DF_ANALOG:
		analog = packet_in->payload;
		fdata = (float *)analog->data;
		num_channels = g_slist_length(analog->channels);
		factor = (float) ctx->factor.p / ctx->factor.q;
		for (i = 0; i < analog->num_samples; i++) {
			/* For now scale all values in all channels. */
			for (l = analog->channels, c = 0; l; l = l->next, c++) {
				ch = l->data;
				(void)ch;
				fdata[i * num_channels + c] *= factor;
			}
		}
		break;
	case SR_DF_ANALOG2:
		analog2 = packet_in->payload;
		analog2->encoding->scale.p *= ctx->factor.p;
		analog2->encoding->scale.q *= ctx->factor.q;
		break;
	default:
		sr_spew("Unsupported packet type %d, ignoring.", packet_in->type);
		break;
	}

	/* Return the in-place-modified packet. */
	*packet_out = packet_in;

	return SR_OK;
}

static int cleanup(struct sr_transform *t)
{
	struct context *ctx;

	if (!t || !t->sdi)
		return SR_ERR_ARG;
	ctx = t->priv;

	g_free(ctx);
	t->priv = NULL;

	return SR_OK;
}

static struct sr_option options[] = {
	{ "factor", "Factor", "Factor by which to scale the analog values", NULL, NULL },
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	int64_t p = 1;
	uint64_t q = 1;

	/* Default to a scaling factor of 1.0. */
	if (!options[0].def)
		options[0].def = g_variant_ref_sink(g_variant_new(("(xt"), &p, &q));

	return options;
}

SR_PRIV struct sr_transform_module transform_scale = {
	.id = "scale",
	.name = "Scale",
	.desc = "Scale analog values by a specified factor",
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
