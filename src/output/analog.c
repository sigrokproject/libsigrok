/*
 * This file is part of the libsigrok project.
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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/analog"

struct context {
	int num_enabled_channels;
	GPtrArray *channellist;
	int digits;
	float *fdata;
};

enum {
	DIGITS_ALL,
	DIGITS_SPEC,
};

static int init(struct sr_output *o, GHashTable *options)
{
	struct context *ctx;
	struct sr_channel *ch;
	GSList *l;
	const char *s;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	o->priv = ctx = g_malloc0(sizeof(struct context));
	s = g_variant_get_string(g_hash_table_lookup(options, "digits"), NULL);
	if (!strcmp(s, "all"))
		ctx->digits = DIGITS_ALL;
	else
		ctx->digits = DIGITS_SPEC;

	/* Get the number of channels and their names. */
	ctx->channellist = g_ptr_array_new();
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch || !ch->enabled)
			continue;
		g_ptr_array_add(ctx->channellist, ch->name);
		ctx->num_enabled_channels++;
	}
	ctx->fdata = NULL;

	return SR_OK;
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	struct context *ctx;
	const struct sr_datafeed_analog *analog;
	const struct sr_datafeed_meta *meta;
	const struct sr_config *src;
	const struct sr_key_info *srci;
	struct sr_channel *ch;
	GSList *l;
	float *fdata;
	unsigned int i;
	int num_channels, c, ret, digits, actual_digits;
	char *number, *suffix;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	ctx = o->priv;

	switch (packet->type) {
	case SR_DF_FRAME_BEGIN:
		*out = g_string_new("FRAME-BEGIN\n");
		break;
	case SR_DF_FRAME_END:
		*out = g_string_new("FRAME-END\n");
		break;
	case SR_DF_META:
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			if (!(srci = sr_key_info_get(SR_KEY_CONFIG, src->key)))
				return SR_ERR;
			*out = g_string_sized_new(512);
			g_string_append(*out, "META ");
			g_string_append_printf(*out, "%s: ", srci->id);
			if (srci->datatype == SR_T_BOOL) {
				g_string_append_printf(*out, "%u",
					g_variant_get_boolean(src->data));
			} else if (srci->datatype == SR_T_FLOAT) {
				g_string_append_printf(*out, "%f",
					g_variant_get_double(src->data));
			} else if (srci->datatype == SR_T_UINT64) {
				g_string_append_printf(*out, "%"
					G_GUINT64_FORMAT,
					g_variant_get_uint64(src->data));
			}
			g_string_append(*out, "\n");
		}
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		num_channels = g_slist_length(analog->meaning->channels);
		if (!(fdata = g_try_realloc(ctx->fdata,
						analog->num_samples * num_channels * sizeof(float))))
			return SR_ERR_MALLOC;
		ctx->fdata = fdata;
		if ((ret = sr_analog_to_float(analog, fdata)) != SR_OK)
			return ret;
		*out = g_string_sized_new(512);
		if (analog->encoding->is_digits_decimal) {
			if (ctx->digits == DIGITS_ALL)
				digits = analog->encoding->digits;
			else
				digits = analog->spec->spec_digits;
		} else {
			/* TODO we don't know how to print by number of bits yet. */
			digits = 6;
		}
		gboolean si_friendly = sr_analog_si_prefix_friendly(analog->meaning->unit);
		sr_analog_unit_to_string(analog, &suffix);
		for (i = 0; i < analog->num_samples; i++) {
			for (l = analog->meaning->channels, c = 0; l; l = l->next, c++) {
				float value = fdata[i * num_channels + c];
				const char *prefix = "";
				actual_digits = digits;
				if (si_friendly)
					prefix = sr_analog_si_prefix(&value, &actual_digits);
				ch = l->data;
				g_string_append_printf(*out, "%s: ", ch->name);
				number = g_strdup_printf("%.*f", MAX(actual_digits, 0), value);
				g_string_append(*out, number);
				g_free(number);
				g_string_append(*out, " ");
				g_string_append(*out, prefix);
				g_string_append(*out, suffix);
				g_string_append(*out, "\n");
			}
		}
		g_free(suffix);
		break;
	}

	return SR_OK;
}

static struct sr_option options[] = {
	{ "digits", "Digits", "Digits to show", NULL, NULL },
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_string("all"));
		options[0].values = g_slist_append(options[0].values,
				g_variant_ref_sink(g_variant_new_string("all")));
		options[0].values = g_slist_append(options[0].values,
				g_variant_ref_sink(g_variant_new_string("spec")));
	}

	return options;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;
	ctx = o->priv;

	g_ptr_array_free(ctx->channellist, 1);
	g_variant_unref(options[0].def);
	g_slist_free_full(options[0].values, (GDestroyNotify)g_variant_unref);
	g_free(ctx->fdata);
	g_free(ctx);
	o->priv = NULL;

	return SR_OK;
}

SR_PRIV struct sr_output_module output_analog = {
	.id = "analog",
	.name = "Analog",
	.desc = "Analog data and types",
	.exts = NULL,
	.flags = 0,
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup
};
