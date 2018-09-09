/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 mhooijboer <marchelh@gmail.com>
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 Mathias Grimmberger <mgri@zaphod.sax.de>
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

#define _GNU_SOURCE

#include <config.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"
#include "protocol.h"

/* Set the next event to wait for in siglent_sds_receive(). */
static void siglent_sds_set_wait_event(struct dev_context *devc, enum wait_events event)
{
	if (event == WAIT_STOP) {
		devc->wait_status = 2;
	} else {
		devc->wait_status = 1;
		devc->wait_event = event;
	}
}

/*
 * Waiting for a event will return a timeout after 2 to 3 seconds in order
 * to not block the application.
 */
static int siglent_sds_event_wait(const struct sr_dev_inst *sdi)
{
	char *buf;
	long s;
	int out;
	struct dev_context *devc;
	time_t start;

	if (!(devc = sdi->priv))
		return SR_ERR;

	start = time(NULL);

	s = 10000; /* Sleep time for status refresh. */
	if (devc->wait_status == 1) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger.");
				return SR_ERR_TIMEOUT;
			}

			if (sr_scpi_get_string(sdi->conn, ":INR?", &buf) != SR_OK)
				return SR_ERR;
			sr_atoi(buf, &out);
			g_usleep(s);
		} while (out == 0);

		sr_dbg("Device triggered.");

		if ((devc->timebase < 0.51) && (devc->timebase > 0.99e-6)) {
			/*
			 * Timebase * num hor. divs * 85(%) * 1e6(usecs) / 100
			 * -> 85 percent of sweep time
			 */
			s = (devc->timebase * devc->model->series->num_horizontal_divs * 1000);
			sr_spew("Sleeping for %ld usecs after trigger, "
				"to let the acq buffer in the device fill", s);
			g_usleep(s);
		}
	}
	if (devc->wait_status == 2) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger.");
				return SR_ERR_TIMEOUT;
			}
			if (sr_scpi_get_string(sdi->conn, ":INR?", &buf) != SR_OK)
				return SR_ERR;
			sr_atoi(buf, &out);
			g_usleep(s);
		/* XXX
		 * Now this loop condition looks suspicious! A bitwise
		 * OR of a variable and a non-zero literal should be
		 * non-zero. Logical AND of several non-zero values
		 * should be non-zero. Are many parts of the condition
		 * not taking effect? Was some different condition meant
		 * to get encoded? This needs review, and adjustment.
		 */
		} while (out != DEVICE_STATE_TRIG_RDY || out != DEVICE_STATE_DATA_TRIG_RDY || out != DEVICE_STATE_STOPPED);

		sr_dbg("Device triggered.");

		siglent_sds_set_wait_event(devc, WAIT_NONE);
	}

	return SR_OK;
}

static int siglent_sds_trigger_wait(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;
	return siglent_sds_event_wait(sdi);
}

/* Wait for scope to got to "Stop" in single shot mode. */
static int siglent_sds_stop_wait(const struct sr_dev_inst *sdi)
{
	return siglent_sds_event_wait(sdi);
}

/* Send a configuration setting. */
SR_PRIV int siglent_sds_config_set(const struct sr_dev_inst *sdi, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = sr_scpi_send_variadic(sdi->conn, format, args);
	va_end(args);

	return ret;
}

/* Start capturing a new frameset. */
SR_PRIV int siglent_sds_capture_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;

	switch (devc->model->series->protocol) {
	case SPO_MODEL:
		if (devc->data_source == DATA_SOURCE_SCREEN) {
			char *buf;
			int out;

			sr_dbg("Starting data capture for active frameset %" PRIu64 " of %" PRIu64,
				devc->num_frames + 1, devc->limit_frames);
			if (siglent_sds_config_set(sdi, "ARM") != SR_OK)
				return SR_ERR;
			if (sr_scpi_get_string(sdi->conn, ":INR?", &buf) != SR_OK)
				return SR_ERR;
			sr_atoi(buf, &out);
			if (out == DEVICE_STATE_TRIG_RDY) {
				siglent_sds_set_wait_event(devc, WAIT_TRIGGER);
			} else if (out == DEVICE_STATE_DATA_TRIG_RDY) {
				sr_spew("Device triggered.");
				siglent_sds_set_wait_event(devc, WAIT_BLOCK);
				return SR_OK;
			} else {
				sr_spew("Device did not enter ARM mode.");
				return SR_ERR;
			}
		} else { /* TODO: Implement history retrieval. */
			unsigned int framecount;
			char buf[200];
			int ret;

			sr_dbg("Starting data capture for history frameset.");
			if (siglent_sds_config_set(sdi, "FPAR?") != SR_OK)
				return SR_ERR;
			ret = sr_scpi_read_data(sdi->conn, buf, 200);
			if (ret < 0) {
				sr_err("Read error while reading data header.");
				return SR_ERR;
			}
			memcpy(&framecount, buf + 40, 4);
			if (devc->limit_frames > framecount)
				sr_err("Frame limit higher than frames in buffer of device!");
			else if (devc->limit_frames == 0)
				devc->limit_frames = framecount;
			sr_dbg("Starting data capture for history frameset %" PRIu64 " of %" PRIu64,
				devc->num_frames + 1, devc->limit_frames);
			if (siglent_sds_config_set(sdi, "FRAM %i", devc->num_frames + 1) != SR_OK)
				return SR_ERR;
			if (siglent_sds_channel_start(sdi) != SR_OK)
				return SR_ERR;
			siglent_sds_set_wait_event(devc, WAIT_STOP);
		}
		break;
	case ESERIES:
		if (devc->data_source == DATA_SOURCE_SCREEN) {
			char *buf;
			int out;

			sr_dbg("Starting data capture for active frameset %" PRIu64 " of %" PRIu64,
				devc->num_frames + 1, devc->limit_frames);
			if (siglent_sds_config_set(sdi, "ARM") != SR_OK)
				return SR_ERR;
			if (sr_scpi_get_string(sdi->conn, ":INR?", &buf) != SR_OK)
				return SR_ERR;
			sr_atoi(buf, &out);
			if (out == DEVICE_STATE_TRIG_RDY) {
				siglent_sds_set_wait_event(devc, WAIT_TRIGGER);
			} else if (out == DEVICE_STATE_DATA_TRIG_RDY) {
				sr_spew("Device triggered.");
				siglent_sds_set_wait_event(devc, WAIT_BLOCK);
				return SR_OK;
			} else {
				sr_spew("Device did not enter ARM mode.");
				return SR_ERR;
			}
		} else { /* TODO: Implement history retrieval. */
			unsigned int framecount;
			char buf[200];
			int ret;

			sr_dbg("Starting data capture for history frameset.");
			if (siglent_sds_config_set(sdi, "FPAR?") != SR_OK)
				return SR_ERR;
			ret = sr_scpi_read_data(sdi->conn, buf, 200);
			if (ret < 0) {
				sr_err("Read error while reading data header.");
				return SR_ERR;
			}
			memcpy(&framecount, buf + 40, 4);
			if (devc->limit_frames > framecount)
				sr_err("Frame limit higher than frames in buffer of device!");
			else if (devc->limit_frames == 0)
				devc->limit_frames = framecount;
			sr_dbg("Starting data capture for history frameset %" PRIu64 " of %" PRIu64,
				devc->num_frames + 1, devc->limit_frames);
			if (siglent_sds_config_set(sdi, "FRAM %i", devc->num_frames + 1) != SR_OK)
				return SR_ERR;
			if (siglent_sds_channel_start(sdi) != SR_OK)
				return SR_ERR;
			siglent_sds_set_wait_event(devc, WAIT_STOP);
		}
		break;
	case NON_SPO_MODEL:
		siglent_sds_set_wait_event(devc, WAIT_TRIGGER);
		break;
	}

	return SR_OK;
}

/* Start reading data from the current channel. */
SR_PRIV int siglent_sds_channel_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const char *s;

	if (!(devc = sdi->priv))
		return SR_ERR;

	ch = devc->channel_entry->data;

	sr_dbg("Start reading data from channel %s.", ch->name);

	switch (devc->model->series->protocol) {
	case NON_SPO_MODEL:
	case SPO_MODEL:
		s = (ch->type == SR_CHANNEL_LOGIC) ? "D%d:WF?" : "C%d:WF? ALL";
		if (sr_scpi_send(sdi->conn, s, ch->index + 1) != SR_OK)
			return SR_ERR;
		siglent_sds_set_wait_event(devc, WAIT_NONE);
		break;
	case ESERIES:
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (sr_scpi_send(sdi->conn, "C%d:WF? ALL",
				ch->index + 1) != SR_OK)
				return SR_ERR;
		}
		siglent_sds_set_wait_event(devc, WAIT_NONE);
		if (sr_scpi_read_begin(sdi->conn) != SR_OK)
			return TRUE;
		siglent_sds_set_wait_event(devc, WAIT_BLOCK);
		break;
	}

	devc->num_channel_bytes = 0;
	devc->num_header_bytes = 0;
	devc->num_block_bytes = 0;

	return SR_OK;
}

/* Read the header of a data block. */
static int siglent_sds_read_header(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;
	char *buf = (char *)devc->buffer;
	int ret, desc_length;
	int block_offset = 15; /* Offset for descriptor block. */
	long data_length = 0;

	/* Read header from device. */
	ret = sr_scpi_read_data(scpi, buf, SIGLENT_HEADER_SIZE);
	if (ret < SIGLENT_HEADER_SIZE) {
		sr_err("Read error while reading data header.");
		return SR_ERR;
	}
	sr_dbg("Device returned %i bytes.", ret);
	devc->num_header_bytes += ret;
	buf += block_offset; /* Skip to start descriptor block. */

	/* Parse WaveDescriptor header. */
	memcpy(&desc_length, buf + 36, 4); /* Descriptor block length */
	memcpy(&data_length, buf + 60, 4); /* Data block length */

	devc->block_header_size = desc_length + 15;
	devc->num_samples = data_length;

	sr_dbg("Received data block header: '%s' -> block length %d.", buf, ret);

	return ret;
}

static int siglent_sds_get_digital(const struct sr_dev_inst *sdi, struct sr_channel *ch)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;
	GArray *tmp_samplebuf; /* Temp buffer while iterating over the scope samples */
	char *buf = (char *)devc->buffer; /* Buffer from scope */
	uint8_t tmp_value; /* Holding temp value from data */
	GArray *data_low_channels, *data_high_channels, *buffdata;
	GSList *l;
	gboolean low_channels; /* Lower channels enabled */
	gboolean high_channels; /* Higher channels enabled */
	int len, channel_index;
	long samples_index;

	len = 0;
	channel_index = 0;
	low_channels = FALSE;
	high_channels = FALSE;
	data_low_channels = g_array_new(FALSE, TRUE, sizeof(uint8_t));
	data_high_channels = g_array_new(FALSE, TRUE, sizeof(uint8_t));

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		samples_index = 0;
		if (ch->type == SR_CHANNEL_LOGIC) {
			if (ch->enabled) {
				if (sr_scpi_send(sdi->conn, "D%d:WF? DAT2", ch->index) != SR_OK)
					return SR_ERR;
				if (sr_scpi_read_begin(scpi) != SR_OK)
					return TRUE;
				len = sr_scpi_read_data(scpi, buf, -1);
				if (len < 0)
					return TRUE;
				len -= 15;
				buffdata = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t), len);
				buf += 15; /* Skipping the data header. */
				g_array_append_vals(buffdata, buf, len);
				tmp_samplebuf = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t), len); /* New temp buffer. */
				for (uint64_t cur_sample_index = 0; cur_sample_index < devc->memory_depth_digital; cur_sample_index++) {
					char sample = (char)g_array_index(buffdata, uint8_t, cur_sample_index);
					for (int ii = 0; ii < 8; ii++, sample >>= 1) {
						if (ch->index < 8) {
							channel_index = ch->index;
							if (data_low_channels->len <= samples_index) {
								tmp_value = 0; /* New sample. */
								low_channels = TRUE; /* We have at least one enabled low channel. */
							} else {
								/* Get previous stored sample from low channel buffer. */
								tmp_value = g_array_index(data_low_channels, uint8_t, samples_index);
							}
						} else {
							channel_index = ch->index - 8;
							if (data_high_channels->len <= samples_index) {
								tmp_value = 0; /* New sample. */
								high_channels = TRUE; /* We have at least one enabled high channel. */
							} else {
								/* Get previous stored sample from high channel buffer. */
								tmp_value = g_array_index(data_high_channels, uint8_t, samples_index);
							}
						}
						/* Check if the current scope sample bit is set. */
						if (sample & 0x1)
							tmp_value |= 1UL << channel_index; /* Set current scope sample bit based on channel index. */
						g_array_append_val(tmp_samplebuf, tmp_value);
						samples_index++;
					}
				}

				/* Clear the buffers to prepare for the new samples */
				if (ch->index < 8) {
					g_array_free(data_low_channels, FALSE);
					data_low_channels = g_array_new(FALSE, FALSE, sizeof(uint8_t));
				} else {
					g_array_free(data_high_channels, FALSE);
					data_high_channels = g_array_new(FALSE, FALSE, sizeof(uint8_t));
				}

				/* Storing the converted temp values from the the scope into the buffers. */
				for (long index = 0; index < tmp_samplebuf->len; index++) {
					uint8_t value = g_array_index(tmp_samplebuf, uint8_t, index);
					if (ch->index < 8)
						g_array_append_val(data_low_channels, value);
					else
						g_array_append_val(data_high_channels, value);
				}
				g_array_free(tmp_samplebuf, TRUE);
				g_array_free(buffdata, TRUE);
			}
		}
	}

	/* Combining the lower and higher channel buffers into one buffer for sigrok. */
	devc->dig_buffer = g_array_new(FALSE, FALSE, sizeof(uint8_t));
	for (uint64_t index = 0; index < devc->memory_depth_digital; index++) {
		uint8_t value;
		if (low_channels) {
			value = g_array_index(data_low_channels, uint8_t, index);
			g_array_append_val(devc->dig_buffer, value);
		} else {
			value = 0;
			g_array_append_val(devc->dig_buffer, value);
		}
		if (high_channels) {
			value = g_array_index(data_high_channels, uint8_t, index);
			g_array_append_val(devc->dig_buffer, value);
		} else {
			value = 0;
			g_array_append_val(devc->dig_buffer, value);
		}
	}

	g_array_free(data_low_channels, TRUE);
	g_array_free(data_high_channels, TRUE);

	return len;
}

SR_PRIV int siglent_sds_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_logic logic;
	struct sr_channel *ch;
	int len, i;
	float wait;
	gboolean read_complete = false;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	scpi = sdi->conn;

	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;

	switch (devc->wait_event) {
	case WAIT_NONE:
		break;
	case WAIT_TRIGGER:
		if (siglent_sds_trigger_wait(sdi) != SR_OK)
			return TRUE;
		if (siglent_sds_channel_start(sdi) != SR_OK)
			return TRUE;
		return TRUE;
	case WAIT_BLOCK:
		if (siglent_sds_channel_start(sdi) != SR_OK)
			return TRUE;
		break;
	case WAIT_STOP:
		if (siglent_sds_stop_wait(sdi) != SR_OK)
			return TRUE;
		if (siglent_sds_channel_start(sdi) != SR_OK)
			return TRUE;
		return TRUE;
	default:
		sr_err("BUG: Unknown event target encountered.");
		break;
	}

	ch = devc->channel_entry->data;
	len = 0;

	if (ch->type == SR_CHANNEL_ANALOG) {
		if (devc->num_block_bytes == 0) {
			/* Wait for the device to fill its output buffers. */
			switch (devc->model->series->protocol) {
			case NON_SPO_MODEL:
			case SPO_MODEL:
				/* The older models need more time to prepare the the output buffers due to CPU speed. */
				wait = (devc->memory_depth_analog * 2.5);
				sr_dbg("Waiting %.f0 ms for device to prepare the output buffers", wait / 1000);
				g_usleep(wait);
				if (sr_scpi_read_begin(scpi) != SR_OK)
					return TRUE;
				break;
			case ESERIES:
				/* The newer models (ending with the E) have faster CPUs but still need time when a slow timebase is selected. */
				if (sr_scpi_read_begin(scpi) != SR_OK)
					return TRUE;
				wait = ((devc->timebase * devc->model->series->num_horizontal_divs) * 100000);
				sr_dbg("Waiting %.f0 ms for device to prepare the output buffers", wait / 1000);
				g_usleep(wait);
				break;
			}

			sr_dbg("New block with header expected.");
			len = siglent_sds_read_header(sdi);
			if (len == 0)
				/* Still reading the header. */
				return TRUE;
			if (len == -1) {
				sr_err("Read error, aborting capture.");
				packet.type = SR_DF_FRAME_END;
				sr_session_send(sdi, &packet);
				sdi->driver->dev_acquisition_stop(sdi);
				return TRUE;
			}
			devc->num_block_bytes = len;
			devc->num_block_read = 0;

			if (len == -1) {
				sr_err("Read error, aborting capture.");
				packet.type = SR_DF_FRAME_END;
				sr_session_send(sdi, &packet);
				sdi->driver->dev_acquisition_stop(sdi);
				return TRUE;
			}

			do {
				read_complete = false;
				if (devc->num_block_bytes > devc->num_samples) {
					/* We received all data as one block. */
					/* Offset the data block buffer past the IEEE header and description header. */
					devc->buffer += devc->block_header_size;
					len = devc->num_samples;
				} else {
					sr_dbg("Requesting: %li bytes.", devc->num_samples - devc->num_block_bytes);
					len = sr_scpi_read_data(scpi, (char *)devc->buffer, devc->num_samples-devc->num_block_bytes);
					if (len == -1) {
						sr_err("Read error, aborting capture.");
						packet.type = SR_DF_FRAME_END;
						sr_session_send(sdi, &packet);
						sdi->driver->dev_acquisition_stop(sdi);
						return TRUE;
					}
					devc->num_block_read++;
					devc->num_block_bytes += len;
				}
				sr_dbg("Received block: %i, %d bytes.", devc->num_block_read, len);
				if (ch->type == SR_CHANNEL_ANALOG) {
					float vdiv = devc->vdiv[ch->index];
					float offset = devc->vert_offset[ch->index];
					GArray *float_data;
					static GArray *data;
					float voltage, vdivlog;
					int digits;

					data = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t), len);
					g_array_append_vals(data, devc->buffer, len);
					float_data = g_array_new(FALSE, FALSE, sizeof(float));
					for (i = 0; i < len; i++) {
						voltage = (float)g_array_index(data, int8_t, i) / 25;
						voltage = ((vdiv * voltage) - offset);
						g_array_append_val(float_data, voltage);
					}
					vdivlog = log10f(vdiv);
					digits = -(int) vdivlog + (vdivlog < 0.0);
					sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
					analog.meaning->channels = g_slist_append(NULL, ch);
					analog.num_samples = float_data->len;
					analog.data = (float *)float_data->data;
					analog.meaning->mq = SR_MQ_VOLTAGE;
					analog.meaning->unit = SR_UNIT_VOLT;
					analog.meaning->mqflags = 0;
					packet.type = SR_DF_ANALOG;
					packet.payload = &analog;
					sr_session_send(sdi, &packet);
					g_slist_free(analog.meaning->channels);
					g_array_free(data, TRUE);
				}
				len = 0;
				if (devc->num_samples == (devc->num_block_bytes - SIGLENT_HEADER_SIZE)) {
					sr_dbg("Transfer has been completed.");
					devc->num_header_bytes = 0;
					devc->num_block_bytes = 0;
					read_complete = true;
					if (!sr_scpi_read_complete(scpi)) {
						sr_err("Read should have been completed.");
						packet.type = SR_DF_FRAME_END;
						sr_session_send(sdi, &packet);
						sdi->driver->dev_acquisition_stop(sdi);
						return TRUE;
					}
					devc->num_block_read = 0;
				} else {
					sr_dbg("%" PRIu64 " of %" PRIu64 " block bytes read.",
						devc->num_block_bytes, devc->num_samples);
				}
			} while (!read_complete);

			if (devc->channel_entry->next) {
				/* We got the frame for this channel, now get the next channel. */
				devc->channel_entry = devc->channel_entry->next;
				siglent_sds_channel_start(sdi);
			} else {
				/* Done with this frame. */
				packet.type = SR_DF_FRAME_END;
				sr_session_send(sdi, &packet);
				if (++devc->num_frames == devc->limit_frames) {
					/* Last frame, stop capture. */
					sdi->driver->dev_acquisition_stop(sdi);
				} else {
					/* Get the next frame, starting with the first channel. */
					devc->channel_entry = devc->enabled_channels;
					siglent_sds_capture_start(sdi);

					/* Start of next frame. */
					packet.type = SR_DF_FRAME_BEGIN;
					sr_session_send(sdi, &packet);
				}
			}
		}
	} else {
		if (!siglent_sds_get_digital(sdi, ch))
			return TRUE;
		logic.length = devc->dig_buffer->len;
		logic.unitsize = 2;
		logic.data = devc->dig_buffer->data;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		sr_session_send(sdi, &packet);
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);
		sdi->driver->dev_acquisition_stop(sdi);

		if (++devc->num_frames == devc->limit_frames) {
			/* Last frame, stop capture. */
			sdi->driver->dev_acquisition_stop(sdi);
		} else {
			/* Get the next frame, starting with the first channel. */
			devc->channel_entry = devc->enabled_channels;
			siglent_sds_capture_start(sdi);

			/* Start of next frame. */
			packet.type = SR_DF_FRAME_BEGIN;
			sr_session_send(sdi, &packet);
		}
	}

	// sr_session_send(sdi, &packet);
	// packet.type = SR_DF_FRAME_END;
	// sr_session_send(sdi, &packet);
	// sdi->driver->dev_acquisition_stop(sdi);

	return TRUE;
}

SR_PRIV int siglent_sds_get_dev_cfg(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	char *cmd, *response;
	unsigned int i;
	int res, num_tokens;
	gchar **tokens;
	int len;
	float trigger_pos;

	devc = sdi->priv;

	/* Analog channel state. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf("C%i:TRA?", i + 1);
		res = sr_scpi_get_bool(sdi->conn, cmd, &devc->analog_channels[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
		ch = g_slist_nth_data(sdi->channels, i);
		ch->enabled = devc->analog_channels[i];
	}
	sr_dbg("Current analog channel state:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %s", i + 1, devc->analog_channels[i] ? "On" : "Off");

	/* Digital channel state. */
	if (devc->model->has_digital) {
		gboolean status;

		sr_dbg("Check logic analyzer channel state.");
		devc->la_enabled = FALSE;
		cmd = g_strdup_printf("DI:SW?");
		res = sr_scpi_get_bool(sdi->conn, cmd, &status);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
		sr_dbg("Logic analyzer status: %s", status ? "On" : "Off");
		if (status) {
			devc->la_enabled = TRUE;
			for (i = 0; i < ARRAY_SIZE(devc->digital_channels); i++) {
				cmd = g_strdup_printf("D%i:TRA?", i);
				res = sr_scpi_get_bool(sdi->conn, cmd, &devc->digital_channels[i]);
				g_free(cmd);
				if (res != SR_OK)
					return SR_ERR;
				ch = g_slist_nth_data(sdi->channels, i + devc->model->analog_channels);
				ch->enabled = devc->digital_channels[i];
				sr_dbg("D%d: %s", i, devc->digital_channels[i] ? "On" : "Off");
			}
		} else {
			for (i = 0; i < ARRAY_SIZE(devc->digital_channels); i++) {
				ch = g_slist_nth_data(sdi->channels, i + devc->model->analog_channels);
				devc->digital_channels[i] = FALSE;
				ch->enabled = devc->digital_channels[i];
				sr_dbg("D%d: %s", i, devc->digital_channels[i] ? "On" : "Off");
			}
		}
	}

	/* Timebase. */
	if (sr_scpi_get_float(sdi->conn, ":TDIV?", &devc->timebase) != SR_OK)
		return SR_ERR;
	sr_dbg("Current timebase: %g.", devc->timebase);

	/* Probe attenuation. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf("C%d:ATTN?", i + 1);
		res = sr_scpi_get_float(sdi->conn, cmd, &devc->attenuation[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current probe attenuation:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %g", i + 1, devc->attenuation[i]);

	/* Vertical gain and offset. */
	if (siglent_sds_get_dev_cfg_vertical(sdi) != SR_OK)
		return SR_ERR;

	/* Coupling. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf("C%d:CPL?", i + 1);
		res = sr_scpi_get_string(sdi->conn, cmd, &devc->coupling[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}

	sr_dbg("Current coupling:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %s", i + 1, devc->coupling[i]);

	/* Trigger source. */
	response = NULL;
	tokens = NULL;
	if (sr_scpi_get_string(sdi->conn, "TRSE?", &response) != SR_OK)
		return SR_ERR;
	tokens = g_strsplit(response, ",", 0);
	for (num_tokens = 0; tokens[num_tokens] != NULL; num_tokens++);
	if (num_tokens < 4) {
		sr_dbg("IDN response not according to spec: %80.s.", response);
		g_strfreev(tokens);
		g_free(response);
		return SR_ERR_DATA;
	}
	g_free(response);
	devc->trigger_source = g_strstrip(g_strdup(tokens[2]));
	sr_dbg("Current trigger source: %s.", devc->trigger_source);

	/* TODO: Horizontal trigger position. */
	response = "";
	trigger_pos = 0;
	// if (sr_scpi_get_string(sdi->conn, g_strdup_printf("%s:TRDL?", devc->trigger_source), &response) != SR_OK)
	// 	return SR_ERR;
	// len = strlen(response);
	len = strlen(tokens[4]);
	if (!g_ascii_strcasecmp(tokens[4] + (len - 2), "us")) {
		trigger_pos = atof(tokens[4]) / SR_GHZ(1);
		sr_dbg("Current trigger position us %s.", tokens[4] );
	} else if (!g_ascii_strcasecmp(tokens[4] + (len - 2), "ns")) {
		trigger_pos = atof(tokens[4]) / SR_MHZ(1);
		sr_dbg("Current trigger position ms %s.", tokens[4] );
	} else if (!g_ascii_strcasecmp(tokens[4] + (len - 2), "ms")) {
		trigger_pos = atof(tokens[4]) / SR_KHZ(1);
		sr_dbg("Current trigger position ns %s.", tokens[4] );
	} else if (!g_ascii_strcasecmp(tokens[4] + (len - 2), "s")) {
		trigger_pos = atof(tokens[4]);
		sr_dbg("Current trigger position s %s.", tokens[4] );
	};
	devc->horiz_triggerpos = trigger_pos;

	sr_dbg("Current horizontal trigger position %.10f.", devc->horiz_triggerpos);

	/* Trigger slope. */
	cmd = g_strdup_printf("%s:TRSL?", devc->trigger_source);
	res = sr_scpi_get_string(sdi->conn, cmd, &devc->trigger_slope);
	g_free(cmd);
	if (res != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger slope: %s.", devc->trigger_slope);

	/* Trigger level, only when analog channel. */
	if (g_str_has_prefix(tokens[2], "C")) {
		cmd = g_strdup_printf("%s:TRLV?", devc->trigger_source);
		res = sr_scpi_get_float(sdi->conn, cmd, &devc->trigger_level);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
		sr_dbg("Current trigger level: %g.", devc->trigger_level);
	}

	return SR_OK;
}

SR_PRIV int siglent_sds_get_dev_cfg_vertical(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *cmd;
	unsigned int i;
	int res;

	devc = sdi->priv;

	/* Vertical gain. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf("C%d:VDIV?", i + 1);
		res = sr_scpi_get_float(sdi->conn, cmd, &devc->vdiv[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current vertical gain:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %g", i + 1, devc->vdiv[i]);

	/* Vertical offset. */
	for (i = 0; i < devc->model->analog_channels; i++) {
		cmd = g_strdup_printf("C%d:OFST?", i + 1);
		res = sr_scpi_get_float(sdi->conn, cmd, &devc->vert_offset[i]);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
	}
	sr_dbg("Current vertical offset:");
	for (i = 0; i < devc->model->analog_channels; i++)
		sr_dbg("CH%d %g", i + 1, devc->vert_offset[i]);

	return SR_OK;
}

SR_PRIV int siglent_sds_get_dev_cfg_horizontal(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *cmd;
	int res;
	char *sample_points_string;
	float samplerate_scope, fvalue;

	devc = sdi->priv;

	switch (devc->model->series->protocol) {
	case SPO_MODEL:
	case NON_SPO_MODEL:
		cmd = g_strdup_printf("SANU? C1");
		res = sr_scpi_get_string(sdi->conn, cmd, &sample_points_string);
		g_free(cmd);
		samplerate_scope = 0;
		fvalue = 0;
		if (res != SR_OK)
			return SR_ERR;
		if (g_strstr_len(sample_points_string, -1, "Mpts") != NULL) {
			sample_points_string[strlen(sample_points_string) - 4] = '\0';
			if (sr_atof_ascii(sample_points_string, &fvalue) != SR_OK) {
				sr_dbg("Invalid float converted from scope response.");
				return SR_ERR;
			}
			samplerate_scope = fvalue * 1000000;
		} else if (g_strstr_len(sample_points_string, -1, "Kpts") != NULL) {
			sample_points_string[strlen(sample_points_string) - 4] = '\0';
			if (sr_atof_ascii(sample_points_string, &fvalue) != SR_OK) {
				sr_dbg("Invalid float converted from scope response.");
				return SR_ERR;
			}
			samplerate_scope = fvalue * 10000;
		} else {
			samplerate_scope = fvalue;
		}
		g_free(sample_points_string);
		devc->memory_depth_analog = samplerate_scope;
		break;
	case ESERIES:
		cmd = g_strdup_printf("SANU? C1");
		if (sr_scpi_get_float(sdi->conn, cmd, &fvalue) != SR_OK)
			return SR_ERR;
		devc->memory_depth_analog = (long)fvalue;
		if (devc->la_enabled) {
			cmd = g_strdup_printf("SANU? D0");
			if (sr_scpi_get_float(sdi->conn, cmd, &fvalue) != SR_OK)
				return SR_ERR;
			devc->memory_depth_digital = (long)fvalue;
		}
		g_free(cmd);
		break;
	};

	/* Get the timebase. */
	if (sr_scpi_get_float(sdi->conn, ":TDIV?", &devc->timebase) != SR_OK)
		return SR_ERR;

	sr_dbg("Current timebase: %g.", devc->timebase);
	devc->samplerate = devc->memory_depth_analog / (devc->timebase * devc->model->series->num_horizontal_divs);
	sr_dbg("Current samplerate: %0f.", devc->samplerate);
	sr_dbg("Current memory depth: %lu.", devc->memory_depth_analog);

	return SR_OK;
}
