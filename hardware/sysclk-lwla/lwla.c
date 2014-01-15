/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
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

#include "lwla.h"
#include "protocol.h"
#include "libsigrok-internal.h"

SR_PRIV int lwla_send_bitstream(const struct sr_usb_dev_inst *usb,
				const char *filename)
{
	GMappedFile *file;
	GError *error;
	char *stream;
	size_t length;
	int ret;
	int xfer_len;

	if (usb == NULL || filename == NULL)
		return SR_ERR_ARG;

	sr_info("Downloading FPGA bitstream at '%s'.", filename);

	/* Map bitstream file into memory. */
	error = NULL;
	file = g_mapped_file_new(filename, FALSE, &error);
	if (!file) {
		sr_err("Unable to open bitstream file: %s.", error->message);
		g_error_free(error);
		return SR_ERR;
	}
	stream = g_mapped_file_get_contents(file);
	length = g_mapped_file_get_length(file);

	/* Sanity check. */
	if (!stream || length < 4 || RB32(stream) != length) {
		sr_err("Invalid FPGA bitstream.");
		g_mapped_file_unref(file);
		return SR_ERR;
	}

	/* Transfer the entire bitstream in one URB. */
	ret = libusb_bulk_transfer(usb->devhdl, EP_BITSTREAM,
				   (unsigned char *)stream, length,
				   &xfer_len, USB_TIMEOUT);
	g_mapped_file_unref(file);

	if (ret != 0) {
		sr_err("Failed to transfer bitstream: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer_len != (int)length) {
		sr_err("Failed to transfer bitstream: incorrect length "
		       "%d != %d.", xfer_len, (int)length);
		return SR_ERR;
	}
	sr_info("FPGA bitstream download of %d bytes done.", xfer_len);

	/* This delay appears to be necessary for reliable operation. */
	g_usleep(30000);

	return SR_OK;
}

SR_PRIV int lwla_send_command(const struct sr_usb_dev_inst *usb,
			      const uint16_t *command, int cmd_len)
{
	int ret;
	int xfer_len;

	if (usb == NULL || command == NULL || cmd_len <= 0)
		return SR_ERR_ARG;

	xfer_len = 0;
	ret = libusb_bulk_transfer(usb->devhdl, EP_COMMAND,
				   (unsigned char *)command, cmd_len * 2,
				   &xfer_len, USB_TIMEOUT);
	if (ret != 0) {
		sr_dbg("Failed to send command %u: %s.",
		       LWLA_READ16(command), libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer_len != cmd_len * 2) {
		sr_dbg("Failed to send command %u: incorrect length %d != %d.",
		       LWLA_READ16(command), xfer_len, cmd_len * 2);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int lwla_receive_reply(const struct sr_usb_dev_inst *usb,
			       uint16_t *reply, int reply_len, int expect_len)
{
	int ret;
	int xfer_len;

	if (usb == NULL || reply == NULL || reply_len <= 0)
		return SR_ERR_ARG;

	xfer_len = 0;
	ret = libusb_bulk_transfer(usb->devhdl, EP_REPLY,
				   (unsigned char *)reply, reply_len * 2,
				   &xfer_len, USB_TIMEOUT);
	if (ret != 0) {
		sr_dbg("Failed to receive reply: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer_len != expect_len * 2) {
		sr_dbg("Failed to receive reply: incorrect length %d != %d.",
		       xfer_len, expect_len * 2);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int lwla_read_reg(const struct sr_usb_dev_inst *usb,
			  uint16_t reg, uint32_t *value)
{
	int ret;
	uint16_t command[2];
	uint16_t reply[256]; /* full EP buffer to avoid overflows */

	command[0] = LWLA_WORD(CMD_READ_REG);
	command[1] = LWLA_WORD(reg);

	ret = lwla_send_command(usb, command, G_N_ELEMENTS(command));

	if (ret != SR_OK)
		return ret;

	ret = lwla_receive_reply(usb, reply, G_N_ELEMENTS(reply), 2);

	if (ret == SR_OK)
		*value = LWLA_READ32(reply);

	return ret;
}

SR_PRIV int lwla_write_reg(const struct sr_usb_dev_inst *usb,
			   uint16_t reg, uint32_t value)
{
	uint16_t command[4];

	command[0] = LWLA_WORD(CMD_WRITE_REG);
	command[1] = LWLA_WORD(reg);
	command[2] = LWLA_WORD_0(value);
	command[3] = LWLA_WORD_1(value);

	return lwla_send_command(usb, command, G_N_ELEMENTS(command));
}

SR_PRIV int lwla_write_regs(const struct sr_usb_dev_inst *usb,
			    const struct regval_pair *regvals, int count)
{
	int i;
	int ret;

	ret = SR_OK;

	for (i = 0; i < count; ++i) {
		ret = lwla_write_reg(usb, regvals[i].reg, regvals[i].val);

		if (ret != SR_OK)
			break;
	}

	return ret;
}
