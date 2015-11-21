/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2015 Daniel Elstner <daniel.kitta@gmail.com>
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

#include <config.h>
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

#if !HAVE_LIBUSB_OS_HANDLE
typedef int libusb_os_handle;
#endif

/** Custom GLib event source for libusb I/O.
 * @internal
 */
struct usb_source {
	GSource base;

	int64_t timeout_us;
	int64_t due_us;

	/* Needed to keep track of installed sources */
	struct sr_session *session;

	struct libusb_context *usb_ctx;
	GPtrArray *pollfds;
};

/** USB event source prepare() method.
 */
static gboolean usb_source_prepare(GSource *source, int *timeout)
{
	int64_t now_us, usb_due_us;
	struct usb_source *usource;
	struct timeval usb_timeout;
	int remaining_ms;
	int ret;

	usource = (struct usb_source *)source;

	ret = libusb_get_next_timeout(usource->usb_ctx, &usb_timeout);
	if (G_UNLIKELY(ret < 0)) {
		sr_err("Failed to get libusb timeout: %s",
			libusb_error_name(ret));
	}
	now_us = g_source_get_time(source);

	if (usource->due_us == 0) {
		/* First-time initialization of the expiration time */
		usource->due_us = now_us + usource->timeout_us;
	}
	if (ret == 1) {
		usb_due_us = (int64_t)usb_timeout.tv_sec * G_USEC_PER_SEC
				+ usb_timeout.tv_usec + now_us;
		if (usb_due_us < usource->due_us)
			usource->due_us = usb_due_us;
	}
	if (usource->due_us != INT64_MAX)
		remaining_ms = (MAX(0, usource->due_us - now_us) + 999) / 1000;
	else
		remaining_ms = -1;

	*timeout = remaining_ms;

	return (remaining_ms == 0);
}

/** USB event source check() method.
 */
static gboolean usb_source_check(GSource *source)
{
	struct usb_source *usource;
	GPollFD *pollfd;
	unsigned int revents;
	unsigned int i;

	usource = (struct usb_source *)source;
	revents = 0;

	for (i = 0; i < usource->pollfds->len; i++) {
		pollfd = g_ptr_array_index(usource->pollfds, i);
		revents |= pollfd->revents;
	}
	return (revents != 0 || (usource->due_us != INT64_MAX
			&& usource->due_us <= g_source_get_time(source)));
}

/** USB event source dispatch() method.
 */
static gboolean usb_source_dispatch(GSource *source,
		GSourceFunc callback, void *user_data)
{
	struct usb_source *usource;
	GPollFD *pollfd;
	unsigned int revents;
	unsigned int i;
	gboolean keep;

	usource = (struct usb_source *)source;
	revents = 0;
	/*
	 * This is somewhat arbitrary, but drivers use revents to distinguish
	 * actual I/O from timeouts. When we remove the user timeout from the
	 * driver API, this will no longer be needed.
	 */
	for (i = 0; i < usource->pollfds->len; i++) {
		pollfd = g_ptr_array_index(usource->pollfds, i);
		revents |= pollfd->revents;
	}
	if (revents != 0)
		sr_spew("%s: revents 0x%.2X", __func__, revents);
	else
		sr_spew("%s: timed out", __func__);

	if (!callback) {
		sr_err("Callback not set, cannot dispatch event.");
		return G_SOURCE_REMOVE;
	}
	keep = (*(sr_receive_data_callback)callback)(-1, revents, user_data);

	if (G_LIKELY(keep) && G_LIKELY(!g_source_is_destroyed(source))) {
		if (usource->timeout_us >= 0)
			usource->due_us = g_source_get_time(source)
					+ usource->timeout_us;
		else
			usource->due_us = INT64_MAX;
	}
	return keep;
}

/** USB event source finalize() method.
 */
static void usb_source_finalize(GSource *source)
{
	struct usb_source *usource;

	usource = (struct usb_source *)source;

	sr_spew("%s", __func__);

	libusb_set_pollfd_notifiers(usource->usb_ctx, NULL, NULL, NULL);

	g_ptr_array_unref(usource->pollfds);
	usource->pollfds = NULL;

	sr_session_source_destroyed(usource->session,
			usource->usb_ctx, source);
}

/** Callback invoked when a new libusb FD should be added to the poll set.
 */
static LIBUSB_CALL void usb_pollfd_added(libusb_os_handle fd,
		short events, void *user_data)
{
	struct usb_source *usource;
	GPollFD *pollfd;

	usource = user_data;

	if (G_UNLIKELY(g_source_is_destroyed(&usource->base)))
		return;

	pollfd = g_slice_new(GPollFD);
#ifdef G_OS_WIN32
	events = G_IO_IN;
#endif
	pollfd->fd = (gintptr)fd;
	pollfd->events = events;
	pollfd->revents = 0;

	g_ptr_array_add(usource->pollfds, pollfd);
	g_source_add_poll(&usource->base, pollfd);
}

/** Callback invoked when a libusb FD should be removed from the poll set.
 */
static LIBUSB_CALL void usb_pollfd_removed(libusb_os_handle fd, void *user_data)
{
	struct usb_source *usource;
	GPollFD *pollfd;
	unsigned int i;

	usource = user_data;

	if (G_UNLIKELY(g_source_is_destroyed(&usource->base)))
		return;

	/* It's likely that the removed poll FD is at the end.
	 */
	for (i = usource->pollfds->len; G_LIKELY(i > 0); i--) {
		pollfd = g_ptr_array_index(usource->pollfds, i - 1);

		if ((libusb_os_handle)pollfd->fd == fd) {
			g_source_remove_poll(&usource->base, pollfd);
			g_ptr_array_remove_index_fast(usource->pollfds, i - 1);
			return;
		}
	}
	sr_err("FD to be removed (%" G_GINTPTR_FORMAT
		") not found in event source poll set.", (gintptr)fd);
}

/** Destroy notify callback for FDs maintained by the USB event source.
 */
static void usb_source_free_pollfd(void *data)
{
	g_slice_free(GPollFD, data);
}

/** Create an event source for libusb I/O.
 *
 * TODO: The combination of the USB I/O source with a user timeout is
 * conceptually broken. The user timeout supplied here is completely
 * unrelated to I/O -- the actual I/O timeout is set when submitting
 * a USB transfer.
 * The sigrok drivers generally use the timeout to poll device state.
 * Usually, this polling can be sensibly done only when there is no
 * active USB transfer -- i.e. it's actually mutually exclusive with
 * waiting for transfer completion.
 * Thus, the user timeout should be removed from the USB event source
 * API at some point. Instead, drivers should install separate timer
 * event sources for their polling needs.
 *
 * @param session The session the event source belongs to.
 * @param usb_ctx The libusb context for which to handle events.
 * @param timeout_ms The timeout interval in ms, or -1 to wait indefinitely.
 * @return A new event source object, or NULL on failure.
 */
static GSource *usb_source_new(struct sr_session *session,
		struct libusb_context *usb_ctx, int timeout_ms)
{
	static GSourceFuncs usb_source_funcs = {
		.prepare  = &usb_source_prepare,
		.check    = &usb_source_check,
		.dispatch = &usb_source_dispatch,
		.finalize = &usb_source_finalize
	};
	GSource *source;
	struct usb_source *usource;
	const struct libusb_pollfd **upollfds, **upfd;

	upollfds = libusb_get_pollfds(usb_ctx);
	if (!upollfds) {
		sr_err("Failed to get libusb file descriptors.");
		return NULL;
	}
	source = g_source_new(&usb_source_funcs, sizeof(struct usb_source));
	usource = (struct usb_source *)source;

	g_source_set_name(source, "usb");

	if (timeout_ms >= 0) {
		usource->timeout_us = 1000 * (int64_t)timeout_ms;
		usource->due_us = 0;
	} else {
		usource->timeout_us = -1;
		usource->due_us = INT64_MAX;
	}
	usource->session = session;
	usource->usb_ctx = usb_ctx;
	usource->pollfds = g_ptr_array_new_full(8, &usb_source_free_pollfd);

	for (upfd = upollfds; *upfd != NULL; upfd++)
		usb_pollfd_added((*upfd)->fd, (*upfd)->events, usource);

#if (LIBUSB_API_VERSION >= 0x01000104)
	libusb_free_pollfds(upollfds);
#else
	free(upollfds);
#endif
	libusb_set_pollfd_notifiers(usb_ctx,
		&usb_pollfd_added, &usb_pollfd_removed, usource);

	return source;
}

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

	if (bus > 255) {
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

SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data)
{
	GSource *source;
	int ret;

	source = usb_source_new(session, ctx->libusb_ctx, timeout);
	if (!source)
		return SR_ERR;

	g_source_set_callback(source, (GSourceFunc)cb, cb_data, NULL);

	ret = sr_session_source_add_internal(session, ctx->libusb_ctx, source);
	g_source_unref(source);

	return ret;
}

SR_PRIV int usb_source_remove(struct sr_session *session, struct sr_context *ctx)
{
	return sr_session_source_remove_internal(session, ctx->libusb_ctx);
}

SR_PRIV int usb_get_port_path(libusb_device *dev, char *path, int path_len)
{
	uint8_t port_numbers[8];
	int i, n, len;

/*
 * FreeBSD requires that devices prior to calling libusb_get_port_numbers()
 * have been opened with libusb_open().
 * This apparently also applies to some Mac OS X versions.
 */
#if defined(__FreeBSD__) || defined(__APPLE__)
	struct libusb_device_handle *devh;
	if (libusb_open(dev, &devh) != 0)
		return SR_ERR;
#endif
	n = libusb_get_port_numbers(dev, port_numbers, sizeof(port_numbers));
#if defined(__FreeBSD__) || defined(__APPLE__)
	libusb_close(devh);
#endif

/* Workaround FreeBSD / Mac OS X libusb_get_port_numbers() returning 0. */
#if defined(__FreeBSD__) || defined(__APPLE__)
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
