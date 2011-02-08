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
 * Output format for the OpenBench Logic Sniffer "Alternative" Java client.
 * Details: https://github.com/jawi/ols/wiki/OLS-data-file-format
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <sigrok.h>
#include "config.h"

struct context {
	GString *header;
	GString *body;
	uint64_t num_samples;
	gboolean got_trigger;
	uint64_t trigger_pos;
	unsigned int unitsize;
};


static void make_header(struct sr_output *o)
{
	struct context *ctx;
	uint64_t samplerate;
	int i;

	ctx = o->internal;

	if (o->device->plugin && sr_device_has_hwcap(o->device, SR_HWCAP_SAMPLERATE))
		samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, SR_DI_CUR_SAMPLERATE));
	else
		samplerate = 0;

	g_string_append_printf(ctx->header, ";Size: %"PRIu64"\n", ctx->num_samples);
	g_string_append_printf(ctx->header, ";Rate: %"PRIu64"\n", samplerate);
	g_string_append_printf(ctx->header, ";Channels: %d\n", ctx->unitsize*8);
	g_string_append_printf(ctx->header, ";EnabledChannels: -1\n");
	if (ctx->got_trigger)
		g_string_append_printf(ctx->header, ";TriggerPosition: %"PRIu64"\n", ctx->trigger_pos);
	g_string_append_printf(ctx->header, ";Compressed: true\n");
	g_string_append_printf(ctx->header, ";AbsoluteLength: %"PRIu64"\n", ctx->num_samples);
	g_string_append_printf(ctx->header, ";CursorEnabled: false\n");
	for (i = 0; i < 10; i++)
		g_string_append_printf(ctx->header, ";Cursor%d: 0\n", i);

}

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	int num_enabled_probes;

	if (!(ctx = g_malloc(sizeof(struct context))))
		return SR_ERR_MALLOC;

	o->internal = ctx;
	ctx->header = g_string_sized_new(512);
	ctx->body = g_string_sized_new(512);
	ctx->num_samples = 0;
	ctx->got_trigger = FALSE;
	ctx->trigger_pos = 0;
	num_enabled_probes = 0;
	for (l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if (probe->enabled)
			num_enabled_probes++;
	}
	ctx->unitsize = (num_enabled_probes + 7) / 8;

	return SR_OK;
}

static int event(struct sr_output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;

	ctx = o->internal;
	*data_out = NULL;
	*length_out = 0;

	switch (event_type) {
	case SR_DF_TRIGGER:
		ctx->got_trigger = TRUE;
		ctx->trigger_pos = ctx->num_samples;
		break;
	case SR_DF_END:
		make_header(o);
		*length_out = ctx->header->len + ctx->body->len;
		*data_out = g_malloc(*length_out);
		memcpy(*data_out, ctx->header->str, ctx->header->len);
		memcpy(*data_out + ctx->header->len, ctx->body->str, ctx->body->len);
		g_string_free(ctx->header, TRUE);
		g_string_free(ctx->body, TRUE);
		free(o->internal);
		o->internal = NULL;
		break;
	}

	return SR_OK;
}

static int data(struct sr_output *o, char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	uint64_t sample;
	unsigned int i;

	ctx = o->internal;
	for (i = 0; i <= length_in - ctx->unitsize; i += ctx->unitsize) {
		sample = 0;
		memcpy(&sample, data_in + i, ctx->unitsize);
		g_string_append_printf(ctx->body, "%08x@%"PRIu64"\n",
				(uint32_t) sample, ctx->num_samples++);
	}

	*data_out = NULL;
	*length_out = 0;

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
