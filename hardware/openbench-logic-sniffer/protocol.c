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
static struct sr_dev_driver *di = &ols_driver_info;

SR_PRIV int send_shortcommand(struct sr_serial_dev_inst *serial,
		uint8_t command)
{
	char buf[1];

	sr_dbg("Sending cmd 0x%.2x.", command);
	buf[0] = command;
	if (serial_write(serial, buf, 1) != 1)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int send_longcommand(struct sr_serial_dev_inst *serial,
		uint8_t command, uint32_t data)
{
	char buf[5];

	sr_dbg("Sending cmd 0x%.2x data 0x%.8x.", command, data);
	buf[0] = command;
	buf[1] = (data & 0xff000000) >> 24;
	buf[2] = (data & 0xff0000) >> 16;
	buf[3] = (data & 0xff00) >> 8;
	buf[4] = data & 0xff;
	if (serial_write(serial, buf, 5) != 5)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int ols_configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct sr_probe *probe;
	const GSList *l;
	int probe_bit, stage, i;
	char *tc;

	devc = sdi->priv;

	devc->probe_mask = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		devc->trigger_mask[i] = 0;
		devc->trigger_value[i] = 0;
	}

	devc->num_stages = 0;
	for (l = sdi->probes; l; l = l->next) {
		probe = (const struct sr_probe *)l->data;
		if (!probe->enabled)
			continue;

		/*
		 * Set up the probe mask for later configuration into the
		 * flag register.
		 */
		probe_bit = 1 << (probe->index);
		devc->probe_mask |= probe_bit;

		if (!probe->trigger)
			continue;

		/* Configure trigger mask and value. */
		stage = 0;
		for (tc = probe->trigger; tc && *tc; tc++) {
			devc->trigger_mask[stage] |= probe_bit;
			if (*tc == '1')
				devc->trigger_value[stage] |= probe_bit;
			stage++;
			if (stage > 3)
				/*
				 * TODO: Only supporting parallel mode, with
				 * up to 4 stages.
				 */
				return SR_ERR;
		}
		if (stage > devc->num_stages)
			devc->num_stages = stage - 1;
	}

	return SR_OK;
}

SR_PRIV uint32_t reverse16(uint32_t in)
{
	uint32_t out;

	out = (in & 0xff) << 8;
	out |= (in & 0xff00) >> 8;
	out |= (in & 0xff0000) << 8;
	out |= (in & 0xff000000) >> 8;

	return out;
}

SR_PRIV uint32_t reverse32(uint32_t in)
{
	uint32_t out;

	out = (in & 0xff) << 24;
	out |= (in & 0xff00) << 8;
	out |= (in & 0xff0000) >> 8;
	out |= (in & 0xff000000) >> 24;

	return out;
}

SR_PRIV struct dev_context *ols_dev_new(void)
{
	struct dev_context *devc;

	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		return NULL;
	}

	/* Device-specific settings */
	devc->max_samples = devc->max_samplerate = devc->protocol_version = 0;

	/* Acquisition settings */
	devc->limit_samples = devc->capture_ratio = 0;
	devc->trigger_at = -1;
	devc->probe_mask = 0xffffffff;
	devc->flag_reg = 0;

	return devc;
}

SR_PRIV struct sr_dev_inst *get_metadata(struct sr_serial_dev_inst *serial)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_probe *probe;
	uint32_t tmp_int, ui;
	uint8_t key, type, token;
	GString *tmp_str, *devname, *version;
	guchar tmp_c;

	sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, NULL, NULL, NULL);
	sdi->driver = di;
	devc = ols_dev_new();
	sdi->priv = devc;

	devname = g_string_new("");
	version = g_string_new("");

	key = 0xff;
	while (key) {
		if (serial_read(serial, &key, 1) != 1 || key == 0x00)
			break;
		type = key >> 5;
		token = key & 0x1f;
		switch (type) {
		case 0:
			/* NULL-terminated string */
			tmp_str = g_string_new("");
			while (serial_read(serial, &tmp_c, 1) == 1 && tmp_c != '\0')
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
			if (serial_read(serial, &tmp_int, 4) != 4)
				break;
			tmp_int = reverse32(tmp_int);
			sr_dbg("Got metadata key 0x%.2x value 0x%.8x.",
			       key, tmp_int);
			switch (token) {
			case 0x00:
				/* Number of usable probes */
				for (ui = 0; ui < tmp_int; ui++) {
					if (!(probe = sr_probe_new(ui, SR_PROBE_LOGIC, TRUE,
							ols_probe_names[ui])))
						return 0;
					sdi->probes = g_slist_append(sdi->probes, probe);
				}
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
			if (serial_read(serial, &tmp_c, 1) != 1)
				break;
			sr_dbg("Got metadata key 0x%.2x value 0x%.2x.",
			       key, tmp_c);
			switch (token) {
			case 0x00:
				/* Number of usable probes */
				for (ui = 0; ui < tmp_c; ui++) {
					if (!(probe = sr_probe_new(ui, SR_PROBE_LOGIC, TRUE,
							ols_probe_names[ui])))
						return 0;
					sdi->probes = g_slist_append(sdi->probes, probe);
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

SR_PRIV int ols_set_samplerate(const struct sr_dev_inst *sdi,
		const uint64_t samplerate)
{
	struct dev_context *devc;

	devc = sdi->priv;
	if (devc->max_samplerate && samplerate > devc->max_samplerate)
		return SR_ERR_SAMPLERATE;

	if (samplerate > CLOCK_RATE) {
		devc->flag_reg |= FLAG_DEMUX;
		devc->cur_samplerate_divider = (CLOCK_RATE * 2 / samplerate) - 1;
	} else {
		devc->flag_reg &= ~FLAG_DEMUX;
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
	sr_source_remove(serial->fd);

	/* Terminate session */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);
}

SR_PRIV int ols_receive_data(int fd, int revents, void *cb_data)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_dev_inst *sdi;
	GSList *l;
	uint32_t sample;
	int num_channels, offset, i, j;
	unsigned char byte;

	drvc = di->priv;

	/* Find this device's devc struct by its fd. */
	devc = NULL;
	for (l = drvc->instances; l; l = l->next) {
		sdi = l->data;
		devc = sdi->priv;
		serial = sdi->conn;
		if (serial->fd == fd)
			break;
		devc = NULL;
	}
	if (!devc)
		/* Shouldn't happen. */
		return TRUE;

	if (devc->num_transfers++ == 0) {
		/*
		 * First time round, means the device started sending data,
		 * and will not stop until done. If it stops sending for
		 * longer than it takes to send a byte, that means it's
		 * finished. We'll double that to 30ms to be sure...
		 */
		sr_source_remove(fd);
		sr_source_add(fd, G_IO_IN, 30, ols_receive_data, cb_data);
		devc->raw_sample_buf = g_try_malloc(devc->limit_samples * 4);
		if (!devc->raw_sample_buf) {
			sr_err("Sample buffer malloc failed.");
			return FALSE;
		}
		/* fill with 1010... for debugging */
		memset(devc->raw_sample_buf, 0x82, devc->limit_samples * 4);
	}

	num_channels = 0;
	for (i = 0x20; i > 0x02; i /= 2) {
		if ((devc->flag_reg & i) == 0)
			num_channels++;
	}

	if (revents == G_IO_IN && devc->num_samples < devc->limit_samples) {
		if (serial_read(serial, &byte, 1) != 1)
			return FALSE;

		/* Ignore it if we've read enough. */
		if (devc->num_samples >= devc->limit_samples)
			return TRUE;

		devc->sample[devc->num_bytes++] = byte;
		sr_dbg("Received byte 0x%.2x.", byte);
		if (devc->num_bytes == num_channels) {
			/* Got a full sample. */
			sample = devc->sample[0] | (devc->sample[1] << 8) \
					| (devc->sample[2] << 16) | (devc->sample[3] << 24);
			sr_dbg("Received sample 0x%.*x.", devc->num_bytes * 2, sample);
			if (devc->flag_reg & FLAG_RLE) {
				/*
				 * In RLE mode -1 should never come in as a
				 * sample, because bit 31 is the "count" flag.
				 */
				if (devc->sample[devc->num_bytes - 1] & 0x80) {
					devc->sample[devc->num_bytes - 1] &= 0x7f;
					/*
					 * FIXME: This will only work on
					 * little-endian systems.
					 */
					devc->rle_count = sample;
					sr_dbg("RLE count: %d.", devc->rle_count);
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

			if (num_channels < 4) {
				/*
				 * Some channel groups may have been turned
				 * off, to speed up transfer between the
				 * hardware and the PC. Expand that here before
				 * submitting it over the session bus --
				 * whatever is listening on the bus will be
				 * expecting a full 32-bit sample, based on
				 * the number of probes.
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
				sr_dbg("Full sample: 0x%.8x.", sample);
			}

			/* the OLS sends its sample buffer backwards.
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
		if (devc->trigger_at != -1) {
			/* a trigger was set up, so we need to tell the frontend
			 * about it.
			 */
			if (devc->trigger_at > 0) {
				/* there are pre-trigger samples, send those first */
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = devc->trigger_at * 4;
				logic.unitsize = 4;
				logic.data = devc->raw_sample_buf +
					(devc->limit_samples - devc->num_samples) * 4;
				sr_session_send(cb_data, &packet);
			}

			/* send the trigger */
			packet.type = SR_DF_TRIGGER;
			sr_session_send(cb_data, &packet);

			/* send post-trigger samples */
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
