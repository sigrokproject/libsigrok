/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <sigrok.h>

#define DEFAULT_BPL_BIN 64
#define DEFAULT_BPL_HEX 256

struct context {
	int num_enabled_probes;
	int samples_per_line;
	int unitsize;
	int line_offset;
	int linebuf_len;
	char *probelist[65];
	char *linebuf;
	int spl_cnt;
	uint8_t *linevalues;
	char *header;
};


static void flush_linebufs(struct context *ctx, GSList *probes, char *outbuf)
{
	static int max_probename_len = 0;
	int len, i;

	if(ctx->linebuf[0] == 0)
		return;

	if(max_probename_len == 0) {
		/* first time through */
		for(i = 0; ctx->probelist[i]; i++) {
			len = strlen(ctx->probelist[i]);
			if(len > max_probename_len)
				max_probename_len = len;
		}
	}

	for(i = 0; ctx->probelist[i]; i++) {
		sprintf(outbuf + strlen(outbuf), "%*s:%s\n", max_probename_len, ctx->probelist[i],
				ctx->linebuf + i * ctx->linebuf_len);
	}
	memset(ctx->linebuf, 0, i * ctx->linebuf_len);

}


static int init(struct output *o, int default_spl)
{
	struct context *ctx;
	struct probe *probe;
	GSList *l;
	uint64_t samplerate;
	int num_probes;
	char *samplerate_s;

	ctx = malloc(sizeof(struct context));
	o->internal = ctx;
	ctx->num_enabled_probes = 0;
	for(l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if(probe->enabled)
			ctx->probelist[ctx->num_enabled_probes++] = probe->name;
	}
	ctx->probelist[ctx->num_enabled_probes] = 0;
	ctx->unitsize = (ctx->num_enabled_probes + 7) / 8;
	ctx->line_offset = 0;
	ctx->spl_cnt = 0;
	if(o->param && o->param[0])
		ctx->samples_per_line = strtoul(o->param, NULL, 10);
	else
		ctx->samples_per_line = default_spl;

	ctx->header = malloc(512);
	num_probes = g_slist_length(o->device->probes);
	samplerate = *((uint64_t *) o->device->plugin->get_device_info(o->device->plugin_index, DI_CUR_SAMPLERATE));
	snprintf(ctx->header, 512, "Acquisition with %d/%d probes at ", ctx->num_enabled_probes, num_probes);

	if ((samplerate_s = sigrok_samplerate_string(samplerate)) == NULL)
		return -1; // FIXME
	snprintf(ctx->header + strlen(ctx->header), 512, "%s\n", samplerate_s);
	free(samplerate_s);

	ctx->linebuf_len = ctx->samples_per_line * 2;
	ctx->linebuf = calloc(1, num_probes * ctx->linebuf_len);
	ctx->linevalues = calloc(1, num_probes);

	return 0;
}


static int event(struct output *o, int event_type, char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	int outsize;
	char *outbuf;

	ctx = o->internal;
	switch(event_type) {
	case DF_TRIGGER:
		break;
	case DF_END:
		outsize = ctx->num_enabled_probes * (ctx->samples_per_line + 20) + 512;
		outbuf = calloc(1, outsize);
		flush_linebufs(ctx, o->device->probes, outbuf);
		*data_out = outbuf;
		*length_out = strlen(outbuf);
		free(o->internal);
		o->internal = NULL;
		break;
	}

	return SIGROK_OK;
}


static int init_binary(struct output *o)
{

	return init(o, DEFAULT_BPL_BIN);

}


static int data_binary(struct output *o, char *data_in, uint64_t length_in, char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	int outsize, offset, p;
	uint64_t sample;
	char *outbuf;

	ctx = o->internal;
	outsize = length_in / ctx->unitsize * ctx->num_enabled_probes * ctx->samples_per_line + 512;
	outbuf = calloc(1, outsize+1);
	if(ctx->header) {
		/* the header is still in here, we must be on the first data packet */
		strncpy(outbuf, ctx->header, outsize);
		free(ctx->header);
		ctx->header = NULL;
	}
	else
		outbuf[0] = 0;

	if(length_in >= ctx->unitsize) {
		for(offset = 0; offset <= length_in - ctx->unitsize; offset += ctx->unitsize) {
			memcpy(&sample, data_in + offset, ctx->unitsize);
			for(p = 0; p < ctx->num_enabled_probes; p++) {
				if(sample & ((uint64_t) 1 << p))
					ctx->linebuf[p * ctx->linebuf_len + ctx->line_offset] = '1';
				else
					ctx->linebuf[p * ctx->linebuf_len + ctx->line_offset] = '0';
			}
			ctx->line_offset++;
			ctx->spl_cnt++;

			/* space every 8th bit */
			if((ctx->spl_cnt & 7) == 0) {
				for(p = 0; p < ctx->num_enabled_probes; p++)
					ctx->linebuf[p * ctx->linebuf_len + ctx->line_offset] = ' ';
				ctx->line_offset++;
			}

			/* end of line */
			if(ctx->spl_cnt >= ctx->samples_per_line) {
				flush_linebufs(ctx, o->device->probes, outbuf);
				ctx->line_offset = ctx->spl_cnt = 0;
			}
		}
	} else
		g_message("short buffer (length_in=%"PRIu64")", length_in);

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SIGROK_OK;
}


static int init_hex(struct output *o)
{

	return init(o, DEFAULT_BPL_BIN);

}


static int data_hex(struct output *o, char *data_in, uint64_t length_in, char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	int outsize, offset, p;
	uint64_t sample;
	char *outbuf;

	ctx = o->internal;
	outsize = length_in / ctx->unitsize * ctx->num_enabled_probes * ctx->samples_per_line + 512;
	outbuf = calloc(1, outsize+1);
	if(ctx->header) {
		/* the header is still in here, we must be on the first data packet */
		strncpy(outbuf, ctx->header, outsize);
		free(ctx->header);
		ctx->header = NULL;
	}
	else
		outbuf[0] = 0;

	ctx->line_offset = 0;
	for(offset = 0; offset <= length_in - ctx->unitsize; offset += ctx->unitsize) {
		memcpy(&sample, data_in + offset, ctx->unitsize);
		for(p = 0; p < ctx->num_enabled_probes; p++) {
			ctx->linevalues[p] <<= 1;
            if(sample & ((uint64_t) 1 << p))
                ctx->linevalues[p] |= 1;
            sprintf(ctx->linebuf + (p * ctx->linebuf_len) + ctx->line_offset, "%.2x", ctx->linevalues[p]);
		}
		ctx->spl_cnt++;

		/* space after every complete hex byte */
		if((ctx->spl_cnt & 7) == 0) {
			for(p = 0; p < ctx->num_enabled_probes; p++)
				ctx->linebuf[p * ctx->linebuf_len + ctx->line_offset + 2] = ' ';
			ctx->line_offset += 3;
		}

		/* end of line */
		if(ctx->spl_cnt >= ctx->samples_per_line) {
			flush_linebufs(ctx, o->device->probes, outbuf);
			ctx->line_offset = ctx->spl_cnt = 0;
		}
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SIGROK_OK;
}



struct output_format output_text_binary = {
	"bin",
	"Text (binary)",
	init_binary,
	data_binary,
	event
};


struct output_format output_text_hex = {
	"hex",
	"Text (hexadecimal)",
	init_hex,
	data_hex,
	event
};

