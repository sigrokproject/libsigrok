/*
 * This file is part of the sigrok project.
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

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "output/ols: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

struct context {
	GString *header;
	uint64_t num_samples;
	unsigned int unitsize;
};

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	uint64_t *samplerate, tmp;
	int num_enabled_probes;

	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("%s: ctx malloc failed", __func__);
		return SR_ERR_MALLOC;
	}
	o->internal = ctx;

	ctx->num_samples = 0;
	num_enabled_probes = 0;
	for (l = o->sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->enabled)
			num_enabled_probes++;
	}
	ctx->unitsize = (num_enabled_probes + 7) / 8;

	if (o->sdi->driver && sr_dev_has_option(o->sdi, SR_CONF_SAMPLERATE))
		o->sdi->driver->config_get(SR_CONF_SAMPLERATE,
				(const void **)&samplerate, o->sdi);
	else {
		tmp = 0;
		samplerate = &tmp;
	}

	ctx->header = g_string_sized_new(512);
	g_string_append_printf(ctx->header, ";Rate: %"PRIu64"\n", *samplerate);
	g_string_append_printf(ctx->header, ";Channels: %d\n", num_enabled_probes);
	g_string_append_printf(ctx->header, ";EnabledChannels: -1\n");
	g_string_append_printf(ctx->header, ";Compressed: true\n");
	g_string_append_printf(ctx->header, ";CursorEnabled: false\n");

	return SR_OK;
}

static int event(struct sr_output *o, int event_type, uint8_t **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;

	ctx = o->internal;

	if (ctx && event_type == SR_DF_END) {
		g_string_free(ctx->header, TRUE);
		g_free(o->internal);
		o->internal = NULL;
	}

	*data_out = NULL;
	*length_out = 0;

	return SR_OK;
}

static int data(struct sr_output *o, const uint8_t *data_in,
		uint64_t length_in, uint8_t **data_out, uint64_t *length_out)
{
	GString *out;
	struct context *ctx;
	uint64_t sample;
	unsigned int i;

	ctx = o->internal;
	if (ctx->header) {
		/* first data packet */
		out = ctx->header;
		ctx->header = NULL;
	} else
		out = g_string_sized_new(512);

	for (i = 0; i <= length_in - ctx->unitsize; i += ctx->unitsize) {
		sample = 0;
		memcpy(&sample, data_in + i, ctx->unitsize);
		g_string_append_printf(out, "%08x@%"PRIu64"\n",
				(uint32_t) sample, ctx->num_samples++);
	}
	*data_out = (uint8_t *)out->str;
	*length_out = out->len;
	g_string_free(out, FALSE);

	return SR_OK;
}

SR_PRIV struct sr_output_format output_ols = {
	.id = "ols",
	.description = "OpenBench Logic Sniffer",
	.df_type = SR_DF_LOGIC,
	.init = init,
	.data = data,
	.event = event,
};
