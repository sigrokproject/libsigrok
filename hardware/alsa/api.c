/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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
#include <unistd.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static const int32_t hwcaps[] = {
	SR_CONF_SAMPLERATE,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
};

SR_PRIV struct sr_dev_driver alsa_driver_info;
static struct sr_dev_driver *di = &alsa_driver_info;

static int clear_instances(void)
{
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		return SR_OK;

	g_slist_free_full(drvc->instances, (GDestroyNotify)alsa_dev_inst_clear);
	drvc->instances = NULL;

	return SR_OK;
}

static int init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	return alsa_scan(options, di);
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	if (!(devc->hwdev)) {
		sr_err("devc->hwdev was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("Opening audio device '%s' for stream capture.", devc->hwdev);
	ret = snd_pcm_open(&devc->capture_handle, devc->hwdev,
			   SND_PCM_STREAM_CAPTURE, 0);
	if (ret < 0) {
		sr_err("Can't open audio device: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	sr_dbg("Initializing hardware parameter structure.");
	ret = snd_pcm_hw_params_any(devc->capture_handle, devc->hw_params);
	if (ret < 0) {
		sr_err("Can't initialize hardware parameter structure: %s.",
		       snd_strerror(ret));
		return SR_ERR;
	}

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->capture_handle) {
		sr_dbg("Closing PCM device.");
		if ((ret = snd_pcm_close(devc->capture_handle)) < 0) {
			sr_err("Failed to close device: %s.",
			       snd_strerror(ret));
			devc->capture_handle = NULL;
            sdi->status = SR_ST_INACTIVE;
		}
	} else {
		sr_dbg("No capture handle, no need to close audio device.");
	}

	return SR_OK;
}

static int cleanup(void)
{
	clear_instances();

	return SR_OK;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		devc = sdi->priv;
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		alsa_set_samplerate(sdi, g_variant_get_uint64(data));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GVariant *gvar;
	GVariantBuilder gvb;
	int i;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi || !sdi->priv)
			return SR_ERR_ARG;
		devc = sdi->priv;
		if (!devc->samplerates) {
			sr_err("Instance did not contain a samplerate list.");
			return SR_ERR_ARG;
		}
		for (i = 0; devc->samplerates[i]; i++)
			;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				devc->samplerates, i, sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	int count, ret;
	char *endianness;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->cb_data = cb_data;
	devc->num_samples = 0;

	sr_dbg("Setting audio access type to RW/interleaved.");
	ret = snd_pcm_hw_params_set_access(devc->capture_handle,
			devc->hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		sr_err("Can't set audio access type: %s.", snd_strerror(ret));
		return SR_ERR;
	}

	/* FIXME: Hardcoded for 16bits. */
	if (SND_PCM_FORMAT_S16 == SND_PCM_FORMAT_S16_LE)
		endianness = "little endian";
	else
		endianness = "big endian";
	sr_dbg("Setting audio sample format to signed 16bit (%s).", endianness);
	ret = snd_pcm_hw_params_set_format(devc->capture_handle,
					   devc->hw_params,
					   SND_PCM_FORMAT_S16);
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

	if (!(devc->ufds = g_try_malloc(count * sizeof(struct pollfd)))) {
		sr_err("Failed to malloc ufds.");
		return SR_ERR_MALLOC;
	}

	sr_spew("Getting %d poll descriptors.", count);
	ret = snd_pcm_poll_descriptors(devc->capture_handle, devc->ufds, count);
	if (ret < 0) {
		sr_err("Unable to obtain poll descriptors: %s.",
		       snd_strerror(ret));
		g_free(devc->ufds);
		return SR_ERR;
	}

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Poll every 10ms, or whenever some data comes in. */
	sr_source_add(devc->ufds[0].fd, devc->ufds[0].events, 10,
		      alsa_receive_data, (void *)sdi);

	// g_free(devc->ufds); /* FIXME */

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
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
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = clear_instances,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
