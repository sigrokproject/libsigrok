/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "protocol.h"

#define REQ_TIMEOUT_MS 500

static int send_cmd(struct sr_serial_dev_inst *serial, const char *cmd)
{
	int ret;
	char *cmd_esc;

	cmd_esc = g_strescape(cmd, NULL);
	sr_dbg("Sending '%s'.", cmd_esc);

	if ((ret = serial_write_blocking(serial, cmd, strlen(cmd))) < 0) {
		sr_err("Error sending command: %d.", ret);
		g_free(cmd_esc);
		return ret;
	}

	g_free(cmd_esc);

	return ret;
}

static int parse_volt_curr_mode(struct sr_dev_inst *sdi, char **tokens)
{
	char *str;
	double val;
	struct dev_context *devc;

	devc = sdi->priv;

	/* Bytes 0-3: Voltage. */
	str = g_strndup(tokens[0], 4);
	val = g_ascii_strtod(str, NULL) / 100;
	devc->voltage = val;
	g_free(str);

	/* Bytes 4-7: Current. */
	str = g_strndup((tokens[0] + 4), 4);
	val = g_ascii_strtod(str, NULL) / 100;
	devc->current = val;
	g_free(str);

	/* Byte 8: Mode ('0' means CV, '1' means CC). */
	devc->cc_mode = (tokens[0][8] == '1');

	return SR_OK;
}

static void send_sample(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;

	devc = sdi->priv;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.channels = sdi->channels;
	analog.num_samples = 1;

	analog.mq = SR_MQ_VOLTAGE;
	analog.unit = SR_UNIT_VOLT;
	analog.mqflags = SR_MQFLAG_DC;
	analog.data = &devc->voltage;
	sr_session_send(sdi, &packet);

	analog.mq = SR_MQ_CURRENT;
	analog.unit = SR_UNIT_AMPERE;
	analog.mqflags = 0;
	analog.data = &devc->current;
	sr_session_send(sdi, &packet);

	devc->num_samples++;
}

static int parse_reply(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	char *reply_esc, **tokens;

	devc = sdi->priv;

	reply_esc = g_strescape(devc->buf, NULL);
	sr_dbg("Received '%s'.", reply_esc);

	tokens = g_strsplit(devc->buf, "\r", 0);

	if (parse_volt_curr_mode(sdi, tokens) < 0)
		return SR_ERR;
	send_sample(sdi);

	g_free(reply_esc);
	g_strfreev(tokens);

	return SR_OK;
}

static int handle_new_data(struct sr_dev_inst *sdi)
{
	int len;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	serial = sdi->conn;

	len = serial_read(serial, devc->buf + devc->buflen, 1);
	if (len < 1)
		return SR_ERR;

	devc->buflen += len;
	devc->buf[devc->buflen] = '\0';

	/* Wait until we received an "OK\r" (among other bytes). */
	if (!g_str_has_suffix(devc->buf, "OK\r"))
		return SR_OK;

	parse_reply(sdi);

	devc->buf[0] = '\0';
	devc->buflen = 0;

	devc->reply_pending = FALSE;

	return SR_OK;
}

SR_PRIV int hcs_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int64_t t, elapsed_us;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) {
		/* New data arrived. */
		handle_new_data(sdi);
	} else {
		/* Timeout. */
	}

	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples)) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	if (devc->limit_msec) {
		t = (g_get_monotonic_time() - devc->starttime) / 1000;
		if (t > (int64_t)devc->limit_msec) {
			sr_info("Requested time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			return TRUE;
		}
	}

	/* Request next packet, if required. */
	if (sdi->status == SR_ST_ACTIVE) {
		if (devc->reply_pending) {
			elapsed_us = g_get_monotonic_time() - devc->req_sent_at;
			if (elapsed_us > (REQ_TIMEOUT_MS * 1000))
				devc->reply_pending = FALSE;
			return TRUE;
		}

		/* Send command to get voltage, current, and mode (CC or CV). */
		if (send_cmd(serial, "GETD\r") < 0)
			return TRUE;

		devc->req_sent_at = g_get_monotonic_time();
		devc->reply_pending = TRUE;
	}

	return TRUE;
}
