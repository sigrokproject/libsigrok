/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

/*
 * Helper functions for the Cypress EZ-USB / FX2 series chips.
 */

#include <config.h>
#include <libusb.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ezusb"

#define FW_CHUNKSIZE (4 * 1024)

SR_PRIV int ezusb_reset(struct libusb_device_handle *hdl, int set_clear)
{
	int ret;
	unsigned char buf[1];

	sr_info("setting CPU reset mode %s...",
		set_clear ? "on" : "off");
	buf[0] = set_clear ? 1 : 0;
	ret = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR, 0xa0,
				      0xe600, 0x0000, buf, 1, 100);
	if (ret < 0)
		sr_err("Unable to send control request: %s.",
				libusb_error_name(ret));

	return ret;
}

SR_PRIV int ezusb_install_firmware(struct sr_context *ctx,
				   libusb_device_handle *hdl,
				   const char *name)
{
	unsigned char *firmware;
	size_t length, offset, chunksize;
	int ret, result;

	/* Max size is 64 kiB since the value field of the setup packet,
	 * which holds the firmware offset, is only 16 bit wide.
	 */
	firmware = sr_resource_load(ctx, SR_RESOURCE_FIRMWARE,
			name, &length, 1 << 16);
	if (!firmware)
		return SR_ERR;

	sr_info("Uploading firmware '%s'.", name);

	result = SR_OK;
	offset = 0;
	while (offset < length) {
		chunksize = MIN(length - offset, FW_CHUNKSIZE);

		ret = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR |
					      LIBUSB_ENDPOINT_OUT, 0xa0, offset,
					      0x0000, firmware + offset,
					      chunksize, 100);
		if (ret < 0) {
			sr_err("Unable to send firmware to device: %s.",
					libusb_error_name(ret));
			g_free(firmware);
			return SR_ERR;
		}
		sr_info("Uploaded %zu bytes.", chunksize);
		offset += chunksize;
	}
	g_free(firmware);

	sr_info("Firmware upload done.");

	return result;
}

SR_PRIV int ezusb_upload_firmware(struct sr_context *ctx, libusb_device *dev,
				  int configuration, const char *name)
{
	struct libusb_device_handle *hdl;
	int ret;

	sr_info("uploading firmware to device on %d.%d",
		libusb_get_bus_number(dev), libusb_get_device_address(dev));

	if ((ret = libusb_open(dev, &hdl)) < 0) {
		sr_err("failed to open device: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

/*
 * The libusb Darwin backend is broken: it can report a kernel driver being
 * active, but detaching it always returns an error.
 */
#if !defined(__APPLE__)
	if (libusb_kernel_driver_active(hdl, 0) == 1) {
		if ((ret = libusb_detach_kernel_driver(hdl, 0)) < 0) {
			sr_err("failed to detach kernel driver: %s",
					libusb_error_name(ret));
			return SR_ERR;
		}
	}
#endif

	if ((ret = libusb_set_configuration(hdl, configuration)) < 0) {
		sr_err("Unable to set configuration: %s",
				libusb_error_name(ret));
		return SR_ERR;
	}

	if ((ezusb_reset(hdl, 1)) < 0)
		return SR_ERR;

	if (ezusb_install_firmware(ctx, hdl, name) < 0)
		return SR_ERR;

	if ((ezusb_reset(hdl, 0)) < 0)
		return SR_ERR;

	libusb_close(hdl);

	return SR_OK;
}
