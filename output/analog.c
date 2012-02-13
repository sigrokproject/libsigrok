/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2011 Håvard Espeland <gus@ping.uio.no>
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
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
#include "sigrok.h"

#define DEFAULT_BPL_BITS 64
#define DEFAULT_BPL_HEX  192
#define DEFAULT_BPL_ASCII 74

enum outputmode {
	MODE_BITS = 1,
	MODE_HEX,
	MODE_ASCII,
};

struct context {
	unsigned int num_enabled_probes;
	int samples_per_line;
	unsigned int unitsize;
	int line_offset;
	int linebuf_len;
	char *probelist[65];
	char *linebuf;
	int spl_cnt;
	uint8_t *linevalues;
	char *header;
	int mark_trigger;
//	struct sr_analog_sample *prevsample;
	enum outputmode mode;
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
	{
		int space_offset = ctx->mark_trigger / 8;

		if (ctx->mode == MODE_ASCII)
			space_offset = 0;

		sprintf(outbuf + strlen(outbuf), "T:%*s^\n",
			ctx->mark_trigger + space_offset, "");
	}

	memset(ctx->linebuf, 0, i * ctx->linebuf_len);
}

static int init(struct sr_output *o, int default_spl, enum outputmode mode)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	uint64_t samplerate;
	int num_probes;
	char *samplerate_s;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("analog out: %s: ctx malloc failed", __func__);
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

	ctx->probelist[ctx->num_enabled_probes] = 0;
	ctx->unitsize = sizeof(struct sr_analog_sample) +
		(ctx->num_enabled_probes * sizeof(struct sr_analog_probe));
	ctx->line_offset = 0;
	ctx->spl_cnt = 0;
	ctx->mark_trigger = -1;
	ctx->mode = mode;

	if (o->param && o->param[0]) {
		ctx->samples_per_line = strtoul(o->param, NULL, 10);
		if (ctx->samples_per_line < 1)
			return SR_ERR;
	} else
		ctx->samples_per_line = default_spl;

	if (!(ctx->header = g_try_malloc(512))) {
		g_free(ctx);
		sr_err("analog out: %s: ctx->header malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	snprintf(ctx->header, 511, "%s\n", PACKAGE_STRING);
	num_probes = g_slist_length(o->device->probes);
	if (o->device->plugin && sr_dev_has_hwcap(o->device, SR_HWCAP_SAMPLERATE)) {
		samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, SR_DI_CUR_SAMPLERATE));
		if (!(samplerate_s = sr_samplerate_string(samplerate))) {
			g_free(ctx->header);
			g_free(ctx);
			return SR_ERR;
		}
		snprintf(ctx->header + strlen(ctx->header),
			 511 - strlen(ctx->header),
			 "Acquisition with %d/%d probes at %s\n",
			 ctx->num_enabled_probes, num_probes, samplerate_s);
		g_free(samplerate_s);
	}

	ctx->linebuf_len = ctx->samples_per_line * 2 + 4;
	if (!(ctx->linebuf = g_try_malloc0(num_probes * ctx->linebuf_len))) {
		g_free(ctx->header);
		g_free(ctx);
		sr_err("analog out: %s: ctx->linebuf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}
	if (!(ctx->linevalues = g_try_malloc0(num_probes))) {
		g_free(ctx->header);
		g_free(ctx);
		sr_err("analog out: %s: ctx->linevalues malloc failed",
		       __func__);
		return SR_ERR_MALLOC;
	}

	return SR_OK;
}

static int event(struct sr_output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;
	int outsize;
	char *outbuf;

	ctx = o->internal;
	switch (event_type) {
	case SR_DF_TRIGGER:
		ctx->mark_trigger = ctx->spl_cnt;
		*data_out = NULL;
		*length_out = 0;
		break;
	case SR_DF_END:
		outsize = ctx->num_enabled_probes
				* (ctx->samples_per_line + 20) + 512;
		if (!(outbuf = g_try_malloc0(outsize))) {
			sr_err("analog out: %s: outbuf malloc failed",
			       __func__);
			return SR_ERR_MALLOC;
		}
		flush_linebufs(ctx, outbuf);
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

static int init_bits(struct sr_output *o)
{
	return init(o, DEFAULT_BPL_BITS, MODE_BITS);
}

static int data_bits(struct sr_output *o, const char *data_in,
		     uint64_t length_in, char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int outsize, offset, p;
	int max_linelen;
	struct sr_analog_sample *sample;
	char *outbuf, c;

	ctx = o->internal;
	max_linelen = SR_MAX_PROBENAME_LEN + 3 + ctx->samples_per_line
			+ ctx->samples_per_line / 8;
        /*
         * Calculate space needed for probes. Set aside 512 bytes for
         * extra output, e.g. trigger.
         */
	outsize = 512 + (1 + (length_in / ctx->unitsize) / ctx->samples_per_line)
            * (ctx->num_enabled_probes * max_linelen);

	if (!(outbuf = g_try_malloc0(outsize + 1))) {
		sr_err("analog out: %s: outbuf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		g_free(ctx->header);
		ctx->header = NULL;

		/* Ensure first transition. */
//		memcpy(&ctx->prevsample, data_in, ctx->unitsize);
//		ctx->prevsample = ~ctx->prevsample;
	}

	if (length_in >= ctx->unitsize) {
		for (offset = 0; offset <= length_in - ctx->unitsize;
		     offset += ctx->unitsize) {
			sample = (struct sr_analog_sample *) (data_in + offset);
			for (p = 0; p < ctx->num_enabled_probes; p++) {
				int val = sample->probes[p].val;
				int res = sample->probes[p].res;
				if (res == 1)
					c = '0' + (val & ((1 << res) - 1));
				else
					/*
					 * Scale analog resolution down so it
					 * fits 25 letters
					 */
					c = 'A' + (((val & ((1 << res) - 1)) /
							(res * res)) / 10);
				ctx->linebuf[p * ctx->linebuf_len +
					     ctx->line_offset] = c;
			}
			ctx->line_offset++;
			ctx->spl_cnt++;

			/* Add a space every 8th bit. */
			if ((ctx->spl_cnt & 7) == 0) {
				for (p = 0; p < ctx->num_enabled_probes; p++)
					ctx->linebuf[p * ctx->linebuf_len +
						     ctx->line_offset] = ' ';
				ctx->line_offset++;
			}

			/* End of line. */
			if (ctx->spl_cnt >= ctx->samples_per_line) {
				flush_linebufs(ctx, outbuf);
				ctx->line_offset = ctx->spl_cnt = 0;
				ctx->mark_trigger = -1;
			}
		}
	} else {
		sr_info("short buffer (length_in=%" PRIu64 ")", length_in);
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SR_OK;
}
#if 0
static int init_hex(struct sr_output *o)
{
	return init(o, DEFAULT_BPL_HEX, MODE_HEX);
}

static int data_hex(struct sr_output *o, const char *data_in,
		    uint64_t length_in, char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int outsize, offset, p;
	int max_linelen;
	uint64_t sample;
	char *outbuf;

	ctx = o->internal;
	max_linelen = SR_MAX_PROBENAME_LEN + 3 + ctx->samples_per_line
			+ ctx->samples_per_line / 2;
	outsize = length_in / ctx->unitsize * ctx->num_enabled_probes
			/ ctx->samples_per_line * max_linelen + 512;

	if (!(outbuf = g_try_malloc0(outsize + 1))) {
		sr_err("analog out: %s: outbuf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		g_free(ctx->header);
		ctx->header = NULL;
	}

	ctx->line_offset = 0;
	for (offset = 0; offset <= length_in - ctx->unitsize;
	     offset += ctx->unitsize) {
		memcpy(&sample, data_in + offset, ctx->unitsize);
		for (p = 0; p < ctx->num_enabled_probes; p++) {
			ctx->linevalues[p] <<= 1;
			if (sample & ((uint64_t) 1 << p))
				ctx->linevalues[p] |= 1;
			sprintf(ctx->linebuf + (p * ctx->linebuf_len) +
				ctx->line_offset, "%.2x", ctx->linevalues[p]);
		}
		ctx->spl_cnt++;

		/* Add a space after every complete hex byte. */
		if ((ctx->spl_cnt & 7) == 0) {
			for (p = 0; p < ctx->num_enabled_probes; p++)
				ctx->linebuf[p * ctx->linebuf_len +
					     ctx->line_offset + 2] = ' ';
			ctx->line_offset += 3;
		}

		/* End of line. */
		if (ctx->spl_cnt >= ctx->samples_per_line) {
			flush_linebufs(ctx, outbuf);
			ctx->line_offset = ctx->spl_cnt = 0;
		}
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SR_OK;
}

static int init_ascii(struct sr_output *o)
{
	return init(o, DEFAULT_BPL_ASCII, MODE_ASCII);
}

static int data_ascii(struct sr_output *o, const char *data_in,
		      uint64_t length_in, char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int outsize, offset, p;
	int max_linelen;
	uint64_t sample;
	char *outbuf;

	ctx = o->internal;
	max_linelen = SR_MAX_PROBENAME_LEN + 3 + ctx->samples_per_line
			+ ctx->samples_per_line / 8;
        /*
         * Calculate space needed for probes. Set aside 512 bytes for
         * extra output, e.g. trigger.
         */
	outsize = 512 + (1 + (length_in / ctx->unitsize) / ctx->samples_per_line)
            * (ctx->num_enabled_probes * max_linelen);

	if (!(outbuf = g_try_malloc0(outsize + 1))) {
		sr_err("analog out: %s: outbuf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		g_free(ctx->header);
		ctx->header = NULL;
	}

	if (length_in >= ctx->unitsize) {
		for (offset = 0; offset <= length_in - ctx->unitsize;
		     offset += ctx->unitsize) {
			memcpy(&sample, data_in + offset, ctx->unitsize);

			char tmpval[ctx->num_enabled_probes];

			for (p = 0; p < ctx->num_enabled_probes; p++) {
				uint64_t curbit = (sample & ((uint64_t) 1 << p));
				uint64_t prevbit = (ctx->prevsample &
						((uint64_t) 1 << p));

				if (curbit < prevbit && ctx->line_offset > 0) {
					ctx->linebuf[p * ctx->linebuf_len +
						ctx->line_offset-1] = '\\';
				}

				if (curbit > prevbit) {
					tmpval[p] = '/';
				} else {
					if (curbit)
						tmpval[p] = '"';
					else
						tmpval[p] = '.';
				}
			}

			/* End of line. */
			if (ctx->spl_cnt >= ctx->samples_per_line) {
				flush_linebufs(ctx, outbuf);
				ctx->line_offset = ctx->spl_cnt = 0;
				ctx->mark_trigger = -1;
			}

			for (p = 0; p < ctx->num_enabled_probes; p++) {
				ctx->linebuf[p * ctx->linebuf_len +
					     ctx->line_offset] = tmpval[p];
			}

			ctx->line_offset++;
			ctx->spl_cnt++;

			ctx->prevsample = sample;
		}
	} else {
		sr_info("short buffer (length_in=%" PRIu64 ")", length_in);
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SR_OK;
}
#endif

SR_PRIV struct sr_output_format output_analog_bits = {
	.id = "analog_bits",
	.description = "Bits (takes argument, default 64)",
	.df_type = SR_DF_ANALOG,
	.init = init_bits,
	.data = data_bits,
	.event = event,
};

#if 0
struct sr_output_format output_analog_hex = {
	.id = "analog_hex",
	.description = "Hexadecimal (takes argument, default 192)",
	.df_type = SR_DF_ANALOG,
	.init = init_hex,
	.data = data_hex,
	.event = event,
};

struct sr_output_format output_analog_ascii = {
	.id = "analog_ascii",
	.description = "ASCII (takes argument, default 74)",
	.df_type = SR_DF_ANALOG,
	.init = init_ascii,
	.data = data_ascii,
	.event = event,
};
#endif
