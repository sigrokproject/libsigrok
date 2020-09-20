/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017-2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include <glib.h>
#ifdef HAVE_LIBHIDAPI
#include <hidapi.h>
#endif
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "serial_hid.h"
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h> /* for HANDLE */
#endif

#define LOG_PREFIX "serial-hid"

#ifdef HAVE_SERIAL_COMM

/**
 * @file
 *
 * Serial port handling, HIDAPI library specific support code.
 */

/**
 * @defgroup grp_serial_hid Serial port handling, HID group
 *
 * Make serial-over-HID communication appear like a regular serial port.
 *
 * @{
 */

#ifdef HAVE_LIBHIDAPI
/* {{{ helper routines */

/* Strip off parity bits for "odd" data bit counts like in 7e1 frames. */
static void ser_hid_mask_databits(struct sr_serial_dev_inst *serial,
	uint8_t *data, size_t len)
{
	uint32_t mask32;
	uint8_t mask;
	size_t idx;

	if ((serial->comm_params.data_bits % 8) == 0)
		return;

	mask32 = (1UL << serial->comm_params.data_bits) - 1;
	mask = mask32 & 0xff;
	for (idx = 0; idx < len; idx++)
		data[idx] &= mask;
}

/* }}} */
/* {{{ open/close/list/find HIDAPI connection, exchange HID requests and data */

#define IOKIT_PATH_PREFIX	"IOService:"

/*
 * Convert a HIDAPI path (which depends on the target platform, and may
 * depend on one of several available API variants on that platform) to
 * something that is usable as a "port name" in conn= specs.
 *
 * Since conn= is passed with -d where multiple options (among them conn=)
 * are separated by colons, port names themselves cannot contain colons.
 *
 * Just replace colons by a period in the simple case (Linux platform,
 * hidapi-libusb implementation, bus/address/interface). Prefix the
 * HIDAPI path in the complex cases (Linux hidapi-hidraw, Windows, Mac).
 * Paths with colons outside of libusb based implementations are unhandled
 * here, but were not yet seen on any sigrok supported platform either.
 * So just reject them.
 */
static char *get_hidapi_path_copy(const char *path)
{
	static const char *accept = "0123456789abcdefABCDEF:";
	static const char *keep = "0123456789abcdefABCDEF";

	int has_colon;
	int is_hex_colon;
	const char *parse, *remain;
	char *copy;

	parse = path;
	has_colon = strchr(parse, ':') != NULL;
	is_hex_colon = strspn(parse, accept) == strlen(parse);
	if (is_hex_colon) {
		/* All hex digits and colon only. Simple substitution. */
		copy = g_strdup_printf("%s%s", SER_HID_USB_PREFIX, parse);
		g_strcanon(copy + strlen(SER_HID_USB_PREFIX), keep, '.');
		return copy;
	}
	if (!has_colon) {
		/* "Something raw" and no colon. Add raw= prefix. */
		copy = g_strdup_printf("%s%s", SER_HID_RAW_PREFIX, parse);
		return copy;
	}
	if (g_str_has_prefix(parse, IOKIT_PATH_PREFIX)) do {
		/*
		 * Path starts with Mac IOKit literal which contains the
		 * colon. Drop that literal from the start of the path,
		 * and check whether any colon remains which we cannot
		 * deal with. Fall though to other approaches which could
		 * be more generic, or to the error path.
		 */
		remain = &parse[strlen(IOKIT_PATH_PREFIX)];
		if (strchr(remain, ':'))
			break;
		copy = g_strdup_printf("%s%s", SER_HID_IOKIT_PREFIX, remain);
		return copy;
	} while (0);

	/* TODO
	 * Consider adding support for more of the currently unhandled
	 * cases. When we get here, the HIDAPI path could be arbitrarily
	 * complex, none of the above "straight" approaches took effect.
	 * Proper escaping or other transformations could get applied,
	 * though they decrease usability the more they obfuscate the
	 * resulting port name. Ideally users remain able to recognize
	 * their device or cable or port after the manipulation.
	 */
	sr_err("Unsupported HIDAPI path format: %s", path);
	return NULL;
}

/*
 * Undo the port name construction that was done during scan. Extract
 * the HIDAPI path from a conn= input spec (the part after the hid/
 * prefix and chip type).
 *
 * Strip off the "raw" prefix, or undo colon substitution. See @ref
 * get_hidapi_path_copy() for details.
 */
static char *extract_hidapi_path(const char *copy)
{
	static const char *keep = "0123456789abcdefABCDEF:";

	const char *p;
	char *path;

	p = copy;
	if (!p || !*p)
		return NULL;

	if (g_str_has_prefix(p, SER_HID_IOKIT_PREFIX)) {
		p += strlen(SER_HID_IOKIT_PREFIX);
		path = g_strdup_printf("%s%s", IOKIT_PATH_PREFIX, p);
		return path;
	}
	if (g_str_has_prefix(p, SER_HID_RAW_PREFIX)) {
		p += strlen(SER_HID_RAW_PREFIX);
		path = g_strdup(p);
		return path;
	}
	if (g_str_has_prefix(p, SER_HID_USB_PREFIX)) {
		p += strlen(SER_HID_USB_PREFIX);
		path = g_strdup(p);
		g_strcanon(path, keep, ':');
		return path;
	}

	return NULL;
}

/*
 * The HIDAPI specific list() callback, invoked by common serial.c code.
 * Enumerate all devices (no VID:PID is involved).
 * Invoke an 'append' callback with "path" and "name".
 */
static GSList *ser_hid_hidapi_list(GSList *list, sr_ser_list_append_t append)
{
	struct hid_device_info *devs, *curdev;
	const char *chipname;
	char *path, *name;
	wchar_t *manuf, *prod, *serno;
	uint16_t vid, pid;
	GString *desc;

	devs = hid_enumerate(0x0000, 0x0000);
	for (curdev = devs; curdev; curdev = curdev->next) {
		/*
		 * Determine the chip name from VID:PID (if it's one of
		 * the supported types with an ID known to us).
		 */
		vid = curdev->vendor_id;
		pid = curdev->product_id;
		chipname = ser_hid_chip_find_name_vid_pid(vid, pid);
		if (!chipname)
			chipname = "<chip>";

		/*
		 * Prefix port names such that open() calls with this
		 * conn= spec will end up here and contain all details
		 * that are essential for processing.
		 */
		path = get_hidapi_path_copy(curdev->path);
		if (!path)
			continue;
		name = g_strdup_printf("%s/%s/%s",
			SER_HID_CONN_PREFIX, chipname, path);
		g_free(path);

		/*
		 * Print whatever information was available. Construct
		 * the description text from pieces. Absence of fields
		 * is not fatal, we have seen perfectly usable cables
		 * that only had a VID and PID (permissions were not an
		 * issue).
		 */
		manuf = curdev->manufacturer_string;
		prod = curdev->product_string;
		serno = curdev->serial_number;
		vid = curdev->vendor_id;
		pid = curdev->product_id;
		desc = g_string_sized_new(128);
		g_string_append_printf(desc, "HID");
		if (manuf && wcslen(manuf) != 0)
			g_string_append_printf(desc, " %ls", manuf);
		if (prod && wcslen(prod) != 0)
			g_string_append_printf(desc, " %ls", prod);
		if (serno && wcslen(serno) != 0)
			g_string_append_printf(desc, " %ls", serno);
		if (vid && pid)
			g_string_append_printf(desc, " [%04hx.%04hx]", vid, pid);
		list = append(list, name, desc->str);
		g_string_free(desc, TRUE);
		g_free(name);
	}
	hid_free_enumeration(devs);

	return list;
}

/*
 * The HIDAPI specific find_usb() callback, invoked by common serial.c code.
 * Enumerate devices for the specified VID:PID pair.
 * Invoke an "append" callback with 'path' for the device.
 */
static GSList *ser_hid_hidapi_find_usb(GSList *list, sr_ser_find_append_t append,
		uint16_t vendor_id, uint16_t product_id)
{
	struct hid_device_info *devs, *curdev;
	const char *name;

	devs = hid_enumerate(vendor_id, product_id);
	for (curdev = devs; curdev; curdev = curdev->next) {
		name = curdev->path;
		list = append(list, name);
	}
	hid_free_enumeration(devs);

	return list;
}

/* Get the serial number of a device specified by path. */
static int ser_hid_hidapi_get_serno(const char *path, char *buf, size_t blen)
{
	char *hidpath;
	hid_device *dev;
	wchar_t *serno_wch;
	int rc;

	if (!path || !*path)
		return SR_ERR_ARG;
	hidpath = extract_hidapi_path(path);
	dev = hidpath ? hid_open_path(hidpath) : NULL;
	g_free(hidpath);
	if (!dev)
		return SR_ERR_IO;

	serno_wch = g_malloc0(blen * sizeof(*serno_wch));
	rc = hid_get_serial_number_string(dev, serno_wch, blen - 1);
	hid_close(dev);
	if (rc != 0) {
		g_free(serno_wch);
		return SR_ERR_IO;
	}

	snprintf(buf, blen, "%ls", serno_wch);
	g_free(serno_wch);

	return SR_OK;
}

/* Get the VID and PID of a device specified by path. */
static int ser_hid_hidapi_get_vid_pid(const char *path,
	uint16_t *vid, uint16_t *pid)
{
#if 0
	/*
	 * Bummer! It would have been most reliable to just open the
	 * device by the specified path, and grab its VID:PID. But
	 * there is no way to get these parameters, neither in the
	 * HIDAPI itself, nor when cheating and reaching behind the API
	 * and accessing the libusb handle in dirty ways. :(
	 */
	hid_device *dev;

	if (!path || !*path)
		return SR_ERR_ARG;
	dev = hid_open_path(path);
	if (!dev)
		return SR_ERR_IO;
	if (vid)
		*vid = dev->vendor_id;
	if (pid)
		*pid = dev->product_id;
	hid_close(dev);

	return SR_OK;
#else
	/*
	 * The fallback approach. Enumerate all devices, compare the
	 * enumerated USB path, and grab the VID:PID. Unfortunately the
	 * caller can provide path specs that differ from enumerated
	 * paths yet mean the same (address the same device). This needs
	 * more attention. Though the specific format of the path and
	 * its meaning are said to be OS specific, which is why we may
	 * not assume anything about it...
	 */
	char *hidpath;
	struct hid_device_info *devs, *dev;
	int found;

	hidpath = extract_hidapi_path(path);
	if (!hidpath)
		return SR_ERR_NA;

	devs = hid_enumerate(0x0000, 0x0000);
	found = 0;
	for (dev = devs; dev; dev = dev->next) {
		if (strcmp(dev->path, hidpath) != 0)
			continue;
		if (vid)
			*vid = dev->vendor_id;
		if (pid)
			*pid = dev->product_id;
		found = 1;
		break;
	}
	hid_free_enumeration(devs);
	g_free(hidpath);

	return found ? SR_OK : SR_ERR_NA;
#endif
}

static int ser_hid_hidapi_open_dev(struct sr_serial_dev_inst *serial)
{
	hid_device *hid_dev;

	if (!serial->usb_path || !*serial->usb_path)
		return SR_ERR_ARG;

	/*
	 * A path is available, assume that either a GUI or a
	 * user has copied what a previous listing has provided.
	 * Or a scan determined a matching device's USB path.
	 */
	if (!serial->hid_path)
		serial->hid_path = extract_hidapi_path(serial->usb_path);
	hid_dev = hid_open_path(serial->hid_path);
	if (!hid_dev) {
		g_free((void *)serial->hid_path);
		serial->hid_path = NULL;
		return SR_ERR_IO;
	}

	serial->hid_dev = hid_dev;
	hid_set_nonblocking(hid_dev, 1);

	return SR_OK;
}

static void ser_hid_hidapi_close_dev(struct sr_serial_dev_inst *serial)
{
	if (serial->hid_dev) {
		hid_close(serial->hid_dev);
		serial->hid_dev = NULL;
		g_free((void *)serial->hid_path);
		serial->hid_path = NULL;
	}
	g_slist_free_full(serial->hid_source_args, g_free);
	serial->hid_source_args = NULL;
}

struct hidapi_source_args_t {
	/* Application callback. */
	sr_receive_data_callback cb;
	void *cb_data;
	/* The serial device, to store RX data. */
	struct sr_serial_dev_inst *serial;
};

/*
 * Gets periodically invoked by the glib main loop. "Drives" (checks)
 * progress of USB communication, and invokes the application's callback
 * which processes RX data (when some has become available), as well as
 * handles application level timeouts.
 */
static int hidapi_source_cb(int fd, int revents, void *cb_data)
{
	struct hidapi_source_args_t *args;
	uint8_t rx_buf[SER_HID_CHUNK_SIZE];
	int rc;

	args = cb_data;

	/*
	 * Drain receive data which the chip might have pending. This is
	 * "a copy" of the "background part" of ser_hid_read(), without
	 * the timeout support code, and not knowing how much data the
	 * application is expecting.
	 */
	do {
		rc = args->serial->hid_chip_funcs->read_bytes(args->serial,
				rx_buf, sizeof(rx_buf), 0);
		if (rc > 0) {
			ser_hid_mask_databits(args->serial, rx_buf, rc);
			sr_ser_queue_rx_data(args->serial, rx_buf, rc);
		}
	} while (rc > 0);

	/*
	 * When RX data became available (now or earlier), pass this
	 * condition to the application callback. Always periodically
	 * run the application callback, since it handles timeouts and
	 * might carry out other tasks as well like signalling progress.
	 */
	if (sr_ser_has_queued_data(args->serial))
		revents |= G_IO_IN;
	rc = args->cb(fd, revents, args->cb_data);

	return rc;
}

#define WITH_MAXIMUM_TIMEOUT_VALUE	10
static int ser_hid_hidapi_setup_source_add(struct sr_session *session,
	struct sr_serial_dev_inst *serial, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
	struct hidapi_source_args_t *args;
	int rc;

	(void)events;

	/* Optionally enforce a minimum poll period. */
	if (WITH_MAXIMUM_TIMEOUT_VALUE && timeout > WITH_MAXIMUM_TIMEOUT_VALUE)
		timeout = WITH_MAXIMUM_TIMEOUT_VALUE;

	/* Allocate status container for background data reception. */
	args = g_malloc0(sizeof(*args));
	args->cb = cb;
	args->cb_data = cb_data;
	args->serial = serial;

	/*
	 * Have a periodic timer installed. Register the allocated block
	 * with the serial device, since the GSource's finalizer won't
	 * free the memory, and we haven't bothered to create a custom
	 * HIDAPI specific GSource.
	 */
	rc = sr_session_source_add(session, -1, events, timeout,
			hidapi_source_cb, args);
	if (rc != SR_OK) {
		g_free(args);
		return rc;
	}
	serial->hid_source_args = g_slist_append(serial->hid_source_args, args);

	return SR_OK;
}

static int ser_hid_hidapi_setup_source_remove(struct sr_session *session,
	struct sr_serial_dev_inst *serial)
{
	(void)serial;

	(void)sr_session_source_remove(session, -1);
	/*
	 * Release callback args here already? Can there be more than
	 * one source registered at any time, given that we pass fd -1
	 * which is used as the key for the session?
	 */

	return SR_OK;
}

SR_PRIV int ser_hid_hidapi_get_report(struct sr_serial_dev_inst *serial,
	uint8_t *data, size_t len)
{
	int rc;

	rc = hid_get_feature_report(serial->hid_dev, data, len);
	if (rc < 0)
		return SR_ERR_IO;

	return rc;
}

SR_PRIV int ser_hid_hidapi_set_report(struct sr_serial_dev_inst *serial,
	const uint8_t *data, size_t len)
{
	int rc;
	const wchar_t *err_text;

	rc = hid_send_feature_report(serial->hid_dev, data, len);
	if (rc < 0) {
		err_text = hid_error(serial->hid_dev);
		sr_dbg("%s() hidapi error: %ls", __func__, err_text);
		return SR_ERR_IO;
	}

	return rc;
}

SR_PRIV int ser_hid_hidapi_get_data(struct sr_serial_dev_inst *serial,
	uint8_t ep, uint8_t *data, size_t len, int timeout)
{
	int rc;

	(void)ep;

	if (timeout)
		rc = hid_read_timeout(serial->hid_dev, data, len, timeout);
	else
		rc = hid_read(serial->hid_dev, data, len);
	if (rc < 0)
		return SR_ERR_IO;
	if (rc == 0)
		return 0;

	return rc;
}

SR_PRIV int ser_hid_hidapi_set_data(struct sr_serial_dev_inst *serial,
	uint8_t ep, const uint8_t *data, size_t len, int timeout)
{
	int rc;

	(void)ep;
	(void)timeout;

	rc = hid_write(serial->hid_dev, data, len);
	if (rc < 0)
		return SR_ERR_IO;

	return rc;
}

/* }}} */
/* {{{ support for serial-over-HID chips */

static struct ser_hid_chip_functions **chips[SER_HID_CHIP_LAST] = {
	[SER_HID_CHIP_UNKNOWN] = NULL,
	[SER_HID_CHIP_BTC_BU86X] = &ser_hid_chip_funcs_bu86x,
	[SER_HID_CHIP_SIL_CP2110] = &ser_hid_chip_funcs_cp2110,
	[SER_HID_CHIP_VICTOR_DMM] = &ser_hid_chip_funcs_victor,
	[SER_HID_CHIP_WCH_CH9325] = &ser_hid_chip_funcs_ch9325,
};

static struct ser_hid_chip_functions *get_hid_chip_funcs(enum ser_hid_chip_t chip)
{
	struct ser_hid_chip_functions *funcs;

	if (chip >= ARRAY_SIZE(chips))
		return NULL;
	if (!chips[chip])
		return NULL;
	funcs = *chips[chip];
	if (!funcs)
		return NULL;

	return funcs;
}

static int ser_hid_setup_funcs(struct sr_serial_dev_inst *serial)
{

	if (!serial)
		return -1;

	if (serial->hid_chip && !serial->hid_chip_funcs) {
		serial->hid_chip_funcs = get_hid_chip_funcs(serial->hid_chip);
		if (!serial->hid_chip_funcs)
			return -1;
	}

	return 0;
}

/*
 * Takes a pointer to the chip spec with potentially trailing data,
 * returns the chip index and advances the spec pointer upon match,
 * returns SER_HID_CHIP_UNKNOWN upon mismatch.
 */
static enum ser_hid_chip_t ser_hid_chip_find_enum(const char **spec_p)
{
	const gchar *spec;
	enum ser_hid_chip_t idx;
	struct ser_hid_chip_functions *desc;

	if (!spec_p || !*spec_p)
		return SER_HID_CHIP_UNKNOWN;
	spec = *spec_p;
	if (!*spec)
		return SER_HID_CHIP_UNKNOWN;
	for (idx = 0; idx < SER_HID_CHIP_LAST; idx++) {
		desc = get_hid_chip_funcs(idx);
		if (!desc)
			continue;
		if (!desc->chipname)
			continue;
		if (!g_str_has_prefix(spec, desc->chipname))
			continue;
		spec += strlen(desc->chipname);
		*spec_p = spec;
		return idx;
	}

	return SER_HID_CHIP_UNKNOWN;
}

/* See if we can find a chip name for a VID:PID spec. */
SR_PRIV const char *ser_hid_chip_find_name_vid_pid(uint16_t vid, uint16_t pid)
{
	size_t chip_idx;
	struct ser_hid_chip_functions *desc;
	const struct vid_pid_item *vid_pids;

	for (chip_idx = 0; chip_idx < SER_HID_CHIP_LAST; chip_idx++) {
		desc = get_hid_chip_funcs(chip_idx);
		if (!desc)
			continue;
		if (!desc->chipname)
			continue;
		vid_pids = desc->vid_pid_items;
		if (!vid_pids)
			continue;
		while (vid_pids->vid) {
			if (vid_pids->vid == vid && vid_pids->pid == pid)
				return desc->chipname;
			vid_pids++;
		}
	}

	return NULL;
}

/**
 * See if a text string is a valid USB path for a HID device.
 * @param[in] serial The serial port that is about to get opened.
 * @param[in] path The (assumed) USB path specification.
 * @return SR_OK upon success, SR_ERR* upon failure.
 */
static int try_open_path(struct sr_serial_dev_inst *serial, const char *path)
{
	int rc;

	serial->usb_path = g_strdup(path);
	rc = ser_hid_hidapi_open_dev(serial);
	ser_hid_hidapi_close_dev(serial);
	g_free(serial->usb_path);
	serial->usb_path = NULL;

	return rc;
}

/**
 * Parse conn= specs for serial over HID communication.
 *
 * @param[in] serial The serial port that is about to get opened.
 * @param[in] spec The caller provided conn= specification.
 * @param[out] chip_ref Pointer to a chip type (enum).
 * @param[out] path_ref Pointer to a USB path (text string).
 * @param[out] serno_ref Pointer to a serial number (text string).
 *
 * @return 0 upon success, non-zero upon failure. Fills the *_ref output
 * values.
 *
 * Summary of parsing rules as they are implemented:
 * - Insist on the "hid" prefix. Accept "hid" alone without any other
 *   additional field.
 * - The first field that follows can be a chip spec, yet is optional.
 * - Any other field is assumed to be either a USB path or a serial
 *   number. There is no point in specifying both of these, as either
 *   of them uniquely identifies a device.
 *
 * Supported formats resulting from these rules:
 *   hid[/<chip>]
 *   hid[/<chip>]/usb=<bus>.<dev>[.<if>]
 *   hid[/<chip>]/raw=<path>	(may contain slashes!)
 *   hid[/<chip>]/sn=serno
 *
 * This routine just parses the conn= spec, which either was provided by
 * a user, or may reflect (cite) an item of a previously gathered listing
 * (clipboard provided by CLI clients, or selected from a GUI form).
 * Another routine will fill in the blanks, and do the cable selection
 * when a filter was specified.
 *
 * Users will want to use short forms when they need to come up with the
 * specs by themselves. The "verbose" or seemingly redundant forms (chip
 * _and_ path/serno spec) are useful when the cable uses non-standard or
 * not-yet-supported VID:PID items when automatic chip detection fails.
 */
static int ser_hid_parse_conn_spec(
	struct sr_serial_dev_inst *serial, const char *spec,
	enum ser_hid_chip_t *chip_ref, char **path_ref, char **serno_ref)
{
	const char *p;
	enum ser_hid_chip_t chip;
	char *path, *serno;
	int rc;

	if (chip_ref)
		*chip_ref = SER_HID_CHIP_UNKNOWN;
	if (path_ref)
		*path_ref = NULL;
	if (serno_ref)
		*serno_ref = NULL;
	chip = SER_HID_CHIP_UNKNOWN;
	path = serno = NULL;

	if (!serial || !spec || !*spec)
		return SR_ERR_ARG;
	p = spec;

	/* The "hid" prefix is mandatory. */
	if (!g_str_has_prefix(p, SER_HID_CONN_PREFIX))
		return SR_ERR_ARG;
	p += strlen(SER_HID_CONN_PREFIX);

	/*
	 * Check for prefixed fields, assume chip type spec otherwise.
	 * Paths and serial numbers "are greedy" (span to the end of
	 * the input spec). Chip types are optional, and cannot repeat
	 * multiple times.
	 */
	while (*p) {
		if (*p == '/')
			p++;
		if (!*p)
			break;
		if (g_str_has_prefix(p, SER_HID_USB_PREFIX)) {
			rc = try_open_path(serial, p);
			if (rc != SR_OK)
				return rc;
			path = g_strdup(p);
			p += strlen(p);
		} else if (g_str_has_prefix(p, SER_HID_IOKIT_PREFIX)) {
			rc = try_open_path(serial, p);
			if (rc != SR_OK)
				return rc;
			path = g_strdup(p);
			p += strlen(p);
		} else if (g_str_has_prefix(p, SER_HID_RAW_PREFIX)) {
			rc = try_open_path(serial, p);
			if (rc != SR_OK)
				return rc;
			path = g_strdup(p);
			p += strlen(p);
		} else if (g_str_has_prefix(p, SER_HID_SNR_PREFIX)) {
			p += strlen(SER_HID_SNR_PREFIX);
			serno = g_strdup(p);
			p += strlen(p);
		} else if (!chip) {
			char *copy;
			const char *endptr;
			copy = g_strdup(p);
			endptr = copy;
			chip = ser_hid_chip_find_enum(&endptr);
			if (!chip) {
				g_free(copy);
				return SR_ERR_ARG;
			}
			p += endptr - copy;
			g_free(copy);
		} else {
			sr_err("unsupported conn= spec %s, error at %s", spec, p);
			return SR_ERR_ARG;
		}
		if (*p == '/')
			p++;
		if (path || serno)
			break;
	}

	if (chip_ref)
		*chip_ref = chip;
	if (path_ref && path)
		*path_ref = path;
	if (serno_ref && serno)
		*serno_ref = serno;

	return SR_OK;
}

/* Get and compare serial number. Boolean return value. */
static int check_serno(const char *path, const char *serno_want)
{
	char *hid_path;
	char serno_got[128];
	int rc;

	hid_path = extract_hidapi_path(path);
	rc = ser_hid_hidapi_get_serno(hid_path, serno_got, sizeof(serno_got));
	g_free(hid_path);
	if (rc) {
		sr_dbg("DBG: %s(), could not get serial number", __func__);
		return 0;
	}

	return strcmp(serno_got, serno_want) == 0;
}

static GSList *append_find(GSList *devs, const char *path)
{
	char *copy;

	if (!path || !*path)
		return devs;

	copy = g_strdup(path);
	devs = g_slist_append(devs, copy);

	return devs;
}

static GSList *list_paths_for_vids_pids(const struct vid_pid_item *vid_pids)
{
	GSList *list;
	size_t idx;
	uint16_t vid, pid;

	list = NULL;
	for (idx = 0; /* EMPTY */; idx++) {
		if (!vid_pids) {
			vid = pid = 0;
		} else if (!vid_pids[idx].vid) {
			break;
		} else {
			vid = vid_pids[idx].vid;
			pid = vid_pids[idx].pid;
		}
		list = ser_hid_hidapi_find_usb(list, append_find, vid, pid);
		if (!vid_pids)
			break;
	}

	return list;
}

/**
 * Search for a matching USB device for HID communication.
 *
 * @param[inout] chip The HID chip type (enum).
 * @param[inout] usbpath The USB path for the device (string).
 * @param[in] serno The serial number to search for.
 *
 * @retval SR_OK upon success
 * @retval SR_ERR_* upon failure.
 *
 * This routine fills in blanks which the conn= spec parser left open.
 * When not specified yet, the HID chip type gets determined. When a
 * serial number was specified, then search the corresponding device.
 * Upon completion, the chip type and USB path for the device shall be
 * known, as these are essential for subsequent operation.
 */
static int ser_hid_chip_search(enum ser_hid_chip_t *chip_ref,
	char **path_ref, const char *serno)
{
	enum ser_hid_chip_t chip;
	char *path;
	int have_chip, have_path, have_serno;
	struct ser_hid_chip_functions *chip_funcs;
	int rc;
	int serno_matched;
	uint16_t vid, pid;
	const char *name;
	const struct vid_pid_item *vid_pids;
	GSList *list, *matched, *matched2, *tmplist;

	if (!chip_ref)
		return SR_ERR_ARG;
	chip = *chip_ref;
	if (!path_ref)
		return SR_ERR_ARG;
	path = *path_ref;

	/*
	 * Simplify the more complex conditions somewhat by assigning
	 * to local variables. Handle the easiest conditions first.
	 * - Either path or serial number can be specified, but not both
	 *   at the same time.
	 * - When a USB path is given, immediately see which HID chip
	 *   the device has, without the need for enumeration.
	 * - When a serial number is given, enumerate the devices and
	 *   search for that number. Either enumerate all devices of the
	 *   specified HID chip type (try the VID:PID pairs that we are
	 *   aware of), or try all HID devices for unknown chip types.
	 *   Not finding the serial number is fatal.
	 * - When no path was found yet, enumerate the devices and pick
	 *   one of them. Try known VID:PID pairs for a HID chip, or all
	 *   devices for unknown chips. Make sure to pick a device of a
	 *   supported chip type if the chip was not specified.
	 * - Determine the chip type if not yet known. There should be
	 *   a USB path by now, determined in one of the above blocks.
	 */
	have_chip = (chip != SER_HID_CHIP_UNKNOWN) ? 1 : 0;
	have_path = (path && *path) ? 1 : 0;
	have_serno = (serno && *serno) ? 1 : 0;
	if (have_path && have_serno) {
		sr_err("Unsupported combination of USB path and serno");
		return SR_ERR_ARG;
	}
	chip_funcs = have_chip ? get_hid_chip_funcs(chip) : NULL;
	if (have_chip && !chip_funcs)
		return SR_ERR_NA;
	if (have_chip && !chip_funcs->vid_pid_items)
		return SR_ERR_NA;
	if (have_path && !have_chip) {
		vid = pid = 0;
		rc = ser_hid_hidapi_get_vid_pid(path, &vid, &pid);
		if (rc != SR_OK)
			return rc;
		name = ser_hid_chip_find_name_vid_pid(vid, pid);
		if (!name || !*name)
			return SR_ERR_NA;
		chip = ser_hid_chip_find_enum(&name);
		if (chip == SER_HID_CHIP_UNKNOWN)
			return SR_ERR_NA;
		have_chip = 1;
	}
	if (have_serno) {
		vid_pids = have_chip ? chip_funcs->vid_pid_items : NULL;
		list = list_paths_for_vids_pids(vid_pids);
		if (!list)
			return SR_ERR_NA;
		matched = NULL;
		for (tmplist = list; tmplist; tmplist = tmplist->next) {
			path = get_hidapi_path_copy(tmplist->data);
			serno_matched = check_serno(path, serno);
			g_free(path);
			if (!serno_matched)
				continue;
			matched = tmplist;
			break;
		}
		if (!matched)
			return SR_ERR_NA;
		path = g_strdup(matched->data);
		have_path = 1;
		g_slist_free_full(list, g_free);
	}
	if (!have_path) {
		vid_pids = have_chip ? chip_funcs->vid_pid_items : NULL;
		list = list_paths_for_vids_pids(vid_pids);
		if (!list)
			return SR_ERR_NA;
		matched = matched2 = NULL;
		if (have_chip) {
			/* List already only contains specified chip. */
			matched = list;
			matched2 = list->next;
		}
		/* Works for lists with one or multiple chips. Saves indentation. */
		for (tmplist = list; tmplist; tmplist = tmplist->next) {
			if (have_chip)
				break;
			path = tmplist->data;
			rc = ser_hid_hidapi_get_vid_pid(path, &vid, &pid);
			if (rc || !ser_hid_chip_find_name_vid_pid(vid, pid))
				continue;
			if (!matched) {
				matched = tmplist;
				continue;
			}
			if (!matched2) {
				matched2 = tmplist;
				break;
			}
		}
		if (!matched) {
			g_slist_free_full(list, g_free);
			return SR_ERR_NA;
		}
		/*
		 * TODO Optionally fail harder, expect users to provide
		 * unambiguous cable specs.
		 */
		if (matched2)
			sr_info("More than one cable matches, random pick.");
		path = get_hidapi_path_copy(matched->data);
		have_path = 1;
		g_slist_free_full(list, g_free);
	}
	if (have_path && !have_chip) {
		vid = pid = 0;
		rc = ser_hid_hidapi_get_vid_pid(path, &vid, &pid);
		if (rc != SR_OK)
			return rc;
		name = ser_hid_chip_find_name_vid_pid(vid, pid);
		if (!name || !*name)
			return SR_ERR_NA;
		chip = ser_hid_chip_find_enum(&name);
		if (chip == SER_HID_CHIP_UNKNOWN)
			return SR_ERR_NA;
		have_chip = 1;
	}

	if (chip_ref)
		*chip_ref = chip;
	if (path_ref)
		*path_ref = path;

	return SR_OK;
}

/* }}} */
/* {{{ transport methods called by the common serial.c code */

/* See if a serial port's name refers to an HID type. */
SR_PRIV int ser_name_is_hid(struct sr_serial_dev_inst *serial)
{
	size_t off;
	char sep;

	if (!serial)
		return 0;
	if (!serial->port || !*serial->port)
		return 0;

	/* Accept either "hid" alone, or "hid/" as a prefix. */
	if (!g_str_has_prefix(serial->port, SER_HID_CONN_PREFIX))
		return 0;
	off = strlen(SER_HID_CONN_PREFIX);
	sep = serial->port[off];
	if (sep != '\0' && sep != '/')
		return 0;

	return 1;
}

static int ser_hid_open(struct sr_serial_dev_inst *serial, int flags)
{
	enum ser_hid_chip_t chip;
	char *usbpath, *serno;
	int rc;

	(void)flags;

	if (ser_hid_setup_funcs(serial) != 0) {
		sr_err("Cannot determine HID communication library.");
		return SR_ERR_NA;
	}

	rc = ser_hid_parse_conn_spec(serial, serial->port,
			&chip, &usbpath, &serno);
	if (rc != SR_OK)
		return SR_ERR_ARG;

	/*
	 * When a serial number was specified, or when the chip type or
	 * the USB path were not specified, do a search to determine the
	 * device's USB path.
	 */
	if (!chip || !usbpath || serno) {
		rc = ser_hid_chip_search(&chip, &usbpath, serno);
		if (rc != 0)
			return SR_ERR_NA;
	}

	/*
	 * Open the HID device. Only store chip type and device handle
	 * when open completes successfully.
	 */
	serial->hid_chip = chip;
	if (ser_hid_setup_funcs(serial) != 0) {
		sr_err("Cannot determine HID chip specific routines.");
		return SR_ERR_NA;
	}
	if (usbpath && *usbpath)
		serial->usb_path = usbpath;
	if (serno && *serno)
		serial->usb_serno = serno;

	rc = ser_hid_hidapi_open_dev(serial);
	if (rc) {
		sr_err("Failed to open HID device.");
		serial->hid_chip = 0;
		g_free(serial->usb_path);
		serial->usb_path = NULL;
		g_free(serial->usb_serno);
		serial->usb_serno = NULL;
		return SR_ERR_IO;
	}

	if (!serial->rcv_buffer)
		serial->rcv_buffer = g_string_sized_new(SER_HID_CHUNK_SIZE);

	return SR_OK;
}

static int ser_hid_close(struct sr_serial_dev_inst *serial)
{
	ser_hid_hidapi_close_dev(serial);

	return SR_OK;
}

static int ser_hid_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	if (ser_hid_setup_funcs(serial) != 0)
		return SR_ERR_NA;
	if (!serial->hid_chip_funcs || !serial->hid_chip_funcs->set_params)
		return SR_ERR_NA;

	return serial->hid_chip_funcs->set_params(serial,
		baudrate, bits, parity, stopbits,
		flowcontrol, rts, dtr);
}

static int ser_hid_setup_source_add(struct sr_session *session,
	struct sr_serial_dev_inst *serial, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
	return ser_hid_hidapi_setup_source_add(session, serial,
		events, timeout, cb, cb_data);
}

static int ser_hid_setup_source_remove(struct sr_session *session,
	struct sr_serial_dev_inst *serial)
{
	return ser_hid_hidapi_setup_source_remove(session, serial);
}

static GSList *ser_hid_list(GSList *list, sr_ser_list_append_t append)
{
	return ser_hid_hidapi_list(list, append);
}

static GSList *ser_hid_find_usb(GSList *list, sr_ser_find_append_t append,
	uint16_t vendor_id, uint16_t product_id)
{
	return ser_hid_hidapi_find_usb(list, append, vendor_id, product_id);
}

static int ser_hid_flush(struct sr_serial_dev_inst *serial)
{
	if (!serial->hid_chip_funcs || !serial->hid_chip_funcs->flush)
		return SR_ERR_NA;

	return serial->hid_chip_funcs->flush(serial);
}

static int ser_hid_drain(struct sr_serial_dev_inst *serial)
{
	if (!serial->hid_chip_funcs || !serial->hid_chip_funcs->drain)
		return SR_ERR_NA;

	return serial->hid_chip_funcs->drain(serial);
}

static int ser_hid_write(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	int total, max_chunk, chunk_len;
	int rc;

	if (!serial->hid_chip_funcs || !serial->hid_chip_funcs->write_bytes)
		return SR_ERR_NA;
	if (!serial->hid_chip_funcs->max_bytes_per_request)
		return SR_ERR_NA;

	total = 0;
	max_chunk = serial->hid_chip_funcs->max_bytes_per_request;
	while (count > 0) {
		chunk_len = count;
		if (max_chunk && chunk_len > max_chunk)
			chunk_len = max_chunk;
		rc = serial->hid_chip_funcs->write_bytes(serial, buf, chunk_len);
		if (rc < 0) {
			sr_err("Error sending transmit data to HID device.");
			return total;
		}
		if (rc != chunk_len) {
			sr_warn("Short transmission to HID device (%d/%d bytes)?",
					rc, chunk_len);
			return total;
		}
		buf += chunk_len;
		count -= chunk_len;
		total += chunk_len;
		/* TODO
		 * Need we wait here? For data to drain through the slow
		 * UART. Not all UART-over-HID chips will have FIFOs.
		 */
		if (!nonblocking) {
			(void)timeout_ms;
			/* TODO */
		}
	}

	return total;
}

static int ser_hid_read(struct sr_serial_dev_inst *serial,
	void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	gint64 deadline_us, now_us;
	uint8_t buffer[SER_HID_CHUNK_SIZE];
	int rc;
	unsigned int got;

	if (!serial->hid_chip_funcs || !serial->hid_chip_funcs->read_bytes)
		return SR_ERR_NA;
	if (!serial->hid_chip_funcs->max_bytes_per_request)
		return SR_ERR_NA;

	/*
	 * Immediately satisfy the caller's request from the RX buffer
	 * if the requested amount of data is available already.
	 */
	if (sr_ser_has_queued_data(serial) >= count)
		return sr_ser_unqueue_rx_data(serial, buf, count);

	/*
	 * When a timeout was specified, then determine the deadline
	 * where to stop reception.
	 */
	deadline_us = 0;
	now_us = 0;		/* Silence a (false) compiler warning. */
	if (timeout_ms) {
		now_us = g_get_monotonic_time();
		deadline_us = now_us + timeout_ms * 1000;
	}

	/*
	 * Keep receiving from the port until the caller's requested
	 * amount of data has become available, or the timeout has
	 * expired. In the absence of a timeout, stop reading when an
	 * attempt no longer yields receive data.
	 *
	 * This implementation assumes that applications will call the
	 * read routine often enough, or that reception continues in
	 * background, such that data is not lost and hardware and
	 * software buffers won't overrun.
	 */
	while (TRUE) {
		/*
		 * Determine the timeout (in milliseconds) for this
		 * iteration. The 'now_us' timestamp was initially
		 * determined above, and gets updated at the bottom of
		 * the loop.
		 */
		if (deadline_us) {
			timeout_ms = (deadline_us - now_us) / 1000;
			if (!timeout_ms)
				timeout_ms = 1;
		} else if (nonblocking) {
			timeout_ms = 10;
		} else {
			timeout_ms = 0;
		}

		/*
		 * Check the HID transport for the availability of more
		 * receive data.
		 */
		rc = serial->hid_chip_funcs->read_bytes(serial,
				buffer, sizeof(buffer), timeout_ms);
		if (rc < 0) {
			sr_dbg("DBG: %s() read error %d.", __func__, rc);
			return SR_ERR;
		}
		if (rc) {
			ser_hid_mask_databits(serial, buffer, rc);
			sr_ser_queue_rx_data(serial, buffer, rc);
		}
		got = sr_ser_has_queued_data(serial);

		/*
		 * Stop reading when the requested amount is available,
		 * or when the timeout has expired.
		 *
		 * TODO Consider whether grabbing all RX data is more
		 * desirable. Implementing this approach requires a cheap
		 * check for the availability of more data on the USB level.
		 */
		if (got >= count)
			break;
		if (nonblocking && !rc)
			break;
		if (deadline_us) {
			now_us = g_get_monotonic_time();
			if (now_us >= deadline_us) {
				sr_dbg("DBG: %s() read loop timeout.", __func__);
				break;
			}
		}
	}

	/*
	 * Satisfy the caller's demand for receive data from previously
	 * queued incoming data.
	 */
	if (got > count)
		got = count;

	return sr_ser_unqueue_rx_data(serial, buf, count);
}

static struct ser_lib_functions serlib_hid = {
	.open = ser_hid_open,
	.close = ser_hid_close,
	.flush = ser_hid_flush,
	.drain = ser_hid_drain,
	.write = ser_hid_write,
	.read = ser_hid_read,
	.set_params = ser_hid_set_params,
	.set_handshake = std_dummy_set_handshake,
	.setup_source_add = ser_hid_setup_source_add,
	.setup_source_remove = ser_hid_setup_source_remove,
	.list = ser_hid_list,
	.find_usb = ser_hid_find_usb,
	.get_frame_format = NULL,
};
SR_PRIV struct ser_lib_functions *ser_lib_funcs_hid = &serlib_hid;

/* }}} */
#else

SR_PRIV int ser_name_is_hid(struct sr_serial_dev_inst *serial)
{
	(void)serial;

	return 0;
}

SR_PRIV struct ser_lib_functions *ser_lib_funcs_hid = NULL;

#endif
#endif
/** @} */
