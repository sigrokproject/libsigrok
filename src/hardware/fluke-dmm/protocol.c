/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static void handle_line(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int n;
	char cmd[16], **tokens;
	int ret;

	devc = sdi->priv;
	serial = sdi->conn;
	sr_spew("Received line '%s' (%d).", devc->buf, devc->buflen);

	if (devc->buflen == 1) {
		if (devc->buf[0] != '0') {
			/* Not just a CMD_ACK from the query command. */
			sr_dbg("Got CMD_ACK '%c'.", devc->buf[0]);
			devc->expect_response = FALSE;
		}
		devc->buflen = 0;
		return;
	}

	tokens = g_strsplit(devc->buf, ",", 0);
	if (tokens[0]) {
		switch (devc->profile->model) {
		case FLUKE_87:
		case FLUKE_89:
		case FLUKE_187:
		case FLUKE_189:
			devc->expect_response = FALSE;
			fluke_handle_qm_18x(sdi, tokens);
			break;
		case FLUKE_190:
			devc->expect_response = FALSE;
			fluke_handle_qm_190(sdi, tokens);
			if (devc->meas_type) {
				/*
				 * Slip the request in now, before the main
				 * timer loop asks for metadata again.
				 */
				n = sprintf(cmd, "QM %d\r", devc->meas_type);
				ret = serial_write_blocking(serial,
					cmd, n, SERIAL_WRITE_TIMEOUT_MS);
				if (ret < 0)
					sr_err("Cannot send QM (measurement).");
			}
			break;
		case FLUKE_287:
		case FLUKE_289:
			devc->expect_response = FALSE;
			fluke_handle_qm_28x(sdi, tokens);
			break;
		}
	}
	g_strfreev(tokens);
	devc->buflen = 0;
}

SR_PRIV int fluke_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;
	int64_t now, elapsed;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;
	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		while (FLUKEDMM_BUFSIZE - devc->buflen - 1 > 0) {
			len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;
			devc->buflen++;
			*(devc->buf + devc->buflen) = '\0';
			if (*(devc->buf + devc->buflen - 1) == '\r') {
				*(devc->buf + --devc->buflen) = '\0';
				handle_line(sdi);
				break;
			}
		}
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	now = g_get_monotonic_time() / 1000;
	elapsed = now - devc->cmd_sent_at;
	/* Send query command at poll_period interval, or after 1 second
	 * has elapsed. This will make it easier to recover from any
	 * out-of-sync or temporary disconnect issues. */
	if ((devc->expect_response == FALSE && elapsed > devc->profile->poll_period)
			|| elapsed > devc->profile->timeout) {
		if (serial_write_blocking(serial, "QM\r", 3, SERIAL_WRITE_TIMEOUT_MS) < 0)
			sr_err("Unable to send QM.");
		devc->cmd_sent_at = now;
		devc->expect_response = TRUE;
	}

	return TRUE;
}
