/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include "config.h"
#include "sigrok.h"
#include "sigrok-internal.h"

struct context {
	unsigned int num_enabled_probes;
	GPtrArray *probelist;
};

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;

	if (!o)
		return SR_ERR_ARG;

	if (!o->dev)
		return SR_ERR_ARG;

	if (!o->dev->driver)
		return SR_ERR_ARG;

	if (!(ctx = g_try_malloc0(sizeof(struct context))))
		return SR_ERR_MALLOC;

	o->internal = ctx;

	/* Get the number of probes and their names. */
	ctx->probelist = g_ptr_array_new();
	for (l = o->dev->probes; l; l = l->next) {
		probe = l->data;
		if (!probe || !probe->enabled)
			continue;
		g_ptr_array_add(ctx->probelist, probe->name);
		ctx->num_enabled_probes++;
	}

	return SR_OK;
}

static int event(struct sr_output *o, int event_type, uint8_t **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;

	if (!o)
		return SR_ERR_ARG;

	if (!(ctx = o->internal))
		return SR_ERR_ARG;

	if (!data_out)
		return SR_ERR_ARG;

	switch (event_type) {
	case SR_DF_FRAME_BEGIN:
		*data_out = (uint8_t *)g_strdup("FRAME-BEGIN\n");
		*length_out = 12;
		break;
	case SR_DF_FRAME_END:
		*data_out = (uint8_t *)g_strdup("FRAME-END\n");
		*length_out = 10;
		break;
	case SR_DF_END:
		*data_out = NULL;
		*length_out = 0;
		g_ptr_array_free(ctx->probelist, TRUE);
		g_free(o->internal);
		o->internal = NULL;
		break;
	default:
		/* Ignore everything else. */
		*data_out = NULL;
		*length_out = 0;
		break;
	}

	return SR_OK;
}

static int data(struct sr_output *o, const uint8_t *data_in,
		uint64_t length_in, uint8_t **data_out, uint64_t *length_out)
{
	struct context *ctx;
	GString *outstr;
	float *fdata;
	uint64_t max, i;
	unsigned int j;

	if (!o)
		return SR_ERR_ARG;

	if (!(ctx = o->internal))
		return SR_ERR_ARG;

	if (!data_in || !data_out)
		return SR_ERR_ARG;

	outstr = g_string_sized_new(512);

	fdata = (float *)data_in;
	max = length_in / sizeof(float);
	for (i = 0; i < max;) {
		for (j = 0; j < ctx->num_enabled_probes; j++) {
			g_string_append_printf(outstr, "%s: %f\n",
					(char *)g_ptr_array_index(ctx->probelist, j),
					fdata[i++]);
		}
	}

	*data_out = (uint8_t *)outstr->str;
	*length_out = outstr->len;
	g_string_free(outstr, FALSE);

	return SR_OK;
}

SR_PRIV struct sr_output_format output_float = {
	.id = "float",
	.description = "Floating point",
	.df_type = SR_DF_ANALOG,
	.init = init,
	.data = data,
	.event = event,
};
