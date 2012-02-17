/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
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

/* Note: This driver doesn't compile, analog support in sigrok is WIP. */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include "sigrok.h"
#include "sigrok-internal.h"

#define NUM_PROBES 2
#define SAMPLE_WIDTH 16
#define AUDIO_DEV "plughw:0,0"

struct sr_analog_probe {
	uint8_t att;
	uint8_t res;    /* Needs to be a power of 2, FIXME */
	uint16_t val;   /* Max hardware ADC width is 16bits */
};

struct sr_analog_sample {
	uint8_t num_probes; /* Max hardware probes is 256 */
	struct sr_analog_probe probes[];
};

static int capabilities[] = {
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
};

static const char *probe_names[NUM_PROBES + 1] = {
	"0",
	"1",
	NULL,
};

static GSList *dev_insts = NULL;

struct alsa {
	uint64_t cur_rate;
	uint64_t limit_samples;
	snd_pcm_t *capture_handle;
	snd_pcm_hw_params_t *hw_params;
	gpointer session_id;
};

static int hw_init(const char *devinfo)
{
	struct sr_dev_inst *sdi;
	struct alsa *alsa;

	/* Avoid compiler warnings. */
	devinfo = devinfo;

	if (!(alsa = g_try_malloc0(sizeof(struct alsa)))) {
		sr_err("alsa: %s: alsa malloc failed", __func__);
		return 0;
	}

	sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, "alsa", NULL, NULL);
	if (!sdi)
		goto free_alsa;

	sdi->priv = alsa;

	dev_insts = g_slist_append(dev_insts, sdi);

	return 1;
free_alsa:
	g_free(alsa);
	return 0;
}

static int hw_opendev(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct alsa *alsa;
	int err;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	alsa = sdi->priv;

	err = snd_pcm_open(&alsa->capture_handle, AUDIO_DEV,
					SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		sr_err("alsa: can't open audio device %s (%s)", AUDIO_DEV,
		       snd_strerror(err));
		return SR_ERR;
	}

	err = snd_pcm_hw_params_malloc(&alsa->hw_params);
	if (err < 0) {
		sr_err("alsa: can't allocate hardware parameter structure (%s)",
		       snd_strerror(err));
		return SR_ERR;
	}

	err = snd_pcm_hw_params_any(alsa->capture_handle, alsa->hw_params);
	if (err < 0) {
		sr_err("alsa: can't initialize hardware parameter structure "
		       "(%s)", snd_strerror(err));
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_closedev(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct alsa *alsa;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("alsa: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	if (!(alsa = sdi->priv)) {
		sr_err("alsa: %s: sdi->priv was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	// TODO: Return values of snd_*?
	if (alsa->hw_params)
		snd_pcm_hw_params_free(alsa->hw_params);
	if (alsa->capture_handle)
		snd_pcm_close(alsa->capture_handle);

	return SR_OK;
}

static int hw_cleanup(void)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, 0))) {
		sr_err("alsa: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	sr_dev_inst_free(sdi);

	return SR_OK;
}

static void *hw_get_dev_info(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct alsa *alsa;
	void *info = NULL;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return NULL;
	alsa = sdi->priv;

	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &alsa->cur_rate;
		break;
	// case SR_DI_PROBE_TYPE:
	// 	info = GINT_TO_POINTER(SR_PROBE_TYPE_ANALOG);
	// 	break;
	}

	return info;
}

static int hw_get_status(int dev_index)
{
	/* Avoid compiler warnings. */
	dev_index = dev_index;

	return SR_ST_ACTIVE;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int hw_set_configuration(int dev_index, int capability, void *value)
{
	struct sr_dev_inst *sdi;
	struct alsa *alsa;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	alsa = sdi->priv;

	switch (capability) {
	case SR_HWCAP_PROBECONFIG:
		return SR_OK;
	case SR_HWCAP_SAMPLERATE:
		alsa->cur_rate = *(uint64_t *) value;
		return SR_OK;
	case SR_HWCAP_LIMIT_SAMPLES:
		alsa->limit_samples = *(uint64_t *) value;
		return SR_OK;
	default:
		return SR_ERR;
	}
}

static int receive_data(int fd, int revents, void *user_data)
{
	struct sr_dev_inst *sdi = user_data;
	struct alsa *alsa = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_analog_sample *sample;
	unsigned int sample_size = sizeof(struct sr_analog_sample) +
		(NUM_PROBES * sizeof(struct sr_analog_probe));
	char *outb;
	char inb[4096];
	int i, x, count;

	fd = fd;
	revents = revents;

	do {
		memset(inb, 0, sizeof(inb));
		count = snd_pcm_readi(alsa->capture_handle, inb,
			MIN(4096/4, alsa->limit_samples));
		if (count < 1) {
			sr_err("alsa: Failed to read samples");
			return FALSE;
		}

		if (!(outb = g_try_malloc(sample_size * count))) {
			sr_err("alsa: %s: outb malloc failed", __func__);
			return FALSE;
		}

		for (i = 0; i < count; i++) {
			sample = (struct sr_analog_sample *)
						(outb + (i * sample_size));
			sample->num_probes = NUM_PROBES;

			for (x = 0; x < NUM_PROBES; x++) {
				sample->probes[x].val =
					*(uint16_t *) (inb + (i * 4) + (x * 2));
				sample->probes[x].val &= ((1 << 16) - 1);
				sample->probes[x].res = 16;
			}
		}

		packet.type = SR_DF_ANALOG;
		packet.length = count * sample_size;
		packet.unitsize = sample_size;
		packet.payload = outb;
		sr_session_bus(user_data, &packet);
		g_free(outb);
		alsa->limit_samples -= count;

	} while (alsa->limit_samples > 0);

	packet.type = SR_DF_END;
	sr_session_bus(user_data, &packet);

	return TRUE;
}

static int hw_start_acquisition(int dev_index, gpointer session_dev_id)
{
	struct sr_dev_inst *sdi;
	struct alsa *alsa;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct pollfd *ufds;
	int count;
	int err;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	alsa = sdi->priv;

	err = snd_pcm_hw_params_set_access(alsa->capture_handle,
			alsa->hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		sr_err("alsa: can't set access type (%s)", snd_strerror(err));
		return SR_ERR;
	}

	/* FIXME: Hardcoded for 16bits */
	err = snd_pcm_hw_params_set_format(alsa->capture_handle,
			alsa->hw_params, SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
		sr_err("alsa: can't set sample format (%s)", snd_strerror(err));
		return SR_ERR;
	}

	err = snd_pcm_hw_params_set_rate_near(alsa->capture_handle,
			alsa->hw_params, (unsigned int *) &alsa->cur_rate, 0);
	if (err < 0) {
		sr_err("alsa: can't set sample rate (%s)", snd_strerror(err));
		return SR_ERR;
	}

	err = snd_pcm_hw_params_set_channels(alsa->capture_handle,
			alsa->hw_params, NUM_PROBES);
	if (err < 0) {
		sr_err("alsa: can't set channel count (%s)", snd_strerror(err));
		return SR_ERR;
	}

	err = snd_pcm_hw_params(alsa->capture_handle, alsa->hw_params);
	if (err < 0) {
		sr_err("alsa: can't set parameters (%s)", snd_strerror(err));
		return SR_ERR;
	}

	err = snd_pcm_prepare(alsa->capture_handle);
	if (err < 0) {
		sr_err("alsa: can't prepare audio interface for use (%s)",
		       snd_strerror(err));
		return SR_ERR;
	}

	count = snd_pcm_poll_descriptors_count(alsa->capture_handle);
	if (count < 1) {
		sr_err("alsa: Unable to obtain poll descriptors count");
		return SR_ERR;
	}

	if (!(ufds = g_try_malloc(count * sizeof(struct pollfd)))) {
		sr_err("alsa: %s: ufds malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	err = snd_pcm_poll_descriptors(alsa->capture_handle, ufds, count);
	if (err < 0) {
		sr_err("alsa: Unable to obtain poll descriptors (%s)",
		       snd_strerror(err));
		g_free(ufds);
		return SR_ERR;
	}

	alsa->session_id = session_dev_id;
	sr_source_add(ufds[0].fd, ufds[0].events, 10, receive_data, sdi);

	packet.type = SR_DF_HEADER;
	packet.length = sizeof(struct sr_datafeed_header);
	packet.payload = (unsigned char *) &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = alsa->cur_rate;
	header.num_analog_probes = NUM_PROBES;
	header.num_logic_probes = 0;
	header.protocol_id = SR_PROTO_RAW;
	sr_session_bus(session_dev_id, &packet);
	g_free(ufds);

	return SR_OK;
}

static int hw_stop_acquisition(int dev_index, gpointer session_dev_id)
{
	/* Avoid compiler warnings. */
	dev_index = dev_index;
	session_dev_id = session_dev_id;

	return SR_OK;
}

SR_PRIV struct sr_dev_plugin alsa_plugin_info = {
	.name = "alsa",
	.longname = "ALSA driver",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.opendev = hw_opendev,
	.closedev = hw_closedev,
	.get_dev_info = hw_get_dev_info,
	.get_status = hw_get_status,
	.get_capabilities = hw_get_capabilities,
	.set_configuration = hw_set_configuration,
	.start_acquisition = hw_start_acquisition,
	.stop_acquisition = hw_stop_acquisition,
};
