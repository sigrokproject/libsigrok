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

#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/wav"

/* Minimum/maximum number of samples per channel to put in a data chunk */
#define MIN_DATA_CHUNK_SAMPLES 10

struct out_context {
	double scale;
	gboolean header_done;
	uint64_t samplerate;
	int num_channels;
	GSList *channels;
	int chanbuf_size;
	int *chanbuf_used;
	uint8_t **chanbuf;
};

static int realloc_chanbufs(const struct sr_output *o, int size)
{
	struct out_context *outc;
	int i;

	outc = o->priv;
	for (i = 0; i < outc->num_channels; i++) {
		if (!(outc->chanbuf[i] = g_try_realloc(outc->chanbuf[i], sizeof(float) * size))) { 
			sr_err("Unable to allocate enough output buffer memory.");
			return SR_ERR;
		}
		outc->chanbuf_used[i] = 0;
	}
	outc->chanbuf_size = size;

	return SR_OK;
}

static int flush_chanbufs(const struct sr_output *o, GString *out)
{
	struct out_context *outc;
	int num_samples, i, j;
	char *buf, *bufp;

	outc = o->priv;

	/* Any one of them will do. */
	num_samples = outc->chanbuf_used[0];
	if (!(buf = g_try_malloc(4 * num_samples * outc->num_channels))) {
		sr_err("Unable to allocate enough interleaved output buffer memory.");
		return SR_ERR;
	}

	bufp = buf;
	for (i = 0; i < num_samples; i++) {
		for (j = 0; j < outc->num_channels; j++) {
			memcpy(bufp, outc->chanbuf[j] + i * 4, 4);
			bufp += 4;
		}
	}
	g_string_append_len(out, buf, 4 * num_samples * outc->num_channels);
	g_free(buf);

	for (i = 0; i < outc->num_channels; i++)
		outc->chanbuf_used[i] = 0;

	return SR_OK;
}

static int init(struct sr_output *o, GHashTable *options)
{
	struct out_context *outc;
	struct sr_channel *ch;
	GSList *l;
	GHashTableIter iter;
	gpointer key, value;

	outc = g_malloc0(sizeof(struct out_context));
	o->priv = outc;

	outc->scale = 0.0;
	if (options) {
		g_hash_table_iter_init(&iter, options);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			if (!strcmp(key, "scale")) {
				if (!g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
					sr_err("Invalid type for 'scale' option.");
					return SR_ERR_ARG;
				}
				outc->scale = g_variant_get_double(value);
			} else {
				sr_err("Unknown option '%s'.", key);
				return SR_ERR_ARG;
			}
		}
	}

	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_ANALOG)
			continue;
		if (!ch->enabled)
			continue;
		outc->channels = g_slist_append(outc->channels, ch);
		outc->num_channels++;
	}

	outc->chanbuf = g_malloc0(sizeof(float *) * outc->num_channels);
	outc->chanbuf_used = g_malloc0(sizeof(int) * outc->num_channels);

	/* Start off the interleaved buffer with 100 samples/channel. */
	realloc_chanbufs(o, 100);

	return SR_OK;
}

static void add_data_chunk(const struct sr_output *o, GString *gs)
{
	struct out_context *outc;
	char tmp[4];

	outc = o->priv;
	g_string_append(gs, "fmt ");
	/* Remaining chunk size */
	WL32(tmp, 0x12);
	g_string_append_len(gs, tmp, 4);
	/* Format code 3 = IEEE float */
	WL16(tmp, 0x0003);
	g_string_append_len(gs, tmp, 2);
	/* Number of channels */
	WL16(tmp, outc->num_channels);
	g_string_append_len(gs, tmp, 2);
	/* Samplerate */
	WL32(tmp, outc->samplerate);
	g_string_append_len(gs, tmp, 4);
	/* Byterate, using 32-bit floats. */
	WL32(tmp, outc->samplerate * outc->num_channels * 4);
	g_string_append_len(gs, tmp, 4);
	/* Blockalign */
	WL16(tmp, outc->num_channels * 4);
	g_string_append_len(gs, tmp, 2);
	/* Bits per sample */
	WL16(tmp, 32);
	g_string_append_len(gs, tmp, 2);
	WL16(tmp, 0);
	g_string_append_len(gs, tmp, 2);

	g_string_append(gs, "data");
	/* Data chunk size, max it out. */
	WL32(tmp, 0xffffffff);
	g_string_append_len(gs, tmp, 4);
}

static GString *gen_header(const struct sr_output *o)
{
	struct out_context *outc;
	GVariant *gvar;
	GString *header;
	char tmp[4];

	outc = o->priv;
	if (outc->samplerate == 0) {
		if (sr_config_get(o->sdi->driver, o->sdi, NULL, SR_CONF_SAMPLERATE,
				&gvar) == SR_OK) {
			outc->samplerate = g_variant_get_uint64(gvar);
			g_variant_unref(gvar);
		}
	}

	header = g_string_sized_new(512);
	g_string_append(header, "RIFF");
	/* Total size. Max out the field. */
	WL32(tmp, 0xffffffff);
	g_string_append_len(header, tmp, 4);
	g_string_append(header, "WAVE");
	add_data_chunk(o, header);

	return header;
}

/*
 * Stores the float in little-endian BINARY32 IEEE-754 2008 format.
 */
static void float_to_le(uint8_t *buf, float value)
{
	char *old;

	old = (char *)&value;
#ifdef WORDS_BIGENDIAN
	buf[0] = old[3];
	buf[1] = old[2];
	buf[2] = old[1];
	buf[3] = old[0];
#else
	buf[0] = old[0];
	buf[1] = old[1];
	buf[2] = old[2];
	buf[3] = old[3];
#endif
}

/*
 * Returns the number of samples used in the current channel buffers,
 * or -1 if they're not all the same.
 */
static int check_chanbuf_size(const struct sr_output *o)
{
	struct out_context *outc;
	int size, i;

	outc = o->priv;
	size = 0;
	for (i = 0; i < outc->num_channels; i++) {
		if (size == 0) {
			if (outc->chanbuf_used[i] == 0) {
				/* Nothing in all the buffers yet. */
				size = -1;
				break;
			} else
				/* New high water mark. */
				size = outc->chanbuf_used[i];
		} else if (outc->chanbuf_used[i] != size) {
			/* All channel buffers are not equally full yet. */
			size = -1;
			break;
		}
	}

	return size;
}
static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	struct out_context *outc;
	const struct sr_datafeed_meta *meta;
	const struct sr_datafeed_analog *analog;
	const struct sr_config *src;
	struct sr_channel *ch;
	GSList *l;
	float f;
	int num_channels, size, *chan_idx, idx, i, j;
	uint8_t *buf;

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
	case SR_DF_ANALOG:
		if (!outc->header_done) {
			*out = gen_header(o);
			outc->header_done = TRUE;
		} else
			*out = g_string_sized_new(512);

		analog = packet->payload;
		if (analog->num_samples == 0)
			return SR_OK;

		num_channels = g_slist_length(analog->channels);
		if (num_channels > outc->num_channels) {
			sr_err("Packet has %d channels, but only %d were enabled.",
					num_channels, outc->num_channels);
			return SR_ERR;
		}

		if (analog->num_samples > outc->chanbuf_size) {
			if (realloc_chanbufs(o, analog->num_samples) != SR_OK)
				return SR_ERR_MALLOC;
		}

		/* Index the channels in this packet, so we can interleave quicker. */
		chan_idx = g_malloc(sizeof(int) * outc->num_channels);
		for (i = 0; i < num_channels; i++) {
			ch = g_slist_nth_data(analog->channels, i);
			chan_idx[i] = g_slist_index(outc->channels, ch);
		}

		for (i = 0; i < analog->num_samples; i++) {
			for (j = 0; j < num_channels; j++) {
				idx = chan_idx[j];
				buf = outc->chanbuf[idx] + outc->chanbuf_used[idx]++ * 4;
				f = analog->data[i * num_channels + j];
				if (outc->scale != 0.0)
					f /= outc->scale;
				float_to_le(buf, f);
			}
		}
		g_free(chan_idx);

		size = check_chanbuf_size(o);
		if (size > MIN_DATA_CHUNK_SAMPLES)
			if (flush_chanbufs(o, *out) != SR_OK)
				return SR_ERR;
		break;
	case SR_DF_END:
		size = check_chanbuf_size(o);
		if (size > 0) {
			*out = g_string_sized_new(4 * size * outc->num_channels);
			if (flush_chanbufs(o, *out) != SR_OK)
				return SR_ERR;
		}
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct out_context *outc;
	int i;

	outc = o->priv;
	g_slist_free(outc->channels);
	for (i = 0; i < outc->num_channels; i++)
		g_free(outc->chanbuf[i]);
	g_free(outc->chanbuf_used);
	g_free(outc->chanbuf);
	g_free(outc);
	o->priv = NULL;

	return SR_OK;
}

static struct sr_option options[] = {
	{ "scale", "Scale", "Scale values by factor", NULL, NULL },
	{ 0 }
};

static struct sr_option *get_options(gboolean cached)
{
	if (cached)
		return options;

	options[0].def = g_variant_new_double(0);
	g_variant_ref_sink(options[0].def);

	return options;
}

SR_PRIV struct sr_output_module output_wav = {
	.id = "wav",
	.name = "WAV",
	.desc = "WAVE file format",
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};

