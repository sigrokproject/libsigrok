/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

/*
 * This implements version 1.3 of the output format for the OpenBench Logic
 * Sniffer "Alternative" Java client. Details:
 * https://github.com/jawi/ols/wiki/OLS-data-file-format
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/ols"

struct context {
	uint64_t samplerate;
	uint64_t num_samples;
};

static int init(struct sr_output *o)
{
	struct context *ctx;

	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("%s: ctx malloc failed", __func__);
		return SR_ERR_MALLOC;
	}
	o->internal = ctx;

	ctx->samplerate = 0;
	ctx->num_samples = 0;

	return SR_OK;
}

static GString *gen_header(const struct sr_dev_inst *sdi, struct context *ctx)
{
	struct sr_probe *probe;
	GSList *l;
	GString *s;
	GVariant *gvar;
	int num_enabled_probes;

	if (!ctx->samplerate && sr_config_get(sdi->driver, sdi, NULL,
			SR_CONF_SAMPLERATE, &gvar) == SR_OK) {
		ctx->samplerate = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	}

	num_enabled_probes = 0;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->enabled)
			num_enabled_probes++;
	}

	s = g_string_sized_new(512);
	g_string_append_printf(s, ";Rate: %"PRIu64"\n", ctx->samplerate);
	g_string_append_printf(s, ";Channels: %d\n", num_enabled_probes);
	g_string_append_printf(s, ";EnabledChannels: -1\n");
	g_string_append_printf(s, ";Compressed: true\n");
	g_string_append_printf(s, ";CursorEnabled: false\n");

	return s;
}

static int receive(struct sr_output *o, const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, GString **out)
{
	struct context *ctx;
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_config *src;
	GSList *l;
	unsigned int i, j;
	uint8_t c;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	ctx = o->internal;

	switch (packet->type) {
	case SR_DF_META:
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			if (src->key == SR_CONF_SAMPLERATE)
				ctx->samplerate = g_variant_get_uint64(src->data);
		}
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		if (ctx->num_samples == 0) {
			/* First logic packet in the feed. */
			*out = gen_header(sdi, ctx);
		} else
			*out = g_string_sized_new(512);
		for (i = 0; i <= logic->length - logic->unitsize; i += logic->unitsize) {
			for (j = 0; j < logic->unitsize; j++) {
				/* The OLS format wants the samples presented MSB first. */
				c = *((uint8_t *)logic->data + i + logic->unitsize - 1 - j);
				g_string_append_printf(*out, "%02x", c);
			}
			g_string_append_printf(*out, "@%"PRIu64"\n", ctx->num_samples++);
		}
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	ctx = o->internal;
	g_free(ctx);
	o->internal = NULL;

	return SR_OK;
}

SR_PRIV struct sr_output_format output_ols = {
	.id = "ols",
	.description = "OpenBench Logic Sniffer",
	.df_type = SR_DF_LOGIC,
	.init = init,
	.receive = receive,
	.cleanup = cleanup
};
