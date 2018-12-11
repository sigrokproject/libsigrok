/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#define PICKIT2_PACKET_LENGTH	64
#define PICKIT2_USB_ENDPOINT	1
#define PICKIT2_USB_TIMEOUT	250

#define PICKIT2_CMD_CHKSTAT	0xa2
#define PICKIT2_CMD_CHKVOLT	0xa3
#define PICKIT2_CMD_READ	0xac
#define PICKIT2_CMD_PADCHAR	0xad
#define PICKIT2_CMD_SETUP	0xb8
#define PICKIT2_CMD_SETPOS	0xb9

#define PICKIT2_SEL_BANK0	0x06
#define PICKIT2_SEL_BANK1	0x07

struct pickit2_cmd {
	size_t length;
	uint8_t raw[PICKIT2_PACKET_LENGTH];
};

static void pickit2_cmd_clear(struct pickit2_cmd *cmd)
{

	if (!cmd)
		return;
	memset(&cmd->raw[0], PICKIT2_CMD_PADCHAR, PICKIT2_PACKET_LENGTH);
	cmd->length = 0;
}

static void pickit2_cmd_append(struct pickit2_cmd *cmd, uint8_t b)
{

	if (!cmd)
		return;
	if (cmd->length == PICKIT2_PACKET_LENGTH)
		return;
	cmd->raw[cmd->length++] = b;
}

static int pickit2_usb_send(const struct sr_dev_inst *sdi, struct pickit2_cmd *cmd)
{
	struct sr_usb_dev_inst *usb;
	int ret, sent;
	GString *text;

	if (!cmd)
		return SR_OK;
	usb = sdi->conn;
	if (!usb)
		return SR_ERR_ARG;

	text = sr_hexdump_new(&cmd->raw[0], cmd->length);
	sr_dbg("usb sent: %s", text->str);
	sr_hexdump_free(text);

	ret = libusb_interrupt_transfer(usb->devhdl,
		LIBUSB_ENDPOINT_OUT | PICKIT2_USB_ENDPOINT,
		&cmd->raw[0], PICKIT2_PACKET_LENGTH,
		&sent, PICKIT2_USB_TIMEOUT);
	if (ret < 0) {
		sr_err("USB transmit error: %s.", libusb_error_name(ret));
		return SR_ERR_IO;
	}
	if (sent != PICKIT2_PACKET_LENGTH) {
		sr_err("USB short send: %d/%d bytes.",
			sent, PICKIT2_PACKET_LENGTH);
		return SR_ERR_IO;
	}

	return SR_OK;
}

static int pickit2_usb_recv(const struct sr_dev_inst *sdi, struct pickit2_cmd *cmd)
{
	struct sr_usb_dev_inst *usb;
	int ret, rcvd;
	GString *text;

	if (!cmd)
		return SR_ERR_ARG;
	usb = sdi->conn;
	if (!usb)
		return SR_ERR_ARG;

	ret = libusb_interrupt_transfer(usb->devhdl,
		LIBUSB_ENDPOINT_IN | PICKIT2_USB_ENDPOINT,
		&cmd->raw[0], PICKIT2_PACKET_LENGTH,
		&rcvd, PICKIT2_USB_TIMEOUT);
	if (ret < 0) {
		if (ret == LIBUSB_ERROR_TIMEOUT)
			sr_dbg("USB receive error: %s.", libusb_error_name(ret));
		else
			sr_err("USB receive error: %s.", libusb_error_name(ret));
		return SR_ERR_IO;
	}

	text = sr_hexdump_new(&cmd->raw[0], rcvd);
	sr_dbg("usb recv: %s", text->str);
	sr_hexdump_free(text);

	cmd->length = rcvd;
	if (rcvd != PICKIT2_PACKET_LENGTH) {
		sr_err("USB short recv: %d/%d bytes.",
			rcvd, PICKIT2_PACKET_LENGTH);
		return SR_ERR_IO;
	}

	return SR_OK;
}

/* Send a request, (optionally) keep reading until response became available. */
static int pickit2_usb_send_recv(const struct sr_dev_inst *sdi,
	struct pickit2_cmd *send_cmd, struct pickit2_cmd *recv_cmd, int do_wait)
{
	int ret;

	/* Send the command when one got specified. Ignore errors. */
	if (send_cmd)
		(void)pickit2_usb_send(sdi, send_cmd);

	/*
	 * Try receiving data, always ignore errors. When requested by
	 * the caller then keep receiving until response data became
	 * available.
	 */
	if (!recv_cmd)
		return SR_OK;
	do {
		ret = pickit2_usb_recv(sdi, recv_cmd);
		if (ret == SR_OK)
			return SR_OK;
		if (!do_wait)
			return ret;
	} while (1);
	/* UNREACH */
}

SR_PRIV int microchip_pickit2_setup_trigger(const struct sr_dev_inst *sdi)
{
	static const uint8_t trigger_channel_masks[PICKIT2_CHANNEL_COUNT] = {
		/* Bit positions for channels in trigger registers. */
		0x04, 0x08, 0x10,
	};
	static const uint16_t captureratio_magics[] = {
		/* TODO
		 * How to exactly calculate these magic 16bit values?
		 * They seem to neither match a percentage value nor a
		 * sample count (assuming 1 window holds 1K samples).
		 * As long as the formula is unknown, we are stuck with
		 * looking up magic values from a table of few pre-sets.
		 */
		0x0000,			/* unspecified ratio value */
		0x03cc, 0x000a, 0x0248,	/* 10%/50%/90% in the first window */
		0x07b4, 0x0b9c, 0x0f84,	/* 10% "plus 1/2/3 window widths" */
	};

	struct dev_context *devc;
	uint8_t trig_en, trig_lvl, trig_edge, trig_rep, trig_div;
	uint16_t trig_pos;
	uint64_t rate;
	size_t trig_pos_idx, ch_idx;
	uint8_t ch_mask, ch_cond;
	struct pickit2_cmd cmd;

	devc = sdi->priv;

	/* Translate user specs to internal setup values. */
	trig_en = trig_lvl = trig_edge = 0;
	for (ch_idx = 0; ch_idx < PICKIT2_CHANNEL_COUNT; ch_idx++) {
		if (!devc->triggers[ch_idx])
			continue;
		ch_mask = trigger_channel_masks[ch_idx];
		ch_cond = devc->triggers[ch_idx];
		trig_en |= ch_mask;
		switch (ch_cond) {
		case SR_TRIGGER_ONE:
		case SR_TRIGGER_RISING:
			trig_lvl |= ch_mask;
			break;
		}
		switch (ch_cond) {
		case SR_TRIGGER_FALLING:
		case SR_TRIGGER_RISING:
			trig_edge |= ch_mask;
			break;
		}
	}
	trig_rep = 1;
	trig_rep = MIN(trig_rep, 255);
	trig_rep = MAX(trig_rep, 1);
	if (!trig_en)
		trig_rep = 0;
	rate = devc->samplerates[devc->curr_samplerate_idx];
	rate = SR_MHZ(1) / rate - 1;
	trig_div = rate & 0xff;
	trig_pos_idx = devc->trigpos;
	if (trig_pos_idx >= ARRAY_SIZE(captureratio_magics))
		trig_pos_idx = 0;
	trig_pos = captureratio_magics[trig_pos_idx];

	/* Construct the SETUP packet. */
	pickit2_cmd_clear(&cmd);
	pickit2_cmd_append(&cmd, PICKIT2_CMD_SETUP);
	pickit2_cmd_append(&cmd, 0x01);
	pickit2_cmd_append(&cmd, trig_en);
	pickit2_cmd_append(&cmd, trig_lvl);
	pickit2_cmd_append(&cmd, trig_edge);
	pickit2_cmd_append(&cmd, trig_rep);
	pickit2_cmd_append(&cmd, trig_pos % 256);
	pickit2_cmd_append(&cmd, trig_pos / 256);
	pickit2_cmd_append(&cmd, trig_div);

	/*
	 * Transmit the SETUP packet. Only send it out, poll for the
	 * response later. When a trigger is involved, the response may
	 * take considerable amounts of time to arrive. We want apps
	 * to remain responsive during that period of time.
	 */
	(void)pickit2_usb_send_recv(sdi, &cmd, NULL, FALSE);

	return SR_OK;
}

/* Read specified bank data at given offset into caller provided buffer. */
static int pickit2_retrieve_bank(struct sr_dev_inst *sdi,
	size_t bank_idx, size_t offset, uint8_t **buf, size_t *len)
{
	struct pickit2_cmd send_cmd, recv_cmd;
	int ret;
	size_t copy_iter, copy_len;

	/* Construct and send the SETPOS packet. No response expected. */
	pickit2_cmd_clear(&send_cmd);
	pickit2_cmd_append(&send_cmd, PICKIT2_CMD_SETPOS);
	pickit2_cmd_append(&send_cmd, offset & 0xff);
	pickit2_cmd_append(&send_cmd, PICKIT2_SEL_BANK0 + bank_idx);
	ret = pickit2_usb_send_recv(sdi, &send_cmd, NULL, FALSE);
	if (ret != SR_OK)
		return ret;
	sr_dbg("read bank: pos set");

	/* Run two READ cycles, get 2x 64 bytes => 128 bytes raw data. */
	pickit2_cmd_clear(&send_cmd);
	pickit2_cmd_append(&send_cmd, PICKIT2_CMD_READ);
	copy_iter = 2;
	while (copy_iter-- > 0) {
		ret = pickit2_usb_send_recv(sdi, &send_cmd, &recv_cmd, TRUE);
		if (ret != SR_OK)
			return ret;
		copy_len = MIN(PICKIT2_PACKET_LENGTH, *len);
		memcpy(*buf, &recv_cmd.raw[0], copy_len);
		*buf += copy_len;
		*len -= copy_len;
	}

	return SR_OK;
}

/* Read all of the (banked, raw) sample data after acquisition completed. */
static int pickit2_retrieve_sample_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t *rdpos;
	size_t rdlen;
	int ret;

	devc = sdi->priv;
	rdpos = &devc->samples_raw[0];
	rdlen = sizeof(devc->samples_raw);

	ret = pickit2_retrieve_bank(sdi, 0, 0x00, &rdpos, &rdlen);
	if (ret)
		return ret;
	ret = pickit2_retrieve_bank(sdi, 0, 0x80, &rdpos, &rdlen);
	if (ret)
		return ret;
	ret = pickit2_retrieve_bank(sdi, 1, 0x00, &rdpos, &rdlen);
	if (ret)
		return ret;
	ret = pickit2_retrieve_bank(sdi, 1, 0x80, &rdpos, &rdlen);
	if (ret)
		return ret;

	return SR_OK;
}

/* Send converted sample data to the sigrok session. */
static int pickit2_submit_logic_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct {
		uint8_t raw_mask, conv_mask;
	} ch_map[PICKIT2_CHANNEL_COUNT] = {
		{ 0x04, 0x01, },
		{ 0x08, 0x02, },
		{ 0x01, 0x04, },
	};
	uint8_t *raw_buf, raw_byte, *conv_buf;
	size_t raw_len, conv_len;
	uint64_t limit;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	devc = sdi->priv;

	/*
	 * TODO Manipulate (or create) the above channel mapping table.
	 * Remove disabled channels, create dense output format.
	 * Could:
	 * - Loop over the index, check the corresponding channel's
	 *   state, clear out the conv_mask part and shift down all
	 *   subsequent conv_mask parts.
	 */

	/*
	 * Convert raw dump (two samples per byte, at odd positions) to
	 * internal sigrok format (one sample per byte, at increasing
	 * offsets which start at 0).
	 */
#define handle_nibble(n) do { \
	uint8_t conv_byte; \
	size_t ch_idx; \
	conv_byte = 0x00; \
	for (ch_idx = 0; ch_idx < PICKIT2_CHANNEL_COUNT; ch_idx++) { \
		if ((n) & ch_map[ch_idx].raw_mask) \
			conv_byte |= ch_map[ch_idx].conv_mask; \
	} \
	*conv_buf++ = conv_byte; \
	conv_len++; \
} while (0)

	raw_len = sizeof(devc->samples_raw);
	raw_buf = &devc->samples_raw[raw_len];
	conv_buf = &devc->samples_conv[0];
	conv_len = 0;
	while (raw_len-- > 0) {
		raw_byte = *(--raw_buf);
		handle_nibble((raw_byte >> 0) & 0x0f);
		handle_nibble((raw_byte >> 4) & 0x0f);
	}

	/* Submit a logic packet to the sigrok session. */
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = sizeof(uint8_t);
	logic.data = &devc->samples_conv[0];
	logic.length = conv_len;
	limit = devc->sw_limits.limit_samples;
	if (limit && limit < logic.length)
		logic.length = limit;
	sr_session_send(sdi, &packet);

	return SR_OK;
}

static gboolean pickit2_status_is_cancel(uint16_t status)
{
	/* "Button press" and "transfer timeout" translate to "cancelled". */
	static const uint16_t status_cancel_mask = 0x4004;

	sr_dbg("recv: status 0x%04x", status);
	if ((status & status_cancel_mask) == status_cancel_mask)
		return TRUE;
	return FALSE;
}

/* Periodically invoked poll routine, checking for incoming receive data. */
SR_PRIV int microchip_pickit2_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct pickit2_cmd cmd;
	int ret;
	uint16_t status;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	devc = sdi->priv;
	if (!devc)
		return TRUE;

	/* Waiting for the trigger condition? */
	if (devc->state == STATE_WAIT) {
		/* Keep waiting until status becomes available. */
		ret = pickit2_usb_send_recv(sdi, NULL, &cmd, FALSE);
		if (ret != SR_OK)
			return TRUE;
		/* Check status flags for cancel requests. */
		devc->state = STATE_DATA;
		status = RL16(&cmd.raw[0]);
		if (pickit2_status_is_cancel(status)) {
			sr_info("User cancelled acquisition.");
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		}
		sr_dbg("recv: Data has become available.");
		/* FALLTHROUGH */
	}

	/*
	 * Retrieve acquired sample data (blocking, acquisition has
	 * completed and samples are few), and stop acquisition (have
	 * the poll routine unregistered).
	 */
	ret = pickit2_retrieve_sample_data(sdi);
	if (ret != SR_OK)
		return ret;
	ret = pickit2_submit_logic_data(sdi);
	if (ret != SR_OK)
		return ret;
	sr_dev_acquisition_stop(sdi);
	return TRUE;
}
