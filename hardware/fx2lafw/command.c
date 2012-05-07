/*
 * This file is part of the sigrok project.
 *
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

#include <libusb.h>

#include "fx2lafw.h"
#include "command.h"
#include "sigrok.h"
#include "sigrok-internal.h"

SR_PRIV int command_get_fw_version(libusb_device_handle *devhdl,
				   struct version_info *vi)
{
	int ret;

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, CMD_GET_FW_VERSION, 0x0000, 0x0000,
		(unsigned char *)vi, sizeof(struct version_info), 100);

	if (ret < 0) {
		sr_err("fx2lafw: Unable to get version info: %d.", ret);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int command_get_revid_version(libusb_device_handle *devhdl,
				      uint8_t *revid)
{
	int ret;

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, CMD_GET_REVID_VERSION, 0x0000, 0x0000,
		revid, 1, 100);

	if (ret < 0) {
		sr_err("fx2lafw: Unable to get REVID: %d.", ret);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int command_start_acquisition(libusb_device_handle *devhdl,
				      uint64_t samplerate,
				      bool samplewide)
{
	struct cmd_start_acquisition cmd;
	int delay = 0, ret;

	/* Compute the sample rate. */
	if ((SR_MHZ(48) % samplerate) == 0) {
		cmd.flags = CMD_START_FLAGS_CLK_48MHZ;
		delay = SR_MHZ(48) / samplerate - 1;
		if (delay > MAX_SAMPLE_DELAY)
			delay = 0;
	}

	if (delay == 0 && (SR_MHZ(30) % samplerate) == 0) {
		cmd.flags = CMD_START_FLAGS_CLK_30MHZ;
		delay = SR_MHZ(30) / samplerate - 1;
	}

	sr_info("fx2lafw: GPIF delay = %d, clocksource = %sMHz", delay,
		(cmd.flags & CMD_START_FLAGS_CLK_48MHZ) ? "48" : "30");

	if (delay <= 0 || delay > MAX_SAMPLE_DELAY) {
		sr_err("fx2lafw: Unable to sample at %" PRIu64 "Hz.",
		       samplerate);
		return SR_ERR;
	}

	cmd.sample_delay_h = (delay >> 8) & 0xff;
	cmd.sample_delay_l = delay & 0xff;

	/* Select the sampling width */
	cmd.flags |= samplewide ? CMD_START_FLAGS_SAMPLE_16BIT :
		CMD_START_FLAGS_SAMPLE_8BIT;

	/* Send the control message. */
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, CMD_START, 0x0000, 0x0000,
			(unsigned char *)&cmd, sizeof(cmd), 100);
	if (ret < 0) {
		sr_err("fx2lafw: Unable to send start command: %d.", ret);
		return SR_ERR;
	}

	return SR_OK;
}
