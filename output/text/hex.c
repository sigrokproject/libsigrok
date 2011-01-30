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


int init_hex(struct sr_output *o)
{
	return init(o, DEFAULT_BPL_HEX, MODE_HEX);
}

int data_hex(struct sr_output *o, char *data_in, uint64_t length_in,
		    char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int outsize, offset, p;
	int max_linelen;
	uint64_t sample;
	char *outbuf;

	ctx = o->internal;
	max_linelen = MAX_PROBENAME_LEN + 3 + ctx->samples_per_line
			+ ctx->samples_per_line / 2;
	outsize = length_in / ctx->unitsize * ctx->num_enabled_probes
			/ ctx->samples_per_line * max_linelen + 512;

	if (!(outbuf = calloc(1, outsize + 1)))
		return SR_ERR_MALLOC;

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy(outbuf, ctx->header, outsize);
		free(ctx->header);
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


struct sr_output_format output_text_hex = {
	"hex",
	"Hexadecimal (takes argument, default 192)",
	SR_DF_LOGIC,
	init_hex,
	data_hex,
	event,
};

