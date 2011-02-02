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

#include "config.h"
#include <stdint.h>
#include <glib.h>
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#endif
#include <sigrok.h>

#ifdef HAVE_LIBUSB_1_0

int opendev2(int device_index, struct sr_device_instance **sdi,
	     libusb_device *dev, struct libusb_device_descriptor *des,
	     int *skip, uint16_t vid, uint16_t pid, int interface)
{
	int err;

	if ((err = libusb_get_device_descriptor(dev, des))) {
		g_warning("failed to get device descriptor: %d", err);
		return -1;
	}

	if (des->idVendor != vid || des->idProduct != pid)
		return 0;

	if (*skip != device_index) {
		/* Skip devices of this type that aren't the one we want. */
		*skip += 1;
		return 0;
	}

	/*
	 * Should check the bus here, since we know that already. But what are
	 * we going to do if it doesn't match after the right number of skips?
	 */
	if (!(err = libusb_open(dev, &((*sdi)->usb->devhdl)))) {
		(*sdi)->usb->address = libusb_get_device_address(dev);
		(*sdi)->status = SR_ST_ACTIVE;
		g_message("opened device %d on %d.%d interface %d",
			  (*sdi)->index, (*sdi)->usb->bus,
			  (*sdi)->usb->address, interface);
	} else {
		g_warning("failed to open device: %d", err);
		*sdi = NULL;
	}

	return 0;
}

int opendev3(struct sr_device_instance **sdi, libusb_device *dev,
	     struct libusb_device_descriptor *des,
	     uint16_t vid, uint16_t pid, int interface)
{
	int err;

	if ((err = libusb_get_device_descriptor(dev, des))) {
		g_warning("failed to get device descriptor: %d", err);
		return -1;
	}

	if (des->idVendor != vid || des->idProduct != pid)
		return 0;

	if (libusb_get_bus_number(dev) == (*sdi)->usb->bus
	    && libusb_get_device_address(dev) == (*sdi)->usb->address) {
		/* Found it. */
		if (!(err = libusb_open(dev, &((*sdi)->usb->devhdl)))) {
			(*sdi)->status = SR_ST_ACTIVE;
			g_message("opened device %d on %d.%d interface %d",
				  (*sdi)->index, (*sdi)->usb->bus,
				  (*sdi)->usb->address, interface);
		} else {
			g_warning("failed to open device: %d", err);
			*sdi = NULL;
		}
	}

	return 0;
}

#endif
