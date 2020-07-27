/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <zip.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/srzip"
#define CHUNK_SIZE (4 * 1024 * 1024)

struct out_context {
	gboolean zip_created;
	uint64_t samplerate;
	char *filename;
	size_t first_analog_index;
	size_t analog_ch_count;
	gint *analog_index_map;
	struct logic_buff {
		size_t unit_size;
		size_t alloc_size;
		uint8_t *samples;
		size_t fill_size;
	} logic_buff;
	struct analog_buff {
		size_t alloc_size;
		float *samples;
		size_t fill_size;
	} *analog_buff;
};

static int init(struct sr_output *o, GHashTable *options)
{
	struct out_context *outc;

	(void)options;

	if (!o->filename || o->filename[0] == '\0') {
		sr_info("srzip output module requires a file name, cannot save.");
		return SR_ERR_ARG;
	}

	outc = g_malloc0(sizeof(*outc));
	outc->filename = g_strdup(o->filename);
	o->priv = outc;

	return SR_OK;
}

static int zip_create(const struct sr_output *o)
{
	struct out_context *outc;
	struct zip *zipfile;
	struct zip_source *versrc, *metasrc;
	struct sr_channel *ch;
	size_t ch_nr;
	size_t alloc_size;
	GVariant *gvar;
	GKeyFile *meta;
	GSList *l;
	const char *devgroup;
	char *s, *metabuf;
	gsize metalen;
	guint logic_channels, enabled_logic_channels;
	guint enabled_analog_channels;
	guint index;

	outc = o->priv;

	if (outc->samplerate == 0 && sr_config_get(o->sdi->driver, o->sdi, NULL,
					SR_CONF_SAMPLERATE, &gvar) == SR_OK) {
		outc->samplerate = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	}

	/* Quietly delete it first, libzip wants replace ops otherwise. */
	g_unlink(outc->filename);
	zipfile = zip_open(outc->filename, ZIP_CREATE, NULL);
	if (!zipfile)
		return SR_ERR;

	/* "version" */
	versrc = zip_source_buffer(zipfile, "2", 1, FALSE);
	if (zip_add(zipfile, "version", versrc) < 0) {
		sr_err("Error saving version into zipfile: %s",
			zip_strerror(zipfile));
		zip_source_free(versrc);
		zip_discard(zipfile);
		return SR_ERR;
	}

	/* init "metadata" */
	meta = g_key_file_new();

	g_key_file_set_string(meta, "global", "sigrok version",
			sr_package_version_string_get());

	devgroup = "device 1";

	logic_channels = 0;
	enabled_logic_channels = 0;
	enabled_analog_channels = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;

		switch (ch->type) {
		case SR_CHANNEL_LOGIC:
			if (ch->enabled)
				enabled_logic_channels++;
			logic_channels++;
			break;
		case SR_CHANNEL_ANALOG:
			if (ch->enabled)
				enabled_analog_channels++;
			break;
		}
	}

	/* When reading the file, the first index of the analog channels
	 * can only be deduced through the "total probes" count, so the
	 * first analog index must follow the last logic one, enabled or not. */
	if (enabled_logic_channels > 0)
		outc->first_analog_index = logic_channels + 1;
	else
		outc->first_analog_index = 1;

	/* Only set capturefile and probes if we will actually save logic data. */
	if (enabled_logic_channels > 0) {
		g_key_file_set_string(meta, devgroup, "capturefile", "logic-1");
		g_key_file_set_integer(meta, devgroup, "total probes", logic_channels);
	}

	s = sr_samplerate_string(outc->samplerate);
	g_key_file_set_string(meta, devgroup, "samplerate", s);
	g_free(s);

	g_key_file_set_integer(meta, devgroup, "total analog", enabled_analog_channels);

	outc->analog_ch_count = enabled_analog_channels;
	alloc_size = sizeof(gint) * outc->analog_ch_count + 1;
	outc->analog_index_map = g_malloc0(alloc_size);

	index = 0;
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;

		s = NULL;
		switch (ch->type) {
		case SR_CHANNEL_LOGIC:
			ch_nr = ch->index + 1;
			s = g_strdup_printf("probe%zu", ch_nr);
			break;
		case SR_CHANNEL_ANALOG:
			ch_nr = outc->first_analog_index + index;
			outc->analog_index_map[index] = ch->index;
			s = g_strdup_printf("analog%zu", ch_nr);
			index++;
			break;
		}
		if (s) {
			g_key_file_set_string(meta, devgroup, s, ch->name);
			g_free(s);
		}
	}

	/*
	 * Allocate one samples buffer for all logic channels, and
	 * several samples buffers for the analog channels. Allocate
	 * buffers of CHUNK_SIZE size (in bytes), and determine the
	 * sample counts from the respective channel counts and data
	 * type widths.
	 *
	 * These buffers are intended to reduce the number of ZIP
	 * archive update calls, and decouple the srzip output module
	 * from implementation details in other acquisition device
	 * drivers and input modules.
	 *
	 * Avoid allocating zero bytes, to not depend on platform
	 * specific malloc(0) return behaviour. Avoid division by zero,
	 * holding a local buffer won't harm when no data is seen later
	 * during execution. This simplifies other locations.
	 */
	alloc_size = CHUNK_SIZE;
	outc->logic_buff.unit_size = logic_channels;
	outc->logic_buff.unit_size += 8 - 1;
	outc->logic_buff.unit_size /= 8;
	outc->logic_buff.samples = g_try_malloc0(alloc_size);
	if (!outc->logic_buff.samples)
		return SR_ERR_MALLOC;
	if (outc->logic_buff.unit_size)
		alloc_size /= outc->logic_buff.unit_size;
	outc->logic_buff.alloc_size = alloc_size;
	outc->logic_buff.fill_size = 0;

	alloc_size = sizeof(outc->analog_buff[0]) * outc->analog_ch_count + 1;
	outc->analog_buff = g_malloc0(alloc_size);
	for (index = 0; index < outc->analog_ch_count; index++) {
		alloc_size = CHUNK_SIZE;
		outc->analog_buff[index].samples = g_try_malloc0(alloc_size);
		if (!outc->analog_buff[index].samples)
			return SR_ERR_MALLOC;
		alloc_size /= sizeof(outc->analog_buff[0].samples[0]);
		outc->analog_buff[index].alloc_size = alloc_size;
		outc->analog_buff[index].fill_size = 0;
	}

	metabuf = g_key_file_to_data(meta, &metalen, NULL);
	g_key_file_free(meta);

	metasrc = zip_source_buffer(zipfile, metabuf, metalen, FALSE);
	if (zip_add(zipfile, "metadata", metasrc) < 0) {
		sr_err("Error saving metadata into zipfile: %s",
			zip_strerror(zipfile));
		zip_source_free(metasrc);
		zip_discard(zipfile);
		g_free(metabuf);
		return SR_ERR;
	}

	if (zip_close(zipfile) < 0) {
		sr_err("Error saving zipfile: %s", zip_strerror(zipfile));
		zip_discard(zipfile);
		g_free(metabuf);
		return SR_ERR;
	}
	g_free(metabuf);

	return SR_OK;
}

/**
 * Append a block of logic data to an srzip archive.
 *
 * @param[in] o Output module instance.
 * @param[in] buf Logic data samples as byte sequence.
 * @param[in] unitsize Logic data unit size (bytes per sample).
 * @param[in] length Byte sequence length (in bytes, not samples).
 *
 * @returns SR_OK et al error codes.
 */
static int zip_append(const struct sr_output *o,
	uint8_t *buf, size_t unitsize, size_t length)
{
	struct out_context *outc;
	struct zip *archive;
	struct zip_source *logicsrc;
	int64_t i, num_files;
	struct zip_stat zs;
	struct zip_source *metasrc;
	GKeyFile *kf;
	GError *error;
	uint64_t chunk_num;
	const char *entry_name;
	char *metabuf;
	gsize metalen;
	char *chunkname;
	unsigned int next_chunk_num;

	if (!length)
		return SR_OK;

	outc = o->priv;
	if (!(archive = zip_open(outc->filename, 0, NULL)))
		return SR_ERR;

	if (zip_stat(archive, "metadata", 0, &zs) < 0) {
		sr_err("Failed to open metadata: %s", zip_strerror(archive));
		zip_discard(archive);
		return SR_ERR;
	}
	kf = sr_sessionfile_read_metadata(archive, &zs);
	if (!kf) {
		zip_discard(archive);
		return SR_ERR_DATA;
	}
	/*
	 * If the file was only initialized but doesn't yet have any
	 * data it in, it won't have a unitsize field in metadata yet.
	 */
	error = NULL;
	metabuf = NULL;
	if (!g_key_file_has_key(kf, "device 1", "unitsize", &error)) {
		if (error && error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
			sr_err("Failed to check unitsize key: %s", error->message);
			g_error_free(error);
			g_key_file_free(kf);
			zip_discard(archive);
			return SR_ERR;
		}
		g_clear_error(&error);

		/* Add unitsize field. */
		g_key_file_set_integer(kf, "device 1", "unitsize", unitsize);
		metabuf = g_key_file_to_data(kf, &metalen, NULL);
		metasrc = zip_source_buffer(archive, metabuf, metalen, FALSE);

		if (zip_replace(archive, zs.index, metasrc) < 0) {
			sr_err("Failed to replace metadata: %s",
				zip_strerror(archive));
			g_key_file_free(kf);
			zip_source_free(metasrc);
			zip_discard(archive);
			g_free(metabuf);
			return SR_ERR;
		}
	}
	g_key_file_free(kf);

	next_chunk_num = 1;
	num_files = zip_get_num_entries(archive, 0);
	for (i = 0; i < num_files; i++) {
		entry_name = zip_get_name(archive, i, 0);
		if (!entry_name || strncmp(entry_name, "logic-1", 7) != 0)
			continue;
		if (entry_name[7] == '\0') {
			/*
			 * This file has no extra chunks, just a single
			 * "logic-1". Rename it to "logic-1-1" and continue
			 * with chunk 2.
			 */
			if (zip_rename(archive, i, "logic-1-1") < 0) {
				sr_err("Failed to rename 'logic-1' to 'logic-1-1': %s",
					zip_strerror(archive));
				zip_discard(archive);
				g_free(metabuf);
				return SR_ERR;
			}
			next_chunk_num = 2;
			break;
		} else if (entry_name[7] == '-') {
			chunk_num = g_ascii_strtoull(entry_name + 8, NULL, 10);
			if (chunk_num < G_MAXINT && chunk_num >= next_chunk_num)
				next_chunk_num = chunk_num + 1;
		}
	}

	if (length % unitsize != 0) {
		sr_warn("Chunk size %zu not a multiple of the"
			" unit size %zu.", length, unitsize);
	}
	logicsrc = zip_source_buffer(archive, buf, length, FALSE);
	chunkname = g_strdup_printf("logic-1-%u", next_chunk_num);
	i = zip_add(archive, chunkname, logicsrc);
	g_free(chunkname);
	if (i < 0) {
		sr_err("Failed to add chunk 'logic-1-%u': %s",
			next_chunk_num, zip_strerror(archive));
		zip_source_free(logicsrc);
		zip_discard(archive);
		g_free(metabuf);
		return SR_ERR;
	}
	if (zip_close(archive) < 0) {
		sr_err("Error saving session file: %s", zip_strerror(archive));
		zip_discard(archive);
		g_free(metabuf);
		return SR_ERR;
	}
	g_free(metabuf);

	return SR_OK;
}

/**
 * Queue a block of logic data for srzip archive writes.
 *
 * @param[in] o Output module instance.
 * @param[in] buf Logic data samples as byte sequence.
 * @param[in] unitsize Logic data unit size (bytes per sample).
 * @param[in] length Number of bytes of sample data.
 * @param[in] flush Force ZIP archive update (queue by default).
 *
 * @returns SR_OK et al error codes.
 */
static int zip_append_queue(const struct sr_output *o,
	uint8_t *buf, size_t unitsize, size_t length, gboolean flush)
{
	struct out_context *outc;
	struct logic_buff *buff;
	size_t send_size, remain, copy_size;
	uint8_t *wrptr, *rdptr;
	int ret;

	outc = o->priv;
	buff = &outc->logic_buff;
	if (length && unitsize != buff->unit_size) {
		sr_warn("Unexpected unit size, discarding logic data.");
		return SR_ERR_ARG;
	}

	/*
	 * Queue most recently received samples to the local buffer.
	 * Flush to the ZIP archive when the buffer space is exhausted.
	 */
	rdptr = buf;
	send_size = buff->unit_size ? length / buff->unit_size : 0;
	while (send_size) {
		remain = buff->alloc_size - buff->fill_size;
		if (remain) {
			wrptr = &buff->samples[buff->fill_size * buff->unit_size];
			copy_size = MIN(send_size, remain);
			send_size -= copy_size;
			buff->fill_size += copy_size;
			memcpy(wrptr, rdptr, copy_size * buff->unit_size);
			rdptr += copy_size * buff->unit_size;
			remain -= copy_size;
		}
		if (send_size && !remain) {
			ret = zip_append(o, buff->samples, buff->unit_size,
				buff->fill_size * buff->unit_size);
			if (ret != SR_OK)
				return ret;
			buff->fill_size = 0;
			remain = buff->alloc_size - buff->fill_size;
		}
	}

	/* Flush to the ZIP archive if the caller wants us to. */
	if (flush && buff->fill_size) {
		ret = zip_append(o, buff->samples, buff->unit_size,
			buff->fill_size * buff->unit_size);
		if (ret != SR_OK)
			return ret;
		buff->fill_size = 0;
	}

	return SR_OK;
}

/**
 * Append analog data of a channel to an srzip archive.
 *
 * @param[in] o Output module instance.
 * @param[in] values Sample data as array of floating point values.
 * @param[in] count Number of samples (float items, not bytes).
 * @param[in] ch_nr 1-based channel number.
 *
 * @returns SR_OK et al error codes.
 */
static int zip_append_analog(const struct sr_output *o,
	const float *values, size_t count, size_t ch_nr)
{
	struct out_context *outc;
	struct zip *archive;
	struct zip_source *analogsrc;
	int64_t i, num_files;
	size_t size;
	struct zip_stat zs;
	uint64_t chunk_num;
	const char *entry_name;
	char *basename;
	gsize baselen;
	char *chunkname;
	unsigned int next_chunk_num;

	outc = o->priv;

	if (!(archive = zip_open(outc->filename, 0, NULL)))
		return SR_ERR;

	if (zip_stat(archive, "metadata", 0, &zs) < 0) {
		sr_err("Failed to open metadata: %s", zip_strerror(archive));
		zip_discard(archive);
		return SR_ERR;
	}

	basename = g_strdup_printf("analog-1-%zu", ch_nr);
	baselen = strlen(basename);
	next_chunk_num = 1;
	num_files = zip_get_num_entries(archive, 0);
	for (i = 0; i < num_files; i++) {
		entry_name = zip_get_name(archive, i, 0);
		if (!entry_name || strncmp(entry_name, basename, baselen) != 0) {
			continue;
		} else if (entry_name[baselen] == '-') {
			chunk_num = g_ascii_strtoull(entry_name + baselen + 1, NULL, 10);
			if (chunk_num < G_MAXINT && chunk_num >= next_chunk_num)
				next_chunk_num = chunk_num + 1;
		}
	}

	size = sizeof(values[0]) * count;
	analogsrc = zip_source_buffer(archive, values, size, FALSE);
	chunkname = g_strdup_printf("%s-%u", basename, next_chunk_num);
	i = zip_add(archive, chunkname, analogsrc);
	if (i < 0) {
		sr_err("Failed to add chunk '%s': %s", chunkname, zip_strerror(archive));
		g_free(chunkname);
		g_free(basename);
		zip_source_free(analogsrc);
		zip_discard(archive);
		return SR_ERR;
	}
	g_free(chunkname);
	if (zip_close(archive) < 0) {
		sr_err("Error saving session file: %s", zip_strerror(archive));
		g_free(basename);
		zip_discard(archive);
		return SR_ERR;
	}

	g_free(basename);

	return SR_OK;
}

/**
 * Queue analog data of a channel for srzip archive writes.
 *
 * @param[in] o Output module instance.
 * @param[in] analog Sample data (session feed packet format).
 * @param[in] flush Force ZIP archive update (queue by default).
 *
 * @returns SR_OK et al error codes.
 */
static int zip_append_analog_queue(const struct sr_output *o,
	const struct sr_datafeed_analog *analog, gboolean flush)
{
	struct out_context *outc;
	const struct sr_channel *ch;
	size_t idx, nr;
	struct analog_buff *buff;
	float *values, *wrptr, *rdptr;
	size_t send_size, remain, copy_size;
	int ret;

	outc = o->priv;

	/* Is this the DF_END flush call without samples submission? */
	if (!analog && flush) {
		for (idx = 0; idx < outc->analog_ch_count; idx++) {
			nr = outc->first_analog_index + idx;
			buff = &outc->analog_buff[idx];
			if (!buff->fill_size)
				continue;
			ret = zip_append_analog(o,
				buff->samples, buff->fill_size, nr);
			if (ret != SR_OK)
				return ret;
			buff->fill_size = 0;
		}
		return SR_OK;
	}

	/* Lookup index and number of the analog channel. */
	/* TODO: support packets covering multiple channels */
	if (g_slist_length(analog->meaning->channels) != 1) {
		sr_err("Analog packets covering multiple channels not supported yet");
		return SR_ERR;
	}
	ch = g_slist_nth_data(analog->meaning->channels, 0);
	for (idx = 0; idx < outc->analog_ch_count; idx++) {
		if (outc->analog_index_map[idx] == ch->index)
			break;
	}
	if (idx == outc->analog_ch_count)
		return SR_ERR_ARG;
	nr = outc->first_analog_index + idx;
	buff = &outc->analog_buff[idx];

	/* Convert the analog data to an array of float values. */
	values = g_try_malloc0(analog->num_samples * sizeof(values[0]));
	if (!values)
		return SR_ERR_MALLOC;
	ret = sr_analog_to_float(analog, values);
	if (ret != SR_OK) {
		g_free(values);
		return ret;
	}

	/*
	 * Queue most recently received samples to the local buffer.
	 * Flush to the ZIP archive when the buffer space is exhausted.
	 */
	rdptr = values;
	send_size = analog->num_samples;
	while (send_size) {
		remain = buff->alloc_size - buff->fill_size;
		if (remain) {
			wrptr = &buff->samples[buff->fill_size];
			copy_size = MIN(send_size, remain);
			send_size -= copy_size;
			buff->fill_size += copy_size;
			memcpy(wrptr, rdptr, copy_size * sizeof(values[0]));
			rdptr += copy_size;
			remain -= copy_size;
		}
		if (send_size && !remain) {
			ret = zip_append_analog(o,
				buff->samples, buff->fill_size, nr);
			if (ret != SR_OK) {
				g_free(values);
				return ret;
			}
			buff->fill_size = 0;
			remain = buff->alloc_size - buff->fill_size;
		}
	}
	g_free(values);

	/* Flush to the ZIP archive if the caller wants us to. */
	if (flush && buff->fill_size) {
		ret = zip_append_analog(o, buff->samples, buff->fill_size, nr);
		if (ret != SR_OK)
			return ret;
		buff->fill_size = 0;
	}

	return SR_OK;
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	struct out_context *outc;
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog *analog;
	const struct sr_config *src;
	GSList *l;
	int ret;

	*out = NULL;
	if (!o || !o->sdi || !(outc = o->priv))
		return SR_ERR_ARG;

	switch (packet->type) {
	case SR_DF_META:
		meta = packet->payload;
		for (l = meta->config; l; l = l->next) {
			src = l->data;
			if (src->key != SR_CONF_SAMPLERATE)
				continue;
			outc->samplerate = g_variant_get_uint64(src->data);
		}
		break;
	case SR_DF_LOGIC:
		if (!outc->zip_created) {
			if ((ret = zip_create(o)) != SR_OK)
				return ret;
			outc->zip_created = TRUE;
		}
		logic = packet->payload;
		ret = zip_append_queue(o, logic->data,
			logic->unitsize, logic->length, FALSE);
		if (ret != SR_OK)
			return ret;
		break;
	case SR_DF_ANALOG:
		if (!outc->zip_created) {
			if ((ret = zip_create(o)) != SR_OK)
				return ret;
			outc->zip_created = TRUE;
		}
		analog = packet->payload;
		ret = zip_append_analog_queue(o, analog, FALSE);
		if (ret != SR_OK)
			return ret;
		break;
	case SR_DF_END:
		if (outc->zip_created) {
			ret = zip_append_queue(o, NULL, 0, 0, TRUE);
			if (ret != SR_OK)
				return ret;
			ret = zip_append_analog_queue(o, NULL, TRUE);
			if (ret != SR_OK)
				return ret;
		}
		break;
	}

	return SR_OK;
}

static struct sr_option options[] = {
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	return options;
}

static int cleanup(struct sr_output *o)
{
	struct out_context *outc;
	size_t idx;

	outc = o->priv;

	g_free(outc->analog_index_map);
	g_free(outc->filename);
	g_free(outc->logic_buff.samples);
	for (idx = 0; idx < outc->analog_ch_count; idx++)
		g_free(outc->analog_buff[idx].samples);
	g_free(outc->analog_buff);

	g_free(outc);
	o->priv = NULL;

	return SR_OK;
}

SR_PRIV struct sr_output_module output_srzip = {
	.id = "srzip",
	.name = "srzip",
	.desc = "srzip session file format data",
	.exts = (const char*[]){"sr", NULL},
	.flags = SR_OUTPUT_INTERNAL_IO_HANDLING,
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
