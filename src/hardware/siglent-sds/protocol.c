/*
 * This file is part of the libsigrok project.
 *
 * Siglent implementation:
 * Copyright (C) 2016 mhooijboer <marchelh@gmail.com>
 *
 * The Siglent implementation is based on Rigol driver sources, which are:
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
#include <glib-2.0/glib/gmacros.h>
#include <glib-2.0/glib/gmain.h>
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

/* Set the next event to wait for in siglent_sds_receive */
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

	s = 10000; /* Sleep time for status refresh */
	if (devc->wait_status == 1) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (sr_scpi_get_string(sdi->conn, ":INR?", &buf) != SR_OK)
				return SR_ERR;
			sr_atoi(buf, &out);
			g_usleep(s);
		} while (out == 0);
		sr_dbg("Device triggerd");
		if (devc->timebase < 0.51) {
			if (devc->timebase > 0.99e-6) {
				/*
				 * Timebase * num hor. divs * 85(%) * 1e6(usecs) / 100
				 * -> 85 percent of sweep time
				 */
				s = (devc->timebase * devc->model->series->num_horizontal_divs * 1000);
				sr_spew("Sleeping for %ld usecs after trigger, to let the acq buffer in the device fill", s);
				g_usleep(s);
			}

		}
	}
	if (devc->wait_status == 2) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
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
		} while ((out | DEVICE_STATE_TRIG_RDY && out | DEVICE_STATE_DATA_ACQ) && out | DEVICE_STATE_STOPPED);
		sr_dbg("Device triggerd 2");
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

/* Wait for scope to got to "Stop" in single shot mode */
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

	if (ret != SR_OK) {
		return SR_ERR;
	}

	return SR_OK;
}

/* Start capturing a new frameset */
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
			} else if (out == DEVICE_STATE_TRIG_RDY + 1) {
				sr_spew("Device Triggerd");
				siglent_sds_set_wait_event(devc, WAIT_BLOCK);
				return SR_OK;
			} else {
				sr_spew("Device did not enter ARM mode");
				return SR_ERR;
			}
		} else { //TODO implement History retrieval
			unsigned int framecount;
			char buf[200];
			int ret;

			sr_dbg("Starting data capture for history frameset");
			if (siglent_sds_config_set(sdi, "FPAR?") != SR_OK)
				return SR_ERR;
			ret = sr_scpi_read_data(sdi->conn, buf, 200);
			if (ret < 0) {
				sr_err("Read error while reading data header.");
				return SR_ERR;
			}
			memcpy(&framecount, buf + 40, 4);
			if (devc->limit_frames > framecount) {
				sr_err("Frame limit higher that frames in buffer of device!");
			} else if (devc->limit_frames == 0) {
				devc->limit_frames = framecount;
			}
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

/* Start reading data from the current channel */
SR_PRIV int siglent_sds_channel_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;

	if (!(devc = sdi->priv))
		return SR_ERR;

	ch = devc->channel_entry->data;

	sr_dbg("Starting reading data from channel %d", ch->index + 1);

	switch (devc->model->series->protocol) {
	case NON_SPO_MODEL:
	case SPO_MODEL:
		if (ch->type == SR_CHANNEL_LOGIC) {
			if (sr_scpi_send(sdi->conn, "D%d:WF?",
				ch->index + 1) != SR_OK)
				return SR_ERR;
		} else {
			if (sr_scpi_send(sdi->conn, "C%d:WF? ALL",
				ch->index + 1) != SR_OK)
				return SR_ERR;
		}
		siglent_sds_set_wait_event(devc, WAIT_NONE);
		break;
	}

	siglent_sds_set_wait_event(devc, WAIT_BLOCK);

	devc->num_channel_bytes = 0;
	devc->num_header_bytes = 0;
	devc->num_block_bytes = 0;

	return SR_OK;
}

/* Read the header of a data block */
static int siglent_sds_read_header(struct sr_dev_inst *sdi, int channelIndex)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	struct dev_context *devc = sdi->priv;
	char *buf = (char *)devc->buffer;
	int ret;
	int descLength;
	int blockOffset = 15; // Offset for Descriptor block.
	long dataLength = 0;

	/* Read header from device */
	ret = sr_scpi_read_data(scpi, buf + devc->num_header_bytes, devc->model->series->buffer_samples);
	if (ret < 346) {
		sr_err("Read error while reading data header.");
		return SR_ERR;
	}
	sr_dbg("Device returned %i bytes", ret);
	devc->num_header_bytes += ret;
	buf += blockOffset; //Skip to start Descriptor block

	// Parse WaveDescriptor Header
	memcpy(&descLength, buf + 36, 4); // Descriptor block length
	memcpy(&dataLength, buf + 60, 4); // Data block length

	devc->vdiv[channelIndex] = 2;
	devc->vert_offset[channelIndex] = 0;
	devc->blockHeaderSize = descLength + 15;
	ret = dataLength;
	sr_dbg("Received data block header: '%s' -> block length %d", buf, ret);
	return ret;
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
	int len, i;
	struct sr_channel *ch;
	gsize expected_data_bytes = 0;
	char *memsize;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	scpi = sdi->conn;

	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;

	memsize = NULL;
	if (sr_scpi_get_string(sdi->conn, "MSIZ?", &memsize) != SR_OK)
		return SR_ERR;

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
		if (siglent_sds_stop_wait(sdi) != SR_OK) {
			return TRUE;
		}
		if (siglent_sds_channel_start(sdi) != SR_OK) {
			return TRUE;
		}
		return TRUE;
	default:
		sr_err("BUG: Unknown event target encountered");
		break;
	}

	ch = devc->channel_entry->data;

	if (devc->num_block_bytes == 0) {

		if (g_ascii_strcasecmp(memsize, "14M") == 0){
			sr_err("Device memory depth is set to 14Mpts, so please be patient");
			g_usleep(4900000); // Sleep for large memory set
		}
		sr_dbg("New block header expected");
		len = siglent_sds_read_header(sdi, ch->index);
		expected_data_bytes = len;
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

		if (devc->data_source == DATA_SOURCE_SCREEN
			&& (unsigned)len < expected_data_bytes) {
			sr_dbg("Discarding short data block");
			sr_scpi_read_data(scpi, (char *)devc->buffer, len + 1);
			return TRUE;
		}
		devc->num_block_bytes = len;
		devc->num_block_read = 0;
	}

	len = devc->num_block_bytes - devc->num_block_read;
	if (len > ACQ_BUFFER_SIZE)
		len = ACQ_BUFFER_SIZE;

	/*Offset the data block buffer past the IEEE header and Description Header*/
	devc->buffer += devc->blockHeaderSize;

	if (len == -1) {
		sr_err("Read error, aborting capture.");
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);
		sdi->driver->dev_acquisition_stop(sdi);
		return TRUE;
	}

	sr_dbg("Received %d bytes.", len);

	devc->num_block_read += len;

	if (ch->type == SR_CHANNEL_ANALOG) {
		float vdiv = devc->vdiv[ch->index];
		float offset = devc->vert_offset[ch->index];
		GArray *float_data;
		static GArray *data;
		float voltage;
		float vdivlog;
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
	} else {
		logic.length = len;
		logic.unitsize = 1;
		logic.data = devc->buffer;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		sr_session_send(sdi, &packet);
	}

	if (devc->num_block_read == devc->num_block_bytes) {
		sr_dbg("Block has been completed");
		/* Prepare for possible next block */
		sr_dbg("Prepare for possible next block");
		devc->num_header_bytes = 0;
		devc->num_block_bytes = 0;
		if (devc->data_source != DATA_SOURCE_SCREEN) {
			siglent_sds_set_wait_event(devc, WAIT_BLOCK);
		}
		if (!sr_scpi_read_complete(scpi)) {
			sr_err("Read should have been completed");
			packet.type = SR_DF_FRAME_END;
			sr_session_send(sdi, &packet);
			sdi->driver->dev_acquisition_stop(sdi);
			return TRUE;
		}
		devc->num_block_read = 0;
	} else {
		sr_dbg("%" PRIu64 " of %" PRIu64 " block bytes read",
			devc->num_block_read, devc->num_block_bytes);
	}
	devc->num_channel_bytes += len;
	if (devc->num_channel_bytes < expected_data_bytes) {
		/* Don't have the full data for this channel yet, re-run. */
		return TRUE;
	}
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
	return TRUE;
}

SR_PRIV int siglent_sds_get_dev_cfg(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	char *cmd;
	unsigned int i;
	int res;
	char *response;
	gchar **tokens;
	int num_tokens;

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

		sr_dbg("Check Logic Analyzer channel state");
		devc->la_enabled = FALSE;
		cmd = g_strdup_printf("DGST?");
		res = sr_scpi_get_bool(sdi->conn, cmd, &status);
		g_free(cmd);
		if (res != SR_OK)
			return SR_ERR;
		sr_dbg("Logic Analyzer status: %s", status ? "On" : "Off");
		if (status) {
			devc->la_enabled = TRUE;
			for (i = 0; i < ARRAY_SIZE(devc->digital_channels); i++) {
				cmd = g_strdup_printf("D%i:DGCH?", i);
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
	sr_dbg("Current timebase %g", devc->timebase);

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
	sr_dbg("Current trigger source %s", devc->trigger_source);

	/* TODO Horizontal trigger position. */

	devc->horiz_triggerpos = 0;
	sr_dbg("Current horizontal trigger position %g", devc->horiz_triggerpos);

	/* Trigger slope. */
	cmd = g_strdup_printf("%s:TRSL?", devc->trigger_source);
	res = sr_scpi_get_string(sdi->conn, cmd, &devc->trigger_slope);
	g_free(cmd);
	if (res != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger slope %s", devc->trigger_slope);

	/* Trigger level. */
	cmd = g_strdup_printf("%s:TRLV?", devc->trigger_source);
	res = sr_scpi_get_float(sdi->conn, cmd, &devc->trigger_level);
	g_free(cmd);
	if (res != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger level %g", devc->trigger_level);
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
	char *samplePointsString;
	float samplerateScope;
	float fvalue;
	char *first, *concat;

	devc = sdi->priv;
	cmd = g_strdup_printf("SANU? C1");
	res = sr_scpi_get_string(sdi->conn, cmd, &samplePointsString);
	g_free(cmd);
	if (res != SR_OK)
		return SR_ERR;
	if (strcasestr(samplePointsString, "Mpts") != NULL) {
		samplePointsString[strlen(samplePointsString) - 4] = '\0';

		if (strcasestr(samplePointsString, ".") != NULL) {
			first = strtok(samplePointsString, ".");
			concat = strcat(first, strtok(NULL, "."));
			if (sr_atof_ascii(concat, &fvalue) != SR_OK || fvalue == 0.0) {
				sr_dbg("Invalid float converted from scope response.");
				return SR_ERR;
			}
		} else {
			if (sr_atof_ascii(samplePointsString, &fvalue) != SR_OK || fvalue == 0.0) {
				sr_dbg("Invalid float converted from scope response.");
				return SR_ERR;
			}
		}
		samplerateScope = fvalue * 100000;
	} else {
		samplePointsString[strlen(samplePointsString) - 4] = '\0';
		if (sr_atof_ascii(samplePointsString, &fvalue) != SR_OK || fvalue == 0.0) {
			sr_dbg("Invalid float converted from scope response.");
			return SR_ERR;
		}
		samplerateScope = fvalue * 1000;
	}
	/* Get the Timebase. */
	if (sr_scpi_get_float(sdi->conn, ":TDIV?", &devc->timebase) != SR_OK)
		return SR_ERR;
	sr_dbg("Current timebase %g", devc->timebase);
	devc->sampleRate = samplerateScope / (devc->timebase * devc->model->series->num_horizontal_divs);
	return SR_OK;
}
