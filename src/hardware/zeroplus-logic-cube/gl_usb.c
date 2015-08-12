/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Sven Peter <sven@fail0verflow.com>
 * Copyright (C) 2010 Haxx Enterprises <bushing@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *  THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "gl_usb.h"
#include "protocol.h"

#define CTRL_IN		(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN | \
			 LIBUSB_RECIPIENT_INTERFACE)
#define CTRL_OUT	(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT | \
			 LIBUSB_RECIPIENT_INTERFACE)
#define EP1_BULK_IN	(LIBUSB_ENDPOINT_IN | 1)

#define TIMEOUT_MS	(5 * 1000)

enum {
	REQ_READBULK = 0x82,
	REQ_WRITEADDR,
	REQ_READDATA,
	REQ_WRITEDATA,
};

static int gl_write_address(libusb_device_handle *devh, unsigned int address)
{
	unsigned char packet[8] = { address & 0xFF };
	int ret;

	ret = libusb_control_transfer(devh, CTRL_OUT, 0xc, REQ_WRITEADDR,
					 0, packet, 1, TIMEOUT_MS);
	if (ret != 1)
		sr_err("%s: %s.", __func__, libusb_error_name(ret));
	return ret;
}

static int gl_write_data(libusb_device_handle *devh, unsigned int val)
{
	unsigned char packet[8] = { val & 0xFF };
	int ret;

	ret = libusb_control_transfer(devh, CTRL_OUT, 0xc, REQ_WRITEDATA,
				      0, packet, 1, TIMEOUT_MS);
	if (ret != 1)
		sr_err("%s: %s.", __func__, libusb_error_name(ret));
	return ret;
}

static int gl_read_data(libusb_device_handle *devh)
{
	unsigned char packet[8] = { 0 };
	int ret;

	ret = libusb_control_transfer(devh, CTRL_IN, 0xc, REQ_READDATA,
				      0, packet, 1, TIMEOUT_MS);
	if (ret != 1)
		sr_err("%s: %s, val=%hhx.", __func__,
		       libusb_error_name(ret), packet[0]);
	return (ret == 1) ? packet[0] : ret;
}

SR_PRIV int gl_read_bulk(libusb_device_handle *devh, void *buffer,
			 unsigned int size)
{
	unsigned char packet[8] =
	    { 0, 0, 0, 0, size & 0xff, (size & 0xff00) >> 8,
	      (size & 0xff0000) >> 16, (size & 0xff000000) >> 24 };
	int ret, transferred = 0;

	ret = libusb_control_transfer(devh, CTRL_OUT, 0x4, REQ_READBULK,
				      0, packet, 8, TIMEOUT_MS);
	if (ret != 8)
		sr_err("%s: libusb_control_transfer: %s.", __func__,
		       libusb_error_name(ret));

	ret = libusb_bulk_transfer(devh, EP1_BULK_IN, buffer, size,
				   &transferred, TIMEOUT_MS);
	if (ret < 0)
		sr_err("%s: libusb_bulk_transfer: %s.", __func__,
		       libusb_error_name(ret));
	return transferred;
}

SR_PRIV int gl_reg_write(libusb_device_handle *devh, unsigned int reg,
		 unsigned int val)
{
	int ret;

	ret = gl_write_address(devh, reg);
	if (ret < 0)
		return ret;
	ret = gl_write_data(devh, val);
	return ret;
}

SR_PRIV int gl_reg_read(libusb_device_handle *devh, unsigned int reg)
{
	int ret;

	ret = gl_write_address(devh, reg);
	if (ret < 0)
		return ret;
	ret = gl_read_data(devh);
	return ret;
}

SR_PRIV int gl_reg_read_buf(libusb_device_handle *devh, unsigned int reg,
		unsigned char *buf, unsigned int len)
{
	int ret;
	unsigned int i;

	ret = gl_write_address(devh, reg);
	if (ret < 0)
		return ret;
	for (i = 0; i < len; i++) {
		ret = gl_read_data(devh);
		if (ret < 0)
			return ret;
		buf[i] = ret;
	}
	return 0;
}
