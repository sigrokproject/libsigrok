/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <check.h>
#include "../libsigrok.h"
#include "lib.h"

/* Get a libsigrok driver by name. */
struct sr_dev_driver *srtest_driver_get(const char *drivername)
{
	struct sr_dev_driver **drivers, *driver = NULL;
	int i;

	drivers = sr_driver_list();
	fail_unless(drivers != NULL, "No drivers found.");

	for (i = 0; drivers[i]; i++) {
		if (strcmp(drivers[i]->name, drivername))
			continue;
		driver = drivers[i];
	}
	fail_unless(driver != NULL, "Driver '%s' not found.", drivername);

	return driver;
}

/* Get a libsigrok input format by ID. */
struct sr_input_format *srtest_input_get(const char *id)
{
	struct sr_input_format **inputs, *input = NULL;
	int i;

	inputs = sr_input_list();
	fail_unless(inputs != NULL, "No input modules found.");

	for (i = 0; inputs[i]; i++) {
		if (strcmp(inputs[i]->id, id))
			continue;
		input = inputs[i];
	}
	fail_unless(input != NULL, "Input module '%s' not found.", id);

	return input;
}

/* Get a libsigrok output format by ID. */
struct sr_output_format *srtest_output_get(const char *id)
{
	struct sr_output_format **outputs, *output = NULL;
	int i;

	outputs = sr_output_list();
	fail_unless(outputs != NULL, "No output modules found.");

	for (i = 0; outputs[i]; i++) {
		if (strcmp(outputs[i]->id, id))
			continue;
		output = outputs[i];
	}
	fail_unless(output != NULL, "Output module '%s' not found.", id);

	return output;
}

/* Initialize a libsigrok driver. */
void srtest_driver_init(struct sr_context *sr_ctx, struct sr_dev_driver *driver)
{
	int ret;

	ret = sr_driver_init(sr_ctx, driver);
	fail_unless(ret == SR_OK, "Failed to init '%s' driver: %d.",
		    driver->name, ret);
}

/* Initialize all libsigrok drivers. */
void srtest_driver_init_all(struct sr_context *sr_ctx)
{
	struct sr_dev_driver **drivers, *driver;
	int i, ret;

	drivers = sr_driver_list();
	fail_unless(drivers != NULL, "No drivers found.");

	for (i = 0; drivers[i]; i++) {
		driver = drivers[i];
		ret = sr_driver_init(sr_ctx, driver);
		fail_unless(ret == SR_OK, "Failed to init '%s' driver: %d.",
			    driver->name, ret);
	}
}

/* Initialize a libsigrok input module. */
void srtest_input_init(struct sr_context *sr_ctx, struct sr_input_format *input)
{
	int ret;
	struct sr_input *in;

	(void)sr_ctx;

	in = g_try_malloc0(sizeof(struct sr_input));
	fail_unless(in != NULL);

	in->format = input;
	in->param = NULL;

	ret = in->format->init(in, "nonexisting.dat");
	fail_unless(ret == SR_OK, "Failed to init '%s' input module: %d.",
		    input->id, ret);

	g_free(in);
}

/* Initialize all libsigrok input modules. */
void srtest_input_init_all(struct sr_context *sr_ctx)
{
	struct sr_input_format **inputs;
	int i;

	inputs = sr_input_list();
	fail_unless(inputs != NULL, "No input modules found.");

	for (i = 0; inputs[i]; i++)
		srtest_input_init(sr_ctx, inputs[i]);
}

/* Set the samplerate for the respective driver to the specified value. */
void srtest_set_samplerate(struct sr_dev_driver *driver, uint64_t samplerate)
{
	int ret;
	struct sr_dev_inst *sdi;
	GVariant *gvar;

	sdi = g_slist_nth_data(driver->priv, 0);

	gvar = g_variant_new_uint64(samplerate);
	ret = driver->config_set(SR_CONF_SAMPLERATE, gvar, sdi, NULL);
	g_variant_unref(gvar);

	fail_unless(ret == SR_OK, "%s: Failed to set SR_CONF_SAMPLERATE: %d.",
		    driver->name, ret);
}

/* Get the respective driver's current samplerate. */
uint64_t srtest_get_samplerate(struct sr_dev_driver *driver)
{
	int ret;
	uint64_t samplerate;
	struct sr_dev_inst *sdi;
	GVariant *gvar;

	sdi = g_slist_nth_data(driver->priv, 0);

	ret = driver->config_get(SR_CONF_SAMPLERATE, &gvar, sdi, NULL);
	samplerate = g_variant_get_uint64(gvar);
	g_variant_unref(gvar);

	fail_unless(ret == SR_OK, "%s: Failed to get SR_CONF_SAMPLERATE: %d.",
		    driver->name, ret);

	return samplerate;
}

/* Check whether the respective driver can set/get the correct samplerate. */
void srtest_check_samplerate(struct sr_context *sr_ctx, const char *drivername,
			     uint64_t samplerate)
{
	struct sr_dev_driver *driver;
	uint64_t s;

	driver = srtest_driver_get(drivername);
	srtest_driver_init(sr_ctx, driver);;
	srtest_set_samplerate(driver, samplerate);
	s = srtest_get_samplerate(driver);
	fail_unless(s == samplerate, "%s: Incorrect samplerate: %" PRIu64 ".",
		    drivername, s);
}

void srtest_buf_to_file(const char *filename, const uint8_t *buf, uint64_t len)
{
	FILE *f;
	GError *error = NULL;
	gboolean ret;

	f = g_fopen(filename, "wb");
	fail_unless(f != NULL);

	ret = g_file_set_contents(filename, (const gchar *)buf, len, &error);
	fail_unless(ret == TRUE);

	fclose(f);
}

GArray *srtest_get_enabled_logic_probes(const struct sr_dev_inst *sdi)
{
	struct sr_channel *ch;
	GArray *probes;
	GSList *l;

	probes = g_array_new(FALSE, FALSE, sizeof(int));
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->type != SR_CHANNEL_LOGIC)
			continue;
		if (probe->enabled != TRUE)
			continue;
		g_array_append_val(probes, probe->index);
	}

	return probes;
}
