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

#include "protocol.h"
#include "libsigrok.h"
#include "libsigrok-internal.h"

SR_PRIV int alsa_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	char inbuf[4096];
	int i, x, count, offset, samples_to_get;
	uint16_t tmp16;

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

	for (i = 0; i < count; i++) {
		for (x = 0; x < devc->num_probes; x++) {
			tmp16 = *(uint16_t *)(inbuf + (i * 4) + (x * 2));
			analog.data[offset++] = (float)tmp16;
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
