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

SR_PRIV int p_tlf_send_shortcommand(struct sr_serial_dev_inst *serial,
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

SR_PRIV int p_tlf_send_longcommand(struct sr_serial_dev_inst *serial,
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

SR_PRIV int tlf_send_reset(struct sr_serial_dev_inst *serial)
{
	unsigned int i;

	for (i = 0; i < 5; i++) {
		if (p_tlf_send_shortcommand(serial, CMD_RESET) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

/* Configures the channel mask based on which channels are enabled. */
SR_PRIV void tlf_channel_mask(const struct sr_dev_inst *sdi)
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

SR_PRIV int tlf_convert_trigger(const struct sr_dev_inst *sdi)
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

SR_PRIV struct dev_context *tlf_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));

	/* Device-specific settings */
	devc->max_samples = devc->max_samplerate = devc->protocol_version = 0;

	/* Acquisition settings */
	devc->limit_samples = devc->capture_ratio = 0;
	devc->trigger_at = -1;
	devc->flag_reg = 0;

	return devc;
}

static void tlf_channel_new(struct sr_dev_inst *sdi, int num_chan)
{
	struct dev_context *devc = sdi->priv;
	int i;

	for (i = 0; i < num_chan; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
				tlf_channel_names[i]);

	devc->max_channels = num_chan;
}

SR_PRIV struct sr_dev_inst *p_tlf_get_metadata(struct sr_serial_dev_inst *serial)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint32_t tmp_int;
	uint8_t key, type, token;
	int delay_ms;
	GString *tmp_str, *devname, *version;
	guchar tmp_c;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	devc = tlf_dev_new();
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
				tlf_channel_new(sdi, tmp_int);
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
				tlf_channel_new(sdi, tmp_c);
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
		case 3:
			/* Various types. */
			delay_ms = serial_timeout(serial, 1);
			if (serial_read_blocking(serial, &tmp_c, 1, delay_ms) != 1)
				break;
			sr_dbg("Got metadata key 0x%.2x value 0x%.2x.",
			       key, tmp_c);
			switch (token) {
			case 0x00:
			    /* Channel info. Name is empty if it's blank. */
			    /* 8-bit unsigned integer for channel, 8-bit unsigned for group,  NULL-terminated string */
				tlf_channel_new(sdi, tmp_c);
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

SR_PRIV int tlf_set_samplerate(const struct sr_dev_inst *sdi,
		const uint64_t samplerate)
{
		sr_info("In set samplerate. %" PRIu64, sdi);

	struct dev_context *devc;

	devc = sdi->priv;
	if (devc->max_samplerate && samplerate > devc->max_samplerate)
		return SR_ERR_SAMPLERATE;

	if (samplerate > CLOCK_RATE) {
		sr_info("Enabling demux mode.");
		devc->flag_reg |= FLAG_DEMUX;
		devc->flag_reg &= ~FLAG_FILTER;
		devc->cur_samplerate_divider = (CLOCK_RATE * 2 / samplerate) - 1;
	} else {
		sr_info("Disabling demux mode.");
		devc->flag_reg &= ~FLAG_DEMUX;
		devc->flag_reg |= FLAG_FILTER;
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

SR_PRIV void p_tlf_abort_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	serial_source_remove(sdi->session, serial);

	std_session_send_df_end(sdi);
}

SR_PRIV int tlf_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t sample;
	int offset, j;
	unsigned int pending_samples;
	unsigned int buffer_size = 256;

	(void)fd;

	//sr_dbg("receiving data");

	sdi = cb_data;
	serial = sdi->conn;
	devc = sdi->priv;

	if (revents == 0) {
		if (devc->num_transfers == 0) {
			/* Ignore timeouts as long as we haven't received anything */
			return TRUE;
		} else {
			/* Free everything. */
			g_free(devc->raw_sample_buf);

			serial_flush(serial);
			p_tlf_abort_acquisition(sdi);

			return TRUE;
		}
	}
	size_t pending = serial_has_receive_data(serial);

	// The first pair of bytes is the first sample. We won't know how long it lasts until we get
	// another sample.
	if (devc->num_transfers == 0) {
		if (pending < 2) {
			// Keep waiting.
			return TRUE;
		}
		if (serial_read_nonblocking(serial, &devc->last_sample, 2) != 2) {
			sr_err("nothing read");
			return FALSE;
		}
		sr_spew("Received first sample %d.", devc->last_sample);

		devc->raw_sample_buf = g_try_malloc(buffer_size * 2);
		if (!devc->raw_sample_buf) {
			sr_err("Sample buffer malloc failed.");
			return FALSE;
		}
		/* fill with 1010... for debugging */
		memset(devc->raw_sample_buf, 0x82, buffer_size * 2);
		devc->num_transfers++;
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = 2;
	logic.data = devc->raw_sample_buf;
	pending_samples = 0;
	while (serial_has_receive_data(serial) > 4) {
		uint16_t sample_count;
		if (serial_read_nonblocking(serial, &sample_count, 2) != 2) {
			sr_err("unable to read sample count");
			return FALSE;
		}
		sr_spew("Received sample count %d.", sample_count);
		// TODO: Handle if the sample count is >> buffer_size. No need to copy
		// data into it more than once.
		for (size_t i = 0; i < sample_count; i++) {
			((uint16_t*) devc->raw_sample_buf)[pending_samples] = devc->last_sample;
			devc->num_samples++;
			pending_samples++;

			if (pending_samples == buffer_size) {
				logic.length = pending_samples * 2;
				sr_session_send(sdi, &packet);
				pending_samples = 0;
			}
		}

		if (serial_read_nonblocking(serial, &devc->last_sample, 2) != 2) {
			sr_err("unable to read next sample");
			return FALSE;
		}
		devc->num_transfers++;
		sr_spew("Received sample %x.", devc->last_sample);
	}


	if (pending_samples > 0) {
		logic.length = pending_samples * 2;
		sr_session_send(sdi, &packet);
	}

	// sr_spew("passing back to outer loop");

	return TRUE;
}
