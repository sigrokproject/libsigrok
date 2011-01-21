/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2011 Håvard Espeland <gus@ping.uio.no>
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
#include <string.h>
#include <glib.h>
#include <sigrok.h>
#include "text.h"


int init_ascii(struct output *o)
{
	return init(o, DEFAULT_BPL_ASCII, MODE_ASCII);
}

int data_ascii(struct output *o, char *data_in, uint64_t length_in,
		     char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int outsize, offset, p;
	int max_linelen;
	uint64_t sample;
	char *outbuf;

	ctx = o->internal;
	max_linelen = MAX_PROBENAME_LEN + 3 + ctx->samples_per_line
			+ ctx->samples_per_line / 8;
        /*
         * Calculate space needed for probes. Set aside 512 bytes for
         * extra output, e.g. trigger.
         */
	outsize = 512 + (1 + (length_in / ctx->unitsize) / ctx->samples_per_line)
            * (ctx->num_enabled_probes * max_linelen);

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
		g_message("short buffer (length_in=%" PRIu64 ")", length_in);
	}

	*data_out = outbuf;
	*length_out = strlen(outbuf);

	return SIGROK_OK;
}


struct output_format output_text_ascii = {
	"ascii",
	"ASCII (takes argument, default 74)",
	DF_LOGIC,
	init_ascii,
	data_ascii,
	event,
};

