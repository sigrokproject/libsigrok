/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
 * Copyright (C) 2021 Eric Neulight
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

#define PICKIT_PACKET_LENGTH	64
#define PICKIT_USB_ENDPOINT		1
#define PICKIT_USB_TIMEOUT		250

#define PICKIT_CMD_CHKSTAT	0xa2
#define PICKIT_CMD_CHKVOLT	0xa3
#define PICKIT_CMD_READ		0xac
#define PICKIT_CMD_PADCHAR	0xad
#define PICKIT_CMD_SETUP	0xb8
#define PICKIT_CMD_SETPOS	0xb9

#define PICKIT2_RAM_BANK	0x06
#define PICKIT3_RAM_BANK	0x40

#define PICKIT_TRIG_SWAP	0x8000

struct pickit_cmd {
	size_t length;
	uint8_t raw[PICKIT_PACKET_LENGTH];
};

static void pickit_cmd_clear(struct pickit_cmd *cmd)
{
	if (!cmd)
		return;
	memset(&cmd->raw[0], PICKIT_CMD_PADCHAR, PICKIT_PACKET_LENGTH);
	cmd->length = 0;
}

static void pickit_cmd_append(struct pickit_cmd *cmd, uint8_t b)
{
	if (!cmd)
		return;
	if (cmd->length == PICKIT_PACKET_LENGTH)
		return;
	cmd->raw[cmd->length++] = b;
}

static int pickit_usb_send(const struct sr_dev_inst *sdi, struct pickit_cmd *cmd)
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
	sr_dbg("USB sent: %s", text->str);
	sr_hexdump_free(text);

	ret = libusb_interrupt_transfer(usb->devhdl,
		LIBUSB_ENDPOINT_OUT | PICKIT_USB_ENDPOINT,
		&cmd->raw[0], PICKIT_PACKET_LENGTH,
		&sent, PICKIT_USB_TIMEOUT);
	if (ret < 0) {
		sr_err("USB transmit error: %s.", libusb_error_name(ret));
		return SR_ERR_IO;
	}
	if (sent != PICKIT_PACKET_LENGTH) {
		sr_err("USB short send: %d/%d bytes.",
			sent, PICKIT_PACKET_LENGTH);
		return SR_ERR_IO;
	}

	return SR_OK;
}

static int pickit_usb_recv(const struct sr_dev_inst *sdi, struct pickit_cmd *cmd)
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
		LIBUSB_ENDPOINT_IN | PICKIT_USB_ENDPOINT,
		&cmd->raw[0], PICKIT_PACKET_LENGTH,
		&rcvd, PICKIT_USB_TIMEOUT);
	if (ret < 0) {
		if (ret == LIBUSB_ERROR_TIMEOUT)
			sr_dbg("USB receive error: %s.", libusb_error_name(ret));
		else
			sr_err("USB receive error: %s.", libusb_error_name(ret));
		return SR_ERR_IO;
	}

	text = sr_hexdump_new(&cmd->raw[0], rcvd);
	sr_dbg("USB recv: %s", text->str);
	sr_hexdump_free(text);

	cmd->length = rcvd;
	if (rcvd != PICKIT_PACKET_LENGTH) {
		sr_err("USB short recv: %d/%d bytes.",
			rcvd, PICKIT_PACKET_LENGTH);
		return SR_ERR_IO;
	}

	return SR_OK;
}

/* Send a request, (optionally) keep reading until response became available. */
static int pickit_usb_send_recv(const struct sr_dev_inst *sdi,
	struct pickit_cmd *send_cmd, struct pickit_cmd *recv_cmd, int do_wait)
{
	int ret;

	/* Send the command when one got specified. Ignore errors. */
	if (send_cmd)
		(void)pickit_usb_send(sdi, send_cmd);

	/*
	 * Try receiving data, always ignore errors. When requested by
	 * the caller then keep receiving until response data became
	 * available.
	 */
	if (!recv_cmd)
		return SR_OK;
	do {
		ret = pickit_usb_recv(sdi, recv_cmd);
		if (ret == SR_OK)
			return SR_OK;
		if (!do_wait)
			return ret;
	} while (1);
	/* UNREACH */
}

SR_PRIV int microchip_pickit_setup_trigger(const struct sr_dev_inst *sdi)
{
	static const uint8_t trigger_channel_masks[PICKIT_CHANNEL_COUNT] = {
		/* Bit positions for channels in trigger registers. */
		0x04, 0x08, 0x10,
	};

	struct dev_context *devc;
	uint8_t trig_en, trig_lvl, trig_edge, trig_rise;
	uint16_t trig_div;
	size_t ch_idx;
	uint8_t ch_mask, ch_cond;
	struct pickit_cmd cmd;

	devc = sdi->priv;

	/* Translate user specs to internal setup values. */
	trig_en = trig_lvl = trig_edge = 0;
	trig_rise = 1;
	for (ch_idx = 0; ch_idx < PICKIT_CHANNEL_COUNT; ch_idx++) {
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
			trig_rise=0;	/* Falls through - this comment avoids the new (dumb) gcc warning. */
		case SR_TRIGGER_RISING:
			trig_edge |= ch_mask;
			break;
		}
	}

	/*
	 * PICkit trig_postsamp is given as number of samples to take post-trigger, minus 1.
	 * Range is 1 - 65536 (0=65536).
	 * A value of 1 is equivalent to (nearly) 100% pre-trigger capture ratio:
	 *     a full buffer of pre-samples are captured, plus the trigger will be captured, plus one initial sample, plus the "1" specified.
	 * A value of 1022 is euivalent to 0% pre-trigger capture ratio:
	 *     the trigger is captured, plus one initial sample, plus the "1022" samples, which fills the PICkit 1024 sample buffer completely.
	 * A value in between 1 and 1022 places the trigger somewhere within the sample buffer, proportionally between 100%-0% capture ratio, respectively.
	 * A value greater than 1022 essentially continues to overwrite the circular FIFO buffer until the total number of samples have been taken,
	 *     leaving the last 1024 samples in the buffer.  This allows sampling further into a digital stream, while only capturing the last 1024 samples.
	 * The PICkit always takes 1024 (pre)samples before the trigger.
	 * If sw_limits.limit_samples is less than or equal to 1024, the PICkit will (inherently) capture 1024 samples (fill its buffer).
	 * If sw_limits.limit_samples is greater than 1024, and capture ratio is 0%, we take that to mean trig_postsamp should be limit_samples.
	 * Unfortunately, PulseView does not yet allow for input of arbitrary nor limited selection of limit_samples, but if/when it does, this will work perfectly (in the meantime it can be used crudely).
	 */
	devc->trig_postsamp = (1021*(100-devc->captureratio)+150)/100;  /* Integer math to round to the nearest integer of 1021*(1-captureratio)+1 */
	if (!devc->captureratio && (devc->sw_limits.limit_samples > 1024))
		devc->trig_postsamp = devc->sw_limits.limit_samples;
	
	/* Calculate samplerate delay count. */	
	trig_div = SR_MHZ(1) / devc->samplerates[devc->curr_samplerate_idx] - 1;

	/* Construct the SETUP packet. */
	pickit_cmd_clear(&cmd);
	pickit_cmd_append(&cmd, PICKIT_CMD_SETUP);
	pickit_cmd_append(&cmd, trig_rise);
	pickit_cmd_append(&cmd, trig_en);
	pickit_cmd_append(&cmd, trig_lvl);
	pickit_cmd_append(&cmd, trig_edge);
	pickit_cmd_append(&cmd, trig_en ? devc->trig_count & 0xFF : 1);
	pickit_cmd_append(&cmd, devc->trig_postsamp & 0xFF);
	pickit_cmd_append(&cmd, devc->trig_postsamp >> 8);
	if (devc->isPk3) {
		/* Pk3 uses a 12-bit (Pk2 x16) divisor */
		pickit_cmd_append(&cmd, (trig_div << 4) & 0xFF);
		pickit_cmd_append(&cmd, (trig_div >> 4) & 0xFF);
	} else {
		/* Pk2 uses an 8-bit divisor */
		pickit_cmd_append(&cmd, trig_div & 0xFF);
	}

	/*
	 * Transmit the SETUP packet. Only send it out, poll for the
	 * response later. When a trigger is involved, the response may
	 * take considerable amounts of time to arrive. We want apps
	 * to remain responsive during that period of time.
	 */
	(void)pickit_usb_send_recv(sdi, &cmd, NULL, FALSE);

	return SR_OK;
}

/* Read specified bank data at given offset into caller provided buffer. */
static int pickit_retrieve_bank(struct sr_dev_inst *sdi,
	size_t bank_idx, size_t offset, uint8_t **buf, size_t *len)
{
	struct pickit_cmd send_cmd, recv_cmd;
	int ret;
	size_t copy_iter, copy_len;

	/* Construct and send the SETPOS packet. No response expected. */
	pickit_cmd_clear(&send_cmd);
	pickit_cmd_append(&send_cmd, PICKIT_CMD_SETPOS);
	pickit_cmd_append(&send_cmd, offset & 0xff);
	pickit_cmd_append(&send_cmd, (((struct dev_context*)sdi->priv)->isPk3 ? PICKIT3_RAM_BANK : PICKIT2_RAM_BANK) + bank_idx);
	ret = pickit_usb_send_recv(sdi, &send_cmd, NULL, FALSE);
	if (ret != SR_OK)
		return ret;
	sr_dbg("retrieve bank: RAM copied to upload buffer");

	/* Run two READ cycles, get 2x 64 bytes => 128 bytes raw data. */
	pickit_cmd_clear(&send_cmd);
	pickit_cmd_append(&send_cmd, PICKIT_CMD_READ);
	copy_iter = 2;
	while (copy_iter-- > 0) {
		ret = pickit_usb_send_recv(sdi, &send_cmd, &recv_cmd, TRUE);
		if (ret != SR_OK)
			return ret;
		copy_len = MIN(PICKIT_PACKET_LENGTH, *len);
		memcpy(*buf, &recv_cmd.raw[0], copy_len);
		*buf += copy_len;
		*len -= copy_len;
	}

	return SR_OK;
}

/* Read all of the (banked, raw) sample data after acquisition completed. */
static int pickit_retrieve_sample_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t *rdpos;
	size_t rdlen;
	int ret;

	devc = sdi->priv;
	rdpos = &devc->samples_pic[0];
	rdlen = sizeof(devc->samples_pic);

	ret = pickit_retrieve_bank(sdi, 0, 0x00, &rdpos, &rdlen);
	if (ret)
		return ret;
	ret = pickit_retrieve_bank(sdi, 0, 0x80, &rdpos, &rdlen);
	if (ret)
		return ret;
	ret = pickit_retrieve_bank(sdi, 1, 0x00, &rdpos, &rdlen);
	if (ret)
		return ret;
	ret = pickit_retrieve_bank(sdi, 1, 0x80, &rdpos, &rdlen);
	if (ret)
		return ret;

	return SR_OK;
}

/* Send converted sample data to the sigrok session. */
static int pickit_submit_logic_data(struct sr_dev_inst *sdi, uint16_t trig_loc)
{
	struct dev_context *devc;
	uint8_t b;
	uint16_t pic_idx, sr_idx;
	bool swappedsamp;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	devc = sdi->priv;

	/*
	 * Unwind legacy PICkit2 packing of samples.  PICkit3 emulates this to be somewhat compatible, so both return the same packed buffer.
	 * 1024 samples are returned in a 512 byte buffer, arranged in reverse time, with increasing time in descending buffer locations.
	 * The samples are packed, 2 samples per byte, in a somewhat convoluted way that was convenient and quick for the PICkit2.
	 * Packed bits: [ 7:pin5odd 6:pin4odd 5:N/A 4:pin6even 3:pin5even 2:pin4even 1:N/A 0:pin6odd ]
	 */
	/* Remember whether trigger happened in the swapped sample part of the packed byte. */
	/* (Swapped sample is equivalent to the odd samples in a 1024 sample space.) */
	swappedsamp = (trig_loc & PICKIT_TRIG_SWAP);
	trig_loc &= 0x1FF;

	/* Calculate equivalent PICkit 1024 RAM buffer location of trigger sample. */
	trig_loc = 1021 - 2 * trig_loc;
	if (swappedsamp) trig_loc++;
	trig_loc &= 0x3FF;	/* Circular buffer modulo 1024 */

	/* Calulate index of "first" sample within the packed reverse time 512 byte buffer. */
	/* The "first" (oldest) sample will be one past the last sample written to the circular FIFO. */
	pic_idx = ((1021 - trig_loc - devc->trig_postsamp) >> 1) & 0x1FF;

	/* The first (oldest) sample is in the swapped (odd) position if the trigger position and devc->trig_postsamp are in opposite odd-even positions. */
	swappedsamp = (trig_loc ^ devc->trig_postsamp) & 1;
	
	/* Calculate sigrok 1024-byte buffer trigger location. */
	trig_loc = (devc->trig_postsamp <= 1022) ? 1022 - devc->trig_postsamp : 0xFFFF;

	logic.unitsize = sizeof(uint8_t);
	logic.length = 0;

	/* Write PICkit's packed reverse-time circular buffer sequentially into sigrok 1024 byte buffer. */
	for (sr_idx=0; sr_idx<sizeof(devc->samples_sr); sr_idx++) {
		b = devc->samples_pic[pic_idx];
		if (swappedsamp) {
			b = (b << 4) | (b >> 4);
			pic_idx--;
			pic_idx &= 0x1FF;
		}
		swappedsamp = !swappedsamp;
		devc->samples_sr[sr_idx] = (b >> 2) & 0x7;
		
		if ((sr_idx == trig_loc) || (sr_idx == (PICKIT_SAMPLE_COUNT - 1))) {
			if (sr_idx) {
				/* Send logic packet. */
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.data = &devc->samples_sr[logic.length];
				logic.length = (sr_idx == trig_loc) ? (uint64_t)sr_idx : (uint64_t)sr_idx - logic.length + 1;
				sr_session_send(sdi, &packet);
			}
			if (sr_idx == trig_loc) {
				/* Indicate trigger occurred at this sample. */
				packet.type = SR_DF_TRIGGER;
				packet.payload = NULL;
				sr_session_send(sdi, &packet);
			}
		}
	}

	return SR_OK;
}

/* Periodically invoked poll routine, checking for incoming receive data. */
SR_PRIV int microchip_pickit_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct pickit_cmd cmd;
	int ret;
	uint16_t trig_loc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	devc = sdi->priv;
	if (!devc)
		return TRUE;

	/* Should never get here unless waiting for the trigger condition and response from PICkit */
	if (devc->state != STATE_WAIT)
		return SR_ERR_BUG;
		
	/* Keep waiting until status becomes available. */
	ret = pickit_usb_send_recv(sdi, NULL, &cmd, FALSE);
	if (ret != SR_OK)
		return TRUE;

	/* Got a response.  Bump to next state. */
	devc->state = STATE_DATA;

	/* Read response. */
	trig_loc = RL16(&cmd.raw[0]);
	sr_dbg("recv: trig_loc 0x%04X", trig_loc);

	/* Check status flags for cancel requests. "Button press" translates to "cancelled". */
	if (devc->isPk3 ? (trig_loc == 0xFFFF) : (trig_loc & 0x4000)) {
		sr_info("User cancelled acquisition.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}
	sr_dbg("recv: Data has become available.");

	/*
	 * Retrieve acquired sample data and stop acquisition
	 * (have the poll routine unregistered).
	 */
	ret = pickit_retrieve_sample_data(sdi);
	if (ret != SR_OK)
		return ret;
	ret = pickit_submit_logic_data(sdi, trig_loc);
	if (ret != SR_OK)
		return ret;
	sr_dev_acquisition_stop(sdi);
	return TRUE;
}
