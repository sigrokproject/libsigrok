/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <glib.h>
#include <libusb.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* SR_HWCAP_CONN takes one of these: */
#define CONN_USB_VIDPID  "^([0-9a-z]{1,4})\\.([0-9a-z]{1,4})$"
#define CONN_USB_BUSADDR "^(\\d+)\\.(\\d+)$"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "usb: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

/**
 * Find USB devices according to a connection string.
 *
 * @param usb_ctx libusb context to use while scanning.
 * @param conn Connection string specifying the device(s) to match. This
 * can be of the form "<bus>.<address>", or "<vendorid>.<productid>".
 *
 * @return A GSList of struct sr_usb_dev_inst, with bus and address fields
 * matching the device that matched the connection string. The GSList and
 * its contents must be freed by the caller.
 */
SR_PRIV GSList *sr_usb_find(libusb_context *usb_ctx, const char *conn)
{
	struct sr_usb_dev_inst *usb;
	struct libusb_device **devlist;
	struct libusb_device_descriptor des;
	GSList *devices;
	GRegex *reg;
	GMatchInfo *match;
	int vid, pid, bus, addr, b, a, ret, i;
	char *mstr;

	vid = pid = bus = addr = 0;
	reg = g_regex_new(CONN_USB_VIDPID, 0, 0, NULL);
	if (g_regex_match(reg, conn, 0, &match)) {
		/* Extract VID. */
		if ((mstr = g_match_info_fetch(match, 1))) {
			vid = strtoul(mstr, NULL, 16);
			sr_spew("Extracted VID 0x%04x.", vid);
		}
		g_free(mstr);

		/* Extract PID. */
		if ((mstr = g_match_info_fetch(match, 2))) {
			pid = strtoul(mstr, NULL, 16);
			sr_spew("Extracted PID 0x%04x.", pid);
		}
		g_free(mstr);
		sr_dbg("Trying to find USB device with VID:PID = %04x:%04x.",
		       vid, pid);
	} else {
		g_match_info_unref(match);
		g_regex_unref(reg);
		reg = g_regex_new(CONN_USB_BUSADDR, 0, 0, NULL);
		if (g_regex_match(reg, conn, 0, &match)) {
			/* Extract bus. */
			if ((mstr = g_match_info_fetch(match, 1))) {
				bus = strtoul(mstr, NULL, 16);
				sr_spew("Extracted bus %d.", bus);
			}
			g_free(mstr);

			/* Extract address. */
			if ((mstr = g_match_info_fetch(match, 2))) {
				addr = strtoul(mstr, NULL, 16);
				sr_spew("Extracted address %d.", addr);
			}
			g_free(mstr);
			sr_dbg("Trying to find USB device with bus.address = "
			       "%d.%d.", bus, addr);
		}
	}
	g_match_info_unref(match);
	g_regex_unref(reg);

	if (vid + pid + bus + addr == 0) {
		sr_err("Neither VID:PID nor bus.address was found.");
		return NULL;
	}

	if (bus > 64) {
		sr_err("Invalid bus specified: %d.", bus);
		return NULL;
	}

	if (addr > 127) {
		sr_err("Invalid address specified: %d.", addr);
		return NULL;
	}

	/* Looks like a valid USB device specification, but is it connected? */
	devices = NULL;
	libusb_get_device_list(usb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}

		if (vid + pid && (des.idVendor != vid || des.idProduct != pid))
			continue;

		b = libusb_get_bus_number(devlist[i]);
		a = libusb_get_device_address(devlist[i]);
		if (bus + addr && (b != bus || a != addr))
			continue;

		sr_dbg("Found USB device (VID:PID = %04x:%04x, bus.address = "
		       "%d.%d).", des.idVendor, des.idProduct, b, a);

		usb = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
		devices = g_slist_append(devices, usb);
	}
	libusb_free_device_list(devlist, 1);

	sr_dbg("Found %d device(s).", g_slist_length(devices));
	
	return devices;
}

SR_PRIV int sr_usb_open(libusb_context *usb_ctx, struct sr_usb_dev_inst *usb)
{
	struct libusb_device **devlist;
	struct libusb_device_descriptor des;
	int ret, r, cnt, i, a, b;

	sr_dbg("Trying to open USB device.");

	if ((cnt = libusb_get_device_list(usb_ctx, &devlist)) < 0) {
		sr_err("Failed to retrieve device list: %s.",
		       libusb_error_name(cnt));
		return SR_ERR;
	}

	ret = SR_ERR;
	for (i = 0; i < cnt; i++) {
		if ((r = libusb_get_device_descriptor(devlist[i], &des)) < 0) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(r));
			continue;
		}

		b = libusb_get_bus_number(devlist[i]);
		a = libusb_get_device_address(devlist[i]);

		if (b != usb->bus || a != usb->address) {
			sr_spew("bus.address = %d.%d does not match.", b, a);
			continue;
		}

		if ((r = libusb_open(devlist[i], &usb->devhdl)) < 0) {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(r));
			break;
		}

		sr_dbg("Opened USB device (VID:PID = %04x:%04x, bus.address = "
		       "%d.%d).", des.idVendor, des.idProduct, b, a);

		ret = SR_OK;
		break;
	}

	libusb_free_device_list(devlist, 1);

	return ret;
}
