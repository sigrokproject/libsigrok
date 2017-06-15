/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Sergey Alirzaev <zl29ah@gmail.com>
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
#include <ftdi.h>
#include "protocol.h"

static void send_samples(struct sr_dev_inst *sdi, uint64_t samples_to_send)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct dev_context *devc;

	sr_spew("Sending %" PRIu64 " samples.", samples_to_send);

	devc = sdi->priv;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = samples_to_send;
	logic.unitsize = 1;
	logic.data = devc->data_buf;
	sr_session_send(sdi, &packet);

	devc->samples_sent += samples_to_send;
	devc->bytes_received -= samples_to_send;
}

SR_PRIV int ftdi_la_set_samplerate(struct dev_context *devc)
{
	int ret;

	ret = ftdi_set_baudrate(devc->ftdic,
			devc->cur_samplerate / devc->desc->samplerate_div);
	if (ret < 0) {
		sr_err("Failed to set baudrate (%d): %s.", devc->cur_samplerate,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int ftdi_la_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int bytes_read;
	uint64_t n;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;
	if (!(devc = sdi->priv))
		return TRUE;
	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;
	if (!devc->ftdic)
		return TRUE;

	/* Get a block of data. */
	bytes_read = ftdi_read_data(devc->ftdic, devc->data_buf, DATA_BUF_SIZE);
	if (bytes_read < 0) {
		sr_err("Failed to read FTDI data (%d): %s.",
		       bytes_read, ftdi_get_error_string(devc->ftdic));
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}
	if (bytes_read == 0) {
		sr_spew("Received 0 bytes, nothing to do.");
		return TRUE;
	}
	sr_spew("Got some data.");
	devc->bytes_received += bytes_read;

	n = devc->samples_sent + devc->bytes_received;

	if (devc->limit_samples && (n >= devc->limit_samples)) {
		send_samples(sdi, devc->limit_samples - devc->samples_sent);
		sr_info("Requested number of samples reached.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	} else {
		send_samples(sdi, devc->bytes_received);
	}

	return TRUE;
}
