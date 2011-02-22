/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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
#include <sigrok.h>
#include "config.h"

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
	uint64_t samplerate;
	int num_enabled_probes, i;

	if (!(ctx = g_malloc(sizeof(struct context))))
		return SR_ERR_MALLOC;
	o->internal = ctx;

	ctx->num_samples = 0;
	num_enabled_probes = 0;
	for (l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if (probe->enabled)
			num_enabled_probes++;
	}
	ctx->unitsize = (num_enabled_probes + 7) / 8;

	if (o->device->plugin && sr_device_has_hwcap(o->device, SR_HWCAP_SAMPLERATE))
		samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, SR_DI_CUR_SAMPLERATE));
	else
		samplerate = 0;

	ctx->header = g_string_sized_new(512);
	g_string_append_printf(ctx->header, ";Rate: %"PRIu64"\n", samplerate);
	g_string_append_printf(ctx->header, ";Channels: %d\n", num_enabled_probes);
	g_string_append_printf(ctx->header, ";EnabledChannels: -1\n");
	g_string_append_printf(ctx->header, ";Compressed: true\n");
	g_string_append_printf(ctx->header, ";CursorEnabled: false\n");
	for (i = 0; i < 10; i++)
		g_string_append_printf(ctx->header, ";Cursor%d: 0\n", i);

	return SR_OK;
}

static int event(struct sr_output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;

	ctx = o->internal;

	if (ctx && event_type == SR_DF_END) {
		if (ctx->header)
			g_string_free(ctx->header, TRUE);
		free(o->internal);
		o->internal = NULL;
	}

	*data_out = NULL;
	*length_out = 0;

	return SR_OK;
}

static int data(struct sr_output *o, const char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
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
	*data_out = out->str;
	*length_out = out->len;
	g_string_free(out, FALSE);

	return SR_OK;
}

struct sr_output_format output_ols = {
	"ols",
	"OpenBench Logic Sniffer",
	SR_DF_LOGIC,
	init,
	data,
	event,
};
