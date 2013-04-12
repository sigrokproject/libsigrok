/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
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

#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

SR_PRIV int rigol_ds1xx2_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	unsigned char buf[WAVEFORM_SIZE];
	double vdiv, offset;
	float data[WAVEFORM_SIZE];
	int probenum, len, i;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		len = read(fd, buf, WAVEFORM_SIZE);
		sr_dbg("Received %d bytes.", len);
		if (len == -1)
			return TRUE;

		if (devc->num_frame_samples == 0) {
			/* Start of a new frame. */
			packet.type = SR_DF_FRAME_BEGIN;
			sr_session_send(sdi, &packet);
		}

		probenum = devc->channel_frame->name[2] == '1' ? 0 : 1;
		for (i = 0; i < len; i++) {
			vdiv = devc->vdiv[probenum];
			offset = devc->vert_offset[probenum];
			data[i] = vdiv / 25.6 * (128 - buf[i]) - offset;
		}
		analog.probes = g_slist_append(NULL, devc->channel_frame);
		analog.num_samples = len;
		analog.data = data;
		analog.mq = SR_MQ_VOLTAGE;
		analog.unit = SR_UNIT_VOLT;
		analog.mqflags = 0;
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(cb_data, &packet);
		g_slist_free(analog.probes);

		if (len != WAVEFORM_SIZE)
			/* Don't have the whole frame yet. */
			return TRUE;

		/* End of the frame. */
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);

		if (devc->channel_frame == devc->enabled_probes->data
				&& devc->enabled_probes->next != NULL) {
			/* We got the frame for the first channel, but
			 * there's a second channel. */
			devc->channel_frame = devc->enabled_probes->next->data;
			rigol_ds1xx2_send(devc, ":WAV:DATA? CHAN%c",
					devc->channel_frame->name[2]);
		} else {
			/* Done with both channels in this frame. */
			if (++devc->num_frames == devc->limit_frames) {
				sdi->driver->dev_acquisition_stop(sdi, cb_data);
			} else {
				/* Get the next frame, starting with the first channel. */
				devc->channel_frame = devc->enabled_probes->data;
				rigol_ds1xx2_send(devc, ":WAV:DATA? CHAN%c",
						devc->channel_frame->name[2]);
			}
		}
	}

	return TRUE;
}

SR_PRIV int rigol_ds1xx2_send(struct dev_context *devc, const char *format, ...)
{
	va_list args;
	char buf[256];
	int len, out, ret;

	va_start(args, format);
	len = vsnprintf(buf, 255, format, args);
	va_end(args);
	strcat(buf, "\n");
	len++;
	out = write(devc->fd, buf, len);
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

static int get_cfg(const struct sr_dev_inst *sdi, char *cmd, char *reply)
{
	struct dev_context *devc;
	int len;

	devc = sdi->priv;

	if (rigol_ds1xx2_send(devc, cmd) != SR_OK)
		return SR_ERR;

	if ((len = read(devc->fd, reply, 255)) < 0)
		return SR_ERR;
	reply[len] = '\0';
	sr_spew("Received '%s'.", reply);

	return SR_OK;
}

static int get_cfg_float(const struct sr_dev_inst *sdi, char *cmd, float *f)
{
	char buf[256], *e;

	if (get_cfg(sdi, cmd, buf) != SR_OK)
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

	if (get_cfg(sdi, cmd, *buf) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int rigol_ds1xx2_get_dev_cfg(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *t_s;

	devc = sdi->priv;

	/* Channel state. */
	if (get_cfg_string(sdi, ":CHAN1:DISP?", &t_s) != SR_OK)
		return SR_ERR;
	devc->channels[0] = !strcmp(t_s, "ON") ? TRUE : FALSE;
	g_free(t_s);
	if (get_cfg_string(sdi, ":CHAN2:DISP?", &t_s) != SR_OK)
		return SR_ERR;
	devc->channels[1] = !strcmp(t_s, "ON") ? TRUE : FALSE;
	g_free(t_s);
	sr_dbg("Current channel state CH1 %s CH2 %s",
			devc->channels[0] ? "on" : "off",
			devc->channels[1] ? "on" : "off");

	/* Timebase. */
	if (get_cfg_float(sdi, ":TIM:SCAL?", &devc->timebase) != SR_OK)
		return SR_ERR;
	sr_dbg("Current timebase %f", devc->timebase);

	/* Vertical gain. */
	if (get_cfg_float(sdi, ":CHAN1:SCAL?", &devc->vdiv[0]) != SR_OK)
		return SR_ERR;
	if (get_cfg_float(sdi, ":CHAN2:SCAL?", &devc->vdiv[1]) != SR_OK)
		return SR_ERR;
	sr_dbg("Current vertical gain CH1 %f CH2 %f", devc->vdiv[0], devc->vdiv[1]);

	/* Vertical offset. */
	if (get_cfg_float(sdi, ":CHAN1:OFFS?", &devc->vert_offset[0]) != SR_OK)
		return SR_ERR;
	if (get_cfg_float(sdi, ":CHAN2:OFFS?", &devc->vert_offset[1]) != SR_OK)
		return SR_ERR;
	sr_dbg("Current vertical offset CH1 %f CH2 %f", devc->vert_offset[0],
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
	sr_dbg("Current horizontal trigger position %f", devc->horiz_triggerpos);

	/* Trigger slope. */
	if (get_cfg_string(sdi, ":TRIG:EDGE:SLOP?", &devc->trigger_slope) != SR_OK)
		return SR_ERR;
	sr_dbg("Current trigger slope %s", devc->trigger_slope);

	return SR_OK;
}

