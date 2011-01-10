/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sigrok.h>
#include <config.h>


/* -10.25 */
#define VALUE_LEN 6

struct context {
	char *header;
	unsigned int num_enabled_probes;
	char *probelist[MAX_NUM_PROBES];
	int samples_per_line;
	unsigned int unitsize;
	int line_offset;
	int linebuf_len;
	char *linebuf;
	int spl_cnt;
	int mark_trigger;
};


static void flush_linebufs(struct context *ctx, char *outbuf)
{
	static int max_probename_len = 0;
	int len, i;

	if (ctx->linebuf[0] == 0)
		return;

	if (max_probename_len == 0) {
		/* First time through... */
		for (i = 0; ctx->probelist[i]; i++) {
			len = strlen(ctx->probelist[i]);
			if (len > max_probename_len)
				max_probename_len = len;
		}
	}

	for (i = 0; ctx->probelist[i]; i++) {
		sprintf(outbuf + strlen(outbuf), "%*s:%s\n", max_probename_len,
			ctx->probelist[i], ctx->linebuf + i * ctx->linebuf_len);
	}

	/* Mark trigger with a ^ character. */
	if (ctx->mark_trigger != -1)
		sprintf(outbuf + strlen(outbuf), "T:%*s^\n",
			ctx->mark_trigger * (VALUE_LEN+1), "");

	memset(ctx->linebuf, 0, i * ctx->linebuf_len);
}

static int init(struct output *o)
{
	struct context *ctx;
	struct probe *probe;
	GSList *l;
	uint64_t samplerate;
	int num_probes;
	char *samplerate_s;

	if (!(ctx = calloc(1, sizeof(struct context))))
		return SIGROK_ERR_MALLOC;

	if (!(ctx->header = malloc(512))) {
		free(ctx);
		return SIGROK_ERR_MALLOC;
	}

	o->internal = ctx;
	ctx->samples_per_line = 5;
	ctx->num_enabled_probes = 0;
	ctx->mark_trigger = -1;
	for (l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if (!probe->enabled)
			continue;
		ctx->probelist[ctx->num_enabled_probes++] = probe->name;
	}
	ctx->unitsize = sizeof(double) * ctx->num_enabled_probes;

	snprintf(ctx->header, 511, "%s\n", PACKAGE_STRING);
	if (o->device->plugin) {
		num_probes = g_slist_length(o->device->probes);
		samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, DI_CUR_SAMPLERATE));
		if (!(samplerate_s = sigrok_samplerate_string(samplerate))) {
			free(ctx->header);
			free(ctx);
			return SIGROK_ERR;
		}
		snprintf(ctx->header + strlen(ctx->header),
			 511 - strlen(ctx->header),
			 "Acquisition with %d/%d probes at %s\n",
			 ctx->num_enabled_probes, num_probes, samplerate_s);
		free(samplerate_s);
	}

	ctx->linebuf_len = MAX_PROBENAME_LEN + ctx->samples_per_line * VALUE_LEN
			+ ctx->samples_per_line + 4;
	if (!(ctx->linebuf = calloc(1, ctx->num_enabled_probes * ctx->linebuf_len))) {
		free(ctx->header);
		free(ctx);
		return SIGROK_ERR_MALLOC;
	}

	return 0;
}

static int data(struct output *o, char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	double probe_sample;
	unsigned int max_linelen, outsize, offset, p;
	char *outbuf, s[VALUE_LEN+2];

	ctx = o->internal;
	max_linelen = MAX_PROBENAME_LEN + 3 + ctx->samples_per_line * VALUE_LEN
			+ ctx->samples_per_line / 8;
	outsize = length_in / ctx->unitsize * ctx->num_enabled_probes
			/ ctx->samples_per_line * max_linelen + 512;
	if (!(outbuf = calloc(1, outsize + 1)))
		return SIGROK_ERR_MALLOC;

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		free(ctx->header);
		ctx->header = NULL;
	}

	if (length_in >= ctx->unitsize) {
		for (offset = 0; offset <= length_in - ctx->unitsize;
		     offset += ctx->unitsize) {
			for (p = 0; p < ctx->num_enabled_probes; p++) {
				memcpy(&probe_sample, data_in + offset * p, sizeof(double));
				snprintf(s, VALUE_LEN+2, "% 6.2f ", probe_sample);
				strcat(ctx->linebuf + p * ctx->linebuf_len + ctx->line_offset, s);
				ctx->line_offset += strlen(s);
				ctx->spl_cnt++;
			}

			/* End of line. */
			if (ctx->spl_cnt >= ctx->samples_per_line) {
				flush_linebufs(ctx, outbuf);
				ctx->line_offset = ctx->spl_cnt = 0;
				ctx->mark_trigger = -1;
			}


		}
	} else {
		g_message("short buffer (length_in=%" PRIu64 ")", length_in);
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SIGROK_OK;
}

static int event(struct output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;

	ctx = o->internal;
	switch (event_type) {
	case DF_TRIGGER:
		ctx->mark_trigger = ctx->spl_cnt;
		break;
	case DF_END:
		if (ctx->header)
			free(ctx->header);
		free(ctx->linebuf);
		free(o->internal);
		o->internal = NULL;
		break;
	}

	*data_out = NULL;
	*length_out = 0;

	return SIGROK_OK;
}

struct output_format output_analog = {
	"analog",
	"Analog data",
	DF_ANALOG,
	init,
	data,
	event,
};

