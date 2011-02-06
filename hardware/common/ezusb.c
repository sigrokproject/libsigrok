/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include <libusb.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "config.h"

int ezusb_reset(struct libusb_device_handle *hdl, int set_clear)
{
	int err;
	unsigned char buf[1];

	g_message("setting CPU reset mode %s...", set_clear ? "on" : "off");
	buf[0] = set_clear ? 1 : 0;
	err = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR, 0xa0,
				      0xe600, 0x0000, buf, 1, 100);
	if (err < 0)
		g_warning("Unable to send control request: %d", err);

	return err;
}

int ezusb_install_firmware(libusb_device_handle *hdl, const char *filename)
{
	FILE *fw;
	int offset, chunksize, err, result;
	unsigned char buf[4096];

	g_message("Uploading firmware at %s", filename);
	if ((fw = g_fopen(filename, "rb")) == NULL) {
		g_warning("Unable to open firmware file %s for reading: %s",
			  filename, strerror(errno));
		return 1;
	}

	result = offset = 0;
	while (1) {
		chunksize = fread(buf, 1, 4096, fw);
		if (chunksize == 0)
			break;
		err = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR |
					      LIBUSB_ENDPOINT_OUT, 0xa0, offset,
					      0x0000, buf, chunksize, 100);
		if (err < 0) {
			g_warning("Unable to send firmware to device: %d", err);
			result = 1;
			break;
		}
		g_message("Uploaded %d bytes", chunksize);
		offset += chunksize;
	}
	fclose(fw);
	g_message("Firmware upload done");

	return result;
}

int ezusb_upload_firmware(libusb_device *dev, int configuration,
			  const char *filename)
{
	struct libusb_device_handle *hdl;
	int err;

	g_message("uploading firmware to device on %d.%d",
		  libusb_get_bus_number(dev), libusb_get_device_address(dev));

	err = libusb_open(dev, &hdl);
	if (err != 0) {
		g_warning("failed to open device: %d", err);
		return 1;
	}

	err = libusb_set_configuration(hdl, configuration);
	if (err != 0) {
		g_warning("Unable to set configuration: %d", err);
		return 1;
	}

	if ((ezusb_reset(hdl, 1)) < 0)
		return 1;

	if (ezusb_install_firmware(hdl, filename) != 0)
		return 1;

	if ((ezusb_reset(hdl, 0)) < 0)
		return 1;

	libusb_close(hdl);

	return 0;
}
