/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Sean W Jasin <swjasin03@gmail.com>
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
#include "protocol.h"

SR_PRIV int adalm2k_driver_set_samplerate(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct iio_context *m2k_ctx;
    struct iio_device *m2k_dev;
    struct iio_attr *attr_sr;
    int err;

    devc = sdi->priv;
    m2k_ctx = devc->m2k;

    m2k_dev = iio_context_find_device(m2k_ctx, M2K_RX);
    if (!m2k_dev) {
        return SR_ERR;
    }

    attr_sr = (struct iio_attr *)iio_device_find_attr(m2k_dev, "sampling_frequency");
    err = iio_attr_write_longlong(attr_sr, devc->samplerate);
    if (err < 0) {
        return SR_ERR_SAMPLERATE;
    }

    return SR_OK;
}

SR_PRIV int adalm2k_driver_enable_channel(const struct sr_dev_inst *sdi, int index) {
    struct dev_context *devc;
    struct iio_context *m2k_ctx;
    struct iio_device *m2k_dev;
    struct iio_channel *m2k_chn;

    devc = sdi->priv;
    m2k_ctx = devc->m2k;
    m2k_dev = iio_context_find_device(m2k_ctx, M2K_RX);
    if (!m2k_dev) {
        return SR_ERR;
    }

    m2k_chn = iio_device_get_channel(m2k_dev, index);
    if (!m2k_chn) {
        return SR_ERR;
    }

    iio_channel_enable(m2k_chn, devc->mask);

    return SR_OK;
}

SR_PRIV int adalm2k_driver_nb_enabled_channels(const struct sr_dev_inst *sdi, int type)
{
	struct sr_channel *ch;
	int nb_channels;
	GSList *l;

	nb_channels = 0;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == type) {
			if (ch->enabled) {
				nb_channels++;
			}
		}
	}
	return nb_channels;
}

SR_PRIV int adalm2k_driver_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_logic logic;
    uint64_t samples_todo, logic_done, analog_done, sending_now, analog_sent;
    uint16_t *logic_data;
    /* float **analog_data; */
    /* GSList *l; */

    struct iio_context *m2k_ctx;
    struct iio_device *m2k_dev;
    struct iio_channel *m2k_chn;
    struct iio_buffer *m2k_buf;
    struct iio_block *m2k_blk;
    struct iio_channels_mask *m2k_msk;
    uint32_t smp_size;

	(void)fd;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	if (revents == G_IO_IN) {
        return TRUE;
	}

    m2k_ctx = devc->m2k;
    m2k_dev = iio_context_find_device(m2k_ctx, M2K_RX);
    if(!m2k_dev) {
        sr_err("Failed to make device");
        return FALSE;
    }

    m2k_msk = devc->mask;
    m2k_chn = iio_device_get_channel(m2k_dev, 0);
    if(!m2k_chn) {
        sr_err("Failed to get channel");
        return FALSE;
    }
    iio_channel_enable(m2k_chn, m2k_msk);

    smp_size= iio_device_get_sample_size(m2k_dev, m2k_msk);
    m2k_buf = iio_device_create_buffer(m2k_dev, 0, m2k_msk);
    if(iio_err(m2k_buf) < 0) {
        sr_err("Failed to make buffer");
        return FALSE;
    }
    m2k_blk = iio_buffer_create_block(m2k_buf, devc->limit_samples * smp_size);
    if(iio_err(m2k_blk) < 0) {
        sr_err("Failed to make block");
        return FALSE;
    }

    iio_block_enqueue(m2k_blk, 0, false);
    iio_buffer_enable(m2k_buf);
    iio_block_dequeue(m2k_blk, false);

    logic_data = iio_block_first(m2k_blk, m2k_chn);

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
    logic.unitsize = 2;

    sending_now = devc->limit_samples;

    logic.length = sending_now * logic.unitsize;
    logic.data = logic_data;

    iio_buffer_destroy(m2k_buf);

	sr_session_send(sdi, &packet);

    sdi->driver->dev_acquisition_stop((struct sr_dev_inst *)sdi);
	return TRUE;
}

/* SR_PRIV uint8_t * adalm2k_driver_get_samples(struct sr_dev_inst *sdi, long samples) { */
/*     return NULL; */
/* } */
