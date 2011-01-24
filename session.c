/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <zip.h>
#include <sigrok.h>
#include <config.h>


/* There can only be one session at a time. */
struct session *session;

struct session *session_load(const char *filename)
{
	struct session *session;

	/* Avoid compiler warnings. */
	filename = filename;

	/* TODO: Implement. */
	session = NULL;

	return session;
}

struct session *session_new(void)
{
	session = calloc(1, sizeof(struct session));

	return session;
}

void session_destroy(void)
{
	g_slist_free(session->devices);

	/* TODO: Loop over protocol decoders and free them. */

	g_free(session);
}

void session_device_clear(void)
{
	g_slist_free(session->devices);
	session->devices = NULL;
}

int session_device_add(struct device *device)
{
	int ret;

	if (device->plugin && device->plugin->open) {
		ret = device->plugin->open(device->plugin_index);
		if (ret != SIGROK_OK)
			return ret;
	}

	session->devices = g_slist_append(session->devices, device);

	return SIGROK_OK;
}

void session_pa_clear(void)
{
	/*
	 * The protocols are pointers to the global set of PA plugins,
	 * so don't free them.
	 */
	g_slist_free(session->analyzers);
	session->analyzers = NULL;
}

void session_pa_add(struct analyzer *an)
{
	session->analyzers = g_slist_append(session->analyzers, an);
}

void session_datafeed_callback_clear(void)
{
	g_slist_free(session->datafeed_callbacks);
	session->datafeed_callbacks = NULL;
}

void session_datafeed_callback_add(datafeed_callback callback)
{
	session->datafeed_callbacks =
	    g_slist_append(session->datafeed_callbacks, callback);
}

int session_start(void)
{
	struct device *device;
	GSList *l;
	int ret;

	g_message("starting acquisition");
	for (l = session->devices; l; l = l->next) {
		device = l->data;
		if ((ret = device->plugin->start_acquisition(
				device->plugin_index, device)) != SIGROK_OK)
			break;
	}

	return ret;
}

void session_stop(void)
{
	struct device *device;
	GSList *l;

	g_message("stopping acquisition");
	for (l = session->devices; l; l = l->next) {
		device = l->data;
		if (device->plugin)
			device->plugin->stop_acquisition(device->plugin_index, device);
	}
}

void session_bus(struct device *device, struct datafeed_packet *packet)
{
	GSList *l;
	datafeed_callback cb;

	/*
	 * TODO: Send packet through PD pipe, and send the output of that to
	 * the callbacks as well.
	 */
	for (l = session->datafeed_callbacks; l; l = l->next) {
		cb = l->data;
		cb(device, packet);
	}
}

int session_save(char *filename)
{
	GSList *l, *p, *d;
	FILE *meta;
	struct device *device;
	struct probe *probe;
	struct datastore *ds;
	struct zip *zipfile;
	struct zip_source *versrc, *metasrc, *logicsrc;
	int bufcnt, devcnt, tmpfile, ret, error;
	char version[1], rawname[16], metafile[32], *newfn, *buf;

	newfn = g_malloc(strlen(filename) + 10);
	strcpy(newfn, filename);
	if (strstr(filename, ".sigrok") != filename+strlen(filename)-7)
		strcat(newfn, ".sigrok");

	/* Quietly delete it first, libzip wants replace ops otherwise. */
	unlink(newfn);
	if (!(zipfile = zip_open(newfn, ZIP_CREATE, &error)))
		return SIGROK_ERR;
	g_free(newfn);

	/* "version" */
	version[0] = '1';
	if (!(versrc = zip_source_buffer(zipfile, version, 1, 0)))
		return SIGROK_ERR;
	if (zip_add(zipfile, "version", versrc) == -1) {
		g_message("error saving version into zipfile: %s",
			  zip_strerror(zipfile));
		return SIGROK_ERR;
	}

	/* init "metadata" */
	strcpy(metafile, "sigrok-meta-XXXXXX");
	if ((tmpfile = g_mkstemp(metafile)) == -1)
		return SIGROK_ERR;
	close(tmpfile);
	meta = fopen(metafile, "wb");
	fprintf(meta, "sigrok version = %s\n", PACKAGE_VERSION);
	/* TODO: save protocol decoders used */

	/* all datastores in all devices */
	devcnt = 1;
	for (l = session->devices; l; l = l->next) {
		device = l->data;
		/* metadata */
		fprintf(meta, "[device]\n");
		if (device->plugin)
			fprintf(meta, "driver = %s\n", device->plugin->name);

		ds = device->datastore;
		if (ds) {
			/* metadata */
			fprintf(meta, "capturefile = logic-%d\n", devcnt);
			for (p = device->probes; p; p = p->next) {
				probe = p->data;
				if (probe->enabled) {
					fprintf(meta, "probe %d", probe->index);
					if (probe->name)
						fprintf(meta, " name \"%s\"", probe->name);
					if (probe->trigger)
						fprintf(meta, " trigger \"%s\"",
							probe->trigger);
					fprintf(meta, "\n");
				}
			}

			/* dump datastore into logic-n */
			buf = malloc(ds->num_units * ds->ds_unitsize +
				   DATASTORE_CHUNKSIZE);
			bufcnt = 0;
			for (d = ds->chunklist; d; d = d->next) {
				memcpy(buf + bufcnt, d->data,
				       DATASTORE_CHUNKSIZE);
				bufcnt += DATASTORE_CHUNKSIZE;
			}
			if (!(logicsrc = zip_source_buffer(zipfile, buf,
				       ds->num_units * ds->ds_unitsize, TRUE)))
				return SIGROK_ERR;
			snprintf(rawname, 15, "logic-%d", devcnt);
			if (zip_add(zipfile, rawname, logicsrc) == -1)
				return SIGROK_ERR;
		}
		devcnt++;
	}
	fclose(meta);

	if (!(metasrc = zip_source_file(zipfile, metafile, 0, -1)))
		return SIGROK_ERR;
	if (zip_add(zipfile, "metadata", metasrc) == -1)
		return SIGROK_ERR;

	if ((ret = zip_close(zipfile)) == -1) {
		g_message("error saving zipfile: %s", zip_strerror(zipfile));
		return SIGROK_ERR;
	}

	unlink(metafile);

	return SIGROK_OK;
}
