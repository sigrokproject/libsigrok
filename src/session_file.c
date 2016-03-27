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

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <zip.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "session-file"
/** @endcond */

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

extern SR_PRIV struct sr_dev_driver session_driver;
static int session_driver_initialized = 0;

#if !HAVE_ZIP_DISCARD
/* Replacement for zip_discard() if it isn't available.
 */
SR_PRIV void sr_zip_discard(struct zip *archive)
{
	if (zip_unchange_all(archive) < 0 || zip_close(archive) < 0)
		sr_err("Failed to discard ZIP archive: %s", zip_strerror(archive));
}
#endif

/** Read metadata entries from a session archive.
 * @param[in] archive An open ZIP archive.
 * @param[in] entry Stat buffer filled in for the metadata archive member.
 * @return A new key/value store containing the session metadata.
 */
SR_PRIV GKeyFile *sr_sessionfile_read_metadata(struct zip *archive,
			const struct zip_stat *entry)
{
	GKeyFile *keyfile;
	GError *error;
	struct zip_file *zf;
	char *metabuf;
	int metalen;

	if (entry->size > G_MAXINT || !(metabuf = g_try_malloc(entry->size))) {
		sr_err("Metadata buffer allocation failed.");
		return NULL;
	}
	zf = zip_fopen_index(archive, entry->index, 0);
	if (!zf) {
		sr_err("Failed to open metadata: %s", zip_strerror(archive));
		g_free(metabuf);
		return NULL;
	}
	metalen = zip_fread(zf, metabuf, entry->size);
	if (metalen < 0) {
		sr_err("Failed to read metadata: %s", zip_file_strerror(zf));
		zip_fclose(zf);
		g_free(metabuf);
		return NULL;
	}
	zip_fclose(zf);

	keyfile = g_key_file_new();
	error = NULL;
	g_key_file_load_from_data(keyfile, metabuf, metalen,
			G_KEY_FILE_NONE, &error);
	g_free(metabuf);

	if (error) {
		sr_err("Failed to parse metadata: %s", error->message);
		g_error_free(error);
		g_key_file_free(keyfile);
		return NULL;
	}
	return keyfile;
}

/** @private */
SR_PRIV int sr_sessionfile_check(const char *filename)
{
	struct zip *archive;
	struct zip_file *zf;
	struct zip_stat zs;
	uint64_t version;
	int ret;
	char s[11];

	if (!filename)
		return SR_ERR_ARG;

	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
		sr_err("Not a regular file: %s.", filename);
		return SR_ERR;
	}

	if (!(archive = zip_open(filename, 0, NULL)))
		/* No logging: this can be used just to check if it's
		 * a sigrok session file or not. */
		return SR_ERR;

	/* check "version" */
	if (!(zf = zip_fopen(archive, "version", 0))) {
		sr_dbg("Not a sigrok session file: no version found.");
		zip_discard(archive);
		return SR_ERR;
	}
	ret = zip_fread(zf, s, sizeof(s) - 1);
	if (ret < 0) {
		sr_err("Failed to read version file: %s",
			zip_file_strerror(zf));
		zip_fclose(zf);
		zip_discard(archive);
		return SR_ERR;
	}
	zip_fclose(zf);
	s[ret] = '\0';
	version = g_ascii_strtoull(s, NULL, 10);
	if (version == 0 || version > 2) {
		sr_dbg("Cannot handle sigrok session file version %" PRIu64 ".",
			version);
		zip_discard(archive);
		return SR_ERR;
	}
	sr_spew("Detected sigrok session file version %" PRIu64 ".", version);

	/* read "metadata" */
	if (zip_stat(archive, "metadata", 0, &zs) < 0) {
		sr_dbg("Not a valid sigrok session file.");
		zip_discard(archive);
		return SR_ERR;
	}
	zip_discard(archive);

	return SR_OK;
}

SR_PRIV struct sr_dev_inst *sr_session_prepare_sdi(const char *filename, struct sr_session **session)
{
	struct sr_dev_inst *sdi = NULL;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->driver = &session_driver;
	sdi->status = SR_ST_ACTIVE;
	if (!session_driver_initialized) {
		/* first device, init the driver */
		session_driver_initialized = 1;
		sdi->driver->init(sdi->driver, NULL);
	}
	sr_dev_open(sdi);
	sr_session_dev_add(*session, sdi);
	(*session)->owned_devs = g_slist_append((*session)->owned_devs, sdi);
	sr_config_set(sdi, NULL, SR_CONF_SESSIONFILE,
			g_variant_new_string(filename));

	return sdi;
}

/**
 * Load the session from the specified filename.
 *
 * @param ctx The context in which to load the session.
 * @param filename The name of the session file to load.
 * @param session The session to load the file into.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_MALLOC Memory allocation error
 * @retval SR_ERR_DATA Malformed session file
 * @retval SR_ERR This is not a session file
 */
SR_API int sr_session_load(struct sr_context *ctx, const char *filename,
		struct sr_session **session)
{
	GKeyFile *kf;
	GError *error;
	struct zip *archive;
	struct zip_stat zs;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	int ret, i, j;
	uint64_t tmp_u64;
	int total_channels, total_analog, k;
	GSList *l;
	int unitsize;
	char **sections, **keys, *val;
	char channelname[SR_MAX_CHANNELNAME_LEN + 1];
	gboolean file_has_logic;

	if ((ret = sr_sessionfile_check(filename)) != SR_OK)
		return ret;

	if (!(archive = zip_open(filename, 0, NULL)))
		return SR_ERR;

	if (zip_stat(archive, "metadata", 0, &zs) < 0) {
		zip_discard(archive);
		return SR_ERR;
	}
	kf = sr_sessionfile_read_metadata(archive, &zs);
	zip_discard(archive);
	if (!kf)
		return SR_ERR_DATA;

	if ((ret = sr_session_new(ctx, session)) != SR_OK) {
		g_key_file_free(kf);
		return ret;
	}

	total_channels = 0;

	error = NULL;
	ret = SR_OK;
	file_has_logic = FALSE;
	sections = g_key_file_get_groups(kf, NULL);
	for (i = 0; sections[i] && ret == SR_OK; i++) {
		if (!strcmp(sections[i], "global"))
			/* nothing really interesting in here yet */
			continue;
		if (!strncmp(sections[i], "device ", 7)) {
			/* device section */
			sdi = NULL;
			keys = g_key_file_get_keys(kf, sections[i], NULL, NULL);

			/* File contains analog data if there are analog channels. */
			total_analog = g_key_file_get_integer(kf, sections[i],
					"total analog",	&error);
			if (total_analog > 0 && !error)
				sdi = sr_session_prepare_sdi(filename, session);
			g_clear_error(&error);

			/* File contains logic data if a capturefile is set. */
			val = g_key_file_get_string(kf, sections[i],
				"capturefile", &error);
			if (val && !error) {
				if (!sdi)
					sdi = sr_session_prepare_sdi(filename, session);
				sr_config_set(sdi, NULL, SR_CONF_CAPTUREFILE,
						g_variant_new_string(val));
				g_free(val);
				file_has_logic = TRUE;
			}
			g_clear_error(&error);

			for (j = 0; keys[j]; j++) {
				if (!strcmp(keys[j], "samplerate")) {
					val = g_key_file_get_string(kf, sections[i],
							keys[j], &error);
					if (!sdi || !val || sr_parse_sizestring(val,
								&tmp_u64) != SR_OK) {
						g_free(val);
						ret = SR_ERR_DATA;
						break;
					}
					g_free(val);
					sr_config_set(sdi, NULL, SR_CONF_SAMPLERATE,
							g_variant_new_uint64(tmp_u64));
				} else if (!strcmp(keys[j], "unitsize") && file_has_logic) {
					unitsize = g_key_file_get_integer(kf, sections[i],
							keys[j], &error);
					if (!sdi || unitsize <= 0 || error) {
						ret = SR_ERR_DATA;
						break;
					}
					sr_config_set(sdi, NULL, SR_CONF_CAPTURE_UNITSIZE,
							g_variant_new_uint64(unitsize));
				} else if (!strcmp(keys[j], "total probes")) {
					total_channels = g_key_file_get_integer(kf,
							sections[i], keys[j], &error);
					if (!sdi || total_channels < 0 || error) {
						ret = SR_ERR_DATA;
						break;
					}
					sr_config_set(sdi, NULL, SR_CONF_NUM_LOGIC_CHANNELS,
							g_variant_new_int32(total_channels));
					for (k = 0; k < total_channels; k++) {
						g_snprintf(channelname, sizeof(channelname),
								"%d", k);
						sr_channel_new(sdi, k, SR_CHANNEL_LOGIC,
								FALSE, channelname);
					}
				} else if (!strcmp(keys[j], "total analog")) {
					total_analog = g_key_file_get_integer(kf,
							sections[i], keys[j], &error);
					if (!sdi || total_analog < 0 || error) {
						ret = SR_ERR_DATA;
						break;
					}
					sr_config_set(sdi, NULL, SR_CONF_NUM_ANALOG_CHANNELS,
							g_variant_new_int32(total_analog));
					for (k = total_channels; k < (total_channels + total_analog); k++) {
						g_snprintf(channelname, sizeof(channelname),
								"%d", k);
						sr_channel_new(sdi, k, SR_CHANNEL_ANALOG,
								FALSE, channelname);
					}
				} else if (!strncmp(keys[j], "probe", 5)) {
					tmp_u64 = g_ascii_strtoull(keys[j] + 5, NULL, 10);
					if (!sdi || tmp_u64 == 0 || tmp_u64 > G_MAXINT) {
						ret = SR_ERR_DATA;
						break;
					}
					ch = g_slist_nth_data(sdi->channels, tmp_u64 - 1);
					if (!ch) {
						ret = SR_ERR_DATA;
						break;
					}
					val = g_key_file_get_string(kf, sections[i],
							keys[j], &error);
					if (!val) {
						ret = SR_ERR_DATA;
						break;
					}
					/* sr_session_save() */
					sr_dev_channel_name_set(ch, val);
					g_free(val);
					sr_dev_channel_enable(ch, TRUE);
				} else if (!strncmp(keys[j], "analog", 6)) {
					tmp_u64 = g_ascii_strtoull(keys[j]+6, NULL, 10);
					if (!sdi || tmp_u64 == 0 || tmp_u64 > G_MAXINT) {
						ret = SR_ERR_DATA;
						break;
					}
					ch = NULL;
					for (l = sdi->channels; l; l = l->next) {
						ch = l->data;
						if ((guint64)ch->index == tmp_u64 - 1)
							break;
						else
							ch = NULL;
					}
					if (!ch) {
						ret = SR_ERR_DATA;
						break;
					}
					val = g_key_file_get_string(kf, sections[i],
							keys[j], &error);
					if (!val) {
						ret = SR_ERR_DATA;
						break;
					}
					/* sr_session_save() */
					sr_dev_channel_name_set(ch, val);
					g_free(val);
					sr_dev_channel_enable(ch, TRUE);
				}
			}
			g_strfreev(keys);
		}
	}
	g_strfreev(sections);
	g_key_file_free(kf);

	if (error) {
		sr_err("Failed to parse metadata: %s", error->message);
		g_error_free(error);
	}
	return ret;
}

/** @} */
