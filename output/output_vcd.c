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
$version\n  %s\n$end\n\
$comment\n  Acquisition with %d/%d probes at %s\n$end\n\
$timescale\n  %i %s\n$end\n\
$scope module %s $end\n\
%s\
$upscope $end\n\
$enddefinitions $end\n\
$dumpvars\n";

static int init(struct output *o)
{
/* Maximum header length */
#define MAX_HEADER_LEN 2048

	struct context *ctx;
	struct probe *probe;
	GSList *l;
	uint64_t samplerate;
	int i, b, num_probes;
	char *c;
	char sbuf[10], wbuf[1000];

	ctx = malloc(sizeof(struct context));
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

	ctx->header = calloc(1, MAX_HEADER_LEN + 1);
	num_probes = g_slist_length(o->device->probes);
	samplerate = *((uint64_t *) o->device->plugin->get_device_info(
			o->device->plugin_index, DI_CUR_SAMPLERATE));

	/* Samplerate string */
	if (samplerate >= GHZ(1))
		snprintf(sbuf, 10, "%"PRIu64" GHz", samplerate / 1000000000);
	else if (samplerate >= MHZ(1))
		snprintf(sbuf, 10, "%"PRIu64" MHz", samplerate / 1000000);
	else if (samplerate >= KHZ(1))
		snprintf(sbuf, 10, "%"PRIu64" KHz", samplerate / 1000);
	else
		snprintf(sbuf, 10, "%"PRIu64" Hz", samplerate);

	/* Wires / channels */
	wbuf[0] = '\0';
	for (i = 0; i < num_probes; i++) {
		c = (char *)&wbuf + strlen((char *)&wbuf);
		sprintf(c, "$var wire 1 %c channel%i $end\n",
			 (char)('!' + i), i);
	}

	/* TODO: date: File or signals? Make y/n configurable. */
	b = snprintf(ctx->header, MAX_HEADER_LEN, vcd_header, "TODO: Date",
		     PACKAGE_STRING, ctx->num_enabled_probes, num_probes,
		     (char *)&sbuf, 1, "ns", PACKAGE, (char *)&wbuf);

	ctx->prevbits = calloc(sizeof(int), num_probes);

	return 0;
}

static int event(struct output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;
	char *outbuf;
	int outlen;

	ctx = o->internal;
	switch(event_type) {
	case DF_TRIGGER:
		break;
	case DF_END:
		outlen = strlen("$dumpoff\n$end\n");
		outbuf = malloc(outlen + 1);
		snprintf(outbuf, outlen, "$dumpoff\n$end\n");
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
	int offset, outsize, p, curbit, prevbit;
	uint64_t sample, prevsample;
	char *outbuf, *c;

	ctx = o->internal;
	outsize = strlen(ctx->header);
	outbuf = calloc(1, outsize + 1 + 10000); // FIXME: Use realloc().
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		free(ctx->header);
		ctx->header = NULL;
	} else {
		outbuf[0] = 0;
	}

	/* TODO: Are disabled probes handled correctly? */

	for (offset = 0; offset <= length_in - ctx->unitsize;
						offset += ctx->unitsize) {
		memcpy(&sample, data_in + offset, ctx->unitsize);
		for (p = 0; p < ctx->num_enabled_probes; p++) {
			curbit = (sample & ((uint64_t) (1 << p))) != 0;
			if (offset == 0) {
				prevbit = ~curbit;
			} else {
				memcpy(&prevsample, data_in + offset - 1, ctx->unitsize);
				prevbit = (prevsample & ((uint64_t) (1 << p))) != 0;
			}

			if (prevbit != curbit) {
				/* FIXME: Only once per sample? */
				c = outbuf + strlen(outbuf);
				sprintf(c, "#%i\n", offset * 1 /* TODO */);

				c = outbuf + strlen(outbuf);
				sprintf(c, "%i%c\n", curbit, (char)('!' + p /* FIXME? */));
			}
		}

		/* TODO: Do a realloc() here if strlen(outbuf) is almost "full"... */
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
