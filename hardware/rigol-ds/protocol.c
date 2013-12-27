/*
 * This file is part of the libsigrok project.
 *
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

#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

/*
 * This is a unified protocol driver for the DS1000 and DS2000 series.
 *
 * DS1000 support tested with a Rigol DS1102D.
 *
 * DS2000 support tested with a Rigol DS2072 using firmware version 01.01.00.02.
 *
 * The Rigol DS2000 series scopes try to adhere to the IEEE 488.2 (I think)
 * standard. If you want to read it - it costs real money...
 *
 * Every response from the scope has a linefeed appended because the
 * standard says so. In principle this could be ignored because sending the
 * next command clears the output queue of the scope. This driver tries to
 * avoid doing that because it may cause an error being generated inside the
 * scope and who knows what bugs the firmware has WRT this.
 *
 * Waveform data is transferred in a format called "arbitrary block program
 * data" specified in IEEE 488.2. See Agilents programming manuals for their
 * 2000/3000 series scopes for a nice description.
 *
 * Each data block from the scope has a header, e.g. "#900000001400".
 * The '#' marks the start of a block.
 * Next is one ASCII decimal digit between 1 and 9, this gives the number of
 * ASCII decimal digits following.
 * Last are the ASCII decimal digits giving the number of bytes (not
 * samples!) in the block.
 *
 * After this header as many data bytes as indicated follow.
 *
 * Each data block has a trailing linefeed too.
 */

static int get_cfg(const struct sr_dev_inst *sdi, char *cmd, char *reply, size_t maxlen);
static int get_cfg_int(const struct sr_dev_inst *sdi, char *cmd, int *i);

static int parse_int(const char *str, int *ret)
{
	char *e;
	long tmp;

	errno = 0;
	tmp = strtol(str, &e, 10);
	if (e == str || *e != '\0') {
		sr_dbg("Failed to parse integer: '%s'", str);
		return SR_ERR;
	}
	if (errno) {
		sr_dbg("Failed to parse integer: '%s', numerical overflow", str);
		return SR_ERR;
	}
	if (tmp > INT_MAX || tmp < INT_MIN) {
		sr_dbg("Failed to parse integer: '%s', value to large/small", str);
		return SR_ERR;
	}

	*ret = (int)tmp;
	return SR_OK;
}

/* Set the next event to wait for in rigol_ds_receive */
static void rigol_ds_set_wait_event(struct dev_context *devc, enum wait_events event)
{
	if (event == WAIT_STOP)
		devc->wait_status = 2;
	else
		devc->wait_status = 1;
	devc->wait_event = event;
}

/*
 * Waiting for a event will return a timeout after 2 to 3 seconds in order
 * to not block the application.
 */
static int rigol_ds_event_wait(const struct sr_dev_inst *sdi, char status1, char status2)
{
	char buf[20];
	struct dev_context *devc;
	time_t start;

	if (!(devc = sdi->priv))
		return SR_ERR;

	start = time(NULL);

	/*
	 * Trigger status may return:
	 * "TD" or "T'D" - triggered
	 * "AUTO"        - autotriggered
	 * "RUN"         - running
	 * "WAIT"        - waiting for trigger
	 * "STOP"        - stopped
	 */

	if (devc->wait_status == 1) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (get_cfg(sdi, ":TRIG:STAT?", buf, sizeof(buf)) != SR_OK)
				return SR_ERR;
		} while (buf[0] == status1 || buf[0] == status2);

		devc->wait_status = 2;
	}
	if (devc->wait_status == 2) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (get_cfg(sdi, ":TRIG:STAT?", buf, sizeof(buf)) != SR_OK)
				return SR_ERR;
		} while (buf[0] != status1 && buf[0] != status2);

		rigol_ds_set_wait_event(devc, WAIT_NONE);
	}

	return SR_OK;
}

/*
 * For live capture we need to wait for a new trigger event to ensure that
 * sample data is not returned twice.
 *
 * Unfortunately this will never really work because for sufficiently fast
 * timebases and trigger rates it just can't catch the status changes.
 *
 * What would be needed is a trigger event register with autoreset like the
 * Agilents have. The Rigols don't seem to have anything like this.
 *
 * The workaround is to only wait for the trigger when the timebase is slow
 * enough. Of course this means that for faster timebases sample data can be
 * returned multiple times, this effect is mitigated somewhat by sleeping
 * for about one sweep time in that case.
 */
static int rigol_ds_trigger_wait(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	long s;

	if (!(devc = sdi->priv))
		return SR_ERR;

	/* 
	 * If timebase < 50 msecs/DIV just sleep about one sweep time except
	 * for really fast sweeps.
	 */
	if (devc->timebase < 0.0499)
	{
		if (devc->timebase > 0.99e-6) {
			/*
			 * Timebase * num hor. divs * 85(%) * 1e6(usecs) / 100
			 * -> 85 percent of sweep time
			 */
			s = (devc->timebase * devc->model->num_horizontal_divs
			     * 85e6) / 100L;
			sr_spew("Sleeping for %ld usecs instead of trigger-wait", s);
			g_usleep(s);
		}
		rigol_ds_set_wait_event(devc, WAIT_NONE);
		return SR_OK;
	} else {
		return rigol_ds_event_wait(sdi, 'T', 'A');
	}
}

/* Wait for scope to got to "Stop" in single shot mode */
static int rigol_ds_stop_wait(const struct sr_dev_inst *sdi)
{
	return rigol_ds_event_wait(sdi, 'S', 'S');
}

/* Check that a single shot acquisition actually succeeded on the DS2000 */
static int rigol_ds_check_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int tmp;

	if (!(devc = sdi->priv))
		return SR_ERR;

	if (sr_scpi_send(sdi->conn, ":WAV:SOUR CHAN%d",
			  devc->channel->index + 1) != SR_OK)
		return SR_ERR;
	/* Check that the number of samples will be accepted */
	if (sr_scpi_send(sdi->conn, ":WAV:POIN %d;*OPC", devc->analog_frame_size) != SR_OK)
		return SR_ERR;
	if (get_cfg_int(sdi, "*ESR?", &tmp) != SR_OK)
		return SR_ERR;
	/*
	 * If we get an "Execution error" the scope went from "Single" to
	 * "Stop" without actually triggering. There is no waveform
	 * displayed and trying to download one will fail - the scope thinks
	 * it has 1400 samples (like display memory) and the driver thinks
	 * it has a different number of samples.
	 *
	 * In that case just try to capture something again. Might still
	 * fail in interesting ways.
	 *
	 * Ain't firmware fun?
	 */
	if (tmp & 0x10) {
		sr_warn("Single shot acquisition failed, retrying...");
		/* Sleep a bit, otherwise the single shot will often fail */
		g_usleep(500000);
		sr_scpi_send(sdi->conn, ":SING");
		rigol_ds_set_wait_event(devc, WAIT_STOP);
		return SR_ERR;
	}

	return SR_OK;
}

/* Wait for enough data becoming available in scope output buffer */
static int rigol_ds_block_wait(const struct sr_dev_inst *sdi)
{
	char buf[30];
	struct dev_context *devc;
	time_t start;
	int len;

	if (!(devc = sdi->priv))
		return SR_ERR;

	start = time(NULL);

	do {
		if (time(NULL) - start >= 3) {
			sr_dbg("Timeout waiting for data block");
			return SR_ERR_TIMEOUT;
		}

		/*
		 * The scope copies data really slowly from sample
		 * memory to its output buffer, so try not to bother
		 * it too much with SCPI requests but don't wait too
		 * long for short sample frame sizes.
		 */
		g_usleep(devc->analog_frame_size < 15000 ? 100000 : 1000000);

		/* "READ,nnnn" (still working) or "IDLE,nnnn" (finished) */
		if (get_cfg(sdi, ":WAV:STAT?", buf, sizeof(buf)) != SR_OK)
			return SR_ERR;

		if (parse_int(buf + 5, &len) != SR_OK)
			return SR_ERR;
	} while (buf[0] == 'R' && len < 1000000);

	rigol_ds_set_wait_event(devc, WAIT_NONE);

	return SR_OK;
}

/* Start capturing a new frameset */
SR_PRIV int rigol_ds_capture_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;

	sr_dbg("Starting data capture for frameset %lu of %lu",
	       devc->num_frames + 1, devc->limit_frames);

	if (sr_scpi_send(sdi->conn, ":WAV:FORM BYTE") != SR_OK)
		return SR_ERR;
	if (devc->data_source == DATA_SOURCE_LIVE) {
		if (sr_scpi_send(sdi->conn, ":WAV:MODE NORM") != SR_OK)
			return SR_ERR;
		rigol_ds_set_wait_event(devc, WAIT_TRIGGER);
	} else {
		if (sr_scpi_send(sdi->conn, ":WAV:MODE RAW") != SR_OK)
			return SR_ERR;
		if (sr_scpi_send(sdi->conn, ":SING", devc->analog_frame_size) != SR_OK)
			return SR_ERR;		
		rigol_ds_set_wait_event(devc, WAIT_STOP);
	}

	return SR_OK;
}

/* Start reading data from the current channel */
SR_PRIV int rigol_ds_channel_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;

	sr_dbg("Starting reading data from channel %d",
	       devc->channel->index + 1);

	if (devc->model->protocol == PROTOCOL_LEGACY) {
		if (devc->channel->type == SR_DF_LOGIC) {
			if (sr_scpi_send(sdi->conn, ":WAV:DATA? DIG") != SR_OK)
				return SR_ERR;
		} else {
			if (sr_scpi_send(sdi->conn, ":WAV:DATA? CHAN%c",
					devc->channel->name[2]) != SR_OK)
				return SR_ERR;
		}
	} else {
		if (sr_scpi_send(sdi->conn, ":WAV:SOUR CHAN%d",
				  devc->channel->index + 1) != SR_OK)
			return SR_ERR;
		if (devc->data_source != DATA_SOURCE_LIVE) {
			if (sr_scpi_send(sdi->conn, ":WAV:RES") != SR_OK)
				return SR_ERR;
			if (sr_scpi_send(sdi->conn, ":WAV:BEG") != SR_OK)
				return SR_ERR;
			rigol_ds_set_wait_event(devc, WAIT_BLOCK);
		} else
			rigol_ds_set_wait_event(devc, WAIT_NONE);
	}

	devc->num_frame_samples = 0;
	devc->num_block_bytes = 0;

	return SR_OK;
}

/* Read the header of a data block */
static int rigol_ds_read_header(struct sr_scpi_dev_inst *scpi)
{
	char start[3], length[10];
	int len, tmp;

	/* Read the hashsign and length digit. */
	tmp = sr_scpi_read(scpi, start, 2);
	start[2] = '\0';
	if (tmp != 2)
	{
		sr_err("Failed to read first two bytes of data block header.");
		return -1;
	}
	if (start[0] != '#' || !isdigit(start[1]) || start[1] == '0')
	{
		sr_err("Received invalid data block header start '%s'.", start);
		return -1;
	}
	len = atoi(start + 1);

	/* Read the data length. */
	tmp = sr_scpi_read(scpi, length, len);
	length[len] = '\0';
	if (tmp != len)
	{
		sr_err("Failed to read %d bytes of data block length.", len);
		return -1;
	}
	if (parse_int(length, &len) != SR_OK)
	{
		sr_err("Received invalid data block length '%s'.", length);
		return -1;
	}

	sr_dbg("Received data block header: %s%s -> block length %d", start, length, len);

	return len;
}

SR_PRIV int rigol_ds_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_datafeed_logic logic;
	double vdiv, offset;
	int len, i, vref;
	struct sr_probe *probe;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	scpi = sdi->conn;

	if (revents == G_IO_IN) {
		if (devc->model->protocol == PROTOCOL_IEEE488_2) {
			switch(devc->wait_event) {
			case WAIT_NONE:
				break;

			case WAIT_TRIGGER:
				if (rigol_ds_trigger_wait(sdi) != SR_OK)
					return TRUE;
				if (rigol_ds_channel_start(sdi) != SR_OK)
					return TRUE;
				break;

			case WAIT_BLOCK:
				if (rigol_ds_block_wait(sdi) != SR_OK)
					return TRUE;
				break;

			case WAIT_STOP:
				if (rigol_ds_stop_wait(sdi) != SR_OK)
					return TRUE;
				if (rigol_ds_check_stop(sdi) != SR_OK)
					return TRUE;
				if (rigol_ds_channel_start(sdi) != SR_OK)
					return TRUE;
				return TRUE;

			default:
				sr_err("BUG: Unknown event target encountered");
			}
		}

		probe = devc->channel;
		
		if (devc->num_block_bytes == 0) {
			if (devc->model->protocol == PROTOCOL_IEEE488_2) {
				sr_dbg("New block header expected");
				if (sr_scpi_send(sdi->conn, ":WAV:DATA?") != SR_OK)
					return TRUE;
				len = rigol_ds_read_header(scpi);
				if (len == -1)
					return TRUE;
				/* At slow timebases in live capture the DS2072
				 * sometimes returns "short" data blocks, with
				 * apparently no way to get the rest of the data.
				 * Discard these, the complete data block will
				 * appear eventually.
				 */
				if (devc->data_source == DATA_SOURCE_LIVE
						&& (unsigned)len < devc->num_frame_samples) {
					sr_dbg("Discarding short data block");
					sr_scpi_read(scpi, (char *)devc->buffer, len + 1);
					return TRUE;
				}
				devc->num_block_bytes = len;
			} else {
				devc->num_block_bytes = probe->type == SR_PROBE_ANALOG ?
					(devc->model->series == RIGOL_VS5000 ?
						VS5000_ANALOG_LIVE_WAVEFORM_SIZE :
						DS1000_ANALOG_LIVE_WAVEFORM_SIZE) :
					DIGITAL_WAVEFORM_SIZE;
			}
			devc->num_block_read = 0;
		}

		len = devc->num_block_bytes - devc->num_block_read;
		len = sr_scpi_read(scpi, (char *)devc->buffer,
				len < ACQ_BUFFER_SIZE ? len : ACQ_BUFFER_SIZE);

		sr_dbg("Received %d bytes.", len);
		if (len == -1)
			return TRUE;

		if (devc->num_frame_samples == 0) {
			/* Start of a new frame. */
			packet.type = SR_DF_FRAME_BEGIN;
			sr_session_send(sdi, &packet);
		}

		if (probe->type == SR_PROBE_ANALOG) {
			if (devc->model->protocol == PROTOCOL_IEEE488_2)
				devc->num_block_read += len;
			vref = devc->vert_reference[probe->index];
			vdiv = devc->vdiv[probe->index] / 25.6;
			offset = devc->vert_offset[probe->index];
			if (devc->model->protocol == PROTOCOL_IEEE488_2)
				for (i = 0; i < len; i++)
					devc->data[i] = ((int)devc->buffer[i] - vref) * vdiv - offset;
			else
				for (i = 0; i < len; i++)
					devc->data[i] = (128 - devc->buffer[i]) * vdiv - offset;
			analog.probes = g_slist_append(NULL, probe);
			analog.num_samples = len;
			analog.data = devc->data;
			analog.mq = SR_MQ_VOLTAGE;
			analog.unit = SR_UNIT_VOLT;
			analog.mqflags = 0;
			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			sr_session_send(cb_data, &packet);
			g_slist_free(analog.probes);

			if (devc->model->protocol == PROTOCOL_IEEE488_2) {
				if (devc->num_block_read == devc->num_block_bytes) {
					sr_dbg("Block has been completed");
					/* Discard the terminating linefeed and prepare for
					   possible next block */
					sr_scpi_read(scpi, (char *)devc->buffer, 1);
					devc->num_block_bytes = 0;
					if (devc->data_source != DATA_SOURCE_LIVE)
						rigol_ds_set_wait_event(devc, WAIT_BLOCK);
				} else
					sr_dbg("%d of %d block bytes read", devc->num_block_read, devc->num_block_bytes);
			}

			devc->num_frame_samples += len;

			if (devc->num_frame_samples < devc->analog_frame_size)
				/* Don't have the whole frame yet. */
				return TRUE;

			sr_dbg("Frame completed, %d samples", devc->num_frame_samples);
		} else {
			logic.length = len - 10;
			logic.unitsize = 2;
			logic.data = devc->buffer + 10;
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			sr_session_send(cb_data, &packet);

			if (len != DIGITAL_WAVEFORM_SIZE)
				/* Don't have the whole frame yet. */
				return TRUE;
		}

		/* End of the frame. */
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);
		if (devc->model->protocol == PROTOCOL_IEEE488_2) {
			/* Signal end of data download to scope */
			if (devc->data_source != DATA_SOURCE_LIVE)
				/*
				 * This causes a query error, without it switching
				 * to the next channel causes an error. Fun with
				 * firmware...
				 */
				sr_scpi_send(sdi->conn, ":WAV:END");
		}

		if (devc->enabled_analog_probes
				&& devc->channel == devc->enabled_analog_probes->data
				&& devc->enabled_analog_probes->next != NULL) {
			/* We got the frame for the first analog channel, but
			 * there's a second analog channel. */
			devc->channel = devc->enabled_analog_probes->next->data;
			rigol_ds_channel_start(sdi);
		} else {
			/* Done with both analog channels in this frame. */
			if (devc->enabled_digital_probes
					&& devc->channel != devc->enabled_digital_probes->data) {
				/* Now we need to get the digital data. */
				devc->channel = devc->enabled_digital_probes->data;
				rigol_ds_channel_start(sdi);
			} else if (++devc->num_frames == devc->limit_frames) {
				/* End of last frame. */
				packet.type = SR_DF_END;
				sr_session_send(sdi, &packet);
				sdi->driver->dev_acquisition_stop(sdi, cb_data);
			} else {
				/* Get the next frame, starting with the first analog channel. */
				if (devc->enabled_analog_probes)
					devc->channel = devc->enabled_analog_probes->data;
				else
					devc->channel = devc->enabled_digital_probes->data;

				if (devc->model->protocol == PROTOCOL_LEGACY)
					rigol_ds_channel_start(sdi);
				else
					rigol_ds_capture_start(sdi);
			}
		}
	}

	return TRUE;
}

static int get_cfg(const struct sr_dev_inst *sdi, char *cmd, char *reply, size_t maxlen)
{
	int len;
	struct dev_context *devc = sdi->priv;
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	char *response;

	if (sr_scpi_send(scpi, cmd) != SR_OK)
		return SR_ERR;

	if (sr_scpi_receive(scpi, &response) != SR_OK)
		return SR_ERR;

	g_strlcpy(reply, response, maxlen);
	g_free(response);
	len = strlen(reply);

	if (devc->model->protocol == PROTOCOL_IEEE488_2) {
		/* get rid of trailing linefeed */
		if (len >= 1 && reply[len-1] == '\n')
			reply[len-1] = '\0';
	}

	sr_spew("Received '%s'.", reply);

	return SR_OK;
}

static int get_cfg_int(const struct sr_dev_inst *sdi, char *cmd, int *i)
{
	char buf[32];

	if (get_cfg(sdi, cmd, buf, sizeof(buf)) != SR_OK)
		return SR_ERR;

	if (parse_int(buf, i) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int get_cfg_float(const struct sr_dev_inst *sdi, char *cmd, float *f)
{
	char buf[32], *e;

	if (get_cfg(sdi, cmd, buf, sizeof(buf)) != SR_OK)
		return SR_ERR;
	*f = strtof(buf, &e);
	if (e == buf || (fpclassify(*f) & (FP_ZERO | FP_NORMAL)) == 0) {
		sr_dbg("failed to parse response to '%s': '%s'", cmd, buf);
		return SR_ERR;
	}

	return SR_OK;
}

static int get_cfg_string(const struct sr_dev_inst *sdi, char *cmd, char **buf)
{
	if (!(*buf = g_try_malloc0(256)))
		return SR_ERR_MALLOC;

	if (get_cfg(sdi, cmd, *buf, 256) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int rigol_ds_get_dev_cfg(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *t_s, *cmd;
	int i, res;

	devc = sdi->priv;

	/* Analog channel state. */
	if (get_cfg_string(sdi, ":CHAN1:DISP?", &t_s) != SR_OK)
		return SR_ERR;
	devc->analog_channels[0] = !strcmp(t_s, "ON") || !strcmp(t_s, "1");
	g_free(t_s);
	if (get_cfg_string(sdi, ":CHAN2:DISP?", &t_s) != SR_OK)
		return SR_ERR;
	devc->analog_channels[1] = !strcmp(t_s, "ON") || !strcmp(t_s, "1");
	g_free(t_s);
	sr_dbg("Current analog channel state CH1 %s CH2 %s",
			devc->analog_channels[0] ? "on" : "off",
			devc->analog_channels[1] ? "on" : "off");

	/* Digital channel state. */
	if (devc->model->has_digital) {
		sr_dbg("Current digital channel state:");
		for (i = 0; i < 16; i++) {
			cmd = g_strdup_printf(":DIG%d:TURN?", i);
			res = get_cfg_string(sdi, cmd, &t_s);
			g_free(cmd);
			if (res != SR_OK)
				return SR_ERR;
			devc->digital_channels[i] = !strcmp(t_s, "ON") ? TRUE : FALSE;
			g_free(t_s);
			sr_dbg("D%d: %s", i, devc->digital_channels[i] ? "on" : "off");
		}
	}

	/* Timebase. */
	if (get_cfg_float(sdi, ":TIM:SCAL?", &devc->timebase) != SR_OK)
		return SR_ERR;
	sr_dbg("Current timebase %g", devc->timebase);

	/* Vertical gain. */
	if (get_cfg_float(sdi, ":CHAN1:SCAL?", &devc->vdiv[0]) != SR_OK)
		return SR_ERR;
	if (get_cfg_float(sdi, ":CHAN2:SCAL?", &devc->vdiv[1]) != SR_OK)
		return SR_ERR;
	sr_dbg("Current vertical gain CH1 %g CH2 %g", devc->vdiv[0], devc->vdiv[1]);

	if (devc->model->protocol == PROTOCOL_IEEE488_2) {
		/* Vertical reference - not certain if this is the place to read it. */
		if (sr_scpi_send(sdi->conn, ":WAV:SOUR CHAN1") != SR_OK)
			return SR_ERR;
		if (get_cfg_int(sdi, ":WAV:YREF?", &devc->vert_reference[0]) != SR_OK)
			return SR_ERR;
		if (sr_scpi_send(sdi->conn, ":WAV:SOUR CHAN2") != SR_OK)
			return SR_ERR;
		if (get_cfg_int(sdi, ":WAV:YREF?", &devc->vert_reference[1]) != SR_OK)
			return SR_ERR;
		sr_dbg("Current vertical reference CH1 %d CH2 %d",
				devc->vert_reference[0], devc->vert_reference[1]);
	}

	/* Vertical offset. */
	if (get_cfg_float(sdi, ":CHAN1:OFFS?", &devc->vert_offset[0]) != SR_OK)
		return SR_ERR;
	if (get_cfg_float(sdi, ":CHAN2:OFFS?", &devc->vert_offset[1]) != SR_OK)
		return SR_ERR;
	sr_dbg("Current vertical offset CH1 %g CH2 %g", devc->vert_offset[0],
			devc->vert_offset[1]);

	/* Coupling. */
	if (get_cfg_string(sdi, ":CHAN1:COUP?", &devc->coupling[0]) != SR_OK)
		return SR_ERR;
	if (get_cfg_string(sdi, ":CHAN2:COUP?", &devc->coupling[1]) != SR_OK)
		return SR_ERR;
	sr_dbg("Current coupling CH1 %s CH2 %s", devc->coupling[0],
			devc->coupling[1]);

	/* Trigger source. */
	if (get_cfg_string(sdi, ":TRIG:EDGE:SOUR?", &devc->trigger_source) != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger source %s", devc->trigger_source);

	/* Horizontal trigger position. */
	if (get_cfg_float(sdi, ":TIM:OFFS?", &devc->horiz_triggerpos) != SR_OK)
		return SR_ERR;
	sr_dbg("Current horizontal trigger position %g", devc->horiz_triggerpos);

	/* Trigger slope. */
	if (get_cfg_string(sdi, ":TRIG:EDGE:SLOP?", &devc->trigger_slope) != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger slope %s", devc->trigger_slope);

	return SR_OK;
}
