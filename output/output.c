/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"

/**
 * @file
 *
 * Output file/data format handling.
 */

/**
 * @defgroup grp_output Output formats
 *
 * Output file/data format handling.
 *
 * libsigrok supports several output (file) formats, e.g. binary, VCD,
 * gnuplot, and so on. It provides an output API that frontends can use.
 * New output formats can be added/implemented in libsigrok without having
 * to change the frontends at all.
 *
 * All output modules are fed data in a stream. Devices that can stream data
 * into libsigrok, instead of storing and then transferring the whole buffer,
 * can thus generate output live.
 *
 * Output modules generate a newly allocated GString. The caller is then
 * expected to free this with g_string_free() when finished with it.
 *
 * @{
 */

/** @cond PRIVATE */
extern SR_PRIV struct sr_output_format output_bits;
extern SR_PRIV struct sr_output_format output_hex;
extern SR_PRIV struct sr_output_format output_ascii;
extern SR_PRIV struct sr_output_format output_binary;
extern SR_PRIV struct sr_output_format output_vcd;
extern SR_PRIV struct sr_output_format output_ols;
extern SR_PRIV struct sr_output_format output_gnuplot;
extern SR_PRIV struct sr_output_format output_chronovu_la8;
extern SR_PRIV struct sr_output_format output_csv;
extern SR_PRIV struct sr_output_format output_analog;
/* @endcond */

static struct sr_output_format *output_module_list[] = {
	&output_ascii,
	&output_binary,
	&output_bits,
	&output_csv,
	&output_gnuplot,
	&output_hex,
	&output_ols,
	&output_vcd,
	&output_chronovu_la8,
	&output_analog,
	NULL,
};

SR_API struct sr_output_format **sr_output_list(void)
{
	return output_module_list;
}

SR_API struct sr_output *sr_output_new(struct sr_output_format *of,
		GHashTable *params, const struct sr_dev_inst *sdi)
{
	struct sr_output *o;

	o = g_malloc(sizeof(struct sr_output));
	o->format = of;
	o->sdi = sdi;
	o->params = params;
	if (o->format->init && o->format->init(o) != SR_OK) {
		g_free(o);
		o = NULL;
	}

	return o;
}

SR_API int sr_output_send(struct sr_output *o,
		const struct sr_datafeed_packet *packet, GString **out)
{
	return o->format->receive(o, packet, out);
}

SR_API int sr_output_free(struct sr_output *o)
{
	int ret;

	ret = SR_OK;
	if (o->format->cleanup)
		ret = o->format->cleanup(o);
	g_free(o);

	return ret;
}


/** @} */
