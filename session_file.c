/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <zip.h>
#include <glib.h>
#include <sigrok.h>

extern struct session *session;
extern struct sr_device_plugin session_driver;


int session_load(const char *filename)
{
	GKeyFile *kf;
	GPtrArray *capturefiles;
	struct zip *archive;
	struct zip_file *zf;
	struct zip_stat zs;
	struct session *session;
	struct sr_device *device;
	struct probe *probe;
	int ret, err, probenum, devcnt, i, j;
	uint64_t tmp_u64, total_probes, enabled_probes, p;
	char **sections, **keys, *metafile, *val, c;

	if (!(archive = zip_open(filename, 0, &err))) {
		g_debug("Failed to open session file: zip error %d", err);
		return SR_ERR;
	}

	/* check "version" */
	if (!(zf = zip_fopen(archive, "version", 0))) {
		g_debug("Not a sigrok session file.");
		return SR_ERR;
	}
	ret = zip_fread(zf, &c, 1);
	if (ret != 1 || c != '1') {
		g_debug("Not a valid sigrok session file.");
		return SR_ERR;
	}
	zip_fclose(zf);

	/* read "metadata" */
	if (zip_stat(archive, "metadata", 0, &zs) == -1) {
		g_debug("Not a valid sigrok session file.");
		return SR_ERR;
	}
	metafile = g_malloc(zs.size);
	zf = zip_fopen_index(archive, zs.index, 0);
	zip_fread(zf, metafile, zs.size);
	zip_fclose(zf);

	kf = g_key_file_new();
	if (!g_key_file_load_from_data(kf, metafile, zs.size, 0, NULL)) {
		g_debug("Failed to parse metadata.");
		return SR_ERR;
	}

	session = session_new();

	devcnt = 0;
	capturefiles = g_ptr_array_new_with_free_func(g_free);
	sections = g_key_file_get_groups(kf, NULL);
	for (i = 0; sections[i]; i++) {
		if (!strcmp(sections[i], "global"))
			/* nothing really interesting in here yet */
			continue;
		if (!strncmp(sections[i], "device ", 7)) {
			/* device section */
			device = NULL;
			enabled_probes = 0;
			keys = g_key_file_get_keys(kf, sections[i], NULL, NULL);
			for (j = 0; keys[j]; j++) {
				val = g_key_file_get_string(kf, sections[i], keys[j], NULL);
				if (!strcmp(keys[j], "capturefile")) {
					device = device_new(&session_driver, devcnt, 0);
					if (devcnt == 0)
						/* first device, init the plugin */
						device->plugin->init((char *)filename);
					session_device_add(device);
					device->plugin->set_configuration(devcnt, SR_HWCAP_CAPTUREFILE, val);
					g_ptr_array_add(capturefiles, val);
				} else if (!strcmp(keys[j], "samplerate")) {
					tmp_u64 = sr_parse_sizestring(val);
					device->plugin->set_configuration(devcnt, SR_HWCAP_SAMPLERATE, &tmp_u64);
				} else if (!strcmp(keys[j], "unitsize")) {
					tmp_u64 = strtoull(val, NULL, 10);
					device->plugin->set_configuration(devcnt, SR_HWCAP_CAPTURE_UNITSIZE, &tmp_u64);
				} else if (!strcmp(keys[j], "total probes")) {
					total_probes = strtoull(val, NULL, 10);
					device->plugin->set_configuration(devcnt, SR_HWCAP_CAPTURE_NUM_PROBES, &total_probes);
					for (p = 1; p <= total_probes; p++)
						device_probe_add(device, NULL);
				} else if (!strncmp(keys[j], "probe", 5)) {
					if (!device)
						continue;
					enabled_probes++;
					tmp_u64 = strtoul(keys[j]+5, NULL, 10);
					device_probe_name(device, tmp_u64, val);
				} else if (!strncmp(keys[j], "trigger", 7)) {
					probenum = strtoul(keys[j]+7, NULL, 10);
					device_trigger_set(device, probenum, val);
				}
			}
			g_strfreev(keys);
			for (p = enabled_probes; p < total_probes; p++) {
				probe = g_slist_nth_data(device->probes, p);
				probe->enabled = FALSE;
			}
		}
	}
	g_strfreev(sections);
	g_key_file_free(kf);

	return SR_OK;
}

int session_save(char *filename)
{
	GSList *l, *p, *d;
	FILE *meta;
	struct sr_device *device;
	struct probe *probe;
	struct datastore *ds;
	struct zip *zipfile;
	struct zip_source *versrc, *metasrc, *logicsrc;
	int bufcnt, devcnt, tmpfile, ret, error, probecnt;
	uint64_t samplerate;
	char version[1], rawname[16], metafile[32], *buf, *s;

	/* Quietly delete it first, libzip wants replace ops otherwise. */
	unlink(filename);
	if (!(zipfile = zip_open(filename, ZIP_CREATE, &error)))
		return SR_ERR;

	/* "version" */
	version[0] = '1';
	if (!(versrc = zip_source_buffer(zipfile, version, 1, 0)))
		return SR_ERR;
	if (zip_add(zipfile, "version", versrc) == -1) {
		g_message("error saving version into zipfile: %s",
			  zip_strerror(zipfile));
		return SR_ERR;
	}

	/* init "metadata" */
	strcpy(metafile, "sigrok-meta-XXXXXX");
	if ((tmpfile = g_mkstemp(metafile)) == -1)
		return SR_ERR;
	close(tmpfile);
	meta = fopen(metafile, "wb");
	fprintf(meta, "[global]\n");
	fprintf(meta, "sigrok version = %s\n", PACKAGE_VERSION);
	/* TODO: save protocol decoders used */

	/* all datastores in all devices */
	devcnt = 1;
	for (l = session->devices; l; l = l->next) {
		device = l->data;
		/* metadata */
		fprintf(meta, "[device %d]\n", devcnt);
		if (device->plugin)
			fprintf(meta, "driver = %s\n", device->plugin->name);

		ds = device->datastore;
		if (ds) {
			/* metadata */
			fprintf(meta, "capturefile = logic-%d\n", devcnt);
			fprintf(meta, "unitsize = %d\n", ds->ds_unitsize);
			fprintf(meta, "total probes = %d\n", g_slist_length(device->probes));
			if (device_has_hwcap(device, SR_HWCAP_SAMPLERATE)) {
				samplerate = *((uint64_t *) device->plugin->get_device_info(
						device->plugin_index, SR_DI_CUR_SAMPLERATE));
				s = sr_samplerate_string(samplerate);
				fprintf(meta, "samplerate = %s\n", s);
				free(s);
			}
			probecnt = 1;
			for (p = device->probes; p; p = p->next) {
				probe = p->data;
				if (probe->enabled) {
					if (probe->name)
						fprintf(meta, "probe%d = %s\n", probecnt, probe->name);
					if (probe->trigger)
						fprintf(meta, " trigger%d = %s\n", probecnt, probe->trigger);
					probecnt++;
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
				return SR_ERR;
			snprintf(rawname, 15, "logic-%d", devcnt);
			if (zip_add(zipfile, rawname, logicsrc) == -1)
				return SR_ERR;
		}
		devcnt++;
	}
	fclose(meta);

	if (!(metasrc = zip_source_file(zipfile, metafile, 0, -1)))
		return SR_ERR;
	if (zip_add(zipfile, "metadata", metasrc) == -1)
		return SR_ERR;

	if ((ret = zip_close(zipfile)) == -1) {
		g_message("error saving zipfile: %s", zip_strerror(zipfile));
		return SR_ERR;
	}

	unlink(metafile);

	return SR_OK;
}

