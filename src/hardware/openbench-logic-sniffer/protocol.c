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

#include "protocol.h"

extern SR_PRIV struct sr_dev_driver ols_driver_info;

SR_PRIV int send_shortcommand(struct sr_serial_dev_inst *serial,
		uint8_t command)
{
	char buf[1];

	sr_dbg("Sending cmd 0x%.2x.", command);
	buf[0] = command;
	if (serial_write_blocking(serial, buf, 1, serial_timeout(serial, 1)) != 1)
		return SR_ERR;

	if (serial_drain(serial) != 0)
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

	if (serial_drain(serial) != 0)
		return SR_ERR;

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

	/* Device-specific settings */
	devc->max_samples = devc->max_samplerate = devc->protocol_version = 0;

	/* Acquisition settings */
	devc->limit_samples = devc->capture_ratio = 0;
	devc->trigger_at = -1;
	devc->channel_mask = 0xffffffff;
	devc->flag_reg = 0;

	return devc;
}

SR_PRIV struct sr_dev_inst *get_metadata(struct sr_serial_dev_inst *serial)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint32_t tmp_int, ui;
	uint8_t key, type, token;
	int delay_ms;
	GString *tmp_str, *devname, *version;
	guchar tmp_c;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->driver = &ols_driver_info;
	devc = ols_dev_new();
	sdi->priv = devc;

	devname = g_string_new("");
	version = g_string_new("");

	key = 0xff;
	while (key) {
		delay_ms = serial_timeout(serial, 1);
		if (serial_read_blocking(serial, &key, 1, delay_ms) != 1)
			break;
		if (key == 0x00) {
			sr_dbg("Got metadata key 0x00, metadata ends.");
			break;
		}
		type = key >> 5;
		token = key & 0x1f;
		switch (type) {
		case 0:
			/* NULL-terminated string */
			tmp_str = g_string_new("");
			delay_ms = serial_timeout(serial, 1);
			while (serial_read_blocking(serial, &tmp_c, 1, delay_ms) == 1 && tmp_c != '\0')
				g_string_append_c(tmp_str, tmp_c);
			sr_dbg("Got metadata key 0x%.2x value '%s'.",
			       key, tmp_str->str);
			switch (token) {
			case 0x01:
				/* Device name */
				devname = g_string_append(devname, tmp_str->str);
				break;
			case 0x02:
				/* FPGA firmware version */
				if (version->len)
					g_string_append(version, ", ");
				g_string_append(version, "FPGA version ");
				g_string_append(version, tmp_str->str);
				break;
			case 0x03:
				/* Ancillary version */
				if (version->len)
					g_string_append(version, ", ");
				g_string_append(version, "Ancillary version ");
				g_string_append(version, tmp_str->str);
				break;
			default:
				sr_info("ols: unknown token 0x%.2x: '%s'",
					token, tmp_str->str);
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
			sr_dbg("Got metadata key 0x%.2x value 0x%.8x.",
			       key, tmp_int);
			switch (token) {
			case 0x00:
				/* Number of usable channels */
				for (ui = 0; ui < tmp_int; ui++)
					sr_channel_new(sdi, ui, SR_CHANNEL_LOGIC, TRUE,
							ols_channel_names[ui]);
				break;
			case 0x01:
				/* Amount of sample memory available (bytes) */
				devc->max_samples = tmp_int;
				break;
			case 0x02:
				/* Amount of dynamic memory available (bytes) */
				/* what is this for? */
				break;
			case 0x03:
				/* Maximum sample rate (Hz) */
				devc->max_samplerate = tmp_int;
				break;
			case 0x04:
				/* protocol version */
				devc->protocol_version = tmp_int;
				break;
			default:
				sr_info("Unknown token 0x%.2x: 0x%.8x.",
					token, tmp_int);
				break;
			}
			break;
		case 2:
			/* 8-bit unsigned integer */
			delay_ms = serial_timeout(serial, 1);
			if (serial_read_blocking(serial, &tmp_c, 1, delay_ms) != 1)
				break;
			sr_dbg("Got metadata key 0x%.2x value 0x%.2x.",
			       key, tmp_c);
			switch (token) {
			case 0x00:
				/* Number of usable channels */
				for (ui = 0; ui < tmp_c; ui++)
					sr_channel_new(sdi, ui, SR_CHANNEL_LOGIC, TRUE,
							ols_channel_names[ui]);
				break;
			case 0x01:
				/* protocol version */
				devc->protocol_version = tmp_c;
				break;
			default:
				sr_info("Unknown token 0x%.2x: 0x%.2x.",
					token, tmp_c);
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
		devc->flag_reg |= FLAG_DEMUX;
		devc->flag_reg &= ~FLAG_FILTER;
		devc->max_channels = NUM_CHANNELS / 2;
		devc->cur_samplerate_divider = (CLOCK_RATE * 2 / samplerate) - 1;
	} else {
		sr_info("Disabling demux mode.");
		devc->flag_reg &= ~FLAG_DEMUX;
		devc->flag_reg |= FLAG_FILTER;
		devc->max_channels = NUM_CHANNELS;
		devc->cur_samplerate_divider = (CLOCK_RATE / samplerate) - 1;
	}

	/* Calculate actual samplerate used and complain if it is different
	 * from the requested.
	 */
	devc->cur_samplerate = CLOCK_RATE / (devc->cur_samplerate_divider + 1);
	if (devc->flag_reg & FLAG_DEMUX)
		devc->cur_samplerate *= 2;
	if (devc->cur_samplerate != samplerate)
		sr_info("Can't match samplerate %" PRIu64 ", using %"
		       PRIu64 ".", samplerate, devc->cur_samplerate);

	return SR_OK;
}

SR_PRIV void abort_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	serial_source_remove(sdi->session, serial);

	/* Terminate session */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);
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

	if (devc->num_transfers++ == 0) {
		/*
		 * First time round, means the device started sending data,
		 * and will not stop until done. If it stops sending for
		 * longer than it takes to send a byte, that means it's
		 * finished. We'll double that to 30ms to be sure...
		 */
		serial_source_remove(sdi->session, serial);
		serial_source_add(sdi->session, serial, G_IO_IN, 30,
				ols_receive_data, cb_data);
		devc->raw_sample_buf = g_try_malloc(devc->limit_samples * 4);
		if (!devc->raw_sample_buf) {
			sr_err("Sample buffer malloc failed.");
			return FALSE;
		}
		/* fill with 1010... for debugging */
		memset(devc->raw_sample_buf, 0x82, devc->limit_samples * 4);
	}

	num_ols_changrp = 0;
	for (i = NUM_CHANNELS; i > 0x02; i /= 2) {
		if ((devc->flag_reg & i) == 0) {
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
			if (devc->flag_reg & FLAG_RLE) {
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
					if (((devc->flag_reg >> 2) & (1 << i)) == 0) {
						/*
						 * This channel group was
						 * enabled, copy from received
						 * sample.
						 */
						devc->tmp_sample[i] = devc->sample[j++];
					} else if (devc->flag_reg & FLAG_DEMUX && (i > 2)) {
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
		if (devc->trigger_at != -1) {
			/*
			 * A trigger was set up, so we need to tell the frontend
			 * about it.
			 */
			if (devc->trigger_at > 0) {
				/* There are pre-trigger samples, send those first. */
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = devc->trigger_at * 4;
				logic.unitsize = 4;
				logic.data = devc->raw_sample_buf +
					(devc->limit_samples - devc->num_samples) * 4;
				sr_session_send(cb_data, &packet);
			}

			/* Send the trigger. */
			packet.type = SR_DF_TRIGGER;
			sr_session_send(cb_data, &packet);

			/* Send post-trigger samples. */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = (devc->num_samples * 4) - (devc->trigger_at * 4);
			logic.unitsize = 4;
			logic.data = devc->raw_sample_buf + devc->trigger_at * 4 +
				(devc->limit_samples - devc->num_samples) * 4;
			sr_session_send(cb_data, &packet);
		} else {
			/* no trigger was used */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = devc->num_samples * 4;
			logic.unitsize = 4;
			logic.data = devc->raw_sample_buf +
				(devc->limit_samples - devc->num_samples) * 4;
			sr_session_send(cb_data, &packet);
		}
		g_free(devc->raw_sample_buf);

		serial_flush(serial);
		abort_acquisition(sdi);
	}

	return TRUE;
}
