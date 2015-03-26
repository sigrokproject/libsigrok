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

#include <errno.h>
#include <glib/gstdio.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"
#include "lwla.h"

#define BITSTREAM_MAX_SIZE    (256 * 1024) /* bitstream size limit for safety */
#define BITSTREAM_HEADER_SIZE 4            /* transfer header size in bytes */

/* Load a bitstream file into memory.  Returns a newly allocated array
 * consisting of a 32-bit length field followed by the bitstream data.
 */
static unsigned char *load_bitstream_file(const char *filename, int *length_p)
{
	GStatBuf statbuf;
	FILE *file;
	unsigned char *stream;
	size_t length, count;

	/* Retrieve and validate the file size. */
	if (g_stat(filename, &statbuf) < 0) {
		sr_err("Failed to access bitstream file: %s.",
		       g_strerror(errno));
		return NULL;
	}
	if (!S_ISREG(statbuf.st_mode)) {
		sr_err("Bitstream is not a regular file.");
		return NULL;
	}
	if (statbuf.st_size <= 0 || statbuf.st_size > BITSTREAM_MAX_SIZE) {
		sr_err("Refusing to load bitstream of unreasonable size "
		       "(%" PRIu64 " bytes).", (uint64_t)statbuf.st_size);
		return NULL;
	}

	/* The message length includes the 4-byte header. */
	length = BITSTREAM_HEADER_SIZE + statbuf.st_size;
	stream = g_try_malloc(length);
	if (!stream) {
		sr_err("Failed to allocate bitstream buffer.");
		return NULL;
	}

	file = g_fopen(filename, "rb");
	if (!file) {
		sr_err("Failed to open bitstream file: %s.", g_strerror(errno));
		g_free(stream);
		return NULL;
	}

	/* Write the message length header. */
	*(uint32_t *)stream = GUINT32_TO_BE(length);

	count = fread(stream + BITSTREAM_HEADER_SIZE,
		      length - BITSTREAM_HEADER_SIZE, 1, file);
	if (count != 1) {
		sr_err("Failed to read bitstream file: %s.", g_strerror(errno));
		fclose(file);
		g_free(stream);
		return NULL;
	}
	fclose(file);

	*length_p = length;
	return stream;
}

/* Load a Raw Binary File (.rbf) from the firmware directory and transfer
 * it to the device.
 */
SR_PRIV int lwla_send_bitstream(const struct sr_usb_dev_inst *usb,
				const char *basename)
{
	char *filename;
	unsigned char *stream;
	int ret;
	int length;
	int xfer_len;

	if (!usb || !basename)
		return SR_ERR_BUG;

	filename = g_build_filename(FIRMWARE_DIR, basename, NULL);
	sr_info("Downloading FPGA bitstream at '%s'.", filename);

	stream = load_bitstream_file(filename, &length);
	g_free(filename);

	if (!stream)
		return SR_ERR;

	/* Transfer the entire bitstream in one URB. */
	ret = libusb_bulk_transfer(usb->devhdl, EP_BITSTREAM,
				   stream, length, &xfer_len, USB_TIMEOUT_MS);
	g_free(stream);

	if (ret != 0) {
		sr_err("Failed to transfer bitstream: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer_len != length) {
		sr_err("Failed to transfer bitstream: incorrect length "
		       "%d != %d.", xfer_len, length);
		return SR_ERR;
	}
	sr_info("FPGA bitstream download of %d bytes done.", xfer_len);

	/* This delay appears to be necessary for reliable operation. */
	g_usleep(30 * 1000);

	return SR_OK;
}

SR_PRIV int lwla_send_command(const struct sr_usb_dev_inst *usb,
			      const uint16_t *command, int cmd_len)
{
	int ret;
	int xfer_len;

	if (!usb || !command || cmd_len <= 0)
		return SR_ERR_BUG;

	xfer_len = 0;
	ret = libusb_bulk_transfer(usb->devhdl, EP_COMMAND,
				   (unsigned char *)command, cmd_len * 2,
				   &xfer_len, USB_TIMEOUT_MS);
	if (ret != 0) {
		sr_dbg("Failed to send command %d: %s.",
		       LWLA_TO_UINT16(command[0]), libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer_len != cmd_len * 2) {
		sr_dbg("Failed to send command %d: incorrect length %d != %d.",
		       LWLA_TO_UINT16(command[0]), xfer_len, cmd_len * 2);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int lwla_receive_reply(const struct sr_usb_dev_inst *usb,
			       uint32_t *reply, int reply_len, int expect_len)
{
	int ret;
	int xfer_len;

	if (!usb || !reply || reply_len <= 0)
		return SR_ERR_BUG;

	xfer_len = 0;
	ret = libusb_bulk_transfer(usb->devhdl, EP_REPLY,
				   (unsigned char *)reply, reply_len * 4,
				   &xfer_len, USB_TIMEOUT_MS);
	if (ret != 0) {
		sr_dbg("Failed to receive reply: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer_len != expect_len * 4) {
		sr_dbg("Failed to receive reply: incorrect length %d != %d.",
		       xfer_len, expect_len * 4);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int lwla_read_reg(const struct sr_usb_dev_inst *usb,
			  uint16_t reg, uint32_t *value)
{
	int ret;
	uint16_t command[2];
	uint32_t reply[128]; /* full EP buffer to avoid overflows */

	command[0] = LWLA_WORD(CMD_READ_REG);
	command[1] = LWLA_WORD(reg);

	ret = lwla_send_command(usb, command, ARRAY_SIZE(command));

	if (ret != SR_OK)
		return ret;

	ret = lwla_receive_reply(usb, reply, ARRAY_SIZE(reply), 1);

	if (ret == SR_OK)
		*value = LWLA_TO_UINT32(reply[0]);

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

	return lwla_send_command(usb, command, ARRAY_SIZE(command));
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
