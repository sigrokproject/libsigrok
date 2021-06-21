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

SR_PRIV int ezusb_fx3_ram_write (libusb_device_handle *hdl,
        unsigned char *buf,
        unsigned int   ramAddress,
        int            len)
{
    int r;
    int index = 0;
    int size;

    while (len > 0) {
        size = MIN(len ,FW_CHUNKSIZE);

        r = libusb_control_transfer (hdl, 0x40, 0xA0, GET_LSW(ramAddress), GET_MSW(ramAddress),
                &buf[index], size, 100);

        if (r != size) {
            sr_err ("Error: Vendor write to FX3 RAM failed\n");
            return SR_ERR;
        }
        ramAddress += size;
        index      += size;
        len        -= size;
    }

    return SR_OK;
}




SR_PRIV int ezusb_install_firmware_fx3(struct sr_context *ctx,
				   libusb_device_handle *hdl,
				   const char *name)
{
	unsigned char *firmware;
	size_t length, offset, chunksize,filesize;
	int ret, result;
	unsigned int  *data_p;
    unsigned int i, checksum;
    unsigned int address;
    int r, index;

	/* Max size of FX3 fw file is 256 KB  = 1 << 18*/
	firmware = sr_resource_load(ctx, SR_RESOURCE_FIRMWARE,
			name, &filesize, 1 << 18);
	if (!firmware)
		return SR_ERR;

	sr_info("Uploading FX3 firmware '%s'.", name);

	result = SR_OK;
	/* Run through each section of code, and use vendor commands to download them to RAM.*/
    index    = 4;
    checksum = 0;
    while (index < filesize) {
        data_p  = (unsigned int *)(firmware + index);
        length  = data_p[0];
        address = data_p[1];
        if (length != 0) {
            for (i = 0; i < length; i++)
                checksum += data_p[2 + i];
            r = ezusb_fx3_ram_write (hdl, firmware + index + 8, address, length * 4);
            if (r != 0) {
                sr_err ("Error: Failed to download data to FX3 RAM\n");
                free (firmware);
                return SR_ERR;
            }
        } else {
            if (checksum != data_p[2]) {
                sr_err ("Error: Checksum error in firmware binary\n");
                free (firmware);
                return SR_ERR;
            }
            r = libusb_control_transfer (hdl, 0x40, 0xA0, GET_LSW(address), GET_MSW(address), NULL,
                    0, 100);
            if (r != 0)
                printf ("Info: Ignored error in control transfer: %d\n", r);
            break;
        }
        index += (8 + length * 4);
    }
	g_free(firmware);

	sr_info("Firmware upload done.");
	return result;
}


SR_PRIV int ezusb_upload_firmware_fx3(struct sr_context *ctx, libusb_device *dev,
				  int configuration, const char *name)
{
	struct libusb_device_handle *hdl;
	int ret;

	sr_info("uploading firmware to FX3 device on %d.%d",
		libusb_get_bus_number(dev), libusb_get_device_address(dev));

	if ((ret = libusb_open(dev, &hdl)) < 0) {
		sr_err("failed to open FX3 device: %s.", libusb_error_name(ret));
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

	if (ezusb_install_firmware_fx3(ctx, hdl, name) < 0)
		return SR_ERR;
	
	libusb_close(hdl);

	return SR_OK;
}




