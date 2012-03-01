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

#include "command.h"
#include "sigrok.h"
#include "sigrok-internal.h"

int command_start_acquisition(libusb_device_handle *devhdl)
{
	const int res = libusb_control_transfer(devhdl,
				LIBUSB_REQUEST_TYPE_VENDOR |
				LIBUSB_ENDPOINT_OUT, CMD_START, 0x0000,
				0x0000, NULL, 0, 100);
	if (res < 0) {
		sr_err("fx2lafw: Unable to send start command: %d",
			res);
		return SR_ERR;
	}

	return SR_OK;
}
