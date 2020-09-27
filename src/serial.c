/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2010-2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2014 Uffe Jakobsen <uffe@uffe.org>
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

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>
#ifdef HAVE_LIBSERIALPORT
#include <libserialport.h>
#endif
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#ifdef _WIN32
#include <windows.h> /* for HANDLE */
#endif

/** @cond PRIVATE */
#define LOG_PREFIX "serial"
/** @endcond */

/**
 * @file
 *
 * Serial port handling.
 */

/**
 * @defgroup grp_serial Serial port handling
 *
 * Serial port handling functions.
 *
 * @{
 */

#ifdef HAVE_SERIAL_COMM

/* See if an (assumed opened) serial port is of any supported type. */
static int dev_is_supported(struct sr_serial_dev_inst *serial)
{
	if (!serial || !serial->lib_funcs)
		return 0;

	return 1;
}

/**
 * Open the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] flags Flags to use when opening the serial port. Possible flags
 *                  include SERIAL_RDWR, SERIAL_RDONLY.
 *
 * If the serial structure contains a serialcomm string, it will be
 * passed to serial_set_paramstr() after the port is opened.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags)
{
	int ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Opening serial port '%s' (flags %d).", serial->port, flags);

	/*
	 * Determine which serial transport library to use. Derive the
	 * variant from the serial port's name. Default to libserialport
	 * for backwards compatibility.
	 */
	if (ser_name_is_hid(serial))
		serial->lib_funcs = ser_lib_funcs_hid;
	else if (ser_name_is_bt(serial))
		serial->lib_funcs = ser_lib_funcs_bt;
	else
		serial->lib_funcs = ser_lib_funcs_libsp;
	if (!serial->lib_funcs)
		return SR_ERR_NA;

	/*
	 * Note that use of the 'rcv_buffer' is optional, and the buffer's
	 * size heavily depends on the specific transport. That's why the
	 * buffer's content gets accessed and the buffer is released here in
	 * common code, but the buffer gets allocated in libraries' open()
	 * routines.
	 */

	/*
	 * Run the transport's open routine. Setup the bitrate and the
	 * UART frame format.
	 */
	if (!serial->lib_funcs->open)
		return SR_ERR_NA;
	ret = serial->lib_funcs->open(serial, flags);
	if (ret != SR_OK)
		return ret;

	if (serial->serialcomm) {
		ret = serial_set_paramstr(serial, serial->serialcomm);
		if (ret != SR_OK)
			return ret;
	}

	/*
	 * Flush potentially dangling RX data. Availability of the
	 * flush primitive depends on the transport/cable, absense
	 * is non-fatal.
	 */
	ret = serial_flush(serial);
	if (ret == SR_ERR_NA)
		ret = SR_OK;
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

/**
 * Close the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_close(struct sr_serial_dev_inst *serial)
{
	int rc;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Closing serial port %s.", serial->port);

	if (!serial->lib_funcs || !serial->lib_funcs->close)
		return SR_ERR_NA;

	rc = serial->lib_funcs->close(serial);
	if (rc == SR_OK && serial->rcv_buffer) {
		g_string_free(serial->rcv_buffer, TRUE);
		serial->rcv_buffer = NULL;
	}

	return rc;
}

/**
 * Flush serial port buffers. Empty buffers, discard pending RX and TX data.
 *
 * @param serial Previously initialized serial port structure.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_flush(struct sr_serial_dev_inst *serial)
{
	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Flushing serial port %s.", serial->port);

	sr_ser_discard_queued_data(serial);

	if (!serial->lib_funcs || !serial->lib_funcs->flush)
		return SR_ERR_NA;

	return serial->lib_funcs->flush(serial);
}

/**
 * Drain serial port buffers. Wait for pending TX data to be sent.
 *
 * @param serial Previously initialized serial port structure.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_drain(struct sr_serial_dev_inst *serial)
{
	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Draining serial port %s.", serial->port);

	if (!serial->lib_funcs || !serial->lib_funcs->drain)
		return SR_ERR_NA;

	return serial->lib_funcs->drain(serial);
}

/*
 * Provide an internal RX data buffer for the serial port. This is not
 * supposed to be used directly by applications. Instead optional and
 * alternative transports for serial communication can use this buffer
 * if their progress is driven from background activity, and is not
 * (directly) driven by external API calls.
 *
 * BEWARE! This implementation assumes that data which gets communicated
 * via UART can get stored in a GString (which is a char array). Since
 * the API hides this detail, we can address this issue later when needed.
 * Callers use the API which communicates bytes.
 *
 * Applications optionally can register a "per RX chunk" callback, when
 * they depend on the frame boundaries of the respective physical layer.
 * Most callers just want the stream of RX data, and can use the buffer.
 *
 * The availability of RX chunks to callbacks, as well as the capability
 * to pass on exact frames as chunks or potential re-assembly of chunks
 * to a single data block, depend on each transport's implementation.
 */

/**
 * Register application callback for RX data chunks.
 *
 * @param[in] serial Previously initialized serial port instance.
 * @param[in] cb Routine to call as RX data becomes available.
 * @param[in] cb_data User data to pass to the callback in addition to RX data.
 *
 * @retval SR_ERR_ARG Invalid parameters.
 * @retval SR_OK Successful registration.
 *
 * Callbacks get unregistered by specifying NULL for the 'cb' parameter.
 *
 * @private
 */
SR_PRIV int serial_set_read_chunk_cb(struct sr_serial_dev_inst *serial,
	serial_rx_chunk_callback cb, void *cb_data)
{
	if (!serial)
		return SR_ERR_ARG;

	serial->rx_chunk_cb_func = cb;
	serial->rx_chunk_cb_data = cb_data;

	return SR_OK;
}

/**
 * Discard previously queued RX data. Internal to the serial subsystem,
 * coordination between common and transport specific support code.
 *
 * @param[in] serial Previously opened serial port instance.
 *
 * @private
 */
SR_PRIV void sr_ser_discard_queued_data(struct sr_serial_dev_inst *serial)
{
	if (!serial || !serial->rcv_buffer)
		return;

	g_string_truncate(serial->rcv_buffer, 0);
}

/**
 * Get amount of queued RX data. Internal to the serial subsystem,
 * coordination between common and transport specific support code.
 *
 * @param[in] serial Previously opened serial port instance.
 *
 * @private
 */
SR_PRIV size_t sr_ser_has_queued_data(struct sr_serial_dev_inst *serial)
{
	if (!serial || !serial->rcv_buffer)
		return 0;

	return serial->rcv_buffer->len;
}

/**
 * Queue received data. Internal to the serial subsystem, coordination
 * between common and transport specific support code.
 *
 * @param[in] serial Previously opened serial port instance.
 * @param[in] data Pointer to data bytes to queue.
 * @param[in] len Number of data bytes to queue.
 *
 * @private
 */
SR_PRIV void sr_ser_queue_rx_data(struct sr_serial_dev_inst *serial,
	const uint8_t *data, size_t len)
{
	if (!serial || !data || !len)
		return;

	if (serial->rx_chunk_cb_func)
		serial->rx_chunk_cb_func(serial, serial->rx_chunk_cb_data, data, len);
	else if (serial->rcv_buffer)
		g_string_append_len(serial->rcv_buffer, (const gchar *)data, len);
}

/**
 * Retrieve previously queued RX data. Internal to the serial subsystem,
 * coordination between common and transport specific support code.
 *
 * @param[in] serial Previously opened serial port instance.
 * @param[out] data Pointer to store retrieved data bytes into.
 * @param[in] len Number of data bytes to retrieve.
 *
 * @private
 */
SR_PRIV size_t sr_ser_unqueue_rx_data(struct sr_serial_dev_inst *serial,
	uint8_t *data, size_t len)
{
	size_t qlen;
	GString *buf;

	if (!serial || !data || !len)
		return 0;

	qlen = sr_ser_has_queued_data(serial);
	if (!qlen)
		return 0;

	buf = serial->rcv_buffer;
	if (len > buf->len)
		len = buf->len;
	if (len) {
		memcpy(data, buf->str, len);
		g_string_erase(buf, 0, len);
	}

	return len;
}

/**
 * Check for available receive data.
 *
 * @param[in] serial Previously opened serial port instance.
 *
 * @returns The number of (known) available RX data bytes.
 *
 * Returns 0 if no receive data is available, or if the amount of
 * available receive data cannot get determined.
 *
 * @private
 */
SR_PRIV size_t serial_has_receive_data(struct sr_serial_dev_inst *serial)
{
	size_t lib_count, buf_count;

	if (!serial)
		return 0;

	lib_count = 0;
	if (serial->lib_funcs && serial->lib_funcs->get_rx_avail)
		lib_count = serial->lib_funcs->get_rx_avail(serial);

	buf_count = sr_ser_has_queued_data(serial);

	return lib_count + buf_count;
}

static int _serial_write(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	ssize_t ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!serial->lib_funcs || !serial->lib_funcs->write)
		return SR_ERR_NA;
	ret = serial->lib_funcs->write(serial, buf, count,
		nonblocking, timeout_ms);
	sr_spew("Wrote %zd/%zu bytes.", ret, count);

	return ret;
}

/**
 * Write a number of bytes to the specified serial port, blocking until finished.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] buf Buffer containing the bytes to write.
 * @param[in] count Number of bytes to write.
 * @param[in] timeout_ms Timeout in ms, or 0 for no timeout.
 *
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Other error.
 * @retval other The number of bytes written. If this is less than the number
 * specified in the call, the timeout was reached.
 *
 * @private
 */
SR_PRIV int serial_write_blocking(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count, unsigned int timeout_ms)
{
	return _serial_write(serial, buf, count, 0, timeout_ms);
}

/**
 * Write a number of bytes to the specified serial port, return immediately.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] buf Buffer containing the bytes to write.
 * @param[in] count Number of bytes to write.
 *
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Other error.
 * @retval other The number of bytes written.
 *
 * @private
 */
SR_PRIV int serial_write_nonblocking(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count)
{
	return _serial_write(serial, buf, count, 1, 0);
}

static int _serial_read(struct sr_serial_dev_inst *serial,
	void *buf, size_t count, int nonblocking, unsigned int timeout_ms)
{
	ssize_t ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!serial->lib_funcs || !serial->lib_funcs->read)
		return SR_ERR_NA;
	ret = serial->lib_funcs->read(serial, buf, count,
		nonblocking, timeout_ms);
	if (ret > 0)
		sr_spew("Read %zd/%zu bytes.", ret, count);

	return ret;
}

/**
 * Read a number of bytes from the specified serial port, block until finished.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer where to store the bytes that are read.
 * @param[in] count The number of bytes to read.
 * @param[in] timeout_ms Timeout in ms, or 0 for no timeout.
 *
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Other error.
 * @retval other The number of bytes read. If this is less than the number
 * requested, the timeout was reached.
 *
 * @private
 */
SR_PRIV int serial_read_blocking(struct sr_serial_dev_inst *serial,
	void *buf, size_t count, unsigned int timeout_ms)
{
	return _serial_read(serial, buf, count, 0, timeout_ms);
}

/**
 * Try to read up to @a count bytes from the specified serial port, return
 * immediately with what's available.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer where to store the bytes that are read.
 * @param[in] count The number of bytes to read.
 *
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR Other error.
 * @retval other The number of bytes read.
 *
 * @private
 */
SR_PRIV int serial_read_nonblocking(struct sr_serial_dev_inst *serial,
	void *buf, size_t count)
{
	return _serial_read(serial, buf, count, 1, 0);
}

/**
 * Set serial parameters for the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] baudrate The baudrate to set.
 * @param[in] bits The number of data bits to use (5, 6, 7 or 8).
 * @param[in] parity The parity setting to use (0 = none, 1 = even, 2 = odd).
 * @param[in] stopbits The number of stop bits to use (1 or 2).
 * @param[in] flowcontrol The flow control settings to use (0 = none,
 *                        1 = RTS/CTS, 2 = XON/XOFF).
 * @param[in] rts Status of RTS line (0 or 1; required by some interfaces).
 * @param[in] dtr Status of DTR line (0 or 1; required by some interfaces).
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	int ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Setting serial parameters on port %s.", serial->port);

	if (!serial->lib_funcs || !serial->lib_funcs->set_params)
		return SR_ERR_NA;
	ret = serial->lib_funcs->set_params(serial,
		baudrate, bits, parity, stopbits,
		flowcontrol, rts, dtr);
	if (ret == SR_OK) {
		serial->comm_params.bit_rate = baudrate;
		serial->comm_params.data_bits = bits;
		serial->comm_params.parity_bits = parity ? 1 : 0;
		serial->comm_params.stop_bits = stopbits;
		sr_dbg("DBG: %s() rate %d, %d%s%d", __func__,
				baudrate, bits,
				(parity == 0) ? "n" : "x",
				stopbits);
	}

	return ret;
}

/**
 * Manipulate handshake state for the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] rts Status of RTS line (0 or 1; or -1 to ignore).
 * @param[in] dtr Status of DTR line (0 or 1; or -1 to ignore).
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_set_handshake(struct sr_serial_dev_inst *serial,
	int rts, int dtr)
{
	int ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Modifying serial parameters on port %s.", serial->port);

	if (!serial->lib_funcs || !serial->lib_funcs->set_handshake)
		return SR_ERR_NA;
	ret = serial->lib_funcs->set_handshake(serial, rts, dtr);

	return ret;
}

/**
 * Set serial parameters for the specified serial port from parameter string.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] paramstr A serial communication parameters string of the form
 * "<baudrate>/<bits><parity><stopbits>{/<option>}".\n
 * Examples: "9600/8n1", "600/7o2/dtr=1/rts=0" or "460800/8n1/flow=2".\n
 * \<baudrate\>=integer Baud rate.\n
 * \<bits\>=5|6|7|8 Number of data bits.\n
 * \<parity\>=n|e|o None, even, odd.\n
 * \<stopbits\>=1|2 One or two stop bits.\n
 * Options:\n
 * dtr=0|1 Set DTR off resp. on.\n
 * flow=0|1|2 Flow control. 0 for none, 1 for RTS/CTS, 2 for XON/XOFF.\n
 * rts=0|1 Set RTS off resp. on.\n
 * Please note that values and combinations of these parameters must be
 * supported by the concrete serial interface hardware and the drivers for it.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
	const char *paramstr)
{
/** @cond PRIVATE */
#define SERIAL_COMM_SPEC "^(\\d+)(/([5678])([neo])([12]))?(.*)$"
/** @endcond */

	GRegex *reg;
	GMatchInfo *match;
	int speed, databits, parity, stopbits, flow, rts, dtr, i;
	char *mstr, **opts, **kv;

	speed = flow = 0;
	databits = 8;
	parity = SP_PARITY_NONE;
	stopbits = 1;
	rts = dtr = -1;
	sr_spew("Parsing parameters from \"%s\".", paramstr);
	reg = g_regex_new(SERIAL_COMM_SPEC, 0, 0, NULL);
	if (g_regex_match(reg, paramstr, 0, &match)) {
		if ((mstr = g_match_info_fetch(match, 1)))
			speed = strtoul(mstr, NULL, 10);
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 3)) && mstr[0])
			databits = strtoul(mstr, NULL, 10);
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 4)) && mstr[0]) {
			switch (mstr[0]) {
			case 'n':
				parity = SP_PARITY_NONE;
				break;
			case 'e':
				parity = SP_PARITY_EVEN;
				break;
			case 'o':
				parity = SP_PARITY_ODD;
				break;
			}
		}
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 5)) && mstr[0])
			stopbits = strtoul(mstr, NULL, 10);
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 6)) && mstr[0] != '\0') {
			if (mstr[0] != '/') {
				sr_dbg("missing separator before extra options");
				speed = 0;
			} else {
				/* A set of "key=value" options separated by / */
				opts = g_strsplit(mstr + 1, "/", 0);
				for (i = 0; opts[i]; i++) {
					kv = g_strsplit(opts[i], "=", 2);
					if (!strncmp(kv[0], "rts", 3)) {
						if (kv[1][0] == '1')
							rts = 1;
						else if (kv[1][0] == '0')
							rts = 0;
						else {
							sr_dbg("invalid value for rts: %c", kv[1][0]);
							speed = 0;
						}
					} else if (!strncmp(kv[0], "dtr", 3)) {
						if (kv[1][0] == '1')
							dtr = 1;
						else if (kv[1][0] == '0')
							dtr = 0;
						else {
							sr_dbg("invalid value for dtr: %c", kv[1][0]);
							speed = 0;
						}
					} else if (!strncmp(kv[0], "flow", 4)) {
						if (kv[1][0] == '0')
							flow = 0;
						else if (kv[1][0] == '1')
							flow = 1;
						else if (kv[1][0] == '2')
							flow = 2;
						else {
							sr_dbg("invalid value for flow: %c", kv[1][0]);
							speed = 0;
						}
					}
					g_strfreev(kv);
				}
				g_strfreev(opts);
			}
		}
		g_free(mstr);
	}
	g_match_info_unref(match);
	g_regex_unref(reg);
	sr_spew("Got params: rate %d, frame %d/%d/%d, flow %d, rts %d, dtr %d.",
		speed, databits, parity, stopbits, flow, rts, dtr);

	if (!speed) {
		sr_dbg("Could not infer speed from parameter string.");
		return SR_ERR_ARG;
	}

	return serial_set_params(serial, speed,
			databits, parity, stopbits,
			flow, rts, dtr);
}

/**
 * Read a line from the specified serial port.
 *
 * @param[in] serial Previously initialized serial port structure.
 * @param[out] buf Buffer where to store the bytes that are read.
 * @param[in] buflen Size of the buffer.
 * @param[in] timeout_ms How long to wait for a line to come in.
 *
 * Reading stops when CR or LF is found, which is stripped from the buffer.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_readline(struct sr_serial_dev_inst *serial,
	char **buf, int *buflen, gint64 timeout_ms)
{
	gint64 start, remaining;
	int maxlen, len;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!dev_is_supported(serial)) {
		sr_dbg("Cannot use unopened serial port %s.", serial->port);
		return -1;
	}

	start = g_get_monotonic_time();
	remaining = timeout_ms;

	maxlen = *buflen;
	*buflen = len = 0;
	while (1) {
		len = maxlen - *buflen - 1;
		if (len < 1)
			break;
		len = serial_read_blocking(serial, *buf + *buflen, 1, remaining);
		if (len > 0) {
			*buflen += len;
			*(*buf + *buflen) = '\0';
			if (*buflen > 0 && (*(*buf + *buflen - 1) == '\r'
					|| *(*buf + *buflen - 1) == '\n')) {
				/* Strip CR/LF and terminate. */
				*(*buf + --*buflen) = '\0';
				break;
			}
		}
		/* Reduce timeout by time elapsed. */
		remaining = timeout_ms - ((g_get_monotonic_time() - start) / 1000);
		if (remaining <= 0)
			/* Timeout */
			break;
		if (len < 1)
			g_usleep(2000);
	}
	if (*buflen)
		sr_dbg("Received %d: '%s'.", *buflen, *buf);

	return SR_OK;
}

/**
 * Try to find a valid packet in a serial data stream.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer containing the bytes to write.
 * @param buflen Size of the buffer.
 * @param[in] packet_size Size, in bytes, of a valid packet.
 * @param is_valid Callback that assesses whether the packet is valid or not.
 * @param[in] timeout_ms The timeout after which, if no packet is detected, to
 *                       abort scanning.
 *
 * @retval SR_OK Valid packet was found within the given timeout.
 * @retval SR_ERR Failure.
 *
 * @private
 */
SR_PRIV int serial_stream_detect(struct sr_serial_dev_inst *serial,
	uint8_t *buf, size_t *buflen,
	size_t packet_size,
	packet_valid_callback is_valid,
	uint64_t timeout_ms)
{
	uint64_t start, time, byte_delay_us;
	size_t ibuf, i, maxlen;
	ssize_t len;

	maxlen = *buflen;

	sr_dbg("Detecting packets on %s (timeout = %" PRIu64 "ms).",
		serial->port, timeout_ms);

	if (maxlen < (packet_size * 2) ) {
		sr_err("Buffer size must be at least twice the packet size.");
		return SR_ERR;
	}

	/* Assume 8n1 transmission. That is 10 bits for every byte. */
	byte_delay_us = serial_timeout(serial, 1) * 1000;
	start = g_get_monotonic_time();

	i = ibuf = len = 0;
	while (ibuf < maxlen) {
		len = serial_read_nonblocking(serial, &buf[ibuf], 1);
		if (len > 0) {
			ibuf += len;
		} else if (len == 0) {
			/* No logging, already done in serial_read(). */
		} else {
			/* Error reading byte, but continuing anyway. */
		}

		time = g_get_monotonic_time() - start;
		time /= 1000;

		if ((ibuf - i) >= packet_size) {
			GString *text;
			/* We have at least a packet's worth of data. */
			text = sr_hexdump_new(&buf[i], packet_size);
			sr_spew("Trying packet: %s", text->str);
			sr_hexdump_free(text);
			if (is_valid(&buf[i])) {
				sr_spew("Found valid %zu-byte packet after "
					"%" PRIu64 "ms.", (ibuf - i), time);
				*buflen = ibuf;
				return SR_OK;
			} else {
				sr_spew("Got %zu bytes, but not a valid "
					"packet.", (ibuf - i));
			}
			/* Not a valid packet. Continue searching. */
			i++;
		}
		if (time >= timeout_ms) {
			/* Timeout */
			sr_dbg("Detection timed out after %" PRIu64 "ms.", time);
			break;
		}
		if (len < 1)
			g_usleep(byte_delay_us);
	}

	*buflen = ibuf;

	sr_info("Didn't find a valid packet (read %zu bytes).", *buflen);

	return SR_ERR;
}

/**
 * Extract the serial device and options from the options linked list.
 *
 * @param options List of options passed from the command line.
 * @param serial_device Pointer where to store the extracted serial device.
 * @param serial_options Pointer where to store the optional extracted serial
 * options.
 *
 * @return SR_OK if a serial_device is found, SR_ERR if no device is found. The
 * returned string should not be freed by the caller.
 *
 * @private
 */
SR_PRIV int sr_serial_extract_options(GSList *options,
	const char **serial_device, const char **serial_options)
{
	GSList *l;
	struct sr_config *src;

	*serial_device = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			*serial_device = g_variant_get_string(src->data, NULL);
			sr_dbg("Parsed serial device: %s.", *serial_device);
			break;
		case SR_CONF_SERIALCOMM:
			*serial_options = g_variant_get_string(src->data, NULL);
			sr_dbg("Parsed serial options: %s.", *serial_options);
			break;
		}
	}

	if (!*serial_device) {
		sr_dbg("No serial device specified.");
		return SR_ERR;
	}

	return SR_OK;
}

/** @private */
SR_PRIV int serial_source_add(struct sr_session *session,
	struct sr_serial_dev_inst *serial, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
	if ((events & (G_IO_IN | G_IO_ERR)) && (events & G_IO_OUT)) {
		sr_err("Cannot poll input/error and output simultaneously.");
		return SR_ERR_ARG;
	}

	if (!dev_is_supported(serial)) {
		sr_err("Invalid serial port.");
		return SR_ERR_ARG;
	}

	if (!serial->lib_funcs || !serial->lib_funcs->setup_source_add)
		return SR_ERR_NA;

	return serial->lib_funcs->setup_source_add(session, serial,
		events, timeout, cb, cb_data);
}

/** @private */
SR_PRIV int serial_source_remove(struct sr_session *session,
	struct sr_serial_dev_inst *serial)
{
	if (!dev_is_supported(serial)) {
		sr_err("Invalid serial port.");
		return SR_ERR_ARG;
	}

	if (!serial->lib_funcs || !serial->lib_funcs->setup_source_remove)
		return SR_ERR_NA;

	return serial->lib_funcs->setup_source_remove(session, serial);
}

/**
 * Create/allocate a new sr_serial_port structure.
 *
 * @param name The OS dependent name of the serial port. Must not be NULL.
 * @param description An end user friendly description for the serial port.
 *                    Can be NULL (in that case the empty string is used
 *                    as description).
 *
 * @return The newly allocated sr_serial_port struct.
 */
static struct sr_serial_port *sr_serial_new(const char *name,
	const char *description)
{
	struct sr_serial_port *serial;

	if (!name)
		return NULL;

	serial = g_malloc0(sizeof(*serial));
	serial->name = g_strdup(name);
	serial->description = g_strdup(description ? description : "");

	return serial;
}

/**
 * Free a previously allocated sr_serial_port structure.
 *
 * @param serial The sr_serial_port struct to free. Must not be NULL.
 */
SR_API void sr_serial_free(struct sr_serial_port *serial)
{
	if (!serial)
		return;
	g_free(serial->name);
	g_free(serial->description);
	g_free(serial);
}

static GSList *append_port_list(GSList *devs, const char *name, const char *desc)
{
	return g_slist_append(devs, sr_serial_new(name, desc));
}

/**
 * List available serial devices.
 *
 * @return A GSList of strings containing the path of the serial devices or
 *         NULL if no serial device is found. The returned list must be freed
 *         by the caller.
 */
SR_API GSList *sr_serial_list(const struct sr_dev_driver *driver)
{
	GSList *tty_devs;
	GSList *(*list_func)(GSList *list, sr_ser_list_append_t append);

	/* Currently unused, but will be used by some drivers later on. */
	(void)driver;

	tty_devs = NULL;
	if (ser_lib_funcs_libsp && ser_lib_funcs_libsp->list) {
		list_func = ser_lib_funcs_libsp->list;
		tty_devs = list_func(tty_devs, append_port_list);
	}
	if (ser_lib_funcs_hid && ser_lib_funcs_hid->list) {
		list_func = ser_lib_funcs_hid->list;
		tty_devs = list_func(tty_devs, append_port_list);
	}
	if (ser_lib_funcs_bt && ser_lib_funcs_bt->list) {
		list_func = ser_lib_funcs_bt->list;
		tty_devs = list_func(tty_devs, append_port_list);
	}

	return tty_devs;
}

static GSList *append_port_find(GSList *devs, const char *name)
{
	if (!name || !*name)
		return devs;

	return g_slist_append(devs, g_strdup(name));
}

/**
 * Find USB serial devices via the USB vendor ID and product ID.
 *
 * @param[in] vendor_id Vendor ID of the USB device.
 * @param[in] product_id Product ID of the USB device.
 *
 * @return A GSList of strings containing the path of the serial device or
 *         NULL if no serial device is found. The returned list must be freed
 *         by the caller.
 *
 * @private
 */
SR_PRIV GSList *sr_serial_find_usb(uint16_t vendor_id, uint16_t product_id)
{
	GSList *tty_devs;
	GSList *(*find_func)(GSList *list, sr_ser_find_append_t append,
			uint16_t vid, uint16_t pid);

	tty_devs = NULL;
	if (ser_lib_funcs_libsp && ser_lib_funcs_libsp->find_usb) {
		find_func = ser_lib_funcs_libsp->find_usb;
		tty_devs = find_func(tty_devs, append_port_find,
			vendor_id, product_id);
	}
	if (ser_lib_funcs_hid && ser_lib_funcs_hid->find_usb) {
		find_func = ser_lib_funcs_hid->find_usb;
		tty_devs = find_func(tty_devs, append_port_find,
			vendor_id, product_id);
	}

	return tty_devs;
}

/** @private */
SR_PRIV int serial_timeout(struct sr_serial_dev_inst *port, int num_bytes)
{
	int bits, baud, ret, timeout_ms;

	/* Get the bitrate and frame length. */
	bits = baud = 0;
	if (port->lib_funcs && port->lib_funcs->get_frame_format) {
		ret = port->lib_funcs->get_frame_format(port, &baud, &bits);
		if (ret != SR_OK)
			bits = baud = 0;
	} else {
		baud = port->comm_params.bit_rate;
		bits = 1 + port->comm_params.data_bits +
			port->comm_params.parity_bits +
			port->comm_params.stop_bits;
	}

	/* Derive the timeout. Default to 1s. */
	timeout_ms = 1000;
	if (bits && baud) {
		/* Throw in 10ms for misc OS overhead. */
		timeout_ms = 10;
		timeout_ms += ((1000.0 / baud) * bits) * num_bytes;
	}

	return timeout_ms;
}

#else

/* TODO Put fallback.c content here? */

#endif

/** @} */
