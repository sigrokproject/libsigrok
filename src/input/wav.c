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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/wav"

/* How many bytes at a time to process and send to the session bus. */
#define CHUNK_SIZE 4096
/* Expect to find the "data" chunk within this offset from the start. */
#define MAX_DATA_CHUNK_OFFSET    256

#define WAVE_FORMAT_PCM          1
#define WAVE_FORMAT_IEEE_FLOAT   3

struct context {
	uint64_t samplerate;
	int samplesize;
	int num_channels;
	int unitsize;
	int fmt_code;
};

static int get_wav_header(const char *filename, char *buf)
{
	struct stat st;
	int fd, l;

	l = strlen(filename);
	if (l <= 4 || strcasecmp(filename + l - 4, ".wav"))
		return SR_ERR;

	if (stat(filename, &st) == -1)
		return SR_ERR;
	if (st.st_size <= 45)
		/* Minimum size of header + 1 8-bit mono PCM sample. */
		return SR_ERR;

	if ((fd = open(filename, O_RDONLY)) == -1)
		return SR_ERR;

	l = read(fd, buf, 40);
	close(fd);
	if (l != 40)
		return SR_ERR;

	return SR_OK;
}

static int format_match(const char *filename)
{
	char buf[40];
	uint16_t fmt_code;

	if (get_wav_header(filename, buf) != SR_OK)
		return FALSE;

	if (strncmp(buf, "RIFF", 4))
		return FALSE;
	if (strncmp(buf + 8, "WAVE", 4))
		return FALSE;
	if (strncmp(buf + 12, "fmt ", 4))
		return FALSE;
	fmt_code = GUINT16_FROM_LE(*(uint16_t *)(buf + 20));
	if (fmt_code != WAVE_FORMAT_PCM
			&& fmt_code != WAVE_FORMAT_IEEE_FLOAT)
		return FALSE;

	return TRUE;
}

static int init(struct sr_input *in, const char *filename)
{
	struct sr_channel *ch;
	struct context *ctx;
	char buf[40], channelname[8];
	int i;

	if (get_wav_header(filename, buf) != SR_OK)
		return SR_ERR;

	if (!(ctx = g_try_malloc0(sizeof(struct context))))
		return SR_ERR_MALLOC;

	/* Create a virtual device. */
	in->sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, NULL, NULL, NULL);
	in->sdi->priv = ctx;

	ctx->fmt_code = GUINT16_FROM_LE(*(uint16_t *)(buf + 20));
   	ctx->samplerate = GUINT32_FROM_LE(*(uint32_t *)(buf + 24));
	ctx->samplesize = GUINT16_FROM_LE(*(uint16_t *)(buf + 32));
	ctx->num_channels = GUINT16_FROM_LE(*(uint16_t *)(buf + 22));
	ctx->unitsize = ctx->samplesize / ctx->num_channels;

	if (ctx->fmt_code == WAVE_FORMAT_PCM) {
		if (ctx->samplesize != 1 && ctx->samplesize != 2
				&& ctx->samplesize != 4) {
			sr_err("only 8, 16 or 32 bits per sample supported.");
			return SR_ERR;
		}
	} else {
		/* WAVE_FORMAT_IEEE_FLOAT */
		if (ctx->samplesize / ctx->num_channels != 4) {
			sr_err("only 32-bit floats supported.");
			return SR_ERR;
		}
	}

	for (i = 0; i < ctx->num_channels; i++) {
		snprintf(channelname, 8, "CH%d", i + 1);
		ch = sr_channel_new(i, SR_CHANNEL_ANALOG, TRUE, channelname);
		in->sdi->channels = g_slist_append(in->sdi->channels, ch);
	}

	return SR_OK;
}

static int find_data_chunk(uint8_t *buf, int initial_offset)
{
	int offset, i;

	offset = initial_offset;
	while(offset < MAX_DATA_CHUNK_OFFSET) {
		if (!memcmp(buf + offset, "data", 4))
			/* Skip into the samples. */
			return offset + 8;
		for (i = 0; i < 4; i++) {
			if (!isalpha(buf[offset + i])
					&& !isascii(buf[offset + i])
					&& !isblank(buf[offset + i]))
				/* Doesn't look like a chunk ID. */
				return -1;
		}
		/* Skip past this chunk. */
		offset += 8 + GUINT32_FROM_LE(*(uint32_t *)(buf + offset + 4));
	}

	return offset;
}

static int loadfile(struct sr_input *in, const char *filename)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_datafeed_analog analog;
	struct sr_config *src;
	struct context *ctx;
	float fdata[CHUNK_SIZE];
	uint64_t sample;
	int offset, chunk_samples, samplenum, fd, l, i;
	uint8_t buf[CHUNK_SIZE], *s, *d;

	ctx = in->sdi->priv;

	/* Send header packet to the session bus. */
	std_session_send_df_header(in->sdi, LOG_PREFIX);

	/* Send the samplerate. */
	packet.type = SR_DF_META;
	packet.payload = &meta;
	src = sr_config_new(SR_CONF_SAMPLERATE,
			g_variant_new_uint64(ctx->samplerate));
	meta.config = g_slist_append(NULL, src);
	sr_session_send(in->sdi, &packet);
	sr_config_free(src);

	if ((fd = open(filename, O_RDONLY)) == -1)
		return SR_ERR;
	if (read(fd, buf, MAX_DATA_CHUNK_OFFSET) < MAX_DATA_CHUNK_OFFSET)
		return -1;

	/* Skip past size of 'fmt ' chunk. */
	i = 20 + GUINT32_FROM_LE(*(uint32_t *)(buf + 16));
	offset = find_data_chunk(buf, i);
	if (offset < 0) {
		sr_err("Couldn't find data chunk.");
		return SR_ERR;
	}
	if (lseek(fd, offset, SEEK_SET) == -1)
		return SR_ERR;

	memset(fdata, 0, CHUNK_SIZE);
	while (TRUE) {
		if ((l = read(fd, buf, CHUNK_SIZE)) < 1)
			break;
		chunk_samples = l / ctx->num_channels / ctx->unitsize;
		s = buf;
		d = (uint8_t *)fdata;
		for (samplenum = 0; samplenum < chunk_samples * ctx->num_channels; samplenum++) {
			if (ctx->fmt_code == WAVE_FORMAT_PCM) {
				sample = 0;
				memcpy(&sample, s, ctx->unitsize);
				switch (ctx->samplesize) {
				case 1:
					/* 8-bit PCM samples are unsigned. */
					fdata[samplenum] = (uint8_t)sample / 255.0;
					break;
				case 2:
					fdata[samplenum] = GINT16_FROM_LE(sample) / 32767.0;
					break;
				case 4:
					fdata[samplenum] = GINT32_FROM_LE(sample) / 65535.0;
					break;
				}
			} else {
				/* BINARY32 float */
#ifdef WORDS_BIGENDIAN
				for (i = 0; i < ctx->unitsize; i++)
					d[i] = s[ctx->unitsize - i];
#else
				memcpy(d, s, ctx->unitsize);
#endif
			}
			s += ctx->unitsize;
			d += ctx->unitsize;

		}
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.channels = in->sdi->channels;
		analog.num_samples = chunk_samples;
		analog.mq = 0;
		analog.unit = 0;
		analog.data = fdata;
		sr_session_send(in->sdi, &packet);
	}

	close(fd);
	packet.type = SR_DF_END;
	sr_session_send(in->sdi, &packet);

	return SR_OK;
}


SR_PRIV struct sr_input_format input_wav = {
	.id = "wav",
	.description = "WAV file",
	.format_match = format_match,
	.init = init,
	.loadfile = loadfile,
};

