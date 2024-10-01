/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 bkidwell <ben.kidwell@gmail.com>
 * Copyright (C) 2024 lukenuc <lukenuculaj@gmail.com>
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

#include <glib.h>
#include <glib/gstdio.h>
#include <config.h>
#include <string.h>
#include "protocol.h"

#define USB_TIMEOUT 100

static void send_samples(
	struct sr_dev_inst* sdi, uint64_t samples_to_send, uint64_t offset)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct dev_context* devc;

	sr_spew("Sending %" PRIu64 " samples.", samples_to_send);

	devc = sdi->priv;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = samples_to_send;
	logic.unitsize = 2;
	logic.data = devc->decoded_data_buf + offset;
	sr_dbg("Sending packet");
	sr_session_send(sdi, &packet);
	sr_dbg("Done sending packet");

	devc->samples_sent += samples_to_send;
	devc->bytes_received -= (samples_to_send << 1);
}

SR_PRIV int fwili_receive_data(int fd, int revents, void* cb_data)
{
	struct sr_dev_inst* sdi;
	struct dev_context* devc;
	int raw_bytes_read;
	int decoded_bytes_read;
	uint64_t n;
	int i, j;
	uint8_t cur_byte_size;
	uint8_t cur_byte_upper;
	uint8_t cur_byte_lower;
	int trigger_offset;
	int pre_trigger_samples;

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

	/* Get a block of data */
	raw_bytes_read = ftdi_read_data(
		devc->ftdic, devc->raw_data_buf, RAW_DATA_BUF_SIZE);
	if (raw_bytes_read < 0) {
		sr_err("Failed to read FTDI data (%d): %s.", raw_bytes_read,
			ftdi_get_error_string(devc->ftdic));
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}
	if (raw_bytes_read == 0) {
		sr_spew("Received 0 bytes, nothing to do.");
		return TRUE;
	}
	sr_spew("Got some data.");

	/* Decode the incoming data for 12 channels*/
	decoded_bytes_read = 0;
	sr_dbg("----- new chunk ------");
	for (i = 0; i < raw_bytes_read; i += 2) {
		cur_byte_size = devc->raw_data_buf[i + 1] & 0b00001111;
		cur_byte_lower = (devc->raw_data_buf[i] << 4) |
			(devc->raw_data_buf[i + 1] >> 4);
		cur_byte_upper = devc->raw_data_buf[i] >> 4;
		for (j = 0; j <= cur_byte_size; j++) {
			devc->decoded_data_buf[decoded_bytes_read] =
				cur_byte_lower;
			devc->decoded_data_buf[decoded_bytes_read + 1] =
				cur_byte_upper;
			decoded_bytes_read += 2;
		}
	}

	/* Send the data */

	if (devc->trigger_fired) {
		devc->bytes_received += decoded_bytes_read;
		n = devc->samples_sent + (devc->bytes_received >> 1);
		if (devc->limit_samples && (n > devc->limit_samples)) {
			send_samples(sdi,
				devc->limit_samples - devc->samples_sent, 0);
			sr_info("Requested number of samples reached.");
			sr_dbg("devc->samples_sent = %d", devc->samples_sent);
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		} else {
			send_samples(sdi, (devc->bytes_received >> 1), 0);
		}
	} else {
		sr_dbg("Trigger not fired!");
		trigger_offset = soft_trigger_logic_check(devc->stl,
			devc->decoded_data_buf, decoded_bytes_read,
			&pre_trigger_samples);
		if (trigger_offset > -1) {
			devc->bytes_received =
				decoded_bytes_read - (trigger_offset << 1);
			if (devc->limit_samples &&
				(devc->bytes_received >> 1) >
					devc->limit_samples) {
				devc->bytes_received =
					(devc->limit_samples << 1);
				send_samples(sdi, devc->bytes_received >> 1,
					trigger_offset << 1);
				sr_info("Requested number of samples reached.");
				sr_dev_acquisition_stop(sdi);
			} else {
				send_samples(sdi, (devc->bytes_received >> 1),
					trigger_offset << 1);
			}
			devc->trigger_fired = TRUE;
		}
	}

	sr_dbg("decoded_bytes_read = %d", decoded_bytes_read);

	return TRUE;
}