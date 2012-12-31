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

#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

/*
 * There is no way to get a list of supported samplerates from ALSA. We could
 * use the 'plughw' interface of ALSA, in which case any format and/or
 * samplerate conversion would be performed by ALSA. However, we are interested
 * in the hardware capabilities, and have the infrastructure in sigrok to do so.
 * We therefore use the 'hw' interface. The downside is that the code gets a
 * little bulkier, as we have to keep track of the hardware capabilities, and
 * only use those that the hardware supports. Case in point, ALSA will not give
 * us a list of capabilities; we have to test for each one individually. Hence,
 * we keep lists of the capabilities we are interested in.
 */
static const unsigned int rates[] = {
	5512,
	8000,
	11025,
	12000,
	16000,
	22050,
	24000,
	32000,
	44100,
	48000,
	64000,
	88200,
	96000,
	176400,
	192000,
	384000,
	768000, /* Yes, there are sound cards that go this high. */
};

static void alsa_scan_handle_dev(GSList **devices,
				 const char *cardname, const char *alsaname,
				 struct sr_dev_driver *di,
				 snd_pcm_info_t *pcminfo)
{
	struct drv_context *drvc = NULL;
	struct sr_dev_inst *sdi = NULL;
	struct dev_context *devc = NULL;
	struct sr_probe *probe;
	int ret;
	unsigned int i, offset, channels, minrate, maxrate, rate;
	uint64_t hwrates[ARRAY_SIZE(rates)];
	uint64_t *devrates = NULL;
	snd_pcm_t *temp_handle = NULL;
	snd_pcm_hw_params_t *hw_params = NULL;
	char p_name[32];

	drvc = di->priv;

	/*
	 * Get hardware parameters:
	 * The number of channels, for example, are our sigrok probes. Getting
	 * this information needs a detour. We need to open the device, then
	 * query it and/or test different parameters. A side-effect of is that
	 * we create a snd_pcm_hw_params_t object. We take advantage of the
	 * situation, and pass this object in our dev_context->hw_params,
	 * eliminating the need to free() it and malloc() it later.
	 */
	ret = snd_pcm_open(&temp_handle, alsaname, SND_PCM_STREAM_CAPTURE, 0);
	if (ret < 0) {
		sr_err("Cannot open device: %s.", snd_strerror(ret));
		goto scan_error_cleanup;
	}

	ret = snd_pcm_hw_params_malloc(&hw_params);
	if (ret < 0) {
		sr_err("Error allocating hardware parameter structure: %s.",
		       snd_strerror(ret));
		goto scan_error_cleanup;
	}

	ret = snd_pcm_hw_params_any(temp_handle, hw_params);
	if (ret < 0) {
		sr_err("Error initializing hardware parameter structure: %s.",
		       snd_strerror(ret));
		goto scan_error_cleanup;
	}

	snd_pcm_hw_params_get_channels_max(hw_params, &channels);

	/*
	 * We need to test if each samplerate between min and max is supported.
	 * Unfortunately, ALSA won't just throw a list at us.
	 */
	snd_pcm_hw_params_get_rate_min(hw_params, &minrate, 0);
	snd_pcm_hw_params_get_rate_max(hw_params, &maxrate, 0);
	for (i = 0, offset = 0; i < ARRAY_SIZE(rates); i++) {
		rate = rates[i];
		if (rate < minrate)
			continue;
		if (rate > maxrate)
			break;
		ret = snd_pcm_hw_params_test_rate(temp_handle, hw_params,
						  rate, 0);
		if (ret >= 0)
			hwrates[offset++] = rate;
	}
	hwrates[offset++] = 0;

	if ((ret = snd_pcm_close(temp_handle)) < 0)
		sr_err("Failed to close device: %s.", snd_strerror(ret));
	temp_handle = NULL;

	/*
	 * Now we are done querying the hardware parameters.
	 * If we made it here, we know everything we want to know, and it's
	 * time to create our sigrok device.
	 */
	sr_info("Device %s has %d channels.", alsaname, channels);
	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "ALSA:",
		cardname, snd_pcm_info_get_name(pcminfo)))) {
		sr_err("Failed to create device instance.");
		goto scan_error_cleanup;
	}
	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_error_cleanup;
	}
	if (!(devrates = g_try_malloc(offset * sizeof(uint64_t)))) {
		sr_err("Samplerate list malloc failed.");
		goto scan_error_cleanup;
	}

	devc->hwdev = g_strdup(alsaname);
	devc->num_probes = channels;
	devc->hw_params = hw_params;
	memcpy(devrates, hwrates, offset * sizeof(uint64_t));
	devc->supp_rates.list = devrates;

	sdi->priv = devc;
	sdi->driver = di;

	for (i = 0; i < devc->num_probes; i++) {
		snprintf(p_name, sizeof(p_name), "Ch_%d", i);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, p_name)))
			goto scan_error_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	drvc->instances = g_slist_append(drvc->instances, sdi);
	*devices = g_slist_append(*devices, sdi);
	return;

scan_error_cleanup:
	if (devc) {
		if (devc->hwdev)
			g_free(devc->hwdev);
		g_free(devc);
	}
	if (devrates)
		g_free(devrates);
	if (sdi)
		sr_dev_inst_free(sdi);
	if (hw_params)
		snd_pcm_hw_params_free(hw_params);
	if (temp_handle)
		if ((ret = snd_pcm_close(temp_handle)) < 0) {
			sr_err("Failed to close device: %s.",
			       snd_strerror(ret));
		}
}

/**
 * Scan all alsa devices, and translate them to sigrok devices.
 *
 * Each alsa device (not alsa card) gets its own sigrok device.
 *
 * For example,
 *     hw:1,0 == sigrok device 0
 *     hw:1,1 == sigrok device 1
 *     hw:2,0 == sigrok device 2
 *     hw:2,1 == sigrok device 3
 *     hw:2,2 == sigrok device 4
 *     [...]
 *
 * We don't currently look at alsa subdevices. We only use subdevice 0.
 * Every input device will have its own channels (left, right, etc). Each of
 * those channels gets mapped to a different sigrok probe. A device with 4
 * channels will have 4 probes from sigrok's perspective.
 */
SR_PRIV GSList *alsa_scan(GSList *options, struct sr_dev_driver *di)
{
	GSList *devices = NULL;
	snd_ctl_t *handle;
	int card, ret, dev;
	snd_ctl_card_info_t *info;
	snd_pcm_info_t *pcminfo;
	const char *cardname;
	char hwcard[32], hwdev[32];

	/* TODO */
	(void)options;

	if ((ret = snd_ctl_card_info_malloc(&info)) < 0) {
		sr_err("Failed to malloc card info: %s.", snd_strerror(ret));
		return NULL;
	}
	if ((ret = snd_pcm_info_malloc(&pcminfo) < 0)) {
		sr_err("Cannot malloc pcm info: %s.", snd_strerror(ret));
		return NULL;
	}

	card = -1;
	while (snd_card_next(&card) >= 0 && card >= 0) {
		snprintf(hwcard, sizeof(hwcard), "hw:%d", card);
		if ((ret = snd_ctl_open(&handle, hwcard, 0)) < 0) {
			sr_err("Cannot open (%d): %s.", card, snd_strerror(ret));
			continue;
		}
		if ((ret = snd_ctl_card_info(handle, info)) < 0) {
			sr_err("Cannot get hardware info (%d): %s.",
			       card, snd_strerror(ret));
			if ((ret = snd_ctl_close(handle)) < 0) {
				sr_err("Cannot close device (%d): %s.",
				       card, snd_strerror(ret));
			}
			continue;
		}
		dev = -1;
		while (snd_ctl_pcm_next_device(handle, &dev) >= 0 && dev >= 0) {
			snprintf(hwdev, sizeof(hwdev), "%s,%d", hwcard, dev);
			/*
			 * TODO: We always use subdevice 0, but we have yet to
			 * explore the possibilities opened up by other
			 * subdevices. Most hardware only has subdevice 0.
			 */
			snd_pcm_info_set_device(pcminfo, dev);
			snd_pcm_info_set_subdevice(pcminfo, 0);
			snd_pcm_info_set_stream(pcminfo,
						SND_PCM_STREAM_CAPTURE);
			if ((ret = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
				sr_err("Cannot get device info (%s): %s.",
				       hwdev, snd_strerror(ret));
				continue;
			}

			cardname = snd_ctl_card_info_get_name(info);
			sr_info("card %d: %s [%s], device %d: %s [%s]",
			       card, snd_ctl_card_info_get_id(info), cardname,
			       dev, snd_pcm_info_get_id(pcminfo),
			       snd_pcm_info_get_name(pcminfo));

			alsa_scan_handle_dev(&devices, cardname, hwdev,
					     di, pcminfo);
		}
		if ((ret = snd_ctl_close(handle)) < 0) {
			sr_err("Cannot close device (%d): %s.",
			       card, snd_strerror(ret));
		}
	}

	snd_pcm_info_free(pcminfo);
	snd_ctl_card_info_free(info);

	return devices;
}

/*
 * Helper to be used with g_slist_free_full(); for properly freeing an alsa
 * dev instance.
 */
SR_PRIV void alsa_dev_inst_clear(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return;

	snd_pcm_hw_params_free(devc->hw_params);
	g_free((void*)devc->supp_rates.list);
	sr_dev_inst_free(sdi);
}

/**
 * Set the samplerate of the ALSA device.
 *
 * Changes the samplerate of the given ALSA device if the specified samplerate
 * is supported by the hardware.
 *
 * The new samplerate is recorded, but it is not applied to the hardware. The
 * samplerate is applied to the hardware only when acquisition is started via
 * dev_acquisition_start(), and cannot be changed during acquisition. To change
 * the samplerate, several steps are needed:
 *
 *    1) If acquisition is running, it must first be stopped.
 *    2) dev_config_set() must be called with the new samplerate.
 *    3) When starting a new acquisition, the new samplerate is applied.
 *
 */
SR_PRIV int alsa_set_samplerate(const struct sr_dev_inst *sdi,
				uint64_t newrate)
{
	struct dev_context *devc;
	size_t i;
	uint64_t rate = 0;

	if (!(devc = sdi->priv))
		return SR_ERR_ARG;

	i = 0;
	do {
		if (newrate == devc->supp_rates.list[i]) {
			rate = newrate;
			break;
		}
	} while (devc->supp_rates.list[i++] != 0);

	if (!rate) {
		sr_err("Sample rate %" PRIu64 " not supported.", newrate);
		return SR_ERR_ARG;
	}

	devc->cur_samplerate = rate;
	return SR_OK;
}

SR_PRIV int alsa_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	int16_t inbuf[4096];
	int i, x, count, offset, samples_to_get;
	int16_t tmp16;
	const float s16norm = 1 / (float)(1 << 15);

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	memset(&analog, 0, sizeof(struct sr_datafeed_analog));
	memset(inbuf, 0, sizeof(inbuf));

	samples_to_get = MIN(4096 / 4, devc->limit_samples);

	sr_spew("Getting %d samples from audio device.", samples_to_get);
	count = snd_pcm_readi(devc->capture_handle, inbuf, samples_to_get);

	if (count < 0) {
		sr_err("Failed to read samples: %s.", snd_strerror(count));
		return FALSE;
	} else if (count != samples_to_get) {
		sr_spew("Only got %d/%d samples.", count, samples_to_get);
	}

	analog.data = g_try_malloc0(count * sizeof(float) * devc->num_probes);
	if (!analog.data) {
		sr_err("Failed to malloc sample buffer.");
		return FALSE;
	}

	offset = 0;
	/*
	 * It's impossible to know what voltage levels the soundcard handles.
	 * Some handle 0 dBV rms, some 0dBV peak-to-peak, +4dbmW (600 ohm), etc
	 * Each of these corresponds to a different voltage, and there is no
	 * mechanism to determine this voltage. The best solution is to send all
	 * audio data as a normalized float, and let the frontend or user worry
	 * about the calibration.
	 */
	for (i = 0; i < count; i += devc->num_probes) {
		for (x = 0; x < devc->num_probes; x++) {
			tmp16 = inbuf[i + x];
			analog.data[offset++] = tmp16 * s16norm;
		}
	}

	/* Send a sample packet with the analog values. */
	analog.num_samples = count;
	analog.mq = SR_MQ_VOLTAGE; /* FIXME */
	analog.unit = SR_UNIT_VOLT; /* FIXME */
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	g_free(analog.data);

	devc->num_samples += count;

	/* Stop acquisition if we acquired enough samples. */
	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	return TRUE;
}
