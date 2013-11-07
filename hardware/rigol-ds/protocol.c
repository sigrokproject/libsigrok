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

/*
 * Waiting for a trigger event will return a timeout after 2, 3 seconds in
 * order to not block the application.
 */

static int rigol_ds2xx2_trigger_wait(const struct sr_dev_inst *sdi)
{
	char buf[20];
	struct dev_context *devc;
	time_t start;

	if (!(devc = sdi->priv))
		return SR_ERR;

	start = time(NULL);

	/*
	 * Trigger status may return:
	 * "TD"   - triggered
	 * "AUTO" - autotriggered
	 * "RUN"  - running
	 * "WAIT" - waiting for trigger
	 * "STOP" - stopped
	 */

	if (devc->trigger_wait_status == 1) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (get_cfg(sdi, ":TRIG:STAT?", buf, sizeof(buf)) != SR_OK)
				return SR_ERR;
		} while (buf[0] == 'T' || buf[0] == 'A');

		devc->trigger_wait_status = 2;
	}
	if (devc->trigger_wait_status == 2) {
		do {
			if (time(NULL) - start >= 3) {
				sr_dbg("Timeout waiting for trigger");
				return SR_ERR_TIMEOUT;
			}

			if (get_cfg(sdi, ":TRIG:STAT?", buf, sizeof(buf)) != SR_OK)
				return SR_ERR;
		} while (buf[0] != 'T' && buf[0] != 'A');

		devc->trigger_wait_status = 0;
	}

	return SR_OK;
}

/*
 * This needs to wait for a new trigger event to ensure that sample data is
 * not returned twice.
 *
 * Unfortunately this will never really work because for sufficiently fast
 * timebases it just can't catch the status changes.
 *
 * What would be needed is a trigger event register with autoreset like the
 * Agilents have. The Rigols don't seem to have anything like this.
 *
 * The workaround is to only wait for the trigger when the timebase is slow
 * enough. Of course this means that for faster timebases sample data can be
 * returned multiple times.
 */

SR_PRIV int rigol_ds2xx2_acquisition_start(const struct sr_dev_inst *sdi,
					   gboolean wait_for_trigger)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;

	sr_dbg("Starting acquisition on channel %d",
		   devc->channel_frame->index + 1);

	if (rigol_ds_send(sdi, ":WAV:FORM BYTE") != SR_OK)
		return SR_ERR;
	if (rigol_ds_send(sdi, ":WAV:SOUR CHAN%d",
				  devc->channel_frame->index + 1) != SR_OK)
		return SR_ERR;
	if (rigol_ds_send(sdi, ":WAV:MODE NORM") != SR_OK)
		return SR_ERR;

	devc->num_frame_bytes = 0;
	devc->num_block_bytes = 0;

	/* only wait for trigger if timbase 50 msecs/DIV or slower */
	if (wait_for_trigger && devc->timebase > 0.0499)
	{
		devc->trigger_wait_status = 1;
	} else {
		devc->trigger_wait_status = 0;
	}

	return SR_OK;
}

static int rigol_ds2xx2_read_header(struct sr_serial_dev_inst *serial)
{
	char start[3], length[10];
	int len, tmp;

	/* Read the hashsign and length digit. */
	tmp = serial_read(serial, start, 2);
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
	tmp = serial_read(serial, length, len);
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
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_datafeed_logic logic;
	unsigned char buf[DS2000_ANALOG_WAVEFORM_SIZE];
	double vdiv, offset;
	float data[DS2000_ANALOG_WAVEFORM_SIZE];
	int len, i, waveform_size, vref;
	struct sr_probe *probe;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) {
		if (devc->trigger_wait_status > 0) {
			if (rigol_ds2xx2_trigger_wait(sdi) != SR_OK)
				return TRUE;
		}

		if (devc->model->series == 2 && devc->num_block_bytes == 0) {
			sr_dbg("New block header expected");
			if (rigol_ds_send(sdi, ":WAV:DATA?") != SR_OK)
				return TRUE;
			len = rigol_ds2xx2_read_header(serial);
			if (len == -1)
				return TRUE;
			/* At slow timebases the scope sometimes returns
			 * "short" data blocks, with apparently no way to
			 * get the rest of the data. Discard these, the
			 * complete data block will appear eventually.
			 */
			if (len < DS2000_ANALOG_WAVEFORM_SIZE) {
				sr_dbg("Discarding short data block");
				serial_read(serial, buf, len + 1);
				return TRUE;
			}
			devc->num_block_bytes = len;
			devc->num_block_read = 0;
		}

		probe = devc->channel_frame;
		if (devc->model->series == 2) {
			len = devc->num_block_bytes - devc->num_block_read;
			len = serial_read(serial, buf,
					len < DS2000_ANALOG_WAVEFORM_SIZE ? len : DS2000_ANALOG_WAVEFORM_SIZE);
		} else {
			waveform_size = probe->type == SR_PROBE_ANALOG ?
					DS1000_ANALOG_WAVEFORM_SIZE : DIGITAL_WAVEFORM_SIZE;
			len = serial_read(serial, buf, waveform_size - devc->num_frame_bytes);
		}
		sr_dbg("Received %d bytes.", len);
		if (len == -1)
			return TRUE;

		if (devc->num_frame_bytes == 0) {
			/* Start of a new frame. */
			packet.type = SR_DF_FRAME_BEGIN;
			sr_session_send(sdi, &packet);
		}

		if (probe->type == SR_PROBE_ANALOG) {
			if (devc->model->series == 2)
				devc->num_block_read += len;
			vref = devc->vert_reference[probe->index];
			vdiv = devc->vdiv[probe->index] / 25.6;
			offset = devc->vert_offset[probe->index];
			if (devc->model->series == 2)
				for (i = 0; i < len; i++)
					data[i] = ((int)buf[i] - vref) * vdiv - offset;
			else
				for (i = 0; i < len; i++)
					data[i] = (128 - buf[i]) * vdiv - offset;
			analog.probes = g_slist_append(NULL, probe);
			analog.num_samples = len;
			analog.data = data;
			analog.mq = SR_MQ_VOLTAGE;
			analog.unit = SR_UNIT_VOLT;
			analog.mqflags = 0;
			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			sr_session_send(cb_data, &packet);
			g_slist_free(analog.probes);

			if (devc->model->series == 2) {
				devc->num_frame_bytes += len;

				if (devc->num_frame_bytes < DS2000_ANALOG_WAVEFORM_SIZE)
					/* Don't have the whole frame yet. */
					return TRUE;

				sr_dbg("Frame completed, %d samples", devc->num_frame_bytes);
			} else {
				if (len != DS1000_ANALOG_WAVEFORM_SIZE)
					/* Don't have the whole frame yet. */
					return TRUE;
			}
		} else {
			logic.length = len - 10;
			logic.unitsize = 2;
			logic.data = buf + 10;
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
		if (devc->model->series == 1)
			devc->num_frame_bytes = 0;

		if (devc->enabled_analog_probes
				&& devc->channel_frame == devc->enabled_analog_probes->data
				&& devc->enabled_analog_probes->next != NULL) {
			/* We got the frame for the first analog channel, but
			 * there's a second analog channel. */
			devc->channel_frame = devc->enabled_analog_probes->next->data;
			if (devc->model->series == 2) {
				/* Do not wait for trigger to try and keep channel data related. */
				rigol_ds2xx2_acquisition_start(sdi, FALSE);
			} else {
				rigol_ds_send(sdi, ":WAV:DATA? CHAN%c",
						devc->channel_frame->name[2]);
			}
		} else {
			/* Done with both analog channels in this frame. */
			if (devc->enabled_digital_probes
					&& devc->channel_frame != devc->enabled_digital_probes->data) {
				/* Now we need to get the digital data. */
				devc->channel_frame = devc->enabled_digital_probes->data;
				rigol_ds_send(sdi, ":WAV:DATA? DIG");
			} else if (++devc->num_frames == devc->limit_frames) {
				/* End of last frame. */
				packet.type = SR_DF_END;
				sr_session_send(sdi, &packet);
				sdi->driver->dev_acquisition_stop(sdi, cb_data);
			} else {
				/* Get the next frame, starting with the first analog channel. */
				if (devc->model->series == 2) {
					if (devc->enabled_analog_probes) {
						devc->channel_frame = devc->enabled_analog_probes->data;
						/* Must wait for trigger because at
						 * slow timebases the scope will
						 * return old data otherwise. */
						rigol_ds2xx2_acquisition_start(sdi, TRUE);
					}
				} else {
					if (devc->enabled_analog_probes) {
						devc->channel_frame = devc->enabled_analog_probes->data;
						rigol_ds_send(sdi, ":WAV:DATA? CHAN%c",
								devc->channel_frame->name[2]);
					} else {
						devc->channel_frame = devc->enabled_digital_probes->data;
						rigol_ds_send(sdi, ":WAV:DATA? DIG");
					}
				}
			}
		}
	}

	return TRUE;
}

SR_PRIV int rigol_ds_send(const struct sr_dev_inst *sdi, const char *format, ...)
{
	va_list args;
	char buf[256];
	int len, out, ret;

	va_start(args, format);
	len = vsnprintf(buf, 255, format, args);
	va_end(args);
	strcat(buf, "\n");
	len++;
	out = serial_write(sdi->conn, buf, len);
	buf[len - 1] = '\0';
	if (out != len) {
		sr_dbg("Only sent %d/%d bytes of '%s'.", out, len, buf);
		ret = SR_ERR;
	} else {
		sr_spew("Sent '%s'.", buf);
		ret = SR_OK;
	}

	return ret;
}

static int get_cfg(const struct sr_dev_inst *sdi, char *cmd, char *reply, size_t maxlen)
{
	int len;
	struct dev_context *devc = sdi->priv;

	if (rigol_ds_send(sdi, cmd) != SR_OK)
		return SR_ERR;

	if ((len = serial_read(sdi->conn, reply, maxlen - 1)) < 0)
		return SR_ERR;
	reply[len] = '\0';

	if (devc->model->series == 2) {
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

	if (devc->model->series == 2) {
		/* Vertical reference - not certain if this is the place to read it. */
		if (rigol_ds_send(sdi, ":WAV:SOUR CHAN1") != SR_OK)
			return SR_ERR;
		if (get_cfg_int(sdi, ":WAV:YREF?", &devc->vert_reference[0]) != SR_OK)
			return SR_ERR;
		if (rigol_ds_send(sdi, ":WAV:SOUR CHAN2") != SR_OK)
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
