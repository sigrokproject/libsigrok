/*
 * This file is part of the libsigrok project.
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
#include <memory.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/* SR_CONF_CONN takes one of these: */
#define CONN_USB_VIDPID  "^([0-9a-z]{4})\\.([0-9a-z]{4})$"
#define CONN_USB_BUSADDR "^(\\d+)\\.(\\d+)$"

#define LOG_PREFIX "usb"

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
		if ((mstr = g_match_info_fetch(match, 1)))
			vid = strtoul(mstr, NULL, 16);
		g_free(mstr);

		if ((mstr = g_match_info_fetch(match, 2)))
			pid = strtoul(mstr, NULL, 16);
		g_free(mstr);
		sr_dbg("Trying to find USB device with VID:PID = %04x:%04x.",
		       vid, pid);
	} else {
		g_match_info_unref(match);
		g_regex_unref(reg);
		reg = g_regex_new(CONN_USB_BUSADDR, 0, 0, NULL);
		if (g_regex_match(reg, conn, 0, &match)) {
			if ((mstr = g_match_info_fetch(match, 1)))
				bus = strtoul(mstr, NULL, 10);
			g_free(mstr);

			if ((mstr = g_match_info_fetch(match, 2)))
				addr = strtoul(mstr, NULL, 10);
			g_free(mstr);
			sr_dbg("Trying to find USB device with bus.address = "
			       "%d.%d.", bus, addr);
		}
	}
	g_match_info_unref(match);
	g_regex_unref(reg);

	if (vid + pid + bus + addr == 0) {
		sr_err("Neither VID:PID nor bus.address was specified.");
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

	sr_dbg("Trying to open USB device %d.%d.", usb->bus, usb->address);

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
		if (b != usb->bus || a != usb->address)
			continue;

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

SR_PRIV void sr_usb_close(struct sr_usb_dev_inst *usb)
{
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sr_dbg("Closed USB device %d.%d.", usb->bus, usb->address);
}

#ifdef _WIN32
/* Thread used to run libusb_wait_for_event() and set a pollable event. */
static gpointer usb_thread(gpointer data)
{
	struct sr_context *ctx = data;

	while (ctx->usb_thread_running) {
		/* Acquire event waiters lock, needed for libusb_wait_for_event(). */
		libusb_lock_event_waiters(ctx->libusb_ctx);
		/* Wait for any libusb event. The main thread can interrupt this wait
		 * by calling libusb_unlock_events(). */
		libusb_wait_for_event(ctx->libusb_ctx, NULL);
		/* Release event waiters lock. */
		libusb_unlock_event_waiters(ctx->libusb_ctx);
		/* Set event that the main loop will be polling on. */
		SetEvent(ctx->usb_wait_complete_event);
		/* Wait for the main thread to signal us to run again. */
		WaitForSingleObject(ctx->usb_wait_request_event, INFINITE);
		ResetEvent(ctx->usb_wait_request_event);
	}

	return NULL;
}

/* Callback wrapper run when main g_poll() gets a USB event or timeout. */
static int usb_callback(int fd, int revents, void *cb_data)
{
	struct sr_context *ctx = cb_data;
	int ret;

	/* Run registered callback to handle libusb events. */
	ret = ctx->usb_cb(fd, revents, ctx->usb_cb_data);

	/* Were we triggered by an event from the wait thread, rather than by a
	 * timeout? */
	int triggered_by_event = (revents & G_IO_IN);

	/* If so, and if the USB event source has not been removed from the
	 * session, reset the event that woke us and tell the wait thread to start
	 * waiting for events again. */
	if (triggered_by_event && ctx->usb_thread_running) {
		ResetEvent(ctx->usb_wait_complete_event);
		SetEvent(ctx->usb_wait_request_event);
	}

	return ret;
}
#endif

SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data)
{
	if (ctx->usb_source_present) {
		sr_err("A USB event source is already present.");
		return SR_ERR;
	}

#ifdef _WIN32
	/* Create events used to signal between main and USB wait threads. */
	ctx->usb_wait_request_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	ctx->usb_wait_complete_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	/* Start USB wait thread. */
	ctx->usb_thread_running = TRUE;
	ctx->usb_thread = g_thread_new("usb", usb_thread, ctx);
	/* Add event, set by USB wait thread, to session poll set. */
	ctx->usb_pollfd.fd = ctx->usb_wait_complete_event;
	ctx->usb_pollfd.events = G_IO_IN;
	ctx->usb_cb = cb;
	ctx->usb_cb_data = cb_data;
	sr_session_source_add_pollfd(session, &ctx->usb_pollfd, timeout,
			usb_callback, ctx);
#else
	const struct libusb_pollfd **lupfd;
	unsigned int i;

	lupfd = libusb_get_pollfds(ctx->libusb_ctx);
	for (i = 0; lupfd[i]; i++)
		sr_session_source_add(session, lupfd[i]->fd, lupfd[i]->events,
				timeout, cb, cb_data);
	free(lupfd);
#endif
	ctx->usb_source_present = TRUE;

	return SR_OK;
}

SR_PRIV int usb_source_remove(struct sr_session *session, struct sr_context *ctx)
{
	if (!ctx->usb_source_present)
		return SR_OK;

#ifdef _WIN32
	/* Signal the USB wait thread to stop, then unblock it so it does. */
	ctx->usb_thread_running = FALSE;
	SetEvent(ctx->usb_wait_request_event);
	libusb_unlock_events(ctx->libusb_ctx);
	/* Wait for USB wait thread to terminate. */
	g_thread_join(ctx->usb_thread);
	/* Remove USB event from session poll set. */
	sr_session_source_remove_pollfd(session, &ctx->usb_pollfd);
	/* Close event handles that were used between threads. */
	CloseHandle(ctx->usb_wait_request_event);
	CloseHandle(ctx->usb_wait_complete_event);
#else
	const struct libusb_pollfd **lupfd;
	unsigned int i;

	lupfd = libusb_get_pollfds(ctx->libusb_ctx);
	for (i = 0; lupfd[i]; i++)
		sr_session_source_remove(session, lupfd[i]->fd);
	free(lupfd);
#endif
	ctx->usb_source_present = FALSE;

	return SR_OK;
}

SR_PRIV int usb_get_port_path(libusb_device *dev, char *path, int path_len)
{
	uint8_t port_numbers[8];
	int i, n, len;

/*
 * FreeBSD requires that devices prior to calling libusb_get_port_numbers()
 * have been opened with libusb_open().
 */
#ifdef __FreeBSD__
	struct libusb_device_handle *devh;
	if (libusb_open(dev, &devh) != 0)
		return SR_ERR;
#endif
	n = libusb_get_port_numbers(dev, port_numbers, sizeof(port_numbers));
#ifdef __FreeBSD__
	libusb_close(devh);
#endif

/* Workaround FreeBSD libusb_get_port_numbers() returning 0. */
#ifdef __FreeBSD__
	if (n == 0) {
		port_numbers[0] = libusb_get_device_address(dev);
		n = 1;
	}
#endif
	if (n < 1)
		return SR_ERR;

	len = snprintf(path, path_len, "usb/%d-%d",
	               libusb_get_bus_number(dev), port_numbers[0]);

	for (i = 1; i < n; i++)
		len += snprintf(path+len, path_len-len, ".%d", port_numbers[i]);

	return SR_OK;
}
