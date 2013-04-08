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
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

SR_PRIV int rigol_ds1xx2_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	unsigned char buf[WAVEFORM_SIZE];
	float data[WAVEFORM_SIZE];
	int len, i;

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

		for (i = 0; i < len; i++)
			data[i] = devc->scale / 25.6 * (128 - buf[i]) - devc->offset;
		analog.probes = devc->enabled_probes;
		analog.num_samples = len;
		analog.data = data;
		analog.mq = SR_MQ_VOLTAGE;
		analog.unit = SR_UNIT_VOLT;
		analog.mqflags = 0;
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(cb_data, &packet);

		if (len == WAVEFORM_SIZE) {
			/* End of the frame. */
			packet.type = SR_DF_FRAME_END;
			sr_session_send(sdi, &packet);

			if (++devc->num_frames == devc->limit_frames)
				sdi->driver->dev_acquisition_stop(sdi, cb_data);
			else
				rigol_ds1xx2_send_data(fd, ":WAV:DATA?");
		}
	}

	return TRUE;
}

SR_PRIV int rigol_ds1xx2_send_data(int fd, const char *format, ...)
{
	va_list args;
	char buf[256];
	int len, out, ret;

	va_start(args, format);
	len = vsprintf(buf, format, args);
	va_end(args);
	strcat(buf, "\n");
	len++;
	out = write(fd, buf, len);
	buf[len - 1] = '\0';
	if (out != len) {
		sr_dbg("Only sent %d/%d bytes of '%s'.", out, len, buf);
		ret = SR_ERR;
	} else {
		sr_dbg("Sent '%s'.", buf);
		ret = SR_OK;
	}

	return ret;
}
