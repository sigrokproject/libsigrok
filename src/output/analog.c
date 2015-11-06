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

	return SR_OK;
}

static void si_printf(float value, GString *out, const char *unitstr)
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

/* Please use the same order as in enum sr_unit (libsigrok.h). */
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
		g_string_append_printf(out, "%f %%", value);
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
	case SR_UNIT_UNITLESS:
		si_printf(value, out, "");
		break;
	case SR_UNIT_DECIBEL_SPL:
		if (mqflags & SR_MQFLAG_SPL_FREQ_WEIGHT_A)
			si_printf(value, out, "dB(A)");
		else if (mqflags & SR_MQFLAG_SPL_FREQ_WEIGHT_C)
			si_printf(value, out, "dB(C)");
		else if (mqflags & SR_MQFLAG_SPL_FREQ_WEIGHT_Z)
			si_printf(value, out, "dB(Z)");
		else
			/* No frequency weighting, or non-standard "flat" */
			si_printf(value, out, "dB(SPL)");
		if (mqflags & SR_MQFLAG_SPL_TIME_WEIGHT_S)
			g_string_append(out, " S");
		else if (mqflags & SR_MQFLAG_SPL_TIME_WEIGHT_F)
			g_string_append(out, " F");
		if (mqflags & SR_MQFLAG_SPL_LAT)
			g_string_append(out, " LAT");
		else if (mqflags & SR_MQFLAG_SPL_PCT_OVER_ALARM)
			/* Not a standard function for SLMs, so this is
			 * a made-up notation. */
			g_string_append(out, " %oA");
		break;
	case SR_UNIT_CONCENTRATION:
		g_string_append_printf(out, "%f ppm", value * (1000 * 1000));
		break;
	case SR_UNIT_REVOLUTIONS_PER_MINUTE:
		si_printf(value, out, "RPM");
		break;
	case SR_UNIT_VOLT_AMPERE:
		si_printf(value, out, "VA");
		break;
	case SR_UNIT_WATT:
		si_printf(value, out, "W");
		break;
	case SR_UNIT_WATT_HOUR:
		si_printf(value, out, "Wh");
		break;
	case SR_UNIT_METER_SECOND:
		si_printf(value, out, "m/s");
		break;
	case SR_UNIT_HECTOPASCAL:
		si_printf(value, out, "hPa");
		break;
	case SR_UNIT_HUMIDITY_293K:
		si_printf(value, out, "%rF");
		break;
	case SR_UNIT_DEGREE:
		si_printf(value, out, "");
		g_string_append_unichar(out, 0x00b0);
		break;
	case SR_UNIT_HENRY:
		si_printf(value, out, "H");
		break;
	case SR_UNIT_GRAM:
		si_printf(value, out, "g");
		break;
	case SR_UNIT_CARAT:
		si_printf(value, out, "ct");
		break;
	case SR_UNIT_OUNCE:
		si_printf(value, out, "oz");
		break;
	case SR_UNIT_TROY_OUNCE:
		si_printf(value, out, "oz t");
		break;
	case SR_UNIT_POUND:
		si_printf(value, out, "lb");
		break;
	case SR_UNIT_PENNYWEIGHT:
		si_printf(value, out, "dwt");
		break;
	case SR_UNIT_GRAIN:
		si_printf(value, out, "gr");
		break;
	case SR_UNIT_TAEL:
		si_printf(value, out, "tael");
		break;
	case SR_UNIT_MOMME:
		si_printf(value, out, "momme");
		break;
	case SR_UNIT_TOLA:
		si_printf(value, out, "tola");
		break;
	case SR_UNIT_PIECE:
		si_printf(value, out, "pcs");
		break;
	default:
		si_printf(value, out, "");
		break;
	}

	/* Please use the same order as in enum sr_mqflag (libsigrok.h). */
	if (mqflags & SR_MQFLAG_AC)
		g_string_append_printf(out, " AC");
	if (mqflags & SR_MQFLAG_DC)
		g_string_append_printf(out, " DC");
	if (mqflags & SR_MQFLAG_RMS)
		g_string_append_printf(out, " RMS");
	if (mqflags & SR_MQFLAG_DIODE)
		g_string_append_printf(out, " DIODE");
	if (mqflags & SR_MQFLAG_HOLD)
		g_string_append_printf(out, " HOLD");
	if (mqflags & SR_MQFLAG_MAX)
		g_string_append_printf(out, " MAX");
	if (mqflags & SR_MQFLAG_MIN)
		g_string_append_printf(out, " MIN");
	if (mqflags & SR_MQFLAG_AUTORANGE)
		g_string_append_printf(out, " AUTO");
	if (mqflags & SR_MQFLAG_RELATIVE)
		g_string_append_printf(out, " REL");
	/* Note: SR_MQFLAG_SPL_* is handled above. */
	if (mqflags & SR_MQFLAG_DURATION)
		g_string_append_printf(out, " DURATION");
	if (mqflags & SR_MQFLAG_AVG)
		g_string_append_printf(out, " AVG");
	if (mqflags & SR_MQFLAG_REFERENCE)
		g_string_append_printf(out, " REF");
	if (mqflags & SR_MQFLAG_UNSTABLE)
		g_string_append_printf(out, " UNSTABLE");
	g_string_append_c(out, '\n');
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	struct context *ctx;
	const struct sr_datafeed_analog_old *analog_old;
	const struct sr_datafeed_analog *analog;
	struct sr_channel *ch;
	GSList *l;
	float *fdata;
	unsigned int i;
	int num_channels, c, ret, si, digits;
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
	case SR_DF_ANALOG_OLD:
		analog_old = packet->payload;
		fdata = (float *)analog_old->data;
		*out = g_string_sized_new(512);
		num_channels = g_slist_length(analog_old->channels);
		for (si = 0; si < analog_old->num_samples; si++) {
			for (l = analog_old->channels, c = 0; l; l = l->next, c++) {
				ch = l->data;
				g_string_append_printf(*out, "%s: ", ch->name);
				fancyprint(analog_old->unit, analog_old->mqflags,
						fdata[si * num_channels + c], *out);
			}
		}
		break;
	case SR_DF_ANALOG:
		analog = packet->payload;
		num_channels = g_slist_length(analog->meaning->channels);
		if (!(fdata = g_try_malloc(
						analog->num_samples * num_channels * sizeof(float))))
			return SR_ERR_MALLOC;
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
		sr_analog_unit_to_string(analog, &suffix);
		for (i = 0; i < analog->num_samples; i++) {
			for (l = analog->meaning->channels, c = 0; l; l = l->next, c++) {
				ch = l->data;
				g_string_append_printf(*out, "%s: ", ch->name);
				number = g_strdup_printf("%.*f", digits,
						fdata[i * num_channels + c]);
				g_string_append(*out, number);
				g_free(number);
				g_string_append(*out, " ");
				g_string_append(*out, suffix);
				g_string_append(*out, "\n");
			}
		}
		g_free(suffix);
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;
	ctx = o->priv;

	g_ptr_array_free(ctx->channellist, 1);
	g_free(ctx);
	o->priv = NULL;

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
