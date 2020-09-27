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
#include "protocol.h"

SR_PRIV int send_shortcommand(struct sr_serial_dev_inst *serial,
		uint8_t command)
{
	char buf[1];

	sr_dbg("Sending cmd 0x%.2x.", command);
	buf[0] = command;
	if (serial_write_blocking(serial, buf, 1, serial_timeout(serial, 1)) != 1)
		return SR_ERR;

	if (serial_drain(serial) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int send_longcommand(struct sr_serial_dev_inst *serial,
		uint8_t command, uint8_t *data)
{
	char buf[5];

	sr_dbg("Sending cmd 0x%.2x data 0x%.2x%.2x%.2x%.2x.", command,
			data[0], data[1], data[2], data[3]);
	buf[0] = command;
	buf[1] = data[0];
	buf[2] = data[1];
	buf[3] = data[2];
	buf[4] = data[3];
	if (serial_write_blocking(serial, buf, 5, serial_timeout(serial, 1)) != 5)
		return SR_ERR;

	if (serial_drain(serial) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int ols_send_reset(struct sr_serial_dev_inst *serial)
{
	unsigned int i;

	for (i = 0; i < 5; i++) {
		if (send_shortcommand(serial, CMD_RESET) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

/* Configures the channel mask based on which channels are enabled. */
SR_PRIV void ols_channel_mask(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *channel;
	const GSList *l;

	devc = sdi->priv;

	devc->channel_mask = 0;
	for (l = sdi->channels; l; l = l->next) {
		channel = l->data;
		if (channel->enabled)
			devc->channel_mask |= 1 << channel->index;
	}
}

SR_PRIV int ols_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	int i;

	devc = sdi->priv;

	devc->num_stages = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		devc->trigger_mask[i] = 0;
		devc->trigger_value[i] = 0;
	}

	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;

	devc->num_stages = g_slist_length(trigger->stages);
	if (devc->num_stages > NUM_TRIGGER_STAGES) {
		sr_err("This device only supports %d trigger stages.",
				NUM_TRIGGER_STAGES);
		return SR_ERR;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			devc->trigger_mask[stage->stage] |= 1 << match->channel->index;
			if (match->match == SR_TRIGGER_ONE)
				devc->trigger_value[stage->stage] |= 1 << match->channel->index;
		}
	}

	return SR_OK;
}

SR_PRIV struct dev_context *ols_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->trigger_at_smpl = OLS_NO_TRIGGER;

	return devc;
}

static void ols_channel_new(struct sr_dev_inst *sdi, int num_chan)
{
	struct dev_context *devc = sdi->priv;
	int i;

	for (i = 0; i < num_chan; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
				ols_channel_names[i]);

	devc->max_channels = num_chan;
}

static void metadata_quirks(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gboolean is_shrimp;

	if (!sdi)
		return;
	devc = sdi->priv;
	if (!devc)
		return;

	is_shrimp = sdi->model && strcmp(sdi->model, "Shrimp1.0") == 0;
	if (is_shrimp) {
		if (!devc->max_channels)
			ols_channel_new(sdi, 4);
		if (!devc->max_samples)
			devc->max_samples = 256 * 1024;
		if (!devc->max_samplerate)
			devc->max_samplerate = SR_MHZ(20);
	}

	if (sdi->version && strstr(sdi->version, "FPGA version 3.07"))
		devc->device_flags |= DEVICE_FLAG_IS_DEMON_CORE;
}

SR_PRIV struct sr_dev_inst *get_metadata(struct sr_serial_dev_inst *serial)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint32_t tmp_int;
	uint8_t key, type;
	int delay_ms;
	GString *tmp_str, *devname, *version;
	guchar tmp_c;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	devc = ols_dev_new();
	sdi->priv = devc;

	devname = g_string_new("");
	version = g_string_new("");

	key = 0xff;
	while (key) {
		delay_ms = serial_timeout(serial, 1);
		if (serial_read_blocking(serial, &key, 1, delay_ms) != 1)
			break;
		if (key == METADATA_TOKEN_END) {
			sr_dbg("Got metadata key 0x00, metadata ends.");
			break;
		}
		type = key >> 5;
		switch (type) {
		case 0:
			/* NULL-terminated string */
			tmp_str = g_string_new("");
			delay_ms = serial_timeout(serial, 1);
			while (serial_read_blocking(serial, &tmp_c, 1, delay_ms) == 1 && tmp_c != '\0')
				g_string_append_c(tmp_str, tmp_c);
			sr_dbg("Got metadata token 0x%.2x value '%s'.", key, tmp_str->str);
			switch (key) {
			case METADATA_TOKEN_DEVICE_NAME:
				/* Device name */
				devname = g_string_append(devname, tmp_str->str);
				break;
			case METADATA_TOKEN_FPGA_VERSION:
				/* FPGA firmware version */
				if (version->len)
					g_string_append(version, ", ");
				g_string_append(version, "FPGA version ");
				g_string_append(version, tmp_str->str);
				break;
			case METADATA_TOKEN_ANCILLARY_VERSION:
				/* Ancillary version */
				if (version->len)
					g_string_append(version, ", ");
				g_string_append(version, "Ancillary version ");
				g_string_append(version, tmp_str->str);
				break;
			default:
				sr_info("ols: unknown token 0x%.2x: '%s'", key, tmp_str->str);
				break;
			}
			g_string_free(tmp_str, TRUE);
			break;
		case 1:
			/* 32-bit unsigned integer */
			delay_ms = serial_timeout(serial, 4);
			if (serial_read_blocking(serial, &tmp_int, 4, delay_ms) != 4)
				break;
			tmp_int = RB32(&tmp_int);
			sr_dbg("Got metadata token 0x%.2x value 0x%.8x.", key, tmp_int);
			switch (key) {
			case METADATA_TOKEN_NUM_PROBES_LONG:
				/* Number of usable channels */
				ols_channel_new(sdi, tmp_int);
				break;
			case METADATA_TOKEN_SAMPLE_MEMORY_BYTES:
				/* Amount of sample memory available (bytes) */
				devc->max_samples = tmp_int;
				break;
			case METADATA_TOKEN_DYNAMIC_MEMORY_BYTES:
				/* Amount of dynamic memory available (bytes) */
				/* what is this for? */
				break;
			case METADATA_TOKEN_MAX_SAMPLE_RATE_HZ:
				/* Maximum sample rate (Hz) */
				devc->max_samplerate = tmp_int;
				break;
			case METADATA_TOKEN_PROTOCOL_VERSION_LONG:
				/* protocol version */
				devc->protocol_version = tmp_int;
				break;
			default:
				sr_info("Unknown token 0x%.2x: 0x%.8x.", key, tmp_int);
				break;
			}
			break;
		case 2:
			/* 8-bit unsigned integer */
			delay_ms = serial_timeout(serial, 1);
			if (serial_read_blocking(serial, &tmp_c, 1, delay_ms) != 1)
				break;
			sr_dbg("Got metadata token 0x%.2x value 0x%.2x.", key, tmp_c);
			switch (key) {
			case METADATA_TOKEN_NUM_PROBES_SHORT:
				/* Number of usable channels */
				ols_channel_new(sdi, tmp_c);
				break;
			case METADATA_TOKEN_PROTOCOL_VERSION_SHORT:
				/* protocol version */
				devc->protocol_version = tmp_c;
				break;
			default:
				sr_info("Unknown token 0x%.2x: 0x%.2x.", key, tmp_c);
				break;
			}
			break;
		default:
			/* unknown type */
			break;
		}
	}

	sdi->model = devname->str;
	sdi->version = version->str;
	g_string_free(devname, FALSE);
	g_string_free(version, FALSE);

	/* Optionally amend received metadata, model specific quirks. */
	metadata_quirks(sdi);

	return sdi;
}

SR_PRIV int ols_set_samplerate(const struct sr_dev_inst *sdi,
		const uint64_t samplerate)
{
	struct dev_context *devc;

	devc = sdi->priv;
	if (devc->max_samplerate && samplerate > devc->max_samplerate)
		return SR_ERR_SAMPLERATE;

	if (samplerate > CLOCK_RATE) {
		sr_info("Enabling demux mode.");
		devc->capture_flags |= CAPTURE_FLAG_DEMUX;
		devc->capture_flags &= ~CAPTURE_FLAG_NOISE_FILTER;
		devc->cur_samplerate_divider = (CLOCK_RATE * 2 / samplerate) - 1;
	} else {
		sr_info("Disabling demux mode.");
		devc->capture_flags &= ~CAPTURE_FLAG_DEMUX;
		devc->capture_flags |= CAPTURE_FLAG_NOISE_FILTER;
		devc->cur_samplerate_divider = (CLOCK_RATE / samplerate) - 1;
	}

	/* Calculate actual samplerate used and complain if it is different
	 * from the requested.
	 */
	devc->cur_samplerate = CLOCK_RATE / (devc->cur_samplerate_divider + 1);
	if (devc->capture_flags & CAPTURE_FLAG_DEMUX)
		devc->cur_samplerate *= 2;
	if (devc->cur_samplerate != samplerate)
		sr_info("Can't match samplerate %" PRIu64 ", using %"
		       PRIu64 ".", samplerate, devc->cur_samplerate);

	return SR_OK;
}

SR_PRIV void abort_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	ols_send_reset(serial);

	serial_source_remove(sdi->session, serial);

	std_session_send_df_end(sdi);
}

SR_PRIV int ols_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t sample;
	int num_ols_changrp, offset, j;
	unsigned int i;
	unsigned char byte;

	(void)fd;

	sdi = cb_data;
	serial = sdi->conn;
	devc = sdi->priv;

	if (devc->num_transfers == 0 && revents == 0) {
		/* Ignore timeouts as long as we haven't received anything */
		return TRUE;
	}

	if (devc->num_transfers++ == 0) {
		devc->raw_sample_buf = g_try_malloc(devc->limit_samples * 4);
		if (!devc->raw_sample_buf) {
			sr_err("Sample buffer malloc failed.");
			return FALSE;
		}
		/* fill with 1010... for debugging */
		memset(devc->raw_sample_buf, 0x82, devc->limit_samples * 4);
	}

	num_ols_changrp = 0;
	for (i = 0x20; i > 0x02; i >>= 1) {
		if ((devc->capture_flags & i) == 0) {
			num_ols_changrp++;
		}
	}

	if (revents == G_IO_IN && devc->num_samples < devc->limit_samples) {
		if (serial_read_nonblocking(serial, &byte, 1) != 1)
			return FALSE;
		devc->cnt_bytes++;

		/* Ignore it if we've read enough. */
		if (devc->num_samples >= devc->limit_samples)
			return TRUE;

		devc->sample[devc->num_bytes++] = byte;
		sr_spew("Received byte 0x%.2x.", byte);
		if (devc->num_bytes == num_ols_changrp) {
			devc->cnt_samples++;
			devc->cnt_samples_rle++;
			/*
			 * Got a full sample. Convert from the OLS's little-endian
			 * sample to the local format.
			 */
			sample = devc->sample[0] | (devc->sample[1] << 8) \
					| (devc->sample[2] << 16) | (devc->sample[3] << 24);
			sr_dbg("Received sample 0x%.*x.", devc->num_bytes * 2, sample);
			if (devc->capture_flags & CAPTURE_FLAG_RLE) {
				/*
				 * In RLE mode the high bit of the sample is the
				 * "count" flag, meaning this sample is the number
				 * of times the previous sample occurred.
				 */
				if (devc->sample[devc->num_bytes - 1] & 0x80) {
					/* Clear the high bit. */
					sample &= ~(0x80 << (devc->num_bytes - 1) * 8);
					devc->rle_count = sample;
					devc->cnt_samples_rle += devc->rle_count;
					sr_dbg("RLE count: %u.", devc->rle_count);
					devc->num_bytes = 0;
					return TRUE;
				}
			}
			devc->num_samples += devc->rle_count + 1;
			if (devc->num_samples > devc->limit_samples) {
				/* Save us from overrunning the buffer. */
				devc->rle_count -= devc->num_samples - devc->limit_samples;
				devc->num_samples = devc->limit_samples;
			}

			if (num_ols_changrp < 4) {
				/*
				 * Some channel groups may have been turned
				 * off, to speed up transfer between the
				 * hardware and the PC. Expand that here before
				 * submitting it over the session bus --
				 * whatever is listening on the bus will be
				 * expecting a full 32-bit sample, based on
				 * the number of channels.
				 */
				j = 0;
				memset(devc->tmp_sample, 0, 4);
				for (i = 0; i < 4; i++) {
					if (((devc->capture_flags >> 2) & (1 << i)) == 0) {
						/*
						 * This channel group was
						 * enabled, copy from received
						 * sample.
						 */
						devc->tmp_sample[i] = devc->sample[j++];
					} else if (devc->capture_flags & CAPTURE_FLAG_DEMUX && (i > 2)) {
						/* group 2 & 3 get added to 0 & 1 */
						devc->tmp_sample[i - 2] = devc->sample[j++];
					}
				}
				memcpy(devc->sample, devc->tmp_sample, 4);
				sr_spew("Expanded sample: 0x%.8x.", sample);
			}

			/*
			 * the OLS sends its sample buffer backwards.
			 * store it in reverse order here, so we can dump
			 * this on the session bus later.
			 */
			offset = (devc->limit_samples - devc->num_samples) * 4;
			for (i = 0; i <= devc->rle_count; i++) {
				memcpy(devc->raw_sample_buf + offset + (i * 4),
				       devc->sample, 4);
			}
			memset(devc->sample, 0, 4);
			devc->num_bytes = 0;
			devc->rle_count = 0;
		}
	} else {
		/*
		 * This is the main loop telling us a timeout was reached, or
		 * we've acquired all the samples we asked for -- we're done.
		 * Send the (properly-ordered) buffer to the frontend.
		 */
		sr_dbg("Received %d bytes, %d samples, %d decompressed samples.",
				devc->cnt_bytes, devc->cnt_samples,
				devc->cnt_samples_rle);
		if (devc->trigger_at_smpl != OLS_NO_TRIGGER) {
			/*
			 * A trigger was set up, so we need to tell the frontend
			 * about it.
			 */
			if (devc->trigger_at_smpl > 0) {
				/* There are pre-trigger samples, send those first. */
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = devc->trigger_at_smpl * 4;
				logic.unitsize = 4;
				logic.data = devc->raw_sample_buf +
					(devc->limit_samples - devc->num_samples) * 4;
				sr_session_send(sdi, &packet);
			}

			/* Send the trigger. */
			std_session_send_df_trigger(sdi);
		}

		/* Send post-trigger / all captured samples. */
		int num_pre_trigger_samples = devc->trigger_at_smpl == OLS_NO_TRIGGER
			? 0 : devc->trigger_at_smpl;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = (devc->num_samples - num_pre_trigger_samples) * 4;
		logic.unitsize = 4;
		logic.data = devc->raw_sample_buf + (num_pre_trigger_samples +
			devc->limit_samples - devc->num_samples) * 4;
		sr_session_send(sdi, &packet);

		g_free(devc->raw_sample_buf);

		serial_flush(serial);
		abort_acquisition(sdi);
	}

	return TRUE;
}
