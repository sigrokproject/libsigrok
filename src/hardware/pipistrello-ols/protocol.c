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

extern SR_PRIV struct sr_dev_driver p_ols_driver_info;
static struct sr_dev_driver *di = &p_ols_driver_info;

SR_PRIV int write_shortcommand(struct dev_context *devc, uint8_t command)
{
	uint8_t buf[1];
	int bytes_written;

	sr_dbg("Sending cmd 0x%.2x.", command);
	buf[0] = command;
	bytes_written = ftdi_write_data(devc->ftdic, buf, 1);
	if (bytes_written < 0) {
		sr_err("Failed to write FTDI data (%d): %s.",
		       bytes_written, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	} else if (bytes_written != 1) {
		sr_err("FTDI write error, only %d/%d bytes written: %s.",
		       bytes_written, 1, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int write_longcommand(struct dev_context *devc, uint8_t command, uint8_t *data)
{
	uint8_t buf[5];
	int bytes_written;

	sr_dbg("Sending cmd 0x%.2x data 0x%.2x%.2x%.2x%.2x.", command,
			data[0], data[1], data[2], data[3]);
	buf[0] = command;
	buf[1] = data[0];
	buf[2] = data[1];
	buf[3] = data[2];
	buf[4] = data[3];
	bytes_written = ftdi_write_data(devc->ftdic, buf, 5);
	if (bytes_written < 0) {
		sr_err("Failed to write FTDI data (%d): %s.",
		       bytes_written, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	} else if (bytes_written != 5) {
		sr_err("FTDI write error, only %d/%d bytes written: %s.",
		       bytes_written, 1, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int p_ols_open(struct dev_context *devc)
{
	int ret;

	/* Note: Caller checks devc and devc->ftdic. */

	/* Select interface B, otherwise communication will fail. */
	ret = ftdi_set_interface(devc->ftdic, INTERFACE_B);
	if (ret < 0) {
		sr_err("Failed to set FTDI interface B (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	sr_dbg("FTDI chip interface B set successfully.");

	/* Check for the device and temporarily open it. */
	ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID, USB_DEVICE_ID,
				 USB_IPRODUCT, NULL);
	if (ret < 0) {
		/* Log errors, except for -3 ("device not found"). */
		if (ret != -3)
			sr_err("Failed to open device (%d): %s", ret,
			       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	sr_dbg("FTDI device opened successfully.");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0) {
		sr_err("Failed to purge FTDI RX/TX buffers (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_open_close_ftdic;
	}
	sr_dbg("FTDI chip buffers purged successfully.");

	/* Reset the FTDI bitmode. */
	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_RESET);
	if (ret < 0) {
		sr_err("Failed to reset the FTDI chip bitmode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_open_close_ftdic;
	}
	sr_dbg("FTDI chip bitmode reset successfully.");

	/* Set the FTDI latency timer to 16. */
	ret = ftdi_set_latency_timer(devc->ftdic, 16);
	if (ret < 0) {
		sr_err("Failed to set FTDI latency timer (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_open_close_ftdic;
	}
	sr_dbg("FTDI chip latency timer set successfully.");

	/* Set the FTDI read data chunk size to 64kB. */
	ret = ftdi_read_data_set_chunksize(devc->ftdic, 64 * 1024);
	if (ret < 0) {
		sr_err("Failed to set FTDI read data chunk size (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_open_close_ftdic;
	}
	sr_dbg("FTDI chip read data chunk size set successfully.");
	
	return SR_OK;

err_open_close_ftdic:
	ftdi_usb_close(devc->ftdic);
	return SR_ERR;
}

SR_PRIV int p_ols_close(struct dev_context *devc)
{
	int ret;

	/* Note: Caller checks devc and devc->ftdic. */

	if ((ret = ftdi_usb_close(devc->ftdic)) < 0) {
		sr_err("Failed to close FTDI device (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	return SR_OK;
}

/* Configures the channel mask based on which channels are enabled. */
SR_PRIV void pols_channel_mask(const struct sr_dev_inst *sdi)
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

SR_PRIV int pols_convert_trigger(const struct sr_dev_inst *sdi)
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
		devc->trigger_edge[i] = 0;
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
			if (match->match == SR_TRIGGER_ONE || match->match == SR_TRIGGER_RISING)
				devc->trigger_value[stage->stage] |= 1 << match->channel->index;
			if (match->match == SR_TRIGGER_RISING || match->match == SR_TRIGGER_FALLING)
				devc->trigger_edge[stage->stage] |= 1 << match->channel->index;
		}
	}

	return SR_OK;
}

SR_PRIV struct sr_dev_inst *p_ols_get_metadata(uint8_t *buf, int bytes_read, struct dev_context *devc)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	uint32_t tmp_int, ui;
	uint8_t key, type, token;
	GString *tmp_str, *devname, *version;
	guchar tmp_c;
	int index, i;

	sdi = sr_dev_inst_new();
	sdi->status = SR_ST_INACTIVE;
	sdi->driver = di;
	sdi->priv = devc;

	devname = g_string_new("");
	version = g_string_new("");

	index = 0;
	while (index < bytes_read) {
		key = buf[index++];
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
			while ((index < bytes_read) && ((tmp_c = buf[index++]) != '\0'))
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
				sr_info("Unknown token 0x%.2x: '%s'",
					token, tmp_str->str);
				break;
			}
			g_string_free(tmp_str, TRUE);
			break;
		case 1:
			/* 32-bit unsigned integer */
			tmp_int = 0;
			for (i = 0; i < 4; i++) {
				tmp_int = (tmp_int << 8) | buf[index++];
			}
			sr_dbg("Got metadata key 0x%.2x value 0x%.8x.",
			       key, tmp_int);
			switch (token) {
			case 0x00:
				/* Number of usable channels */
				for (ui = 0; ui < tmp_int; ui++) {
					if (!(ch = sr_channel_new(ui, SR_CHANNEL_LOGIC, TRUE,
							p_ols_channel_names[ui])))
						return 0;
					sdi->channels = g_slist_append(sdi->channels, ch);
				}
				break;
			case 0x01:
				/* Amount of sample memory available (bytes) */
				devc->max_samplebytes = tmp_int;
				break;
			case 0x02:
				/* Amount of dynamic memory available (bytes) */
				/* what is this for? */
				break;
			case 0x03:
				/* Maximum sample rate (hz) */
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
			tmp_c = buf[index++];
			sr_dbg("Got metadata key 0x%.2x value 0x%.2x.",
			       key, tmp_c);
			switch (token) {
			case 0x00:
				/* Number of usable channels */
				for (ui = 0; ui < tmp_c; ui++) {
					if (!(ch = sr_channel_new(ui, SR_CHANNEL_LOGIC, TRUE,
							p_ols_channel_names[ui])))
						return 0;
					sdi->channels = g_slist_append(sdi->channels, ch);
				}
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

SR_PRIV int p_ols_set_samplerate(const struct sr_dev_inst *sdi,
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


SR_PRIV int p_ols_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t sample;
	int num_channels, offset, j;
	int bytes_read, index;
	unsigned int i;
	unsigned char byte;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	if (devc->num_transfers++ == 0) {
		devc->raw_sample_buf = g_try_malloc(devc->limit_samples * 4);
		if (!devc->raw_sample_buf) {
			sr_err("Sample buffer malloc failed.");
			return FALSE;
		}
		/* fill with 1010... for debugging */
		memset(devc->raw_sample_buf, 0x82, devc->limit_samples * 4);
	}

	if ((devc->num_samples < devc->limit_samples) && (devc->cnt_samples < devc->max_samples)) {

		num_channels = 0;
		for (i = NUM_CHANNELS; i > 0x02; i /= 2) {
			if ((devc->flag_reg & i) == 0) {
				num_channels++;
			}
		}

		/* Get a block of data. */
		bytes_read = ftdi_read_data(devc->ftdic, devc->ftdi_buf, FTDI_BUF_SIZE);
		if (bytes_read < 0) {
			sr_err("Failed to read FTDI data (%d): %s.",
			       bytes_read, ftdi_get_error_string(devc->ftdic));
			sdi->driver->dev_acquisition_stop(sdi, sdi);
			return FALSE;
		}
		if (bytes_read == 0) {
			sr_spew("Received 0 bytes, nothing to do.");
			return TRUE;
		}

		sr_dbg("Received %d bytes", bytes_read);

		index = 0;
		while (index < bytes_read) {
			byte = devc->ftdi_buf[index++];
			devc->cnt_bytes++;

			devc->sample[devc->num_bytes++] = byte;
			sr_spew("Received byte 0x%.2x.", byte);

			if ((devc->flag_reg & FLAG_DEMUX) && (devc->flag_reg & FLAG_RLE)) {
				/* RLE in demux mode must be processed differently 
				* since in this case the RLE encoder is operating on pairs of samples.
				*/
				if (devc->num_bytes == num_channels * 2) {
					devc->cnt_samples += 2;
					devc->cnt_samples_rle += 2;
					/*
					 * Got a sample pair. Convert from the OLS's little-endian
					 * sample to the local format.
					 */
					sample = devc->sample[0] | (devc->sample[1] << 8) \
							| (devc->sample[2] << 16) | (devc->sample[3] << 24);
					sr_spew("Received sample pair 0x%.*x.", devc->num_bytes * 2, sample);

					/*
					 * In RLE mode the high bit of the sample pair is the
					 * "count" flag, meaning this sample pair is the number
					 * of times the previous sample pair occurred.
					 */
					if (devc->sample[devc->num_bytes - 1] & 0x80) {
						/* Clear the high bit. */
						sample &= ~(0x80 << (devc->num_bytes - 1) * 8);
						devc->rle_count = sample;
						devc->cnt_samples_rle += devc->rle_count * 2;
						sr_dbg("RLE count: %u.", devc->rle_count * 2);
						devc->num_bytes = 0;
						continue;
					}
					devc->num_samples += (devc->rle_count + 1) * 2;
					if (devc->num_samples > devc->limit_samples) {
						/* Save us from overrunning the buffer. */
						devc->rle_count -= (devc->num_samples - devc->limit_samples) / 2;
						devc->num_samples = devc->limit_samples;
						index = bytes_read;
					}

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
					/* expand first sample */
					memset(devc->tmp_sample, 0, 4);
					for (i = 0; i < 2; i++) {
						if (((devc->flag_reg >> 2) & (1 << i)) == 0) {
							/*
							 * This channel group was
							 * enabled, copy from received
							 * sample.
							 */
							devc->tmp_sample[i] = devc->sample[j++];
						} 
					}
					/* Clear out the most significant bit of the sample */
					devc->tmp_sample[devc->num_bytes - 1] &= 0x7f;
					sr_spew("Expanded sample 1: 0x%.8x.", devc->tmp_sample);

					/* expand second sample */
					memset(devc->tmp_sample2, 0, 4);
					for (i = 0; i < 2; i++) {
						if (((devc->flag_reg >> 2) & (1 << i)) == 0) {
							/*
							 * This channel group was
							 * enabled, copy from received
							 * sample.
							 */
							devc->tmp_sample2[i] = devc->sample[j++];
						} 
					}
					/* Clear out the most significant bit of the sample */
					devc->tmp_sample2[devc->num_bytes - 1] &= 0x7f;
					sr_spew("Expanded sample 2: 0x%.8x.", devc->tmp_sample2);

					/*
					 * OLS sends its sample buffer backwards.
					 * store it in reverse order here, so we can dump
					 * this on the session bus later.
					 */
					offset = (devc->limit_samples - devc->num_samples) * 4;
					for (i = 0; i <= devc->rle_count; i++) {
						memcpy(devc->raw_sample_buf + offset + (i * 8),
									 devc->tmp_sample2, 4);
						memcpy(devc->raw_sample_buf + offset + (4 + (i * 8)),
									 devc->tmp_sample, 4);
					}
					memset(devc->sample, 0, 4);
					devc->num_bytes = 0;
					devc->rle_count = 0;
				}
			}
			else {
				if (devc->num_bytes == num_channels) {
					devc->cnt_samples++;
					devc->cnt_samples_rle++;
					/*
					 * Got a full sample. Convert from the OLS's little-endian
					 * sample to the local format.
					 */
					sample = devc->sample[0] | (devc->sample[1] << 8) \
							| (devc->sample[2] << 16) | (devc->sample[3] << 24);
					sr_spew("Received sample 0x%.*x.", devc->num_bytes * 2, sample);
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
							continue;
						}
					}
					devc->num_samples += devc->rle_count + 1;
					if (devc->num_samples > devc->limit_samples) {
						/* Save us from overrunning the buffer. */
						devc->rle_count -= devc->num_samples - devc->limit_samples;
						devc->num_samples = devc->limit_samples;
						index = bytes_read;
					}

					if (num_channels < 4) {
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
							} 
						}
						memcpy(devc->sample, devc->tmp_sample, 4);
						sr_spew("Expanded sample: 0x%.8x.", sample);
					}

					/*
					 * Pipistrello OLS sends its sample buffer backwards.
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
			}
		}
		return TRUE;
	} else {
		do bytes_read = ftdi_read_data(devc->ftdic, devc->ftdi_buf, FTDI_BUF_SIZE);
		while (bytes_read > 0);

		/*
		 * We've acquired all the samples we asked for -- we're done.
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

		sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	return TRUE;
}
