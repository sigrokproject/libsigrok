/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021-2023 Frank Stettner <frank-stettner@gmx.net>
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

#include "protocol.h"

#define SERIAL_TIMEOUT_MS		1000

#define ICSTATION_USBRELAY_CMD_ID	0x50
#define ICSTATION_USBRELAY_CMD_START	0x51

static int icstation_usbrelay_send_byte(struct sr_serial_dev_inst *serial,
	uint8_t b)
{
	int ret;

	ret = serial_write_blocking(serial, &b, sizeof(b), SERIAL_TIMEOUT_MS);
	if (ret < SR_OK)
		return SR_ERR_IO;
	if ((size_t)ret != sizeof(b))
		return SR_ERR_IO;

	return SR_OK;
}

static int icstation_usbrelay_recv_byte(struct sr_serial_dev_inst *serial,
	uint8_t *b)
{
	int ret;

	ret = serial_read_blocking(serial, b, sizeof(*b), SERIAL_TIMEOUT_MS);
	if (ret < SR_OK)
		return SR_ERR_IO;
	if ((size_t)ret != sizeof(*b))
		return SR_ERR_IO;

	return SR_OK;
}

SR_PRIV int icstation_usbrelay_identify(struct sr_serial_dev_inst *serial,
	uint8_t *id)
{
	int ret;

	if (!id)
		return SR_ERR_ARG;

	/*
	 * Send the identification request. Receive the device firmware's
	 * identification response.
	 *
	 * BEWARE!
	 * A vendor firmware implementation detail prevents the host from
	 * identifying the device again once command mode was entered.
	 * The UART protocol provides no means to leave command mode.
	 * The subsequent identification request is mistaken instead as
	 * another relay control request! Identifying the device will fail.
	 * The device must be power cycled before it identifies again.
	 */
	ret = icstation_usbrelay_send_byte(serial, ICSTATION_USBRELAY_CMD_ID);
	if (ret != SR_OK) {
		sr_dbg("Could not send identification request.");
		return SR_ERR_IO;
	}
	ret = icstation_usbrelay_recv_byte(serial, id);
	if (ret != SR_OK) {
		sr_dbg("Could not receive identification response.");
		return SR_ERR_IO;
	}
	sr_dbg("Identification response 0x%02hhx.", *id);

	return SR_OK;
}

SR_PRIV int icstation_usbrelay_start(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	if (!sdi)
		return SR_ERR_ARG;
	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;

	return icstation_usbrelay_send_byte(serial,
		ICSTATION_USBRELAY_CMD_START);
}

SR_PRIV int icstation_usbrelay_switch_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean on)
{
	struct dev_context *devc;
	struct channel_group_context *cgc;
	uint8_t state, mask;
	uint8_t tx_state;

	devc = sdi->priv;

	/*
	 * The device requires the communication of all relay states
	 * at the same time. Calling applications control individual
	 * relays. The device wants active-low state in the physical
	 * transport. Application uses positive logic (active-high).
	 *
	 * Update the locally cached state from the most recent request.
	 * Invert the result and send it to the device. Only update
	 * the internal cache after successful transmission.
	 */

	state = devc->relay_state;
	if (!cg) {
		/* Set all relays. */
		if (on)
			state |= devc->relay_mask;
		else
			state &= ~devc->relay_mask;
	} else {
		cgc = cg->priv;
		mask = 1UL << cgc->index;
		if (on)
			state |= mask;
		else
			state &= ~mask;
	}

	tx_state = ~state & devc->relay_mask;
	sr_spew("Sending status byte: %x", tx_state);
	if (icstation_usbrelay_send_byte(sdi->conn, tx_state) != SR_OK) {
		sr_err("Unable to send status byte.");
		return SR_ERR_IO;
	}

	devc->relay_state = state;

	return SR_OK;
}
