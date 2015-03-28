/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com> (code from atten-pps3xxx)
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

/** @file
 *  <em>Motech LPS-30x series</em> power supply driver
 *  @internal
 */

#include <errno.h>
#include <string.h>
#include "protocol.h"

/** Send data packets for current measurements. */
static void send_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	int i;
	float data[MAX_CHANNELS];

	devc = sdi->priv;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.channels = sdi->channels;
	analog.num_samples = 1;

	analog.mq = SR_MQ_VOLTAGE;
	analog.unit = SR_UNIT_VOLT;
	analog.mqflags = SR_MQFLAG_DC;
	analog.data = data;
	for (i = 0; i < devc->model->num_channels; i++)
		analog.data[i] = devc->channel_status[i].output_voltage_last; /* Value always 3.3 or 5 for channel 3, if present! */
	sr_session_send(sdi, &packet);

	analog.mq = SR_MQ_CURRENT;
	analog.unit = SR_UNIT_AMPERE;
	analog.mqflags = 0;
	analog.data = data;
	for (i = 0; i < devc->model->num_channels; i++)
		analog.data[i] = devc->channel_status[i].output_current_last; /* Value always 0 for channel 3, if present! */
	sr_session_send(sdi, &packet);

	devc->num_samples++;
}

/** Process a complete line (without CR/LF) in buf. */
static void process_line(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	double dbl;
	int auxint;

	devc = sdi->priv;

	switch (devc->acq_req_pending) {
	case 0: /* Should not happen... */
		break;
	case 1: /* Waiting for data reply to request */
		/* Convert numbers */
		switch (devc->acq_req) {
		case AQ_U1:
		case AQ_U2:
		case AQ_I1:
		case AQ_I2:
			if (sr_atod(devc->buf, &dbl) != SR_OK) {
				sr_err("Failed to convert '%s' to double, errno=%d %s",
					devc->buf, errno, strerror(errno));
				dbl = 0.0;
			}
			break;
		case AQ_STATUS:
			if (sr_atoi(devc->buf, &auxint) != SR_OK) {
				sr_err("Failed to convert '%s' to int, errno=%d %s",
					devc->buf, errno, strerror(errno));
				auxint = 0;
			}
			break;
		default:
			break;
		}

		switch (devc->acq_req) {
		case AQ_U1:
			devc->channel_status[0].output_voltage_last = dbl;
			break;
		case AQ_I1:
			devc->channel_status[0].output_current_last = dbl;
			break;
		case AQ_U2:
			devc->channel_status[1].output_voltage_last = dbl;
			break;
		case AQ_I2:
			devc->channel_status[1].output_current_last = dbl;
			break;
		case AQ_STATUS: /* Process status and generate data. */
			if (lps_process_status(sdi, auxint) == SR_OK) {
				send_data(sdi);
			}
			break;
		default:
			break;
		}

		devc->acq_req_pending = 2;
		break;
	case 2: /* Waiting for OK after request */
		if (strcmp(devc->buf, "OK")) {
			sr_err("Unexpected reply while waiting for OK: '%s'", devc->buf);
		}
		devc->acq_req_pending = 0;
		break;
	}

	devc->buf[0] = '\0';
	devc->buflen = 0;
}

SR_PRIV int motech_lps_30x_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;
	gdouble elapsed_s;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) { /* Serial data arrived. */
		while (LINELEN_MAX - devc->buflen - 2 > 0) {
			len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;

			/* Eliminate whitespace at beginning of line. */
			if (g_ascii_isspace(devc->buf[0])) {
				devc->buf[0] = '\0';
				devc->buflen = 0;
				continue;
			}

			devc->buflen += len;
			devc->buf[devc->buflen] = '\0';

			/* If line complete, process msg. */
			if ((devc->buflen > 0) && ((devc->buf[devc->buflen-1] == '\r') || devc->buf[devc->buflen-1] == '\n')) {
				devc->buflen--;
				devc->buf[devc->buflen] = '\0';

				sr_spew("Line complete: \"%s\"", devc->buf);
				process_line(sdi);
			}
		}
	}

	/* If number of samples or time limit reached, stop acquisition. */
	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples))
		sdi->driver->dev_acquisition_stop(sdi, cb_data);

	if (devc->limit_msec) {
		elapsed_s = g_timer_elapsed(devc->elapsed_msec, NULL);
		if ((elapsed_s * 1000) >= devc->limit_msec)
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
	}

	/* Only request the next packet if required. */
	if (!((sdi->status == SR_ST_ACTIVE) && (devc->acq_running)))
		return TRUE;

	if (devc->acq_req_pending) {
		gint64 elapsed_us = g_get_monotonic_time() - devc->req_sent_at;
		if (elapsed_us > (REQ_TIMEOUT_MS * 1000)) {
			sr_spew("Request timeout: req=%d t=%lldus", (int)devc->acq_req, elapsed_us);
			devc->acq_req_pending = 0;
		}
	}

	if (devc->acq_req_pending == 0) {
		switch (devc->acq_req) {
		case AQ_NONE: /* Fall through */
		case AQ_STATUS:
			devc->acq_req = AQ_U1;
			lps_send_req(serial, "VOUT1");
			break;
		case AQ_U1:
			devc->acq_req = AQ_I1;
			lps_send_req(serial, "IOUT1");
			break;
		case AQ_I1:
			if (devc->model->num_channels == 1) {
				devc->acq_req = AQ_STATUS;
				lps_send_req(serial, "STATUS");
			} else {
				devc->acq_req = AQ_U2;
				lps_send_req(serial, "VOUT2");
			}
			break;
		case AQ_U2:
			devc->acq_req = AQ_I2;
			lps_send_req(serial, "IOUT2");
			break;
		case AQ_I2:
			devc->acq_req = AQ_STATUS;
			lps_send_req(serial, "STATUS");
			break;
		default:
			sr_err("Illegal devc->acq_req=%d", devc->acq_req);
			return SR_ERR;
		}
		devc->req_sent_at = g_get_real_time();
		devc->acq_req_pending = 1;
	}

	return TRUE;
}
