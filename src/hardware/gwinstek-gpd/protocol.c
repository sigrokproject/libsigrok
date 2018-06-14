/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Bastian Schmitz <bastian.schmitz@udo.edu>
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
#include <string.h>
#include "protocol.h"

SR_PRIV int gpd_send_cmd(struct sr_serial_dev_inst *serial,
					const char *cmd, ...)
{
	int ret;
	char cmdbuf[50];
	char *cmd_esc;
	va_list args;

	va_start(args, cmd);
	vsnprintf(cmdbuf, sizeof(cmdbuf), cmd, args);
	va_end(args);

	cmd_esc = g_strescape(cmdbuf, NULL);
	sr_dbg("Sending '%s'.", cmd_esc);
	g_free(cmd_esc);

	ret = serial_write_blocking(serial, cmdbuf, strlen(cmdbuf),
				    serial_timeout(serial, strlen(cmdbuf)));
	if (ret < 0) {
		sr_err("Error sending command: %d.", ret);
		return ret;
	}

	return ret;
}


SR_PRIV int gpd_receive_reply(struct sr_serial_dev_inst *serial,
					     char *buf, int buflen)
{
	int l_recv = 0;
	int bufpos = 0;
	int retc;
	int l_startpos = 0;
	int lines = 1;
	gint64 start, remaining;
	const int timeout_ms = 100;

	if (!serial || (lines <= 0) || !buf || (buflen <= 0))
		return SR_ERR_ARG;

	start = g_get_monotonic_time();
	remaining = timeout_ms; // timeout ms


	while ((l_recv < lines) && (bufpos < (buflen + 1))) {
		retc = serial_read_blocking(serial, &buf[bufpos], 1, remaining);
		if (retc != 1)
			return SR_ERR;

		if (bufpos == 0 && buf[bufpos] == '\r')
			continue;
		if (bufpos == 0 && buf[bufpos] == '\n')
			continue;

		if (buf[bufpos] == '\n') {
			buf[bufpos] = '\0';
			sr_dbg("Received line '%s'.", &buf[l_startpos]);
			buf[bufpos] = '\n';
			l_startpos = bufpos + 1;
			l_recv++;
		}
		bufpos++;

		/* Reduce timeout by time elapsed. */
		remaining = timeout_ms - ((g_get_monotonic_time() - start)
					  / 1000);
		if (remaining <= 0)
			/* Timeout */
			return SR_ERR;
	}
	buf[bufpos] = '\0';

	if (l_recv == lines)
		return SR_OK;
	else
		return SR_ERR;
}


SR_PRIV int gpd_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_channel *ch;

	unsigned int i;
	char reply[50];
	char *reply_esc;

	(void)fd;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) {
		sr_dbg("%s(G_IO_IN)", __func__);
		if (!devc->reply_pending) {
			sr_err("no reply pending");
			gpd_receive_reply(serial, reply,
							 sizeof(reply));

			reply_esc = g_strescape(reply, NULL);
			sr_err("unexpected data '%s'", reply_esc);
			g_free(reply_esc);
		} else {
			for (i = 0; i < devc->model->num_channels; i++) {
				reply[0] = '\0';
				gpd_receive_reply(serial, reply,
					sizeof(reply));
				if (sscanf(reply, "%f",
					&devc->config[i].output_voltage_max)
						!= 1) {
					sr_err("invalid reply to VOUT1?: '%s'",
						reply);
					return TRUE;
				}

				sr_analog_init(&analog, &encoding, &meaning,
					&spec, 0);

				/* Send the value forward. */
				packet.type = SR_DF_ANALOG;
				packet.payload = &analog;

				analog.num_samples = 1;
				ch = g_slist_nth_data(sdi->channels, i);
				analog.meaning->channels =
					g_slist_append(NULL, ch);
				analog.meaning->mq = SR_MQ_CURRENT;
				analog.meaning->unit = SR_UNIT_AMPERE;
				analog.meaning->mqflags = 0;
				analog.encoding->digits = 3;
				analog.spec->spec_digits = 3;
				analog.data =
					&devc->config[i].output_current_max;
				sr_session_send(sdi, &packet);

				reply[0] = '\0';
				gpd_receive_reply(serial, reply,
					sizeof(reply));
				if (sscanf(reply, "%f",
					&devc->config[i].output_voltage_max)
						!= 1) {
					sr_err("invalid reply to VOUT1?: '%s'",
						reply);
					return TRUE;
				}

				sr_analog_init(&analog, &encoding, &meaning,
					&spec, 0);

				/* Send the value forward. */
				packet.type = SR_DF_ANALOG;
				packet.payload = &analog;

				analog.num_samples = 1;
				ch = g_slist_nth_data(sdi->channels, i);
				analog.meaning->channels =
					g_slist_append(NULL, ch);
				analog.meaning->mq = SR_MQ_VOLTAGE;
				analog.meaning->unit = SR_UNIT_VOLT;
				analog.meaning->mqflags = SR_MQFLAG_DC;
				analog.encoding->digits = 3;
				analog.spec->spec_digits = 3;
				analog.data =
					&devc->config[i].output_voltage_max;
				sr_session_send(sdi, &packet);
			}


			devc->reply_pending = FALSE;
		}
	} else {
		sr_dbg("%s(TIMEOUT)", __func__);
		if (!devc->reply_pending) {
			for (i = 0; i < devc->model->num_channels; i++)
				gpd_send_cmd(serial,
					"IOUT%d?\nVOUT%d?\n",
					i + 1, i + 1);

			devc->req_sent_at = g_get_monotonic_time();
			devc->reply_pending = TRUE;
		}
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	return TRUE;
}
