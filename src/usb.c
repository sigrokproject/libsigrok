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
#define CONN_USB_VIDPID  "^([0-9a-fA-F]{4})\\.([0-9a-fA-F]{4})$"
#define CONN_USB_BUSADDR "^(\\d+)\\.(\\d+)$"

#define LOG_PREFIX "usb"

// Structure that will be passed to the poll libusb callback and to its destructor
struct poll_libusb_callback_arg
{
	struct sr_session *session; ///< Pointer to Sigrok session struct
	libusb_context * libusb_ctx; ///< Pointer to libusb context
	GSource * usb_source; ///< Pointer to USB idle source itself
	sr_receive_data_callback cb; ///< Callback to driver each poll
	void *cb_data; ///< Arg for callback to driver
};

// Glib source callback which polls libusb.  This will, in turn,
// invoke any callbacks defined by the hardware layer.
static gboolean poll_libusb_callback(gpointer user_data_ptr)
{
	struct poll_libusb_callback_arg * callback_arg = (struct poll_libusb_callback_arg *)user_data_ptr;

	// Wait for something to happen on the USB descriptors.
	libusb_handle_events_completed(callback_arg->libusb_ctx, NULL);

	// Poll driver if it has a callback
	if(callback_arg->cb != NULL)
	{
		// As far as I can tell, the first 2 parameters to this callback are not used for USB drivers
		if(!callback_arg->cb(-1, 0, callback_arg->cb_data))
        {
            return G_SOURCE_REMOVE;
        }
	}

	return G_SOURCE_CONTINUE;
}

// Destroy callback for USB sources
static void usb_source_destroyed(gpointer user_data_ptr)
{
	struct poll_libusb_callback_arg * callback_arg = (struct poll_libusb_callback_arg *)user_data_ptr;

	// Callback to sr_session that a source was destroyed
	sr_session_source_destroyed(callback_arg->session, callback_arg->libusb_ctx, callback_arg->usb_source);

	g_free(user_data_ptr);
}

/**
 * Extract VID:PID or bus.addr from a connection string.
 *
 * @param[in] conn Connection string.
 * @param[out] vid Pointer to extracted vendor ID. Can be #NULL.
 * @param[out] pid Pointer to extracted product ID. Can be #NULL.
 * @param[out] bus Pointer to extracted bus number. Can be #NULL.
 * @param[out] addr Pointer to extracted device number. Can be #NULL.
 *
 * @return SR_OK when parsing succeeded, SR_ERR* otherwise.
 *
 * @private
 *
 * The routine fills in the result variables, and returns the scan success
 * in the return code. Callers can specify #NULL for variable references
 * if they are not interested in specific aspects of the USB address.
 */
SR_PRIV int sr_usb_split_conn(const char *conn,
	uint16_t *vid, uint16_t *pid, uint8_t *bus, uint8_t *addr)
{
	gboolean valid;
	GRegex *reg;
	GMatchInfo *match;
	char *mstr;
	uint32_t num;

	if (vid) *vid = 0;
	if (pid) *pid = 0;
	if (bus) *bus = 0;
	if (addr) *addr = 0;

	valid = TRUE;
	reg = g_regex_new(CONN_USB_VIDPID, 0, 0, NULL);
	if (g_regex_match(reg, conn, 0, &match)) {
		/* Found a VID:PID style pattern. */
		if ((mstr = g_match_info_fetch(match, 1))) {
			num = strtoul(mstr, NULL, 16);
			if (num > 0xffff)
				valid = FALSE;
			if (vid)
				*vid = num & 0xffff;
		}
		g_free(mstr);

		if ((mstr = g_match_info_fetch(match, 2))) {
			num = strtoul(mstr, NULL, 16);
			if (num > 0xffff)
				valid = FALSE;
			if (pid)
				*pid = num & 0xffff;
		}
		g_free(mstr);
	} else {
		g_match_info_unref(match);
		g_regex_unref(reg);
		reg = g_regex_new(CONN_USB_BUSADDR, 0, 0, NULL);
		if (g_regex_match(reg, conn, 0, &match)) {
			/* Found a bus.address style pattern. */
			if ((mstr = g_match_info_fetch(match, 1))) {
				num = strtoul(mstr, NULL, 10);
				if (num > 255)
					valid = FALSE;
				if (bus)
					*bus = num & 0xff;
			}
			g_free(mstr);

			if ((mstr = g_match_info_fetch(match, 2))) {
				num = strtoul(mstr, NULL, 10);
				if (num > 127)
					valid = FALSE;
				if (addr)
					*addr = num & 0x7f;
			}
			g_free(mstr);
		}
	}
	g_match_info_unref(match);
	g_regex_unref(reg);

	return valid ? SR_OK : SR_ERR_ARG;
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
	uint16_t vid, pid;
	uint8_t bus, addr;
	int b, a, ret, i;

	ret = sr_usb_split_conn(conn, &vid, &pid, &bus, &addr);
	if (ret != SR_OK) {
		sr_err("Invalid input, or neither VID:PID nor bus.address specified.");
		return NULL;
	}
	if (!(vid && pid) && !(bus && addr)) {
		sr_err("Could neither determine VID:PID nor bus.address numbers.");
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

		if (vid && pid && (des.idVendor != vid || des.idProduct != pid))
			continue;

		b = libusb_get_bus_number(devlist[i]);
		a = libusb_get_device_address(devlist[i]);
		if (bus && addr && (b != bus || a != addr))
			continue;

		sr_dbg("Found USB device (VID:PID = %04x:%04x, bus.address = "
			   "%d.%d).", des.idVendor, des.idProduct, b, a);

		usb = sr_usb_dev_inst_new(b, a, NULL);
		devices = g_slist_append(devices, usb);
	}
	libusb_free_device_list(devlist, 1);

	/* No log message for #devices found (caller will log that). */

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

/**
 * @brief Add the source of USB events to a session.  This event source will poll libusb
 *   each time the main loop executes so that it can process events and deliver callbacks.
 *
 * @note In the current version of sigrok, the USB source polls libusb for events automatically.  If all you want to
 *     do is poll libusb so it delievers callbacks, you do not need to register a callback.  And if you do register
 *     a callback, that callback no longer needs to poll libusb.
 *
 * @param session Session to use
 * @param ctx Sigrok context
 * @param timeout Timeout.  Currently unused by this function.
 * @param cb Callback for your hardware layer to use.  This callback will be polled each time libusb sees
 *     activity on the USB port (i.e. each time \c libusb_handle_events_completed() returns).
 *     You can use it to monitor the status of your device (though you may wish to use libusb callbacks instead).
 *     In the callback, the \c fd and\c revents parameters are unused and will be -1 and 0.
 *     If the callback returns false, the USB source will be removed from the main loop.
 * @param cb_data User data pointer passed to the callback when executed.
 *
 * @return Error code or success
 */
SR_PRIV int usb_source_add(struct sr_session *session, struct sr_context *ctx,
		int timeout, sr_receive_data_callback cb, void *cb_data)
{
	(void)timeout;

	int ret;

	// Set up argument
	struct poll_libusb_callback_arg * callback_arg = g_malloc0(sizeof(struct poll_libusb_callback_arg));
	callback_arg->session = session;
	callback_arg->libusb_ctx = ctx->libusb_ctx;
	callback_arg->cb = cb;
	callback_arg->cb_data = cb_data;

	// Create idle source to poll libusb.
	// Despite the name "idle", this really just means a source which is polled every cycle of the main loop.
	GSource *source = g_idle_source_new();
	callback_arg->usb_source = source;

	g_source_set_priority(source, G_PRIORITY_DEFAULT); // Increase priority to DEFAULT instead of IDLE
	g_source_set_name(source, "usb");
	g_source_set_callback(source, poll_libusb_callback, callback_arg, usb_source_destroyed);

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

/**
 * Check the USB configuration to determine if this device has a given
 * manufacturer and product string.
 *
 * @return TRUE if the device's configuration profile strings
 *         configuration, FALSE otherwise.
 */
SR_PRIV gboolean usb_match_manuf_prod(libusb_device *dev,
		const char *manufacturer, const char *product)
{
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	gboolean ret;
	unsigned char strdesc[64];

	hdl = NULL;
	ret = FALSE;
	while (!ret) {
		/* Assume the FW has not been loaded, unless proven wrong. */
		libusb_get_device_descriptor(dev, &des);

		if (libusb_open(dev, &hdl) != 0)
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, manufacturer))
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, product))
			break;

		ret = TRUE;
	}
	if (hdl)
		libusb_close(hdl);

	return ret;
}
