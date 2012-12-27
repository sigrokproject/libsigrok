/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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

#include "protocol.h"
#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define DEFAULT_PROBES		2
#define SAMPLE_WIDTH		16
#define DEFAULT_SAMPLERATE	44100
// #define AUDIO_DEV		"plughw:0,0"
#define AUDIO_DEV		"default"

static const int hwcaps[] = {
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0,
};

static const char *probe_names[] = {
	"Channel 0", "Channel 1",
	NULL,
};

SR_PRIV struct sr_dev_driver alsa_driver_info;
static struct sr_dev_driver *di = &alsa_driver_info;

static int clear_instances(void)
{
	/* TODO */

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	drvc->sr_ctx = sr_ctx;
	di->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	GSList *devices;
	int i;

	(void)options;

	drvc = di->priv;
	drvc->instances = NULL;

	devices = NULL;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		return NULL;
	}

	if (!(sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, "alsa", NULL, NULL))) {
		sr_err("Failed to create device instance.");
		return NULL;
	}

	/* Set the samplerate to a default value for now. */
	devc->cur_samplerate = DEFAULT_SAMPLERATE;
	devc->num_probes = DEFAULT_PROBES;

	sdi->priv = devc;
	sdi->driver = di;

	for (i = 0; probe_names[i]; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_ANALOG, TRUE,
					   probe_names[i]))) {
			sr_err("Failed to create probe.");
			return NULL;
		}
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	sr_dbg("Opening audio device '%s' for stream capture.", AUDIO_DEV);
	ret = snd_pcm_open(&devc->capture_handle, AUDIO_DEV,
			   SND_PCM_STREAM_CAPTURE, 0);
	if (ret < 0) {
		sr_err("Can't open audio device: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	sr_dbg("Allocating hardware parameter structure.");
	ret = snd_pcm_hw_params_malloc(&devc->hw_params);
	if (ret < 0) {
		sr_err("Can't allocate hardware parameter structure: %s.",
		       snd_strerror(ret));
		return SR_ERR_MALLOC;
	}

	sr_dbg("Initializing hardware parameter structure.");
	ret = snd_pcm_hw_params_any(devc->capture_handle, devc->hw_params);
	if (ret < 0) {
		sr_err("Can't initialize hardware parameter structure: %s.",
		       snd_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	sr_dbg("Closing device.");

	if (devc->hw_params) {
		sr_dbg("Freeing hardware parameters.");
		snd_pcm_hw_params_free(devc->hw_params);
	} else {
		sr_dbg("No hardware parameters, no need to free.");
	}

	if (devc->capture_handle) {
		sr_dbg("Closing PCM device.");
		if ((ret = snd_pcm_close(devc->capture_handle)) < 0) {
			sr_err("Failed to close device: %s.",
			       snd_strerror(ret));
		}
	} else {
		sr_dbg("No capture handle, no need to close audio device.");
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
		       const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (info_id != SR_DI_HWCAPS) /* For SR_DI_HWCAPS sdi will be NULL. */
		devc = sdi->priv;

	switch (info_id) {
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(DEFAULT_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	case SR_DI_CUR_SAMPLERATE:
		*data = &devc->cur_samplerate;
		break;
	default:
		sr_err("Invalid info_id: %d.", info_id);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
			     const void *value)
{
	struct dev_context *devc;

	devc = sdi->priv;

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		devc->cur_samplerate = *(const uint64_t *)value;
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		devc->limit_samples = *(const uint64_t *)value;
		break;
	default:
		sr_err("Unknown capability: %d.", hwcap);
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct dev_context *devc;
	int count, ret;

	devc = sdi->priv;
	devc->cb_data = cb_data;

	sr_dbg("Setting audio access type to RW/interleaved.");
	ret = snd_pcm_hw_params_set_access(devc->capture_handle,
			devc->hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		sr_err("Can't set audio access type: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	/* FIXME: Hardcoded for 16bits. */
	sr_dbg("Setting audio sample format to signed 16bit (little endian).");
	ret = snd_pcm_hw_params_set_format(devc->capture_handle,
			devc->hw_params, SND_PCM_FORMAT_S16_LE);
	if (ret < 0) {
		sr_err("Can't set audio sample format: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	sr_dbg("Setting audio samplerate to %" PRIu64 "Hz.",
	       devc->cur_samplerate);
	ret = snd_pcm_hw_params_set_rate(devc->capture_handle, devc->hw_params,
					 (unsigned int)devc->cur_samplerate, 0);
	if (ret < 0) {
		sr_err("Can't set audio sample rate: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	sr_dbg("Setting audio channel count to %d.", devc->num_probes);
	ret = snd_pcm_hw_params_set_channels(devc->capture_handle,
					     devc->hw_params, devc->num_probes);
	if (ret < 0) {
		sr_err("Can't set channel count: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	sr_dbg("Setting audio parameters.");
	ret = snd_pcm_hw_params(devc->capture_handle, devc->hw_params);
	if (ret < 0) {
		sr_err("Can't set parameters: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	sr_dbg("Preparing audio interface for use.");
	ret = snd_pcm_prepare(devc->capture_handle);
	if (ret < 0) {
		sr_err("Can't prepare audio interface for use: %s.",
		       snd_strerror(ret));
		return SR_ERR;
	}

	count = snd_pcm_poll_descriptors_count(devc->capture_handle);
	if (count < 1) {
		sr_err("Unable to obtain poll descriptors count.");
		return SR_ERR;
	}
	sr_spew("Obtained poll descriptor count: %d.", count);

	if (!(devc->ufds = g_try_malloc(count * sizeof(struct pollfd)))) {
		sr_err("Failed to malloc ufds.");
		return SR_ERR_MALLOC;
	}

	sr_err("Getting %d poll descriptors.", count);
	ret = snd_pcm_poll_descriptors(devc->capture_handle, devc->ufds, count);
	if (ret < 0) {
		sr_err("Unable to obtain poll descriptors: %s.",
		       snd_strerror(ret));
		g_free(devc->ufds);
		return SR_ERR;
	}

	/* Send header packet to the session bus. */
	sr_dbg("Sending SR_DF_HEADER packet.");
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(devc->cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	sr_dbg("Sending SR_DF_META_ANALOG packet.");
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = devc->num_probes;
	sr_session_send(devc->cb_data, &packet);

	/* Poll every 10ms, or whenever some data comes in. */
	sr_source_add(devc->ufds[0].fd, devc->ufds[0].events, 10,
		      alsa_receive_data, (void *)sdi);

	// g_free(devc->ufds); /* FIXME */

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	devc = sdi->priv;
	devc->cb_data = cb_data;

	sr_source_remove(devc->ufds[0].fd);

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END packet.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver alsa_driver_info = {
	.name = "alsa",
	.longname = "ALSA driver",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
