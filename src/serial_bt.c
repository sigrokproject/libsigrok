/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018-2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <string.h>
#include <memory.h>

#define LOG_PREFIX "serial-bt"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_BLUETOOTH

#define SER_BT_CONN_PREFIX	"bt"
#define SER_BT_CHUNK_SIZE	1200

/**
 * @file
 *
 * Serial port handling, wraps the external BT/BLE dependencies.
 */

/**
 * @defgroup grp_serial_bt Serial port handling, BT/BLE group
 *
 * Make serial-over-BT communication appear like a regular serial port.
 *
 * @{
 */

/* {{{ support for serial-over-BT channels */

static const struct scan_supported_item {
	const char *name;
	enum ser_bt_conn_t type;
} scan_supported_items[] = {
	/* Guess connection types from device names (useful for scans). */
	{ "121GW", SER_BT_CONN_BLE122, },
	{ "Adafruit Bluefruit LE 8134", SER_BT_CONN_NRF51, },
	{ "HC-05", SER_BT_CONN_RFCOMM, },
	{ NULL, SER_BT_CONN_UNKNOWN, },
};

static const char *ser_bt_conn_names[SER_BT_CONN_MAX] = {
	[SER_BT_CONN_UNKNOWN] = "<type>",
	[SER_BT_CONN_RFCOMM] = "rfcomm",
	[SER_BT_CONN_BLE122] = "ble122",
	[SER_BT_CONN_NRF51] = "nrf51",
	[SER_BT_CONN_CC254x] = "cc254x",
};

static enum ser_bt_conn_t lookup_conn_name(const char *name)
{
	size_t idx;
	const char *item;

	if (!name || !*name)
		return SER_BT_CONN_UNKNOWN;
	idx = ARRAY_SIZE(ser_bt_conn_names);
	while (idx-- > 0) {
		item = ser_bt_conn_names[idx];
		if (strcmp(item, name) == 0)
			return idx;
	}

	return SER_BT_CONN_UNKNOWN;
}

static const char *conn_name_text(enum ser_bt_conn_t type)
{
	if (type >= ARRAY_SIZE(ser_bt_conn_names))
		type = SER_BT_CONN_UNKNOWN;

	return ser_bt_conn_names[type];
}

/**
 * Parse conn= specs for serial over Bluetooth communication.
 *
 * @param[in] serial The serial port that is about to get opened.
 * @param[in] spec The caller provided conn= specification.
 * @param[out] conn_type The type of BT comm (BT RFCOMM, BLE notify).
 * @param[out] remote_addr The remote device address.
 * @param[out] rfcomm_channel The RFCOMM channel (if applicable).
 * @param[out] read_hdl The BLE notify read handle (if applicable).
 * @param[out] write_hdl The BLE notify write handle (if applicable).
 * @param[out] cccd_hdl The BLE notify CCCD handle (if applicable).
 * @param[out] cccd_val The BLE notify CCCD value (if applicable).
 *
 * @return 0 upon success, non-zero upon failure.
 *
 * Summary of parsing rules as they are implemented:
 * - Implementor's note: Automatic scan for available devices is not
 *   yet implemented. So strictly speaking some parts of the input
 *   spec are optional, but fallbacks may not take effect ATM.
 * - Insist on the "bt" prefix. Accept "bt" alone without any other
 *   additional field.
 * - The first field that follows is the connection type. Supported
 *   types are 'rfcomm', 'ble122', 'cc254x', and potentially others
 *   in a future implementation.
 * - The next field is the remote device's address, either separated
 *   by colons or dashes or spaces, or not separated at all.
 * - Other parameters (RFCOMM channel, notify handles and write values)
 *   get derived from the connection type. A future implementation may
 *   accept more fields, but the syntax is yet to get developed.
 *
 * Supported formats resulting from these rules:
 *   bt/<conn>/<addr>
 *
 * Examples:
 *   bt/rfcomm/11-22-33-44-55-66
 *   bt/ble122/88:6b:12:34:56:78
 *   bt/cc254x/0123456789ab
 *
 * It's assumed that users easily can create those conn= specs from
 * available information, or that scan routines will create such specs
 * that copy'n'paste results (or GUI choices from previous scan results)
 * can get processed here.
 */
static int ser_bt_parse_conn_spec(
	struct sr_serial_dev_inst *serial, const char *spec,
	enum ser_bt_conn_t *conn_type, const char **remote_addr,
	size_t *rfcomm_channel,
	uint16_t *read_hdl, uint16_t *write_hdl,
	uint16_t *cccd_hdl, uint16_t *cccd_val)
{
	enum ser_bt_conn_t type;
	const char *addr;
	char **fields, *field;

	if (conn_type)
		*conn_type = SER_BT_CONN_UNKNOWN;
	if (remote_addr)
		*remote_addr = NULL;
	if (rfcomm_channel)
		*rfcomm_channel = 0;
	if (read_hdl)
		*read_hdl = 0;
	if (write_hdl)
		*write_hdl = 0;
	if (cccd_hdl)
		*cccd_hdl = 0;
	if (cccd_val)
		*cccd_val = 0;

	type = SER_BT_CONN_UNKNOWN;
	addr = NULL;

	if (!serial || !spec || !spec[0])
		return SR_ERR_ARG;

	/* Evaluate the mandatory first three fields. */
	fields = g_strsplit_set(spec, "/", 0);
	if (!fields)
		return SR_ERR_ARG;
	if (g_strv_length(fields) < 3) {
		g_strfreev(fields);
		return SR_ERR_ARG;
	}
	field = fields[0];
	if (strcmp(field, SER_BT_CONN_PREFIX) != 0) {
		g_strfreev(fields);
		return SR_ERR_ARG;
	}
	field = fields[1];
	type = lookup_conn_name(field);
	if (!type) {
		g_strfreev(fields);
		return SR_ERR_ARG;
	}
	if (conn_type)
		*conn_type = type;
	field = fields[2];
	if (!field || !*field) {
		g_strfreev(fields);
		return SR_ERR_ARG;
	}
	addr = g_strdup(field);
	if (remote_addr)
		*remote_addr = addr;

	/* Derive default parameters that match the connection type. */
	/* TODO Lookup defaults from a table? */
	switch (type) {
	case SER_BT_CONN_RFCOMM:
		if (rfcomm_channel)
			*rfcomm_channel = 1;
		break;
	case SER_BT_CONN_BLE122:
		if (read_hdl)
			*read_hdl = 8;
		if (write_hdl)
			*write_hdl = 0;
		if (cccd_hdl)
			*cccd_hdl = 9;
		if (cccd_val)
			*cccd_val = 0x0003;
		break;
	case SER_BT_CONN_NRF51:
		/* TODO
		 * Are these values appropriate? Check the learn article at
		 * https://learn.adafruit.com/introducing-the-adafruit-bluefruit-le-uart-friend?view=all
		 */
		if (read_hdl)
			*read_hdl = 13;
		if (write_hdl)
			*write_hdl = 11;
		if (cccd_hdl)
			*cccd_hdl = 14;
		if (cccd_val)
			*cccd_val = 0x0001;
		/* TODO 'random' type, sec-level=high */
		break;
	case SER_BT_CONN_CC254x:
		/* TODO Are these values appropriate? Just guessing here. */
		if (read_hdl)
			*read_hdl = 20;
		if (write_hdl)
			*write_hdl = 0;
		if (cccd_hdl)
			*cccd_hdl = 21;
		if (cccd_val)
			*cccd_val = 0x0001;
		break;
	default:
		return SR_ERR_ARG;
	}

	/* TODO Evaluate optionally trailing fields, override defaults? */

	g_strfreev(fields);
	return SR_OK;
}

static void ser_bt_mask_databits(struct sr_serial_dev_inst *serial,
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

static int ser_bt_data_cb(void *cb_data, uint8_t *data, size_t dlen)
{
	struct sr_serial_dev_inst *serial;

	serial = cb_data;
	if (!serial)
		return -1;

	ser_bt_mask_databits(serial, data, dlen);
	sr_ser_queue_rx_data(serial, data, dlen);

	return 0;
}

/* }}} */
/* {{{ wrap serial-over-BT operations in a common serial.c API */

/* See if a serial port's name refers to a BT type. */
SR_PRIV int ser_name_is_bt(struct sr_serial_dev_inst *serial)
{
	size_t off;
	char sep;

	if (!serial)
		return 0;
	if (!serial->port || !*serial->port)
		return 0;

	/* Accept either "bt" alone, or "bt/" as a prefix. */
	if (!g_str_has_prefix(serial->port, SER_BT_CONN_PREFIX))
		return 0;
	off = strlen(SER_BT_CONN_PREFIX);
	sep = serial->port[off];
	if (sep != '\0' && sep != '/')
		return 0;

	return 1;
}

/* The open() wrapper for BT ports. */
static int ser_bt_open(struct sr_serial_dev_inst *serial, int flags)
{
	enum ser_bt_conn_t conn_type;
	const char *remote_addr;
	size_t rfcomm_channel;
	uint16_t read_hdl, write_hdl, cccd_hdl, cccd_val;
	int rc;
	struct sr_bt_desc *desc;

	(void)flags;

	/* Derive BT specific parameters from the port spec. */
	rc = ser_bt_parse_conn_spec(serial, serial->port,
			&conn_type, &remote_addr,
			&rfcomm_channel,
			&read_hdl, &write_hdl,
			&cccd_hdl, &cccd_val);
	if (rc != SR_OK)
		return SR_ERR_ARG;

	if (!conn_type || !remote_addr || !remote_addr[0]) {
		/* TODO Auto-search for available connections? */
		return SR_ERR_NA;
	}

	/* Create the connection. Only store params after successful use. */
	desc = sr_bt_desc_new();
	if (!desc)
		return SR_ERR;
	serial->bt_desc = desc;
	rc = sr_bt_config_addr_remote(desc, remote_addr);
	if (rc < 0)
		return SR_ERR;
	serial->bt_addr_remote = g_strdup(remote_addr);
	switch (conn_type) {
	case SER_BT_CONN_RFCOMM:
		rc = sr_bt_config_rfcomm(desc, rfcomm_channel);
		if (rc < 0)
			return SR_ERR;
		serial->bt_rfcomm_channel = rfcomm_channel;
		break;
	case SER_BT_CONN_BLE122:
	case SER_BT_CONN_NRF51:
	case SER_BT_CONN_CC254x:
		rc = sr_bt_config_notify(desc,
			read_hdl, write_hdl, cccd_hdl, cccd_val);
		if (rc < 0)
			return SR_ERR;
		serial->bt_notify_handle_read = read_hdl;
		serial->bt_notify_handle_write = write_hdl;
		serial->bt_notify_handle_cccd = cccd_hdl;
		serial->bt_notify_value_cccd = cccd_val;
		break;
	default:
		/* Unsupported type, or incomplete implementation. */
		return SR_ERR_ARG;
	}
	serial->bt_conn_type = conn_type;

	/* Make sure the receive buffer can accept input data. */
	if (!serial->rcv_buffer)
		serial->rcv_buffer = g_string_sized_new(SER_BT_CHUNK_SIZE);
	rc = sr_bt_config_cb_data(desc, ser_bt_data_cb, serial);
	if (rc < 0)
		return SR_ERR;

	/* Open the connection. */
	switch (conn_type) {
	case SER_BT_CONN_RFCOMM:
		rc = sr_bt_connect_rfcomm(desc);
		if (rc < 0)
			return SR_ERR;
		break;
	case SER_BT_CONN_BLE122:
	case SER_BT_CONN_NRF51:
	case SER_BT_CONN_CC254x:
		rc = sr_bt_connect_ble(desc);
		if (rc < 0)
			return SR_ERR;
		rc = sr_bt_start_notify(desc);
		if (rc < 0)
			return SR_ERR;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int ser_bt_close(struct sr_serial_dev_inst *serial)
{
	if (!serial)
		return SR_ERR_ARG;

	if (!serial->bt_desc)
		return SR_OK;

	sr_bt_disconnect(serial->bt_desc);
	sr_bt_desc_free(serial->bt_desc);
	serial->bt_desc = NULL;

	g_free(serial->bt_addr_local);
	serial->bt_addr_local = NULL;
	g_free(serial->bt_addr_remote);
	serial->bt_addr_remote = NULL;
	g_slist_free_full(serial->bt_source_args, g_free);
	serial->bt_source_args = NULL;

	return SR_OK;
}

/* Flush, discards pending RX data, empties buffers. */
static int ser_bt_flush(struct sr_serial_dev_inst *serial)
{
	(void)serial;
	/* EMPTY */

	return SR_OK;
}

/* Drain, waits for completion of pending TX data. */
static int ser_bt_drain(struct sr_serial_dev_inst *serial)
{
	(void)serial;
	/* EMPTY */	/* TODO? */

	return SR_ERR_BUG;
}

static int ser_bt_write(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count,
		int nonblocking, unsigned int timeout_ms)
{
	ssize_t wrlen;

	/*
	 * TODO Support chunked transmission when callers' requests
	 * exceed the BT channel's capacity? See ser_hid_write().
	 */

	switch (serial->bt_conn_type) {
	case SER_BT_CONN_RFCOMM:
		(void)nonblocking;
		(void)timeout_ms;
		wrlen = sr_bt_write(serial->bt_desc, buf, count);
		if (wrlen < 0)
			return SR_ERR_IO;
		return wrlen;
	case SER_BT_CONN_BLE122:
	case SER_BT_CONN_NRF51:
	case SER_BT_CONN_CC254x:
		/*
		 * Assume that when applications call the serial layer's
		 * write routine, then the BLE chip/module does support
		 * a TX handle. Just call the serial-BT library's write
		 * routine.
		 */
		(void)nonblocking;
		(void)timeout_ms;
		wrlen = sr_bt_write(serial->bt_desc, buf, count);
		if (wrlen < 0)
			return SR_ERR_IO;
		return wrlen;
	default:
		return SR_ERR_ARG;
	}
	/* UNREACH */
}

static int ser_bt_read(struct sr_serial_dev_inst *serial,
		void *buf, size_t count,
		int nonblocking, unsigned int timeout_ms)
{
	gint64 deadline_us, now_us;
	uint8_t buffer[SER_BT_CHUNK_SIZE];
	ssize_t rdlen;
	int rc;
	size_t dlen;

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
	now_us = 0;	/* Silence a (false) compiler warning. */
	if (timeout_ms) {
		now_us = g_get_monotonic_time();
		deadline_us = now_us + timeout_ms * 1000;
	}

	/*
	 * Keep receiving from the port until the caller's requested
	 * amount of data has become available, or the timeout has
	 * expired. In the absence of a timeout, stop reading when an
	 * attempt no longer yields receive data.
	 */
	while (TRUE) {
		/* Run another attempt to receive data. */
		switch (serial->bt_conn_type) {
		case SER_BT_CONN_RFCOMM:
			rdlen = sr_bt_read(serial->bt_desc, buffer, sizeof(buffer));
			if (rdlen <= 0)
				break;
			rc = ser_bt_data_cb(serial, buffer, rdlen);
			if (rc < 0)
				rdlen = -1;
			break;
		case SER_BT_CONN_BLE122:
		case SER_BT_CONN_NRF51:
		case SER_BT_CONN_CC254x:
			dlen = sr_ser_has_queued_data(serial);
			rc = sr_bt_check_notify(serial->bt_desc);
			if (rc < 0)
				rdlen = -1;
			else if (sr_ser_has_queued_data(serial) != dlen)
				rdlen = +1;
			else
				rdlen = 0;
			break;
		default:
			rdlen = -1;
			break;
		}

		/*
		 * Stop upon receive errors, or timeout expiration. Only
		 * stop upon empty reception in the absence of a timeout.
		 */
		if (rdlen < 0)
			break;
		if (nonblocking && !rdlen)
			break;
		if (deadline_us) {
			now_us = g_get_monotonic_time();
			if (now_us > deadline_us)
				break;
		}

		/* Also stop when sufficient data has become available. */
		if (sr_ser_has_queued_data(serial) >= count)
			break;
	}

	/*
	 * Satisfy the caller's demand for receive data from previously
	 * queued incoming data.
	 */
	dlen = sr_ser_has_queued_data(serial);
	if (dlen > count)
		dlen = count;
	if (!dlen)
		return 0;

	return sr_ser_unqueue_rx_data(serial, buf, dlen);
}

struct bt_source_args_t {
	/* The application callback. */
	sr_receive_data_callback cb;
	void *cb_data;
	/* The serial device, to store RX data. */
	struct sr_serial_dev_inst *serial;
};

/*
 * Gets periodically invoked by the glib main loop. "Drives" (checks)
 * progress of BT communication, and invokes the application's callback
 * which processes RX data (when some has become available), as well as
 * handles application level timeouts.
 */
static int bt_source_cb(int fd, int revents, void *cb_data)
{
	struct bt_source_args_t *args;
	struct sr_serial_dev_inst *serial;
	uint8_t rx_buf[SER_BT_CHUNK_SIZE];
	ssize_t rdlen;
	size_t dlen;
	int rc;

	args = cb_data;
	if (!args)
		return -1;
	serial = args->serial;
	if (!serial)
		return -1;
	if (!serial->bt_conn_type)
		return -1;

	/*
	 * Drain receive data which the channel might have pending.
	 * This is "a copy" of the "background part" of ser_bt_read(),
	 * without the timeout support code, and not knowing how much
	 * data the application is expecting.
	 */
	do {
		switch (serial->bt_conn_type) {
		case SER_BT_CONN_RFCOMM:
			rdlen = sr_bt_read(serial->bt_desc, rx_buf, sizeof(rx_buf));
			if (rdlen <= 0)
				break;
			rc = ser_bt_data_cb(serial, rx_buf, rdlen);
			if (rc < 0)
				rdlen = -1;
			break;
		case SER_BT_CONN_BLE122:
		case SER_BT_CONN_NRF51:
		case SER_BT_CONN_CC254x:
			dlen = sr_ser_has_queued_data(serial);
			rc = sr_bt_check_notify(serial->bt_desc);
			if (rc < 0)
				rdlen = -1;
			else if (sr_ser_has_queued_data(serial) != dlen)
				rdlen = +1;
			else
				rdlen = 0;
			break;
		default:
			rdlen = -1;
			break;
		}
	} while (rdlen > 0);

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

/* TODO Can we use the Bluetooth socket's file descriptor? Probably not portably. */
#define WITH_MAXIMUM_TIMEOUT_VALUE	0
static int ser_bt_setup_source_add(struct sr_session *session,
		struct sr_serial_dev_inst *serial,
		int events, int timeout,
		sr_receive_data_callback cb, void *cb_data)
{
	struct bt_source_args_t *args;
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
	 * BT specific GSource.
	 */
	rc = sr_session_source_add(session, -1, events, timeout, bt_source_cb, args);
	if (rc != SR_OK) {
		g_free(args);
		return rc;
	}
	serial->bt_source_args = g_slist_append(serial->bt_source_args, args);

	return SR_OK;
}

static int ser_bt_setup_source_remove(struct sr_session *session,
		struct sr_serial_dev_inst *serial)
{
	(void)serial;

	(void)sr_session_source_remove(session, -1);
	/* Release callback args here already? */

	return SR_OK;
}

static enum ser_bt_conn_t scan_is_supported(const char *name)
{
	size_t idx;
	const struct scan_supported_item *item;

	for (idx = 0; idx < ARRAY_SIZE(scan_supported_items); idx++) {
		item = &scan_supported_items[idx];
		if (!item->name)
			break;
		if (strcmp(name, item->name) != 0)
			continue;
		return item->type;
	}

	return SER_BT_CONN_UNKNOWN;
}

struct bt_scan_args_t {
	GSList *port_list;
	sr_ser_list_append_t append;
	GSList *addr_list;
	const char *bt_type;
};

static void scan_cb(void *cb_args, const char *addr, const char *name)
{
	struct bt_scan_args_t *scan_args;
	GSList *l;
	char addr_text[20];
	enum ser_bt_conn_t type;
	char *port_name, *port_desc;
	char *addr_copy;

	scan_args = cb_args;
	if (!scan_args)
		return;
	sr_info("BT scan, found: %s - %s\n", addr, name);

	/* Check whether the device was seen before. */
	for (l = scan_args->addr_list; l; l = l->next) {
		if (strcmp(addr, l->data) == 0)
			return;
	}

	/* Substitute colons in the address by dashes. */
	if (!addr || !*addr)
		return;
	snprintf(addr_text, sizeof(addr_text), "%s", addr);
	g_strcanon(addr_text, "0123456789abcdefABCDEF", '-');

	/* Create a port name, and a description. */
	type = scan_is_supported(name);
	port_name = g_strdup_printf("%s/%s/%s",
		SER_BT_CONN_PREFIX, conn_name_text(type), addr_text);
	port_desc = g_strdup_printf("%s (%s)", name, scan_args->bt_type);

	scan_args->port_list = scan_args->append(scan_args->port_list, port_name, port_desc);
	g_free(port_name);
	g_free(port_desc);

	/* Keep track of the handled address. */
	addr_copy = g_strdup(addr);
	scan_args->addr_list = g_slist_append(scan_args->addr_list, addr_copy);
}

static GSList *ser_bt_list(GSList *list, sr_ser_list_append_t append)
{
	static const int scan_duration = 2;

	struct bt_scan_args_t scan_args;
	struct sr_bt_desc *desc;

	/*
	 * Implementor's note: This "list" routine is best-effort. We
	 * assume that registering callbacks always succeeds. Silently
	 * ignore failure to scan for devices. Just return those which
	 * we happen to find.
	 */

	desc = sr_bt_desc_new();
	if (!desc)
		return list;

	memset(&scan_args, 0, sizeof(scan_args));
	scan_args.port_list = list;
	scan_args.append = append;

	scan_args.addr_list = NULL;
	scan_args.bt_type = "BT";
	(void)sr_bt_config_cb_scan(desc, scan_cb, &scan_args);
	(void)sr_bt_scan_bt(desc, scan_duration);
	g_slist_free_full(scan_args.addr_list, g_free);

	scan_args.addr_list = NULL;
	scan_args.bt_type = "BLE";
	(void)sr_bt_config_cb_scan(desc, scan_cb, &scan_args);
	(void)sr_bt_scan_le(desc, scan_duration);
	g_slist_free_full(scan_args.addr_list, g_free);

	sr_bt_desc_free(desc);

	return scan_args.port_list;
}

static struct ser_lib_functions serlib_bt = {
	.open = ser_bt_open,
	.close = ser_bt_close,
	.flush = ser_bt_flush,
	.drain = ser_bt_drain,
	.write = ser_bt_write,
	.read = ser_bt_read,
	/*
	 * Bluetooth communication has no concept of bitrate, so ignore
	 * these arguments silently. Neither need we pass the frame format
	 * down to internal BT comm routines, nor need we keep the values
	 * here, since the caller will cache/register them already.
	 */
	.set_params = std_dummy_set_params,
	.set_handshake = std_dummy_set_handshake,
	.setup_source_add = ser_bt_setup_source_add,
	.setup_source_remove = ser_bt_setup_source_remove,
	.list = ser_bt_list,
	.get_frame_format = NULL,
};
SR_PRIV struct ser_lib_functions *ser_lib_funcs_bt = &serlib_bt;

/* }}} */
#else

SR_PRIV int ser_name_is_bt(struct sr_serial_dev_inst *serial)
{
	(void)serial;

	return 0;
}

SR_PRIV struct ser_lib_functions *ser_lib_funcs_bt = NULL;

#endif
#endif

/** @} */
