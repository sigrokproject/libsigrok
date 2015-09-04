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

#ifdef G_OS_WIN32
/*
 * USB callback wrapper run when the main loop is idle.
 */
static int usb_callback(int fd, int revents, void *cb_data)
{
	int64_t start_time, stop_time, due, timeout;
	struct timeval tv;
	struct sr_context *ctx;
	int ret;

	(void)fd;
	(void)revents;
	ctx = cb_data;

	start_time = g_get_monotonic_time();

	due = ctx->usb_due;
	timeout = MAX(due - start_time, 0);
	tv.tv_sec  = timeout / G_USEC_PER_SEC;
	tv.tv_usec = timeout % G_USEC_PER_SEC;

	sr_spew("libusb_handle_events enter, timeout %g ms", 1e-3 * timeout);

	ret = libusb_handle_events_timeout_completed(ctx->libusb_ctx,
			(ctx->usb_timeout < 0) ? NULL : &tv, NULL);
	if (ret != 0) {
		/* Warn but still invoke the callback, to give the driver
		 * a chance to deal with the problem.
		 */
		sr_warn("Error handling libusb event (%s)",
			libusb_error_name(ret));
	}
	stop_time = g_get_monotonic_time();

	sr_spew("libusb_handle_events leave, %g ms elapsed",
		1e-3 * (stop_time - start_time));

	if (ctx->usb_timeout >= 0)
		ctx->usb_due = stop_time + ctx->usb_timeout;
	/*
	 * Run registered callback to execute any follow-up activity
	 * to libusb event handling.
	 */
	return ctx->usb_cb(-1, (stop_time < due) ? G_IO_IN : 0,
			ctx->usb_cb_data);
}
#endif

SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data)
{
	int ret;

	if (ctx->usb_source_present) {
		sr_err("A USB event source is already present.");
		return SR_ERR;
	}

#ifdef G_OS_WIN32
	if (timeout >= 0) {
		ctx->usb_timeout = INT64_C(1000) * timeout;
		ctx->usb_due = g_get_monotonic_time() + ctx->usb_timeout;
	} else {
		ctx->usb_timeout = -1;
		ctx->usb_due = INT64_MAX;
	}
	ctx->usb_cb = cb;
	ctx->usb_cb_data = cb_data;
	/*
	 * TODO: Install an idle source which will fire permanently, and block
	 * in a wrapper callback until any libusb events have been processed.
	 * This will have to do for now, until we implement a proper way to
	 * deal with libusb events on Windows.
	 */
	ret = sr_session_source_add(session, -2, 0, 0, &usb_callback, ctx);
#else
	const struct libusb_pollfd **lupfd;
	GPollFD *pollfds;
	int i;
	int num_fds = 0;

	lupfd = libusb_get_pollfds(ctx->libusb_ctx);
	if (!lupfd || !lupfd[0]) {
		free(lupfd);
		sr_err("Failed to get libusb file descriptors.");
		return SR_ERR;
	}
	while (lupfd[num_fds])
		++num_fds;
	pollfds = g_new(GPollFD, num_fds);

	for (i = 0; i < num_fds; ++i) {
		pollfds[i].fd = lupfd[i]->fd;
		pollfds[i].events = lupfd[i]->events;
		pollfds[i].revents = 0;
	}
	free(lupfd);
	ret = sr_session_source_add_internal(session, pollfds, num_fds,
			timeout, cb, cb_data, (gintptr)ctx->libusb_ctx);
	g_free(pollfds);
#endif
	ctx->usb_source_present = (ret == SR_OK);

	return ret;
}

SR_PRIV int usb_source_remove(struct sr_session *session, struct sr_context *ctx)
{
	int ret;

	if (!ctx->usb_source_present)
		return SR_OK;

#ifdef G_OS_WIN32
	/* Remove our idle source */
	sr_session_source_remove(session, -2);
#else
	ret = sr_session_source_remove_internal(session,
			(gintptr)ctx->libusb_ctx);
#endif
	ctx->usb_source_present = FALSE;

	return ret;
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
