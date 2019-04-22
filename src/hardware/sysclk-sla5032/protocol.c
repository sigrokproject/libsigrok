/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Vitaliy Vorobyov
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
#include <string.h>
#include "protocol.h"
#include "sla5032.h"

/* Callback handling data */
static int la_prepare_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int i, j, ret, xfer_len;
	uint8_t *rle_buf, *samples;
	const uint8_t *p, *q;
	uint16_t rle_count;
	int samples_count, rle_samples_count;
	uint32_t status[3];
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t value;
	int trigger_offset;

	enum {
		RLE_SAMPLE_SIZE = sizeof(uint32_t) + sizeof(uint16_t),
		RLE_SAMPLES_COUNT = 0x100000,
		RLE_BUF_SIZE = RLE_SAMPLES_COUNT * RLE_SAMPLE_SIZE,
		RLE_END_MARKER = 0xFFFF,
	};

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	usb = sdi->conn;

	memset(status, 0, sizeof(status));
	ret = sla5032_get_status(usb, status);
	if (ret != SR_OK) {
		sla5032_write_reg14_zero(usb);
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	/* data not ready (acquision in progress) */
	if (status[1] != 3)
		return G_SOURCE_CONTINUE;

	sr_dbg("acquision done, status: %u.", (unsigned int)status[2]);

	/* data ready (download, decode and send to sigrok) */
	ret = sla5032_set_read_back(usb);
	if (ret != SR_OK) {
		sla5032_write_reg14_zero(usb);
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	rle_buf = g_try_malloc(RLE_BUF_SIZE);
	if (rle_buf == NULL) {
		sla5032_write_reg14_zero(usb);
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	do {
		xfer_len = 0;
		ret = sla5032_read_data_chunk(usb, rle_buf, RLE_BUF_SIZE, &xfer_len);
		if (ret != SR_OK) {
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);

			sr_dbg("acquision done, ret: %d.", ret);
			return G_SOURCE_CONTINUE;
		}

		sr_dbg("acquision done, xfer_len: %d.", xfer_len);

		if (xfer_len == 0) {
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);
			return G_SOURCE_CONTINUE;
		}

		p = rle_buf;
		samples_count = 0;
		rle_samples_count = xfer_len / RLE_SAMPLE_SIZE;

		sr_dbg("acquision done, rle_samples_count: %d.", rle_samples_count);

		for (i = 0; i < rle_samples_count; i++) {
			p += sizeof(uint32_t); /* skip sample value */

			rle_count = RL16(p); /* read RLE counter */
			p += sizeof(uint16_t);
			if (rle_count == RLE_END_MARKER) {
				rle_samples_count = i;
				break;
			}
			samples_count += rle_count + 1;
		}
		sr_dbg("acquision done, samples_count: %d.", samples_count);

		if (samples_count == 0) {
			sr_dbg("acquision done, no samples.");
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);
			return G_SOURCE_CONTINUE;
		}

		/* Decode RLE */
		samples = g_try_malloc(samples_count * sizeof(uint32_t));
		if (!samples) {
			sr_dbg("memory allocation error.");
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);
			return G_SOURCE_CONTINUE;
		}

		p = rle_buf;
		q = samples;
		for (i = 0; i < rle_samples_count; i++) {
			value = RL32(p);
			p += sizeof(uint32_t); /* read sample value */

			rle_count = RL16(p); /* read RLE counter */
			p += sizeof(uint16_t);

			if (rle_count == RLE_END_MARKER) {
				sr_dbg("RLE end marker found.");
				break;
			}

			for (j = 0; j <= rle_count; j++) {
				WL32(q, value);
				q += sizeof(uint32_t);
			}
		}

		if (devc->trigger_fired) {
			/* Send the incoming transfer to the session bus. */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;

			logic.length = samples_count * sizeof(uint32_t);
			logic.unitsize = sizeof(uint32_t);
			logic.data = samples;
			sr_session_send(sdi, &packet);
		} else {
			trigger_offset = soft_trigger_logic_check(devc->stl,
				samples, samples_count * sizeof(uint32_t), NULL);
			if (trigger_offset > -1) {
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				int num_samples = samples_count - trigger_offset;

				logic.length = num_samples * sizeof(uint32_t);
				logic.unitsize = sizeof(uint32_t);
				logic.data = samples + trigger_offset * sizeof(uint32_t);
				sr_session_send(sdi, &packet);

				devc->trigger_fired = TRUE;
			}
		}

		g_free(samples);
	} while (rle_samples_count == RLE_SAMPLES_COUNT);

	sr_dbg("acquision stop, rle_samples_count < RLE_SAMPLES_COUNT.");

	sla5032_write_reg14_zero(usb);

	sr_dev_acquisition_stop(sdi); /* if all data transfered */

	g_free(rle_buf);

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}

	return G_SOURCE_CONTINUE;
}

SR_PRIV int la_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_trigger* trigger;
	int ret;
	enum { poll_interval_ms = 100 };
	uint64_t pre, post;

	devc = sdi->priv;
	usb = sdi->conn;

	if (devc->state != STATE_IDLE) {
		sr_err("Not in idle state, cannot start acquisition.");
		return SR_ERR;
	}

	pre = (devc->limit_samples * devc->capture_ratio) / 100;
	post = devc->limit_samples - pre;

	if ((trigger = sr_session_trigger_get(sdi->session))) {
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre);
		if (!devc->stl) {
			sr_err("stl alloc error.");
			return SR_ERR_MALLOC;
		}
		devc->trigger_fired = FALSE;
	}
	else
		devc->trigger_fired = TRUE;

	sr_dbg("start acquision, smp lim: %" PRIu64 ", cap ratio: %" PRIu64
	       ".", devc->limit_samples, devc->capture_ratio);

	sr_dbg("start acquision, pre: %" PRIu64 ", post: %" PRIu64 ".", pre, post);
	pre /= 256;
	pre = MAX(pre, 2);
	pre--;

	post /= 256;
	post = MAX(post, 2);
	post--;

	sr_dbg("start acquision, pre: %" PRIx64 ", post: %" PRIx64 ".", pre, post);

	/* (x + 1) * 256 (samples)  pre, post */
	ret = sla5032_set_depth(usb, pre, post);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_set_triggers(usb, devc->trigger_values, devc->trigger_edge_mask, devc->trigger_mask);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_set_samplerate(usb, devc->samplerate);
	if (ret != SR_OK)
		return ret;

	/* TODO: make PWM generator as separate configurable subdevice */
	enum {
		pwm1_hi = 20000000 - 1,
		pwm1_lo = 200000 - 1,
		pwm2_hi = 15 - 1,
		pwm2_lo = 5 - 1,
	};

	ret = sla5032_set_pwm1(usb, pwm1_hi, pwm1_lo);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_set_pwm2(usb, pwm2_hi, pwm2_lo);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_start_sample(usb);
	if (ret != SR_OK)
		return ret;

	sr_session_source_add(sdi->session, -1, 0, poll_interval_ms,
			la_prepare_data, (struct sr_dev_inst *)sdi);

	std_session_send_df_header(sdi);

	return ret;
}
