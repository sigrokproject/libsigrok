/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/chronovu-la8"

#define DEFAULT_NUM_CHANNELS    8
#define DEFAULT_SAMPLERATE      100000000L
#define MAX_CHUNK_SIZE          4096
#define CHRONOVU_LA8_FILESIZE   8 * 1024 * 1024 + 5

struct context {
	gboolean started;
	uint64_t samplerate;
};

static int format_match(GHashTable *metadata)
{
	int size;

	size = GPOINTER_TO_INT(g_hash_table_lookup(metadata,
			GINT_TO_POINTER(SR_INPUT_META_FILESIZE)));
	if (size == CHRONOVU_LA8_FILESIZE)
		return SR_OK;

	return SR_ERR;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct sr_channel *ch;
	struct context *inc;
	int num_channels, i;
	char name[16];

	num_channels = g_variant_get_int32(g_hash_table_lookup(options, "numchannels"));
	if (num_channels < 1) {
		sr_err("Invalid value for numchannels: must be at least 1.");
		return SR_ERR_ARG;
	}

	in->sdi = sr_dev_inst_new(SR_ST_ACTIVE, NULL, NULL, NULL);
	in->priv = inc = g_malloc0(sizeof(struct context));

	inc->samplerate = g_variant_get_uint64(g_hash_table_lookup(options, "samplerate"));

	for (i = 0; i < num_channels; i++) {
		snprintf(name, 16, "%d", i);
		ch = sr_channel_new(i, SR_CHANNEL_LOGIC, TRUE, name);
		in->sdi->channels = g_slist_append(in->sdi->channels, ch);
	}

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_datafeed_logic logic;
	struct sr_config *src;
	struct context *inc;
	gsize chunk_size, i;
	int chunk, num_channels;

	inc = in->priv;

	g_string_append_len(in->buf, buf->str, buf->len);

	num_channels = g_slist_length(in->sdi->channels);

	if (!in->sdi_ready) {
		/* sdi is ready, notify frontend. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	if (!inc->started) {
		std_session_send_df_header(in->sdi, LOG_PREFIX);

		if (inc->samplerate) {
			packet.type = SR_DF_META;
			packet.payload = &meta;
			src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(inc->samplerate));
			meta.config = g_slist_append(NULL, src);
			sr_session_send(in->sdi, &packet);
			sr_config_free(src);
		}

		inc->started = TRUE;
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = (num_channels + 7) / 8;

	/* Cut off at multiple of unitsize. */
	chunk_size = in->buf->len / logic.unitsize * logic.unitsize;

	chunk = 0;
	for (i = 0; i < chunk_size; i += chunk) {
		logic.data = in->buf->str + i;
		chunk = MIN(MAX_CHUNK_SIZE, chunk_size - i);
		logic.length = chunk;
		sr_session_send(in->sdi, &packet);
	}
	g_string_erase(in->buf, 0, chunk_size);

	return SR_OK;
}

static int cleanup(struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;

	inc = in->priv;
	if (!inc)
		return SR_OK;

	if (inc->started) {
		packet.type = SR_DF_END;
		sr_session_send(in->sdi, &packet);
	}
	g_free(in->priv);
	in->priv = NULL;

	return SR_OK;
}

static struct sr_option options[] = {
	{ "numchannels", "Number of channels", "Number of channels", NULL, NULL },
	{ "samplerate", "Sample rate", "Sample rate", NULL, NULL },
	ALL_ZERO
};

static struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_int32(DEFAULT_NUM_CHANNELS));
		options[1].def = g_variant_ref_sink(g_variant_new_uint64(DEFAULT_SAMPLERATE));
	}

	return options;
}

SR_PRIV struct sr_input_module input_chronovu_la8 = {
	.id = "chronovu-la8",
	.name = "Chronovu-LA8",
	.desc = "ChronoVu LA8",
	.metadata = { SR_INPUT_META_FILESIZE | SR_INPUT_META_REQUIRED },
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
