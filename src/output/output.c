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

#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output"

/**
 * @file
 *
 * Output module handling.
 */

/**
 * @defgroup grp_output Output modules
 *
 * Output module handling.
 *
 * libsigrok supports several output modules for file formats such as binary,
 * VCD, gnuplot, and so on. It provides an output API that frontends can use.
 * New output modules can be added/implemented in libsigrok without having
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
extern SR_PRIV struct sr_output_module output_bits;
extern SR_PRIV struct sr_output_module output_hex;
extern SR_PRIV struct sr_output_module output_ascii;
extern SR_PRIV struct sr_output_module output_binary;
extern SR_PRIV struct sr_output_module output_vcd;
extern SR_PRIV struct sr_output_module output_ols;
extern SR_PRIV struct sr_output_module output_gnuplot;
extern SR_PRIV struct sr_output_module output_chronovu_la8;
extern SR_PRIV struct sr_output_module output_csv;
extern SR_PRIV struct sr_output_module output_analog;
extern SR_PRIV struct sr_output_module output_wav;
/* @endcond */

static const struct sr_output_module *output_module_list[] = {
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
	&output_wav,
	NULL,
};

/**
 * Returns a NULL-terminated list of all the available output modules.
 *
 * @since 0.4.0
 */
SR_API const struct sr_output_module **sr_output_list(void)
{
	return output_module_list;
}

/**
 * Returns the specified output module's ID.
 *
 * @since 0.4.0
 */
SR_API const char *sr_output_id_get(const struct sr_output_module *o)
{
	if (!o) {
		sr_err("Invalid output module NULL!");
		return NULL;
	}

	return o->id;
}

/**
 * Returns the specified output module's name.
 *
 * @since 0.4.0
 */
SR_API const char *sr_output_name_get(const struct sr_output_module *o)
{
	if (!o) {
		sr_err("Invalid output module NULL!");
		return NULL;
	}

	return o->name;
}

/**
 * Returns the specified output module's description.
 *
 * @since 0.4.0
 */
SR_API const char *sr_output_description_get(const struct sr_output_module *o)
{
	if (!o) {
		sr_err("Invalid output module NULL!");
		return NULL;
	}

	return o->desc;
}

/**
 * Return the output module with the specified ID, or NULL if no module
 * with that id is found.
 *
 * @since 0.4.0
 */
SR_API const struct sr_output_module *sr_output_find(char *id)
{
	int i;

	for (i = 0; output_module_list[i]; i++) {
		if (!strcmp(output_module_list[i]->id, id))
			return output_module_list[i];
	}

	return NULL;
}

/**
 * Returns a NULL-terminated array of struct sr_option, or NULL if the
 * module takes no options.
 *
 * Each call to this function must be followed by a call to
 * sr_output_options_free().
 *
 * @since 0.4.0
 */
SR_API const struct sr_option *sr_output_options_get(const struct sr_output_module *o)
{

	if (!o || !o->options)
		return NULL;

	return o->options();
}

/**
 * After a call to sr_output_options_get(), this function cleans up all
 * the resources allocated by that call.
 *
 * @since 0.4.0
 */
SR_API void sr_output_options_free(const struct sr_output_module *o)
{
	struct sr_option *opt;

	if (!o || !o->options)
		return;

	for (opt = o->options(); opt->id; opt++) {
		if (opt->def) {
			g_variant_unref(opt->def);
			opt->def = NULL;
		}

		if (opt->values) {
			g_slist_free_full(opt->values, (GDestroyNotify)g_variant_unref);
			opt->values = NULL;
		}
	}
}

/**
 * Create a new output instance using the specified output module.
 *
 * <code>options</code> is a *HashTable with the keys corresponding with
 * the module options' <code>id</code> field. The values should be GVariant
 * pointers with sunk * references, of the same GVariantType as the option's
 * default value.
 *
 * The sr_dev_inst passed in can be used by the instance to determine
 * channel names, samplerate, and so on.
 *
 * @since 0.4.0
 */
SR_API const struct sr_output *sr_output_new(const struct sr_output_module *o,
		GHashTable *options, const struct sr_dev_inst *sdi)
{
	struct sr_output *op;

	op = g_malloc(sizeof(struct sr_output));
	op->module = o;
	op->sdi = sdi;

	if (op->module->init && op->module->init(op, options) != SR_OK) {
		g_free(op);
		op = NULL;
	}

	return op;
}

/**
 * Send a packet to the specified output instance.
 *
 * The instance's output is returned as a newly allocated GString,
 * which must be freed by the caller.
 *
 * @since 0.4.0
 */
SR_API int sr_output_send(const struct sr_output *o,
		const struct sr_datafeed_packet *packet, GString **out)
{
	return o->module->receive(o, packet, out);
}

/**
 * Free the specified output instance and all associated resources.
 *
 * @since 0.4.0
 */
SR_API int sr_output_free(const struct sr_output *o)
{
	int ret;

	if (!o)
		return SR_ERR_ARG;

	ret = SR_OK;
	if (o->module->cleanup)
		ret = o->module->cleanup((struct sr_output *)o);
	g_free((gpointer)o);

	return ret;
}

/** @} */
