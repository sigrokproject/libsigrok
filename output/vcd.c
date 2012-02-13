/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "config.h"
#include "sigrok.h"
#include "sigrok-internal.h"

struct context {
	int num_enabled_probes;
	int unitsize;
	char *probelist[65];
	int *prevbits;
	GString *header;
	uint64_t prevsample;
	int period;
	uint64_t samplerate;
};

static const char *vcd_header_comment = "\
$comment\n  Acquisition with %d/%d probes at %s\n$end\n";

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	int num_probes, i;
	char *samplerate_s, *frequency_s, *timestamp;
	time_t t;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("vcd out: %s: ctx malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	o->internal = ctx;
	ctx->num_enabled_probes = 0;

	for (l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if (!probe->enabled)
			continue;
		ctx->probelist[ctx->num_enabled_probes++] = probe->name;
	}
	if (ctx->num_enabled_probes > 94) {
		sr_err("VCD only supports 94 probes.");
		return SR_ERR;
	}

	ctx->probelist[ctx->num_enabled_probes] = 0;
	ctx->unitsize = (ctx->num_enabled_probes + 7) / 8;
	ctx->header = g_string_sized_new(512);
	num_probes = g_slist_length(o->device->probes);

	/* timestamp */
	t = time(NULL);
	timestamp = g_strdup(ctime(&t));
	timestamp[strlen(timestamp)-1] = 0;
	g_string_printf(ctx->header, "$date %s $end\n", timestamp);
	g_free(timestamp);

	/* generator */
	g_string_append_printf(ctx->header, "$version %s %s $end\n",
			PACKAGE, PACKAGE_VERSION);

	if (o->device->plugin && sr_dev_has_hwcap(o->device, SR_HWCAP_SAMPLERATE)) {
		ctx->samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, SR_DI_CUR_SAMPLERATE));
		if (!((samplerate_s = sr_samplerate_string(ctx->samplerate)))) {
			g_string_free(ctx->header, TRUE);
			g_free(ctx);
			return SR_ERR;
		}
		g_string_append_printf(ctx->header, vcd_header_comment,
				 ctx->num_enabled_probes, num_probes, samplerate_s);
		g_free(samplerate_s);
	}

	/* timescale */
	/* VCD can only handle 1/10/100 (s - fs), so scale up first */
	if (ctx->samplerate > SR_MHZ(1))
		ctx->period = SR_GHZ(1);
	else if (ctx->samplerate > SR_KHZ(1))
		ctx->period = SR_MHZ(1);
	else
		ctx->period = SR_KHZ(1);
	if (!(frequency_s = sr_period_string(ctx->period))) {
		g_string_free(ctx->header, TRUE);
		g_free(ctx);
		return SR_ERR;
	}
	g_string_append_printf(ctx->header, "$timescale %s $end\n", frequency_s);
	g_free(frequency_s);

	/* scope */
	g_string_append_printf(ctx->header, "$scope module %s $end\n", PACKAGE);

	/* Wires / channels */
	for (i = 0; i < ctx->num_enabled_probes; i++) {
		g_string_append_printf(ctx->header, "$var wire 1 %c %s $end\n",
				(char)('!' + i), ctx->probelist[i]);
	}

	g_string_append(ctx->header, "$upscope $end\n"
			"$enddefinitions $end\n$dumpvars\n");

	if (!(ctx->prevbits = g_try_malloc0(sizeof(int) * num_probes))) {
		g_string_free(ctx->header, TRUE);
		g_free(ctx);
		sr_err("vcd out: %s: ctx->prevbits malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	return SR_OK;
}

static int event(struct sr_output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;
	char *outbuf;

	ctx = o->internal;
	switch (event_type) {
	case SR_DF_END:
		outbuf = g_strdup("$dumpoff\n$end\n");
		*data_out = outbuf;
		*length_out = strlen(outbuf);
		g_free(o->internal);
		o->internal = NULL;
		break;
	default:
		*data_out = NULL;
		*length_out = 0;
		break;
	}

	return SR_OK;
}

static int data(struct sr_output *o, const char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int i;
	int p, curbit, prevbit;
	uint64_t sample;
	static uint64_t samplecount = 0;
	GString *out;
	int first_sample = 0;

	ctx = o->internal;
	out = g_string_sized_new(512);

	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		g_string_append(out, ctx->header->str);
		g_string_free(ctx->header, TRUE);
		ctx->header = NULL;
		first_sample = 1;
	}

	for (i = 0; i <= length_in - ctx->unitsize; i += ctx->unitsize) {
		samplecount++;

		memcpy(&sample, data_in + i, ctx->unitsize);

		if (first_sample) {
			/* First packet. We neg to make sure sample is stored. */
			ctx->prevsample = ~sample;
			first_sample = 0;
		}

		for (p = 0; p < ctx->num_enabled_probes; p++) {
			curbit = (sample & ((uint64_t) (1 << p))) >> p;
			prevbit = (ctx->prevsample & ((uint64_t) (1 << p))) >> p;

			/* VCD only contains deltas/changes of signals. */
			if (prevbit == curbit)
				continue;

			/* Output which signal changed to which value. */
			g_string_append_printf(out, "#%" PRIu64 "\n%i%c\n",
					(uint64_t)(((float)samplecount / ctx->samplerate)
					* ctx->period), curbit, (char)('!' + p));
		}

		ctx->prevsample = sample;
	}

	*data_out = out->str;
	*length_out = out->len;
	g_string_free(out, FALSE);

	return SR_OK;
}

struct sr_output_format output_vcd = {
	.id = "vcd",
	.description = "Value Change Dump (VCD)",
	.df_type = SR_DF_LOGIC,
	.init = init,
	.data = data,
	.event = event,
};
