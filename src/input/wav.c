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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/wav"

/* How many bytes at a time to process and send to the session bus. */
#define CHUNK_SIZE               4096

/* Minimum size of header + 1 8-bit mono PCM sample. */
#define MIN_DATA_CHUNK_OFFSET    45

/* Expect to find the "data" chunk within this offset from the start. */
#define MAX_DATA_CHUNK_OFFSET    1024

#define WAVE_FORMAT_PCM_         0x0001
#define WAVE_FORMAT_IEEE_FLOAT_  0x0003
#define WAVE_FORMAT_EXTENSIBLE_  0xfffe

struct context {
	gboolean started;
	int fmt_code;
	uint64_t samplerate;
	int samplesize;
	int num_channels;
	int unitsize;
	gboolean found_data;
};

static int parse_wav_header(GString *buf, struct context *inc)
{
	uint64_t samplerate;
	unsigned int fmt_code, samplesize, num_channels, unitsize;

	if (buf->len < MIN_DATA_CHUNK_OFFSET)
		return SR_ERR_NA;

	fmt_code = RL16(buf->str + 20);
	samplerate = RL32(buf->str + 24);

	samplesize = RL16(buf->str + 32);
	num_channels = RL16(buf->str + 22);
	if (num_channels == 0)
		return SR_ERR;
	unitsize = samplesize / num_channels;
	if (unitsize != 1 && unitsize != 2 && unitsize != 4) {
		sr_err("Only 8, 16 or 32 bits per sample supported.");
		return SR_ERR_DATA;
	}

	if (fmt_code == WAVE_FORMAT_PCM_) {
	} else if (fmt_code == WAVE_FORMAT_IEEE_FLOAT_) {
		if (unitsize != 4) {
			sr_err("only 32-bit floats supported.");
			return SR_ERR_DATA;
		}
	} else if (fmt_code == WAVE_FORMAT_EXTENSIBLE_) {
		if (buf->len < 70)
			/* Not enough for extensible header and next chunk. */
			return SR_ERR_NA;

		if (RL16(buf->str + 16) != 40) {
			sr_err("WAV extensible format chunk must be 40 bytes.");
			return SR_ERR;
		}
		if (RL16(buf->str + 36) != 22) {
			sr_err("WAV extension must be 22 bytes.");
			return SR_ERR;
		}
		if (RL16(buf->str + 34) != RL16(buf->str + 38)) {
			sr_err("Reduced valid bits per sample not supported.");
			return SR_ERR_DATA;
		}
		/* Real format code is the first two bytes of the GUID. */
		fmt_code = RL16(buf->str + 44);
		if (fmt_code != WAVE_FORMAT_PCM_ && fmt_code != WAVE_FORMAT_IEEE_FLOAT_) {
			sr_err("Only PCM and floating point samples are supported.");
			return SR_ERR_DATA;
		}
		if (fmt_code == WAVE_FORMAT_IEEE_FLOAT_ && unitsize != 4) {
			sr_err("only 32-bit floats supported.");
			return SR_ERR_DATA;
		}
	} else {
		sr_err("Only PCM and floating point samples are supported.");
		return SR_ERR_DATA;
	}

	if (inc) {
		inc->fmt_code = fmt_code;
		inc->samplerate = samplerate;
		inc->samplesize = samplesize;
		inc->num_channels = num_channels;
		inc->unitsize = unitsize;
		inc->found_data = FALSE;
	}

	return SR_OK;
}

static int format_match(GHashTable *metadata)
{
	GString *buf;
	int ret;

	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
	if (strncmp(buf->str, "RIFF", 4))
		return SR_ERR;
	if (strncmp(buf->str + 8, "WAVE", 4))
		return SR_ERR;
	if (strncmp(buf->str + 12, "fmt ", 4))
		return SR_ERR;
	/*
	 * Only gets called when we already know this is a WAV file, so
	 * this parser can log error messages.
	 */
	if ((ret = parse_wav_header(buf, NULL)) != SR_OK)
		return ret;

	return SR_OK;
}

static int init(struct sr_input *in, GHashTable *options)
{
	(void)options;

	in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
	in->priv = g_malloc0(sizeof(struct context));

	return SR_OK;
}

static int find_data_chunk(GString *buf, int initial_offset)
{
	unsigned int offset, i;

	offset = initial_offset;
	while (offset < MIN(MAX_DATA_CHUNK_OFFSET, buf->len)) {
		if (!memcmp(buf->str + offset, "data", 4))
			/* Skip into the samples. */
			return offset + 8;
		for (i = 0; i < 4; i++) {
			if (!isalnum(buf->str[offset + i])
					&& !isblank(buf->str[offset + i]))
				/* Doesn't look like a chunk ID. */
				return -1;
		}
		/* Skip past this chunk. */
		offset += 8 + RL32(buf->str + offset + 4);
	}

	if (offset > MAX_DATA_CHUNK_OFFSET)
		return -1;

	return offset;
}

static void send_chunk(const struct sr_input *in, int offset, int num_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct context *inc;
	float fdata[CHUNK_SIZE];
	int total_samples, samplenum;
	char *s, *d;

	inc = in->priv;

	s = in->buf->str + offset;
	d = (char *)fdata;
	memset(fdata, 0, CHUNK_SIZE * sizeof(float));
	total_samples = num_samples * inc->num_channels;
	for (samplenum = 0; samplenum < total_samples; samplenum++) {
		if (inc->fmt_code == WAVE_FORMAT_PCM_) {
			switch (inc->unitsize) {
			case 1:
				/* 8-bit PCM samples are unsigned. */
				fdata[samplenum] = *(uint8_t*)(s) / (float)255;
				break;
			case 2:
				fdata[samplenum] = RL16S(s) / (float)INT16_MAX;
				break;
			case 4:
				fdata[samplenum] = RL32S(s) / (float)INT32_MAX;
				break;
			}
		} else {
			/* BINARY32 float */
#ifdef WORDS_BIGENDIAN
			int i;
			for (i = 0; i < inc->unitsize; i++)
				d[i] = s[inc->unitsize - 1 - i];
#else
			memcpy(d, s, inc->unitsize);
#endif
		}
		s += inc->unitsize;
		d += inc->unitsize;
	}

	/* TODO: Use proper 'digits' value for this device (and its modes). */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = num_samples;
	analog.data = fdata;
	analog.meaning->channels = in->sdi->channels;
	analog.meaning->mq = 0;
	analog.meaning->mqflags = 0;
	analog.meaning->unit = 0;
	sr_session_send(in->sdi, &packet);
}

static int process_buffer(struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	int offset, chunk_samples, total_samples, processed, max_chunk_samples;
	int num_samples, i;

	inc = in->priv;
	if (!inc->started) {
		std_session_send_df_header(in->sdi);

		packet.type = SR_DF_META;
		packet.payload = &meta;
		src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(inc->samplerate));
		meta.config = g_slist_append(NULL, src);
		sr_session_send(in->sdi, &packet);
		g_slist_free(meta.config);
		sr_config_free(src);

		inc->started = TRUE;
	}

	if (!inc->found_data) {
		/* Skip past size of 'fmt ' chunk. */
		i = 20 + RL32(in->buf->str + 16);
		offset = find_data_chunk(in->buf, i);
		if (offset < 0) {
			if (in->buf->len > MAX_DATA_CHUNK_OFFSET) {
				sr_err("Couldn't find data chunk.");
				return SR_ERR;
			}
		}
		inc->found_data = TRUE;
	} else
		offset = 0;

	/* Round off up to the last channels * unitsize boundary. */
	chunk_samples = (in->buf->len - offset) / inc->samplesize;
	max_chunk_samples = CHUNK_SIZE / inc->samplesize;
	processed = 0;
	total_samples = chunk_samples;
	while (processed < total_samples) {
		if (chunk_samples > max_chunk_samples)
			num_samples = max_chunk_samples;
		else
			num_samples = chunk_samples;
		send_chunk(in, offset, num_samples);
		offset += num_samples * inc->samplesize;
		chunk_samples -= num_samples;
		processed += num_samples;
	}

	if ((unsigned int)offset < in->buf->len) {
		/*
		 * The incoming buffer wasn't processed completely. Stash
		 * the leftover data for next time.
		 */
		g_string_erase(in->buf, 0, offset);
	} else
		g_string_truncate(in->buf, 0);

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;
	char channelname[8];

	g_string_append_len(in->buf, buf->str, buf->len);

	if (in->buf->len < MIN_DATA_CHUNK_OFFSET) {
		/*
		 * Don't even try until there's enough room
		 * for the data segment to start.
		 */
		return SR_OK;
	}

	inc = in->priv;
	if (!in->sdi_ready) {
		if ((ret = parse_wav_header(in->buf, inc)) == SR_ERR_NA)
			/* Not enough data yet. */
			return SR_OK;
		else if (ret != SR_OK)
			return ret;

		for (int i = 0; i < inc->num_channels; i++) {
			snprintf(channelname, 8, "CH%d", i + 1);
			sr_channel_new(in->sdi, i, SR_CHANNEL_ANALOG, TRUE, channelname);
		}

		/* sdi is ready, notify frontend. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	ret = process_buffer(in);

	return ret;
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	if (in->sdi_ready)
		ret = process_buffer(in);
	else
		ret = SR_OK;

	inc = in->priv;
	if (inc->started)
		std_session_send_df_end(in->sdi);

	return ret;
}

static int reset(struct sr_input *in)
{
	struct context *inc = in->priv;

	inc->started = FALSE;
	g_string_truncate(in->buf, 0);

	return SR_OK;
}

SR_PRIV struct sr_input_module input_wav = {
	.id = "wav",
	.name = "WAV",
	.desc = "WAV file",
	.exts = (const char*[]){"wav", NULL},
	.metadata = { SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.reset = reset,
};
