/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "protocol.h"

#define FW_BUFSIZE 4096
int dslogic_fpga_firmware_upload(struct libusb_device_handle *hdl,
		const char *filename)
{
	FILE *fw;
	struct stat st;
	int chunksize, result, ret;
	unsigned char *buf;
	int sum, transferred;

	sr_info("Uploading FPGA firmware at %s.", filename);

	if (stat(filename, &st) < 0) {
		sr_err("Unable to upload FPGA firmware: %s", strerror(errno));
		return SR_ERR;
	}

	/* Tell the device firmware is coming. */
	if ((ret = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, CMD_DSLOGIC_CONFIG, 0x0000, 0x0000,
			NULL, 0, 3000)) < 0) {
		sr_err("Failed to upload FPGA firmware: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	buf = g_malloc(FW_BUFSIZE);

	if ((fw = g_fopen(filename, "rb")) == NULL) {
		sr_err("Unable to open %s for reading: %s.", filename, strerror(errno));
		return SR_ERR;
	}

	/* Give the FX2 time to get ready for FPGA firmware upload. */
	g_usleep(10 * 1000);

	sum = 0;
	result = SR_OK;
	while (1) {
		if ((chunksize = fread(buf, 1, FW_BUFSIZE, fw)) == 0)
			break;

		if ((ret = libusb_bulk_transfer(hdl, 2 | LIBUSB_ENDPOINT_OUT,
				buf, chunksize, &transferred, 1000)) < 0) {
			sr_err("Unable to configure FPGA firmware: %s.",
					libusb_error_name(ret));
			result = SR_ERR;
			break;
		}
		sum += transferred;
		sr_info("Uploaded %d/%d bytes.", sum, st.st_size);

		if (transferred != chunksize) {
			sr_err("Short transfer while uploading FPGA firmware.");
			result = SR_ERR;
			break;
		}
	}
	fclose(fw);
	if (result == SR_OK)
		sr_info("FPGA firmware upload done.");

	return result;
}

