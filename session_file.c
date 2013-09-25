/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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
#include <stdlib.h>
#include <unistd.h>
#include <zip.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "config.h" /* Needed for PACKAGE_VERSION and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "session-file: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

/**
 * @file
 *
 * Loading and saving libsigrok session files.
 */

/**
 * @addtogroup grp_session
 *
 * @{
 */

extern struct sr_session *session;
extern SR_PRIV struct sr_dev_driver session_driver;

/** @private */
SR_PRIV int sr_sessionfile_check(const char *filename)
{
	struct zip *archive;
	struct zip_file *zf;
	struct zip_stat zs;
	int version, ret;
	char s[11];

	if (!filename)
		return SR_ERR_ARG;

	if (!(archive = zip_open(filename, 0, &ret)))
		/* No logging: this can be used just to check if it's
		 * a sigrok session file or not. */
		return SR_ERR;

	/* check "version" */
	version = 0;
	if (!(zf = zip_fopen(archive, "version", 0))) {
		sr_dbg("Not a sigrok session file: no version found.");
		return SR_ERR;
	}
	if ((ret = zip_fread(zf, s, 10)) == -1)
		return SR_ERR;
	zip_fclose(zf);
	s[ret] = 0;
	version = strtoull(s, NULL, 10);
	if (version != 1) {
		sr_dbg("Cannot handle sigrok session file version %d.", version);
		return SR_ERR;
	}

	/* read "metadata" */
	if (zip_stat(archive, "metadata", 0, &zs) == -1) {
		sr_dbg("Not a valid sigrok session file.");
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * Load the session from the specified filename.
 *
 * @param filename The name of the session file to load. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments,
 *         SR_ERR_MALLOC upon memory allocation errors, or SR_ERR upon
 *         other errors.
 */
SR_API int sr_session_load(const char *filename)
{
	GKeyFile *kf;
	GPtrArray *capturefiles;
	struct zip *archive;
	struct zip_file *zf;
	struct zip_stat zs;
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	int ret, probenum, devcnt, i, j;
	uint64_t tmp_u64, total_probes, enabled_probes, p;
	char **sections, **keys, *metafile, *val;
	char probename[SR_MAX_PROBENAME_LEN + 1];

	if ((ret = sr_sessionfile_check(filename)) != SR_OK)
		return ret;

	if (!(archive = zip_open(filename, 0, &ret)))
		return SR_ERR;

	if (zip_stat(archive, "metadata", 0, &zs) == -1)
		return SR_ERR;

	if (!(metafile = g_try_malloc(zs.size))) {
		sr_err("%s: metafile malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	zf = zip_fopen_index(archive, zs.index, 0);
	zip_fread(zf, metafile, zs.size);
	zip_fclose(zf);

	kf = g_key_file_new();
	if (!g_key_file_load_from_data(kf, metafile, zs.size, 0, NULL)) {
		sr_dbg("Failed to parse metadata.");
		return SR_ERR;
	}

	sr_session_new();

	devcnt = 0;
	capturefiles = g_ptr_array_new_with_free_func(g_free);
	sections = g_key_file_get_groups(kf, NULL);
	for (i = 0; sections[i]; i++) {
		if (!strcmp(sections[i], "global"))
			/* nothing really interesting in here yet */
			continue;
		if (!strncmp(sections[i], "device ", 7)) {
			/* device section */
			sdi = NULL;
			enabled_probes = total_probes = 0;
			keys = g_key_file_get_keys(kf, sections[i], NULL, NULL);
			for (j = 0; keys[j]; j++) {
				val = g_key_file_get_string(kf, sections[i], keys[j], NULL);
				if (!strcmp(keys[j], "capturefile")) {
					sdi = sr_dev_inst_new(devcnt, SR_ST_ACTIVE, NULL, NULL, NULL);
					sdi->driver = &session_driver;
					if (devcnt == 0)
						/* first device, init the driver */
						sdi->driver->init(NULL);
					sr_dev_open(sdi);
					sr_session_dev_add(sdi);
					sdi->driver->config_set(SR_CONF_SESSIONFILE,
							g_variant_new_string(filename), sdi);
					sdi->driver->config_set(SR_CONF_CAPTUREFILE,
							g_variant_new_string(val), sdi);
					g_ptr_array_add(capturefiles, val);
				} else if (!strcmp(keys[j], "samplerate")) {
					sr_parse_sizestring(val, &tmp_u64);
					sdi->driver->config_set(SR_CONF_SAMPLERATE,
							g_variant_new_uint64(tmp_u64), sdi);
				} else if (!strcmp(keys[j], "unitsize")) {
					tmp_u64 = strtoull(val, NULL, 10);
					sdi->driver->config_set(SR_CONF_CAPTURE_UNITSIZE,
							g_variant_new_uint64(tmp_u64), sdi);
				} else if (!strcmp(keys[j], "total probes")) {
					total_probes = strtoull(val, NULL, 10);
					sdi->driver->config_set(SR_CONF_CAPTURE_NUM_PROBES,
							g_variant_new_uint64(total_probes), sdi);
					for (p = 0; p < total_probes; p++) {
						snprintf(probename, SR_MAX_PROBENAME_LEN, "%" PRIu64, p);
						if (!(probe = sr_probe_new(p, SR_PROBE_LOGIC, TRUE,
								probename)))
							return SR_ERR;
						sdi->probes = g_slist_append(sdi->probes, probe);
					}
				} else if (!strncmp(keys[j], "probe", 5)) {
					if (!sdi)
						continue;
					enabled_probes++;
					tmp_u64 = strtoul(keys[j]+5, NULL, 10);
					/* sr_session_save() */
					sr_dev_probe_name_set(sdi, tmp_u64 - 1, val);
				} else if (!strncmp(keys[j], "trigger", 7)) {
					probenum = strtoul(keys[j]+7, NULL, 10);
					sr_dev_trigger_set(sdi, probenum, val);
				}
			}
			g_strfreev(keys);
			/* Disable probes not specifically listed. */
			if (total_probes)
				for (p = enabled_probes; p < total_probes; p++)
					sr_dev_probe_enable(sdi, p, FALSE);
		}
		devcnt++;
	}
	g_strfreev(sections);
	g_key_file_free(kf);

	return SR_OK;
}

/**
 * Save the current session to the specified file.
 *
 * @param filename The name of the filename to save the current session as.
 *                 Must not be NULL.
 * @param sdi The device instance from which the data was captured.
 * @param buf The data to be saved.
 * @param unitsize The number of bytes per sample.
 * @param units The number of samples.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 */
SR_API int sr_session_save(const char *filename, const struct sr_dev_inst *sdi,
		unsigned char *buf, int unitsize, int units)
{
	GSList *l;
	GVariant *gvar;
	FILE *meta;
	struct sr_probe *probe;
	struct zip *zipfile;
	struct zip_source *versrc, *metasrc, *logicsrc;
	int tmpfile, ret, probecnt;
	uint64_t samplerate;
	char version[1], rawname[16], metafile[32], *s;

	if (!filename) {
		sr_err("%s: filename was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Quietly delete it first, libzip wants replace ops otherwise. */
	unlink(filename);
	if (!(zipfile = zip_open(filename, ZIP_CREATE, &ret)))
		return SR_ERR;

	/* "version" */
	version[0] = '1';
	if (!(versrc = zip_source_buffer(zipfile, version, 1, 0)))
		return SR_ERR;
	if (zip_add(zipfile, "version", versrc) == -1) {
		sr_info("error saving version into zipfile: %s",
			zip_strerror(zipfile));
		return SR_ERR;
	}

	/* init "metadata" */
	strcpy(metafile, "sigrok-meta-XXXXXX");
	if ((tmpfile = g_mkstemp(metafile)) == -1)
		return SR_ERR;
	close(tmpfile);
	meta = g_fopen(metafile, "wb");
	fprintf(meta, "[global]\n");
	fprintf(meta, "sigrok version = %s\n", PACKAGE_VERSION);

	/* metadata */
	fprintf(meta, "[device 1]\n");
	if (sdi->driver)
		fprintf(meta, "driver = %s\n", sdi->driver->name);

	/* metadata */
	fprintf(meta, "capturefile = logic-1\n");
	fprintf(meta, "unitsize = %d\n", unitsize);
	fprintf(meta, "total probes = %d\n", g_slist_length(sdi->probes));
	if (sr_dev_has_option(sdi, SR_CONF_SAMPLERATE)) {
		if (sr_config_get(sdi->driver, SR_CONF_SAMPLERATE,
				&gvar, sdi) == SR_OK) {
			samplerate = g_variant_get_uint64(gvar);
			s = sr_samplerate_string(samplerate);
			fprintf(meta, "samplerate = %s\n", s);
			g_free(s);
			g_variant_unref(gvar);
		}
	}
	probecnt = 1;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->enabled) {
			if (probe->name)
				fprintf(meta, "probe%d = %s\n", probecnt, probe->name);
			if (probe->trigger)
				fprintf(meta, " trigger%d = %s\n", probecnt, probe->trigger);
			probecnt++;
		}
	}

	if (!(logicsrc = zip_source_buffer(zipfile, buf,
			   units * unitsize, FALSE)))
		return SR_ERR;
	snprintf(rawname, 15, "logic-1");
	if (zip_add(zipfile, rawname, logicsrc) == -1)
		return SR_ERR;
	fclose(meta);

	if (!(metasrc = zip_source_file(zipfile, metafile, 0, -1)))
		return SR_ERR;
	if (zip_add(zipfile, "metadata", metasrc) == -1)
		return SR_ERR;

	if ((ret = zip_close(zipfile)) == -1) {
		sr_info("error saving zipfile: %s", zip_strerror(zipfile));
		return SR_ERR;
	}

	unlink(metafile);

	return SR_OK;
}

/**
 * Append data to an existing session file.
 *
 * @param filename The name of the filename to append to. Must not be NULL.
 * @param buf The data to be appended.
 * @param unitsize The number of bytes per sample.
 * @param units The number of samples.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 */
SR_API int sr_session_append(const char *filename, unsigned char *buf,
		int unitsize, int units)
{
	struct zip *archive;
	struct zip_source *logicsrc;
	zip_int64_t num_files;
	int chunk_num, next_chunk_num, ret, i;
	const char *entry_name;
	char chunkname[16];

	if ((ret = sr_sessionfile_check(filename)) != SR_OK)
		return ret;

	if (!(archive = zip_open(filename, 0, &ret)))
		return SR_ERR;

	next_chunk_num = 1;
	num_files = zip_get_num_entries(archive, 0);
	for (i = 0; i < num_files; i++) {
		entry_name = zip_get_name(archive, i, 0);
		if (strncmp(entry_name, "logic-1", 7))
			continue;
		if (strlen(entry_name) == 7) {
			/* This file has no extra chunks, just a single "logic-1".
			 * Rename it to "logic-1-1" * and continue with chunk 2. */
			if (zip_rename(archive, i, "logic-1-1") == -1) {
				sr_err("Failed to rename 'logic-1' to 'logic-1-1'.");
				return SR_ERR;
			}
			next_chunk_num = 2;
			break;
		} else if (strlen(entry_name) > 8 && entry_name[7] == '-') {
			chunk_num = strtoull(entry_name + 8, NULL, 10);
			if (chunk_num >= next_chunk_num)
				next_chunk_num = chunk_num + 1;
		}
	}
	snprintf(chunkname, 15, "logic-1-%d", next_chunk_num);
	if (!(logicsrc = zip_source_buffer(archive, buf, units * unitsize, FALSE)))
		return SR_ERR;
	if (zip_add(archive, chunkname, logicsrc) == -1)
		return SR_ERR;
	if ((ret = zip_close(archive)) == -1) {
		sr_info("error saving session file: %s", zip_strerror(archive));
		return SR_ERR;
	}

	return SR_OK;
}

/** @} */
