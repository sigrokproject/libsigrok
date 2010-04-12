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

static int init(struct output *o)
{
	/* Prevent compiler warnings. */
	o = o;

	/* Nothing so far, but we might want to add stuff here later. */
	return 0;
}

static int event(struct output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	char *outbuf;
	int outlen = 1; /* FIXME */

	/* Prevent compiler warnings. */
	o = o;
	event_type = event_type;

	switch(event_type) {
	case DF_TRIGGER:
		break;
	case DF_END:
		outbuf = calloc(1, outlen); /* FIXME */
		if (outbuf == NULL)
			return SIGROK_ERR_MALLOC;
		*data_out = outbuf;
		*length_out = outlen;
		break;
	}

	return SIGROK_OK;
}

static int data(struct output *o, char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
{
	uint64_t outsize;
	char *outbuf;

	/* Prevent compiler warnings. */
	o = o;

	outsize = length_in;
	outbuf = calloc(1, outsize);
	if (outbuf == NULL)
		return SIGROK_ERR_MALLOC;

	/* TODO: Are disabled probes handled correctly? */
	memcpy(outbuf, data_in, length_in);

	*data_out = outbuf;
	*length_out = outsize;

	return SIGROK_OK;
}

struct output_format output_binary = {
	"binary",
	"Raw binary",
	init,
	data,
	event,
};
