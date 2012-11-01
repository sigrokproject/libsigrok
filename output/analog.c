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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include <math.h>

struct context {
	int num_enabled_probes;
	GPtrArray *probelist;
	GString *out;
};

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;

	sr_spew("output/analog: initializing");
	if (!o || !o->sdi)
		return SR_ERR_ARG;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("output/analog: Context malloc failed.");
		return SR_ERR_MALLOC;
	}
	o->internal = ctx;

	/* Get the number of probes and their names. */
	ctx->probelist = g_ptr_array_new();
	for (l = o->sdi->probes; l; l = l->next) {
		probe = l->data;
		if (!probe || !probe->enabled)
			continue;
		g_ptr_array_add(ctx->probelist, probe->name);
		ctx->num_enabled_probes++;
	}

	ctx->out = g_string_sized_new(512);

	return SR_OK;
}

static void si_printf(float value, GString *out, char *unitstr)
{
	float v;

	if (signbit(value))
		v = -(value);
	else
		v = value;

	if (v < 1e-12 || v > 1e+12)
		g_string_append_printf(out, "%f %s", value, unitstr);
	else if (v > 1e+9)
		g_string_append_printf(out, "%f G%s", value / 1e+9, unitstr);
	else if (v > 1e+6)
		g_string_append_printf(out, "%f M%s", value / 1e+6, unitstr);
	else if (v > 1e+3)
		g_string_append_printf(out, "%f k%s", value / 1e+3, unitstr);
	else if (v < 1e-9)
		g_string_append_printf(out, "%f n%s", value * 1e+9, unitstr);
	else if (v < 1e-6)
		g_string_append_printf(out, "%f u%s", value * 1e+6, unitstr);
	else if (v < 1e-3)
		g_string_append_printf(out, "%f m%s", value * 1e+3, unitstr);
	else
		g_string_append_printf(out, "%f %s", value, unitstr);

}

static void fancyprint(int unit, int mqflags, float value, GString *out)
{

	switch (unit) {
		case SR_UNIT_VOLT:
			si_printf(value, out, "V");
			break;
		case SR_UNIT_AMPERE:
			si_printf(value, out, "A");
			break;
		case SR_UNIT_OHM:
			si_printf(value, out, "");
			g_string_append_unichar(out, 0x2126);
			break;
		case SR_UNIT_FARAD:
			si_printf(value, out, "F");
			break;
		case SR_UNIT_KELVIN:
			si_printf(value, out, "K");
			break;
		case SR_UNIT_CELSIUS:
			si_printf(value, out, "");
			g_string_append_unichar(out, 0x00b0);
			g_string_append_c(out, 'C');
			break;
		case SR_UNIT_FAHRENHEIT:
			si_printf(value, out, "");
			g_string_append_unichar(out, 0x00b0);
			g_string_append_c(out, 'F');
			break;
		case SR_UNIT_HERTZ:
			si_printf(value, out, "Hz");
			break;
		case SR_UNIT_PERCENTAGE:
			g_string_append_printf(out, "%f%%", value);
			break;
		case SR_UNIT_BOOLEAN:
			if (value > 0)
				g_string_append_printf(out, "TRUE");
			else
				g_string_append_printf(out, "FALSE");
			break;
		case SR_UNIT_SECOND:
			si_printf(value, out, "s");
			break;
		case SR_UNIT_SIEMENS:
			si_printf(value, out, "S");
			break;
		case SR_UNIT_DECIBEL_MW:
			si_printf(value, out, "dBu");
			break;
		case SR_UNIT_DECIBEL_VOLT:
			si_printf(value, out, "dBV");
			break;
		default:
			si_printf(value, out, "");
	}
	if ((mqflags & (SR_MQFLAG_AC | SR_MQFLAG_DC)) == (SR_MQFLAG_AC | SR_MQFLAG_DC))
		g_string_append_printf(out, " AC+DC");
	else if (mqflags & SR_MQFLAG_AC)
		g_string_append_printf(out, " AC");
	else if (mqflags & SR_MQFLAG_DC)
		g_string_append_printf(out, " DC");
	g_string_append_c(out, '\n');

}

static GString *receive(struct sr_output *o, const struct sr_dev_inst *sdi,
		struct sr_datafeed_packet *packet)
{
	struct sr_datafeed_analog *analog;
	struct context *ctx;
	float *fdata;
	int i, j;

	(void)sdi;
	if (!o || !o->sdi)
		return NULL;
	ctx = o->internal;

	g_string_set_size(ctx->out, 0);
	switch (packet->type) {
	case SR_DF_HEADER:
		break;
	case SR_DF_FRAME_BEGIN:
		g_string_append_printf(ctx->out, "FRAME-BEGIN\n");
		break;
	case SR_DF_FRAME_END:
		g_string_append_printf(ctx->out, "FRAME-END\n");
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		fdata = (float *)analog->data;
		for (i = 0; i < analog->num_samples; i++) {
			for (j = 0; j < ctx->num_enabled_probes; j++) {
				g_string_append_printf(ctx->out, "%s: ",
						(char *)g_ptr_array_index(ctx->probelist, j));
				fancyprint(analog->unit, analog->mqflags,
						fdata[i + j], ctx->out);
			}
		}
		break;
	}

	return ctx->out;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;
	ctx = o->internal;

	g_ptr_array_free(ctx->probelist, 1);
	g_string_free(ctx->out, 1);
	g_free(ctx);
	o->internal = NULL;

	return SR_OK;
}


SR_PRIV struct sr_output_format output_analog = {
	.id = "analog",
	.description = "Analog data",
	.df_type = SR_DF_ANALOG,
	.init = init,
	.recv = receive,
	.cleanup = cleanup
};
