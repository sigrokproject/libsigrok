/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2011 Ian Davis
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

struct ols_basic_trigger_desc {
	uint32_t trigger_mask[NUM_BASIC_TRIGGER_STAGES];
	uint32_t trigger_value[NUM_BASIC_TRIGGER_STAGES];
	int num_stages;
};

static int ols_convert_and_set_up_advanced_trigger(
	const struct sr_dev_inst *sdi, int *num_stages);

SR_PRIV int send_shortcommand(struct sr_serial_dev_inst *serial,
		uint8_t command)
{
	char buf[1];

	sr_dbg("Sending cmd 0x%.2x.", command);
	buf[0] = command;
	if (serial_write_blocking(serial, buf, 1, serial_timeout(serial, 1)) != 1)
		return SR_ERR;

	RETURN_ON_ERROR(serial_drain(serial));

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

	RETURN_ON_ERROR(serial_drain(serial));

	return SR_OK;
}

static int ols_send_longdata(struct sr_serial_dev_inst *serial,
		uint8_t command, uint32_t value)
{
	uint8_t data[4];
	WL32(data, value);
	return send_longcommand(serial, command, data);
}

SR_PRIV int ols_send_reset(struct sr_serial_dev_inst *serial)
{
	int i, ret;
	char dummy[16];

	/* Drain all data so that the remote side is surely listening. */
	while ((ret = serial_read_nonblocking(serial, &dummy, 16)) > 0);
	if (ret != SR_OK)
		return ret;

	for (i = 0; i < 5; i++)
		RETURN_ON_ERROR(send_shortcommand(serial, CMD_RESET));

	/*
	 * Remove all stray output that arrived in between.
	 * This is likely to happen when RLE is being used because
	 * the device seems to return a bit more data than we request.
	 */
	int delay_ms = serial_timeout(serial, 16);
	while ((ret = serial_read_blocking(serial, &dummy, 16, delay_ms)) > 0);

	return ret;
}

/* Configures the channel mask based on which channels are enabled. */
SR_PRIV uint32_t ols_channel_mask(const struct sr_dev_inst *sdi)
{
	uint32_t channel_mask = 0;
	for (const GSList *l = sdi->channels; l; l = l->next) {
		struct sr_channel *channel = l->data;
		if (channel->enabled)
			channel_mask |= 1 << channel->index;
	}

	return channel_mask;
}

static int ols_convert_basic_trigger(
	const struct sr_dev_inst *sdi, struct ols_basic_trigger_desc *ols_trigger)
{
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	int i;

	ols_trigger->num_stages = 0;
	for (i = 0; i < NUM_BASIC_TRIGGER_STAGES; i++) {
		ols_trigger->trigger_mask[i] = 0;
		ols_trigger->trigger_value[i] = 0;
	}

	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;

	ols_trigger->num_stages = g_slist_length(trigger->stages);
	if (ols_trigger->num_stages > NUM_BASIC_TRIGGER_STAGES) {
		sr_err("This device only supports %d trigger stages.",
				NUM_BASIC_TRIGGER_STAGES);
		return SR_ERR;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;

			ols_trigger->trigger_mask[stage->stage] |= 1 << match->channel->index;
			switch (match->match) {
			case SR_TRIGGER_ZERO:
				break;
			case SR_TRIGGER_ONE:
				ols_trigger->trigger_value[stage->stage] |= 1 << match->channel->index;
				break;
			default:
				sr_err("Unsupported trigger type: %d", match->match);
				return SR_ERR;
			}
		}
	}

	return SR_OK;
}

SR_PRIV struct dev_context *ols_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->trigger_at_smpl = OLS_NO_TRIGGER;
	devc->trigger_rle_at_smpl_from_end = OLS_NO_TRIGGER;

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

static void ols_metadata_quirks(struct sr_dev_inst *sdi)
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
			while (serial_read_blocking(serial, &tmp_c, 1, delay_ms) == 1
					&& tmp_c != '\0')
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
	ols_metadata_quirks(sdi);

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
	struct sr_serial_dev_inst *serial = sdi->conn;

	ols_send_reset(serial);

	int ret = serial_source_remove(sdi->session, serial);
	if (ret != SR_OK)
		sr_warn("Couldn't close serial port: %i", ret);

	ret = std_session_send_df_end(sdi);
	if (ret != SR_OK)
		sr_warn("Couldn't end session: %i", ret);
}

SR_PRIV int ols_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t sample;
	int num_bytes_read;
	unsigned int num_changroups;
	gboolean received_a_byte;

	(void)fd;

	sdi = cb_data;
	serial = sdi->conn;
	devc = sdi->priv;

	if (devc->cnt_rx_bytes == 0 && revents == 0) {
		/* Ignore timeouts as long as we haven't received anything */
		return TRUE;
	}

	num_changroups = 0;
	for (uint16_t i = 0x20; i > 0x02; i >>= 1) {
		if ((devc->capture_flags & i) == 0) {
			num_changroups++;
		}
	}

	received_a_byte = FALSE;
	while (revents == G_IO_IN &&
			(num_bytes_read = serial_read_nonblocking(serial,
				devc->raw_sample + devc->raw_sample_size,
				num_changroups - devc->raw_sample_size)) > 0) {
		received_a_byte = TRUE;
		devc->cnt_rx_bytes += num_bytes_read;
		devc->raw_sample_size += num_bytes_read;

		sr_spew("Received data. Current sample: %.2x%.2x%.2x%.2x (%u bytes)",
			devc->raw_sample[0], devc->raw_sample[1],
			devc->raw_sample[2], devc->raw_sample[3], devc->raw_sample_size);

		if (devc->raw_sample_size == num_changroups) {
			devc->cnt_rx_raw_samples++;
			/*
			 * Got a full sample. Convert from the OLS's little-endian
			 * sample to the local format.
			 */
			sample = devc->raw_sample[0] | (devc->raw_sample[1] << 8) \
					| (devc->raw_sample[2] << 16) | (devc->raw_sample[3] << 24);
			sr_dbg("Received sample 0x%.*x.", devc->raw_sample_size * 2, sample);
			if (devc->capture_flags & CAPTURE_FLAG_RLE) {
				/*
				 * In RLE mode the high bit of the sample is the
				 * "count" flag, meaning this sample is the number
				 * of times the previous sample occurred.
				 */
				if (devc->raw_sample[devc->raw_sample_size - 1] & 0x80) {
					/* Clear the high bit. */
					sample &= ~(0x80 << (devc->raw_sample_size - 1) * 8);
					devc->rle_count += sample;
					sr_dbg("RLE count: %" PRIu64, devc->rle_count);
					devc->raw_sample_size = 0;

					if (devc->trigger_at_smpl != OLS_NO_TRIGGER
						&& devc->trigger_rle_at_smpl_from_end == OLS_NO_TRIGGER
						&& (unsigned int)devc->trigger_at_smpl == devc->cnt_rx_raw_samples)
							devc->trigger_rle_at_smpl_from_end = devc->cnt_samples;

					/*
					 * Even on the rare occasion that the sampling ends with an RLE message,
					 * the acquisition should end immediately, without any timeout.
					 */
					goto process_and_forward;
				}
			}

			if (num_changroups < 4) {
				/*
				 * Some channel groups may have been turned
				 * off, to speed up transfer between the
				 * hardware and the PC. Expand that here before
				 * submitting it over the session bus --
				 * whatever is listening on the bus will be
				 * expecting a full 32-bit sample, based on
				 * the number of channels.
				 */
				unsigned int j = 0;
				uint8_t tmp_sample[4] = {0,0,0,0};
				for (unsigned int i = 0; i < 4; i++) {
					if (((devc->capture_flags >> 2) & (1 << i)) == 0) {
						/*
						 * This channel group was
						 * enabled, copy from received
						 * sample.
						 */
						tmp_sample[i] = devc->raw_sample[j++];
					}
				}
				memcpy(devc->raw_sample, tmp_sample, 4);
				sr_spew("Expanded sample: 0x%.2hhx%.2hhx%.2hhx%.2hhx ",
					devc->raw_sample[3], devc->raw_sample[2], devc->raw_sample[1],
					devc->raw_sample[0]);
			}

			uint64_t samples_to_write = devc->rle_count + 1;
			uint64_t new_sample_buf_size =
				4 * MAX(devc->limit_samples, devc->cnt_samples + samples_to_write);
			if (devc->sample_buf_size < new_sample_buf_size) {
				uint64_t old_size = devc->sample_buf_size;
				new_sample_buf_size *= 2;
				devc->sample_buf = g_try_realloc(devc->sample_buf, new_sample_buf_size);
				devc->sample_buf_size = new_sample_buf_size;

				if (!devc->sample_buf) {
					sr_err("Sample buffer malloc failed.");
					return FALSE;
				}
				/* fill with 1010... for debugging */
				memset(devc->sample_buf + old_size, 0x82,
					new_sample_buf_size - old_size);
			}

			if (devc->capture_flags & CAPTURE_FLAG_RLE
				&& devc->trigger_at_smpl != OLS_NO_TRIGGER
				&& devc->trigger_rle_at_smpl_from_end == OLS_NO_TRIGGER
				&& (unsigned int)devc->trigger_at_smpl == devc->cnt_rx_raw_samples)
					devc->trigger_rle_at_smpl_from_end = devc->cnt_samples;

			for (uint64_t i = 0; i < samples_to_write; i++)
				memcpy(devc->sample_buf + (devc->cnt_samples + i) * 4, devc->raw_sample,
					4);

			devc->cnt_samples += samples_to_write;

			memset(devc->raw_sample, 0, 4);
			devc->raw_sample_size = 0;
			devc->rle_count = 0;
		}
	}
	if (revents == G_IO_IN && !received_a_byte)
		return FALSE;

process_and_forward:
	if (revents != G_IO_IN || devc->cnt_rx_raw_samples == devc->limit_samples) {
		if (devc->cnt_rx_raw_samples != devc->limit_samples)
			sr_warn("Finished with unexpected sample count. Timeout?");

		/*
		 * This is the main loop telling us a timeout was reached, or
		 * we've acquired all the samples we asked for -- we're done.
		 * Send the (properly-ordered) buffer to the frontend.
		 */
		sr_dbg(
				"Received %d bytes, %d raw samples, %" PRIu64 " decompressed samples.",
				devc->cnt_rx_bytes, devc->cnt_rx_raw_samples,
				devc->cnt_samples);

		if (devc->capture_flags & CAPTURE_FLAG_RLE) {
			if (devc->trigger_rle_at_smpl_from_end != OLS_NO_TRIGGER)
				devc->trigger_at_smpl =
					devc->cnt_samples - devc->trigger_rle_at_smpl_from_end;
			else {
				if (devc->trigger_at_smpl != OLS_NO_TRIGGER)
					sr_warn("No trigger point found. Short read?");
				devc->trigger_at_smpl = OLS_NO_TRIGGER;
			}
		}

		/*
		 * The OLS sends its sample buffer backwards.
		 * Flip it back before sending it on the session bus.
		 */
		for(uint64_t i = 0; i < devc->cnt_samples/2; i++) {
			uint8_t temp[4];
			memcpy(temp, &devc->sample_buf[4*i], 4);
			memmove(&devc->sample_buf[4*i],
				&devc->sample_buf[4*(devc->cnt_samples-i-1)], 4);
			memcpy(&devc->sample_buf[4*(devc->cnt_samples-i-1)], temp, 4);
		}

		if (devc->trigger_at_smpl != OLS_NO_TRIGGER) {
			/*
			 * A trigger was set up, so we need to tell the frontend
			 * about it.
			 */
			if (devc->trigger_at_smpl > 0
					&& (unsigned int)devc->trigger_at_smpl <= devc->cnt_samples) {
				/* There are pre-trigger samples, send those first. */
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = devc->trigger_at_smpl * 4;
				logic.unitsize = 4;
				logic.data = devc->sample_buf;
				sr_session_send(sdi, &packet);
			}

			/* Send the trigger. */
			std_session_send_df_trigger(sdi);
		}

		/* Send post-trigger / all captured samples. */
		unsigned int num_pre_trigger_samples =
			devc->trigger_at_smpl == OLS_NO_TRIGGER ? 0
			: MIN((unsigned int)devc->trigger_at_smpl, devc->cnt_samples);
		if (devc->cnt_samples > num_pre_trigger_samples) {
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = (devc->cnt_samples - num_pre_trigger_samples) * 4;
			logic.unitsize = 4;
			logic.data = devc->sample_buf + num_pre_trigger_samples *4;
			sr_session_send(sdi, &packet);
		}

		g_free(devc->sample_buf);
		devc->sample_buf = 0;
		devc->sample_buf_size = 0;

		serial_flush(serial);
		abort_acquisition(sdi);
	}

	return TRUE;
}

static int ols_set_basic_trigger_stage(
	const struct ols_basic_trigger_desc *trigger_desc,
	struct sr_serial_dev_inst *serial,
	int stage)
{
	uint8_t cmd, arg[4];

	cmd = CMD_SET_BASIC_TRIGGER_MASK0 + stage * 4;
	RETURN_ON_ERROR(ols_send_longdata(serial, cmd,
		trigger_desc->trigger_mask[stage]));

	cmd = CMD_SET_BASIC_TRIGGER_VALUE0 + stage * 4;
	RETURN_ON_ERROR(ols_send_longdata(serial, cmd,
		trigger_desc->trigger_value[stage]));

	cmd = CMD_SET_BASIC_TRIGGER_CONFIG0 + stage * 4;
	arg[0] = arg[1] = arg[3] = 0x00;
	arg[2] = stage;
	if (stage == trigger_desc->num_stages - 1)
		/* Last stage, fire when this one matches. */
		arg[3] |= TRIGGER_START;
	RETURN_ON_ERROR(send_longcommand(serial, cmd, arg));

	return SR_OK;
}

SR_PRIV int ols_prepare_acquisition(const struct sr_dev_inst *sdi) {
	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial = sdi->conn;

	/*
	* According to http://mygizmos.org/ols/Logic-Sniffer-FPGA-Spec.pdf
	* reset command must be send prior each arm command
	*/
	sr_dbg("Send reset command before trigger configure");
	RETURN_ON_ERROR(ols_send_reset(serial));


	int num_changroups = 0;
	uint8_t changroup_mask = 0;
	uint32_t channel_mask = ols_channel_mask(sdi);
	for (unsigned int i = 0; i < 4; i++) {
		if (channel_mask & (0xff << (i * 8))) {
			changroup_mask |= (1 << i);
			num_changroups++;
		}
	}

	/*
	 * Limit the number of samples to what the hardware can do.
	 * The sample count is always a multiple of four.
	 */
	devc->limit_samples =
		(MIN(devc->max_samples / num_changroups, devc->limit_samples) + 3) / 4 * 4;
	uint32_t readcount = devc->limit_samples / 4;
	uint32_t delaycount;
	int trigger_point = OLS_NO_TRIGGER;

	if (!(devc->device_flags & DEVICE_FLAG_IS_DEMON_CORE)) {
		/* basic trigger only */
		struct ols_basic_trigger_desc basic_trigger_desc;
		if (ols_convert_basic_trigger(sdi, &basic_trigger_desc) != SR_OK) {
			sr_err("Failed to configure channels.");
			return SR_ERR;
		}
		if (basic_trigger_desc.num_stages > 0) {
			delaycount = readcount * (1 - devc->capture_ratio / 100.0);
			trigger_point = (readcount - delaycount) * 4 - 1;
			for (int i = 0; i < basic_trigger_desc.num_stages; i++) {
				sr_dbg("Setting OLS stage %d trigger.", i);
				RETURN_ON_ERROR(ols_set_basic_trigger_stage(
					&basic_trigger_desc, serial, i));
			}
		} else {
			/* No triggers configured, force trigger on first stage. */
			sr_dbg("Forcing trigger at stage 0.");
			basic_trigger_desc.num_stages = 1;
			RETURN_ON_ERROR(ols_set_basic_trigger_stage(
				&basic_trigger_desc, serial, 0));
			delaycount = readcount;
		}
	} else {
		/* advanced trigger setup */
		gboolean will_trigger = FALSE;
		if(ols_convert_and_set_up_advanced_trigger(sdi, &will_trigger) != SR_OK) {
			sr_err("Advanced trigger setup failed.");
			return SR_ERR;
		}

		if (will_trigger) {
			delaycount = readcount * (1 - devc->capture_ratio / 100.0);
			trigger_point = (readcount - delaycount) * 4;
		} else
			delaycount = readcount;
	}

	/*
	 * To determine the proper trigger sample position in RLE mode, a reverse
	 * lookup is needed while reading the samples. Set up the right trigger
	 * point in that case or the normal trigger point for non-RLE acquisitions.
	 */
	devc->trigger_at_smpl = trigger_point == OLS_NO_TRIGGER ? OLS_NO_TRIGGER
		: devc->capture_flags & CAPTURE_FLAG_RLE ?
			(int)devc->limit_samples - trigger_point : trigger_point;

	/* Samplerate. */
	sr_dbg("Setting samplerate to %" PRIu64 "Hz (divider %u)",
			devc->cur_samplerate, devc->cur_samplerate_divider);
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_DIVIDER, devc->cur_samplerate_divider & 0x00FFFFFF));

	/* Send sample limit and pre/post-trigger capture ratio. */
	sr_dbg("Setting sample limit %d, trigger point at %d",
			(readcount - 1) * 4, (delaycount - 1) * 4);

	if (devc->max_samples > 256 * 1024) {
		RETURN_ON_ERROR(ols_send_longdata(
			serial, CMD_CAPTURE_READCOUNT, readcount-1));
		RETURN_ON_ERROR(ols_send_longdata(
			serial, CMD_CAPTURE_DELAYCOUNT, delaycount-1));
	} else {
		uint8_t arg[4];
		WL16(&arg[0], readcount-1);
		WL16(&arg[2], delaycount-1);
		RETURN_ON_ERROR(send_longcommand(serial, CMD_CAPTURE_SIZE, arg));
	}

	/* Flag register. */
	sr_dbg(
			"Setting intpat %s, extpat %s, RLE %s, noise_filter %s, demux %s, "
			"%s clock%s",
			devc->capture_flags & CAPTURE_FLAG_INTERNAL_TEST_MODE ? "on": "off",
			devc->capture_flags & CAPTURE_FLAG_EXTERNAL_TEST_MODE ? "on": "off",
			devc->capture_flags & CAPTURE_FLAG_RLE ? "on" : "off",
			devc->capture_flags & CAPTURE_FLAG_NOISE_FILTER ? "on": "off",
			devc->capture_flags & CAPTURE_FLAG_DEMUX ? "on" : "off",
			devc->capture_flags & CAPTURE_FLAG_CLOCK_EXTERNAL ? "external" : "internal",
			devc->capture_flags & CAPTURE_FLAG_CLOCK_EXTERNAL
				? (devc->capture_flags & CAPTURE_FLAG_INVERT_EXT_CLOCK
						? " on falling edge" : "on rising edge")
				: "");

	/*
	 * Enable/disable OLS channel groups in the flag register according
	 * to the channel mask. 1 means "disable channel".
	 */
	devc->capture_flags &= ~0x3c;
	devc->capture_flags |= ~(changroup_mask << 2) & 0x3c;

	/*
	 * Demon Core supports RLE mode 3. In this mode, an arbitrary number of
	 * consecutive RLE messages can occur. The value is only sent whenever
	 * it changes. In contrast, mode 0 repeats the value after every RLE
	 * message, even if it didn't change.
	 */
	if (devc->device_flags & DEVICE_FLAG_IS_DEMON_CORE)
		devc->capture_flags |= CAPTURE_FLAG_RLEMODE0 | CAPTURE_FLAG_RLEMODE1;
	else
		devc->capture_flags &= ~(CAPTURE_FLAG_RLEMODE0 | CAPTURE_FLAG_RLEMODE1);

	RETURN_ON_ERROR(ols_send_longdata(serial, CMD_SET_FLAGS, devc->capture_flags));

	return SR_OK;
}


/* set up a level trigger stage to trigger when (input & mask) == target */
static int ols_set_advanced_level_trigger(
	struct sr_serial_dev_inst *serial,
	uint8_t num_trigger_term, /* 0-9 for trigger terms a-j */
	uint32_t target,
	uint32_t mask)
{
	uint32_t lutmask = 1;
	uint32_t lutbits[4] = {0, 0, 0, 0};

	for (uint32_t i = 0; i < 16; ++i)
	{
		if (((i ^ ((target >>  0) & 0xF)) & ((mask >>  0) & 0xF)) == 0)
      lutbits[0] |= lutmask;
		if (((i ^ ((target >>  4) & 0xF)) & ((mask >>  4) & 0xF)) == 0)
      lutbits[0] |= (lutmask << 16);
		if (((i ^ ((target >>  8) & 0xF)) & ((mask >>  8) & 0xF)) == 0)
      lutbits[1] |= lutmask;
		if (((i ^ ((target >> 12) & 0xF)) & ((mask >> 12) & 0xF)) == 0)
      lutbits[1] |= (lutmask << 16);
		if (((i ^ ((target >> 16) & 0xF)) & ((mask >> 16) & 0xF)) == 0)
      lutbits[2] |= lutmask;
		if (((i ^ ((target >> 20) & 0xF)) & ((mask >> 20) & 0xF)) == 0)
			lutbits[2] |= (lutmask << 16);
		if (((i ^ ((target >> 24) & 0xF)) & ((mask >> 24) & 0xF)) == 0)
			lutbits[3] |= lutmask;
		if (((i ^ ((target >> 28) & 0xF)) & ((mask >> 28) & 0xF)) == 0)
			lutbits[3] |= (lutmask << 16);
		lutmask <<= 1;
	}

	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_SEL, 0x20 + (num_trigger_term % 10)));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, lutbits[3]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, lutbits[2]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, lutbits[1]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, lutbits[0]));
	return SR_OK;
}

#define OLS_ADV_TRIG_EDGERISE0 0x0A0A
#define OLS_ADV_TRIG_EDGERISE1 0x00CC
#define OLS_ADV_TRIG_EDGEFALL0 0x5050
#define OLS_ADV_TRIG_EDGEFALL1 0x3300
#define OLS_ADV_TRIG_EDGEBOTH0 (OLS_ADV_TRIG_EDGERISE0 | OLS_ADV_TRIG_EDGEFALL0)
#define OLS_ADV_TRIG_EDGEBOTH1 (OLS_ADV_TRIG_EDGERISE1 | OLS_ADV_TRIG_EDGEFALL1)
#define OLS_ADV_TRIG_EDGENEITHER0 (~OLS_ADV_TRIG_EDGEBOTH0 & 0xFFFF) /* means neither rise nor fall: constant signal */
#define OLS_ADV_TRIG_EDGENEITHER1 (~OLS_ADV_TRIG_EDGEBOTH1 & 0xFFFF)

/* Set up edge trigger LUTs.
 *
 * All edge triggers of one unit are ORed together, not ANDed. This
 * differs from level triggers, where all levels have to be met at the
 * same time. This code is at least consistent in that it also ORs together
 * pairs of edge triggers.
 */
static int ols_set_advanced_edge_trigger(
	struct sr_serial_dev_inst *serial,
	int edgesel, /* which edge trigger unit, 0 or 1 */
	uint32_t rising_edge,
	uint32_t falling_edge,
	uint32_t neither_edge)
{
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_SEL, 0x34 + (edgesel & 1)));

	uint32_t lutbits = 0;
	uint32_t bitmask = 0x80000000;
	for (unsigned int i = 0; i < 16; i = i + 1) {
		/* Evaluate indata bit1... */
		if (neither_edge & bitmask)
			lutbits |= OLS_ADV_TRIG_EDGENEITHER1;
		else {
			if (rising_edge & bitmask) lutbits |= OLS_ADV_TRIG_EDGERISE1;
			if (falling_edge & bitmask) lutbits |= OLS_ADV_TRIG_EDGEFALL1;
		}
		bitmask >>= 1;

		/* Evaluate indata bit0... */
		if (neither_edge & bitmask)
			lutbits |= OLS_ADV_TRIG_EDGENEITHER0;
		else {
			if (rising_edge & bitmask) lutbits |= OLS_ADV_TRIG_EDGERISE0;
			if (falling_edge & bitmask) lutbits |= OLS_ADV_TRIG_EDGEFALL0;
		}
		bitmask >>= 1;

		if ((i & 1) == 0)
			lutbits <<= 16;
		else {
			/* write total of 256 bits */
			RETURN_ON_ERROR(ols_send_longdata(
				serial, CMD_SET_ADVANCED_TRIG_WRITE, lutbits));
			lutbits = 0;
		}
	}

	return SR_OK;
}

static int ols_set_advanced_trigger_timer (
	struct sr_serial_dev_inst *serial,
	int timersel, /* 0 or 1 */
	uint64_t count_10ns)
{
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_SEL, 0x38 + (timersel & 1) * 2));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, count_10ns & 0xFFFFFFFF));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_SEL, 0x39 + (timersel & 1) * 2));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, count_10ns >> 32));
	return SR_OK;
}

#define OLS_ADV_TRIG_STATETERM_HIT 0
#define OLS_ADV_TRIG_STATETERM_ELSE 1
#define OLS_ADV_TRIG_STATETERM_CAPTURE 2

#define OLS_ADV_TRIG_OP_NOP 0
#define OLS_ADV_TRIG_OP_ANY 1
#define OLS_ADV_TRIG_OP_AND 2
#define OLS_ADV_TRIG_OP_NAND 3
#define OLS_ADV_TRIG_OP_OR 4
#define OLS_ADV_TRIG_OP_NOR 5
#define OLS_ADV_TRIG_OP_XOR 6
#define OLS_ADV_TRIG_OP_NXOR 7
#define OLS_ADV_TRIG_OP_A 8
#define OLS_ADV_TRIG_OP_B 9
/*                                      NOP    ANY      AND    NAND     OR      NOR     XOR    NXOR      A      B */
static const uint32_t pairvalue[] =  {0x0000, 0xFFFF, 0x8000, 0x7FFF, 0xF888, 0x0777, 0x7888, 0x8777, 0x8888, 0xF000};
static const uint32_t midvalue[] =   {0x0000, 0xFFFF, 0x8000, 0x7FFF, 0xFFFE, 0x0001, 0x0116, 0xFEE9, 0xEEEE, 0xFFF0};
static const uint32_t finalvalue[] = {0x0000, 0xFFFF, 0x0008, 0x0007, 0x000E, 0x0001, 0x0006, 0x0009, 0x0002, 0x0004};

/*
 * Trigger input summing stage: Combines different inputs in arbitrary ways.
 * Keep in mind that all primary inputs (a, edge etc.) output a pair of bits per
 * input. That's why those LUTs have 2 inputs, whereas the mid LUTs have
 * 4 inputs. Only half of the final LUT is used.
 */
static int ols_set_advanced_trigger_sum(
	struct sr_serial_dev_inst *serial,
	int statenum, /* 0-15 */
	int stateterm, /* 0: hit, 1: else, 2: capture */
	int op_ab,
	int op_c_range1,
	int op_d_edge1,
	int op_e_timer1,
	int op_fg,
	int op_h_range2,
	int op_i_edge2,
	int op_j_timer2,
	int op_mid1, /* sums up a, b, c, range1, d, edge1, e, timer1 */
	int op_mid2, /* sums up f, g, h, range2, i, edge2, j, timer2 */
	int op_final) /* sums up mid1, mid2 */
{
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_SEL, 0x40 + (statenum * 4) + stateterm));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE, finalvalue[op_final]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE,
		(midvalue[op_mid2] << 16) | midvalue[op_mid1]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE,
		(pairvalue[op_j_timer2] << 16) | pairvalue[op_i_edge2]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE,
		(pairvalue[op_h_range2] << 16) | pairvalue[op_fg]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE,
		(pairvalue[op_e_timer1] << 16) | pairvalue[op_d_edge1]));
	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_WRITE,
		(pairvalue[op_c_range1] << 16) | pairvalue[op_ab]));
	return SR_OK;
}

#define TRIGSTATE_STATENUM_MASK 0xF
#define TRIGSTATE_OBTAIN_MASK 0x000FFFFF
#define TRIGSTATE_ELSE_BITOFS 20
#define TRIGSTATE_STOLS_ADV_TRIG_OP_TIMER0 0x01000000
#define TRIGSTATE_STOLS_ADV_TRIG_OP_TIMER1 0x02000000
#define TRIGSTATE_CLEAR_TIMER0 0x04000000
#define TRIGSTATE_CLEAR_TIMER1 0x08000000
#define TRIGSTATE_START_TIMER0 0x10000000
#define TRIGSTATE_START_TIMER1 0x20000000
#define TRIGSTATE_TRIGGER_FLAG 0x40000000
#define TRIGSTATE_LASTSTATE 0x80000000

static int ols_set_advanced_trigger_state(
	struct sr_serial_dev_inst *serial,
	uint8_t statenum, /* 0 to 15 */
	gboolean last_state,
	gboolean set_trigger,
	uint8_t start_timer, /* bit0=timer1, bit1=timer2 */
	uint8_t stop_timer,  /* bit0=timer1, bit1=timer2 */
	uint8_t clear_timer, /* bit0=timer1, bit1=timer2 */
	uint8_t else_state,  /* 0 to 15 */
	uint32_t obtain_count)
{
	uint32_t value =
		((else_state & TRIGSTATE_STATENUM_MASK) << TRIGSTATE_ELSE_BITOFS) |
		(obtain_count & TRIGSTATE_OBTAIN_MASK);
	if (last_state)      value |= TRIGSTATE_LASTSTATE;
	if (set_trigger)     value |= TRIGSTATE_TRIGGER_FLAG;
	if (start_timer & 1) value |= TRIGSTATE_START_TIMER0;
	if (start_timer & 2) value |= TRIGSTATE_START_TIMER1;
	if (stop_timer  & 1) value |= TRIGSTATE_STOLS_ADV_TRIG_OP_TIMER0;
	if (stop_timer  & 2) value |= TRIGSTATE_STOLS_ADV_TRIG_OP_TIMER1;
	if (clear_timer & 1) value |= TRIGSTATE_CLEAR_TIMER0;
	if (clear_timer & 2) value |= TRIGSTATE_CLEAR_TIMER1;

	RETURN_ON_ERROR(ols_send_longdata(
		serial, CMD_SET_ADVANCED_TRIG_SEL, statenum & TRIGSTATE_STATENUM_MASK));
	RETURN_ON_ERROR(ols_send_longdata(serial, CMD_SET_ADVANCED_TRIG_WRITE, value));
	return SR_OK;
}

static int ols_set_advanced_trigger_sums_and_stages(
	struct sr_serial_dev_inst *serial,
	int ols_stage,
	int sum_inputs[8],
	gboolean is_last_stage,
	gboolean start_timer0)
{
	/*
	 * Hit only when all inputs are true. Always capture for pre-trigger and
	 * acquisition. Never execute the "Else" action, since we advance trigger
	 * stages implicity via hits.
	 */
	RETURN_ON_ERROR(ols_set_advanced_trigger_sum(serial, ols_stage, OLS_ADV_TRIG_STATETERM_HIT,
		sum_inputs[0], sum_inputs[1], sum_inputs[2], sum_inputs[3],
		sum_inputs[4], sum_inputs[5], sum_inputs[6], sum_inputs[7],
		OLS_ADV_TRIG_OP_AND, OLS_ADV_TRIG_OP_AND,
		OLS_ADV_TRIG_OP_AND));
	RETURN_ON_ERROR(ols_set_advanced_trigger_sum(serial, ols_stage, OLS_ADV_TRIG_STATETERM_CAPTURE,
		OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
		OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
		OLS_ADV_TRIG_OP_AND, OLS_ADV_TRIG_OP_AND,
		OLS_ADV_TRIG_OP_ANY));
	RETURN_ON_ERROR(ols_set_advanced_trigger_sum(serial, ols_stage, OLS_ADV_TRIG_STATETERM_ELSE,
		OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
		OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
		OLS_ADV_TRIG_OP_AND, OLS_ADV_TRIG_OP_AND,
		OLS_ADV_TRIG_OP_NOP));

	/*
		* Tell the state machine to move to the next stage on a hit by not
		* setting the trigger flag. The last stage executes the trigger.
		*/
	RETURN_ON_ERROR(ols_set_advanced_trigger_state(serial, ols_stage,
		is_last_stage, is_last_stage, start_timer0 ? 1 : 0, 0, 0, 0, 0));

	return SR_OK;
}

static int ols_convert_and_set_up_advanced_trigger(
		const struct sr_dev_inst *sdi, gboolean* will_trigger) {
	struct sr_serial_dev_inst *serial = sdi->conn;
	struct dev_context *devc = sdi->priv;

	int ols_stage = 0;

	if(devc->capture_ratio > 0) {
		/*
		 * We need to set up a timer to ensure enough samples are captured to
		 * fulfill the pre-trigger ratio. In RLE mode, this is not necessarily
		 * true. It would be possible to wait longer, to ensure that enough
		 * compressed samples are captured, but this could take ages and is
		 * probably not what the user wants.
		 */

		uint64_t effective_divider = devc->capture_flags & CAPTURE_FLAG_DEMUX
			? (devc->cur_samplerate_divider + 1) / 2
			: (devc->cur_samplerate_divider + 1);
		uint64_t pretrigger_10ns_ticks =
			devc->limit_samples * effective_divider * devc->capture_ratio
			/ 100 /* percent */;
		sr_dbg(
			"Inserting pre-trigger delay of %" PRIu64 "0 ns", pretrigger_10ns_ticks);

		RETURN_ON_ERROR(ols_set_advanced_trigger_timer(
			serial, 0, pretrigger_10ns_ticks));

		int sum_inputs[8] = {
				OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
				OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY
		};

		/* first stage: start timer, advance immediately to second stage */
		RETURN_ON_ERROR(ols_set_advanced_trigger_sums_and_stages(
			serial, ols_stage++, sum_inputs, FALSE, TRUE));

		/* second stage: wait until timer expires */
		sum_inputs[3] = OLS_ADV_TRIG_OP_B;
		RETURN_ON_ERROR(ols_set_advanced_trigger_sums_and_stages(
			serial, ols_stage++, sum_inputs, FALSE, TRUE));
	}

	struct sr_trigger *trigger;
	if (!(trigger = sr_session_trigger_get(sdi->session))) {
		*will_trigger = FALSE;

		/* Set up immediate trigger to capture and trigger regardless of any input. */
		int sum_inputs[8] = {
				OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
				OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY
		};
		RETURN_ON_ERROR(ols_set_advanced_trigger_sums_and_stages(
			serial, ols_stage, sum_inputs, TRUE, FALSE));
		return SR_OK;
	}

	int num_req_trigger_stages = g_slist_length(trigger->stages);
	if (ols_stage + num_req_trigger_stages > NUM_ADVANCED_TRIGGER_STAGES) {
		sr_err("Too many trigger stages: %d requested + %d internal > %d available",
				num_req_trigger_stages, ols_stage, NUM_ADVANCED_TRIGGER_STAGES);
		return SR_ERR;
	}

	sr_dbg("Setting OLS advanced trigger for %i stages", num_req_trigger_stages);

	const int last_stage = ols_stage + num_req_trigger_stages - 1;
	int num_stages_with_level_trigger = 0;
	int num_stages_with_edge_trigger = 0;
	for (const GSList *l = trigger->stages; l; l = l->next) {
		struct sr_trigger_stage *stage = l->data;

		/* channel bit masks: */
		uint32_t level_mask = 0;
		uint32_t level_value = 0;
		uint32_t edge_rising = 0;
		uint32_t edge_falling = 0;

		int current_level_term = -1;
		int current_edge_term = -1;

		for (const GSList *m = stage->matches; m; m = m->next) {
			struct sr_trigger_match *match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;

			int chan_bit = 1 << match->channel->index;
			switch (match->match) {
			case SR_TRIGGER_ZERO:
				level_mask |= chan_bit;
				break;
			case SR_TRIGGER_ONE:
				level_mask |= chan_bit;
				level_value |= chan_bit;
				break;
			case SR_TRIGGER_RISING:
				edge_rising |= chan_bit;
				break;
			case SR_TRIGGER_FALLING:
				edge_falling |= chan_bit;
				break;
			case SR_TRIGGER_EDGE:
				edge_rising |= chan_bit;
				edge_falling |= chan_bit;
				break;
			default:
				sr_err("Unsupported trigger type: %d", match->match);
				return SR_ERR;
			}
		}

		if (level_mask) {
			if(num_stages_with_level_trigger >= NUM_ADVANCED_LEVEL_TRIGGERS) {
				sr_err("Too many level triggers, only %d supported.",
					NUM_ADVANCED_LEVEL_TRIGGERS);
				return SR_ERR;
			}

			ols_set_advanced_level_trigger(
				serial, num_stages_with_level_trigger, level_value, level_mask);
			current_level_term = num_stages_with_level_trigger;
			++num_stages_with_level_trigger;
		}

		if (edge_rising | edge_falling) {
			if(num_stages_with_edge_trigger >= NUM_ADVANCED_EDGE_TRIGGERS) {
				sr_err("Too many edge triggers, only %d supported.",
					NUM_ADVANCED_EDGE_TRIGGERS);
				return SR_ERR;
			}

			ols_set_advanced_edge_trigger(
				serial, num_stages_with_edge_trigger, edge_rising, edge_falling, 0U);
			current_edge_term = num_stages_with_edge_trigger;
			++num_stages_with_edge_trigger;
		}

		gboolean is_last_stage = ols_stage == last_stage;

		/* map stage indices to the input pairs and pair position in the summing unit: */
		int sum_inputs[8] = {
				OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY,
				OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY, OLS_ADV_TRIG_OP_ANY
		};
		#define A OLS_ADV_TRIG_OP_A
		#define B OLS_ADV_TRIG_OP_B
		static const int level_stage_to_input_pair[10] = {0, 0, 1, 2, 3, 4, 4, 5, 6, 7};
		static const int level_stage_to_input_ab[10] =   {A, B, A, A, A, A, B, A, A, A};
		static const int edge_stage_to_input_pair[2] =   {2, 6};
		#undef A
		#undef B

		int level_summing_input = current_level_term >= 0
			? level_stage_to_input_pair[current_level_term] : -1 ;
		int edge_summing_input = current_edge_term >= 0
			? edge_stage_to_input_pair[current_edge_term] : -1;

		if(level_summing_input >= 0)
			sum_inputs[level_summing_input] =
				level_stage_to_input_ab[current_level_term];
		if(edge_summing_input >= 0)
			sum_inputs[edge_summing_input] = OLS_ADV_TRIG_OP_B;

		/* If level and edge input end up in on the same input pair, and them together: */
		if(level_summing_input >= 0 && level_summing_input == edge_summing_input)
			sum_inputs[level_summing_input] = OLS_ADV_TRIG_OP_AND;

		sr_spew(" Stage %d, lvl mask %.4x, edge %.4x, level term %d, "
			"edge term %d -> "
			"trigger sum %.4X %.4X %.4X %.4X %.4X %.4X %.4X %.4X",
			ols_stage, level_mask, (edge_falling | edge_rising), current_level_term,
			current_edge_term,
			sum_inputs[0], sum_inputs[1], sum_inputs[2], sum_inputs[3],
			sum_inputs[4], sum_inputs[5], sum_inputs[6], sum_inputs[7]);

		RETURN_ON_ERROR(ols_set_advanced_trigger_sums_and_stages(
			serial, ols_stage, sum_inputs, is_last_stage, 0));

		++ols_stage;
	}

	*will_trigger = ols_stage > 0 ? TRUE : FALSE;
	return SR_OK;
}
