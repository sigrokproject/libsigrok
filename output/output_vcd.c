/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
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
#include <sigrok.h>
#include "config.h"

struct context {
	int num_enabled_probes;
	int unitsize;
	char *probelist[65];
	int *prevbits;
	char *header;
};

const char *vcd_header = "\
$date\n  %s\n$end\n\
$version\n  %s\n$end\n%s\
$timescale\n  %i %s\n$end\n\
$scope module %s $end\n\
%s\
$upscope $end\n\
$enddefinitions $end\n\
$dumpvars\n";

const char *vcd_header_comment = "\
$comment\n  Acquisition with %d/%d probes at %s\n$end\n";

static int init(struct output *o)
{
/* Maximum header length */
#define MAX_HEADER_LEN 2048

	struct context *ctx;
	struct probe *probe;
	GSList *l;
	uint64_t samplerate;
	int i, b, num_probes;
	char *c, *samplerate_s;
	char wbuf[1000], comment[128];

	if (!(ctx = calloc(1, sizeof(struct context))))
		return SIGROK_ERR_MALLOC;
	o->internal = ctx;
	ctx->num_enabled_probes = 0;
	for (l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if (probe->enabled)
			ctx->probelist[ctx->num_enabled_probes++] = probe->name;
	}

	ctx->probelist[ctx->num_enabled_probes] = 0;
	ctx->unitsize = (ctx->num_enabled_probes + 7) / 8;

	/* TODO: Allow for configuration via o->param. */

	if (!(ctx->header = calloc(1, MAX_HEADER_LEN + 1))) {
		free(ctx);
		return SIGROK_ERR_MALLOC;
	}
	num_probes = g_slist_length(o->device->probes);

	comment[0] = '\0';
	if (o->device->plugin) {
		/* TODO: Handle num_probes == 0, too many probes, etc. */
		/* TODO: Error handling. */
		samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, DI_CUR_SAMPLERATE));
		if (!((samplerate_s = sigrok_samplerate_string(samplerate)))) {
			free(ctx->header);
			free(ctx);
			return SIGROK_ERR;
		}
		/* TODO: Handle sprintf() errors. */
		snprintf(comment, 127, vcd_header_comment,
			 ctx->num_enabled_probes, num_probes, samplerate_s);
		free(samplerate_s);
	}

	/* Wires / channels */
	wbuf[0] = '\0';
	for (i = 0; i < ctx->num_enabled_probes; i++) {
		c = (char *)&wbuf + strlen((char *)&wbuf);
		/* TODO: Needs fixing for very large number of probes. */
		/* TODO: Handle sprintf() errors. */
		sprintf(c, "$var wire 1 %c channel%s $end\n",
			(char)('!' + i), ctx->probelist[i]);
	}

	/* TODO: Date: File or signals? Make y/n configurable. */
	b = snprintf(ctx->header, MAX_HEADER_LEN, vcd_header, "TODO: Date",
		     PACKAGE_STRING, comment, 1, "ns", PACKAGE, (char *)&wbuf);
	/* TODO: Handle snprintf() errors. */

	if (!(ctx->prevbits = calloc(sizeof(int), num_probes))) {
		free(ctx->header);
		free(ctx);
		return SIGROK_ERR_MALLOC;
	}

	return SIGROK_OK;
}

static int event(struct output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;
	char *outbuf;
	int outlen;

	ctx = o->internal;
	switch (event_type) {
	case DF_TRIGGER:
		/* TODO */
		break;
	case DF_END:
		outlen = strlen("$dumpoff\n$end\n");
		if (!(outbuf = malloc(outlen + 1)))
			return SIGROK_ERR_MALLOC;
		/* TODO: Bug? Drop the + 1? */
		/* TODO: Handle snprintf() errors. */
		snprintf(outbuf, outlen + 1, "$dumpoff\n$end\n");
		*data_out = outbuf;
		*length_out = outlen;
		free(o->internal);
		o->internal = NULL;
		break;
	}

	return SIGROK_OK;
}

static int data(struct output *o, char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int i, outsize;
	int p, curbit, prevbit;
	uint64_t sample, prevsample;
	char *outbuf, *c;

	ctx = o->internal;
	outsize = 0;
	if (ctx->header)
		outsize = strlen(ctx->header);

	/* FIXME: Use realloc(). */
	if (!(outbuf = calloc(1, outsize + 1 + 10000)))
		return SIGROK_ERR_MALLOC; /* TODO: free()? What to free? */

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		free(ctx->header);
		ctx->header = NULL;
	}

	/* TODO: Are disabled probes handled correctly? */

	for (i = 0; i <= length_in - ctx->unitsize; i += ctx->unitsize) {
		memcpy(&sample, data_in + i, ctx->unitsize);
		for (p = 0; p < ctx->num_enabled_probes; p++) {
			curbit = (sample & ((uint64_t) (1 << p))) != 0;
			if (i == 0) {
				prevbit = ~curbit;
			} else {
				memcpy(&prevsample, data_in + i - 1,
				       ctx->unitsize);
				prevbit =
				    (prevsample & ((uint64_t) (1 << p))) != 0;
			}

			/* VCD only contains deltas/changes. */
			if (prevbit == curbit)
				continue;

			/* FIXME: Only once per sample? */
			/* TODO: Is 'i' correct here? */
			c = outbuf + strlen(outbuf);
			sprintf(c, "#%i\n%i%c\n", i, curbit, (char)('!' + p));
		}

		/* TODO: Use realloc() if strlen(outbuf) is almost "full"... */
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SIGROK_OK;
}

struct output_format output_vcd = {
	"vcd",
	"Value Change Dump (VCD)",
	init,
	data,
	event,
};
