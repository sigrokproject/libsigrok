/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2010-2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <libserialport.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "serial: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

/**
 * Open the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param flags Flags to use when opening the serial port. Possible flags
 *              include SERIAL_RDWR, SERIAL_RDONLY, SERIAL_NONBLOCK.
 *
 * If the serial structure contains a serialcomm string, it will be
 * passed to serial_set_paramstr() after the port is opened.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags)
{
	int ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Opening serial port '%s' (flags %d).", serial->port, flags);

	sp_get_port_by_name(serial->port, &serial->data);

	ret = sp_open(serial->data, flags);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to open serial port with invalid parameters.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error opening port: %s.", error);
		sp_free_error_message(error);
		return SR_ERR;
	}

#ifndef _WIN32
	serial->fd = serial->data->fd;
#endif

	if (serial->serialcomm)
		return serial_set_paramstr(serial, serial->serialcomm);
	else
		return SR_OK;
}

/**
 * Close the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int serial_close(struct sr_serial_dev_inst *serial)
{
	int ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot close unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return SR_ERR;
	}

	sr_spew("Closing serial port %s (fd %d).", serial->port, serial->fd);

	ret = sp_close(serial->data);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to close an invalid serial port.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error closing port: %s.", error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	serial->fd = -1;

	return SR_OK;
}

/**
 * Flush serial port buffers.
 *
 * @param serial Previously initialized serial port structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int serial_flush(struct sr_serial_dev_inst *serial)
{
	int ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot flush unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return SR_ERR;
	}

	sr_spew("Flushing serial port %s (fd %d).", serial->port, serial->fd);

	ret = sp_flush(serial->data);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to flush an invalid serial port.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error flushing port: %s.", error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * Write a number of bytes to the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer containing the bytes to write.
 * @param count Number of bytes to write.
 *
 * @return The number of bytes written, or a negative error code upon failure.
 */
SR_PRIV int serial_write(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count)
{
	ssize_t ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot use unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return SR_ERR;
	}

	ret = sp_write(serial->data, buf, count);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempted serial port write with invalid arguments.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Write error: %s.", error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	sr_spew("Wrote %d/%d bytes (fd %d).", ret, count, serial->fd);

	return ret;
}

/**
 * Read a number of bytes from the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer where to store the bytes that are read.
 * @param count The number of bytes to read.
 *
 * @return The number of bytes read, or a negative error code upon failure.
 */
SR_PRIV int serial_read(struct sr_serial_dev_inst *serial, void *buf,
		size_t count)
{
	ssize_t ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot use unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return SR_ERR;
	}

	ret = sp_read(serial->data, buf, count);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempted serial port read with invalid arguments.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Read error: %s.", error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	sr_spew("Read %d/%d bytes (fd %d).", ret, count, serial->fd);

	return ret;
}

/**
 * Set serial parameters for the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param baudrate The baudrate to set.
 * @param bits The number of data bits to use.
 * @param parity The parity setting to use (0 = none, 1 = even, 2 = odd).
 * @param stopbits The number of stop bits to use (1 or 2).
 * @param flowcontrol The flow control settings to use (0 = none, 1 = RTS/CTS,
 *                    2 = XON/XOFF).
 *
 * @return SR_OK upon success, SR_ERR upon failure.
 */
SR_PRIV int serial_set_params(struct sr_serial_dev_inst *serial, int baudrate,
			      int bits, int parity, int stopbits,
			      int flowcontrol, int rts, int dtr)
{
	int ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot configure unopened serial port %s (fd %d).",
		       serial->port, serial->fd);
		return SR_ERR;
	}

	sr_spew("Setting serial parameters on port %s (fd %d).", serial->port,
		serial->fd);

	ret = sp_set_params(serial->data, baudrate, bits, parity, stopbits,
			flowcontrol, rts, dtr);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Invalid arguments for setting serial port parameters.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error setting serial port parameters: %s.", error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * Set serial parameters for the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param paramstr A serial communication parameters string, in the form
 * of <speed>/<data bits><parity><stopbits><flow>, for example "9600/8n1" or
 * "600/7o2" or "460800/8n1/flow=2" where flow is 0 for none, 1 for rts/cts and 2 for xon/xoff.
 *
 * @return SR_OK upon success, SR_ERR upon failure.
 */
#define SERIAL_COMM_SPEC "^(\\d+)/([78])([neo])([12])(.*)$"
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
		const char *paramstr)
{
	GRegex *reg;
	GMatchInfo *match;
	int speed, databits, parity, stopbits, flow, rts, dtr, i;
	char *mstr, **opts, **kv;

	speed = databits = parity = stopbits = flow = 0;
	rts = dtr = -1;
	sr_spew("Parsing parameters from \"%s\".", paramstr);
	reg = g_regex_new(SERIAL_COMM_SPEC, 0, 0, NULL);
	if (g_regex_match(reg, paramstr, 0, &match)) {
		if ((mstr = g_match_info_fetch(match, 1)))
			speed = strtoul(mstr, NULL, 10);
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 2)))
			databits = strtoul(mstr, NULL, 10);
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 3))) {
			switch (mstr[0]) {
			case 'n':
				parity = SERIAL_PARITY_NONE;
				break;
			case 'e':
				parity = SERIAL_PARITY_EVEN;
				break;
			case 'o':
				parity = SERIAL_PARITY_ODD;
				break;
			}
		}
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 4)))
			stopbits = strtoul(mstr, NULL, 10);
		g_free(mstr);
		if ((mstr = g_match_info_fetch(match, 5)) && mstr[0] != '\0') {
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

	if (speed) {
		return serial_set_params(serial, speed, databits, parity,
					 stopbits, flow, rts, dtr);
	} else {
		sr_dbg("Could not infer speed from parameter string.");
		return SR_ERR_ARG;
	}
}

/**
 * Read a line from the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer where to store the bytes that are read.
 * @param buflen Size of the buffer.
 * @param timeout_ms How long to wait for a line to come in.
 *
 * Reading stops when CR of LR is found, which is stripped from the buffer.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int serial_readline(struct sr_serial_dev_inst *serial, char **buf,
		int *buflen, gint64 timeout_ms)
{
	gint64 start;
	int maxlen, len;

	if (!serial || serial->fd == -1) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot use unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return -1;
	}

	timeout_ms *= 1000;
	start = g_get_monotonic_time();

	maxlen = *buflen;
	*buflen = len = 0;
	while(1) {
		len = maxlen - *buflen - 1;
		if (len < 1)
			break;
		len = serial_read(serial, *buf + *buflen, 1);
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
		if (g_get_monotonic_time() - start > timeout_ms)
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
 * @param count Size of the buffer.
 * @param packet_size Size, in bytes, of a valid packet.
 * @param is_valid Callback that assesses whether the packet is valid or not.
 * @param timeout_ms The timeout after which, if no packet is detected, to
 *                   abort scanning.
 * @param baudrate The baudrate of the serial port. This parameter is not
 *                 critical, but it helps fine tune the serial port polling
 *                 delay.
 *
 * @return SR_OK if a valid packet is found within the given timeout,
 *         SR_ERR upon failure.
 */
SR_PRIV int serial_stream_detect(struct sr_serial_dev_inst *serial,
				 uint8_t *buf, size_t *buflen,
				 size_t packet_size, packet_valid_t is_valid,
				 uint64_t timeout_ms, int baudrate)
{
	uint64_t start, time, byte_delay_us;
	size_t ibuf, i, maxlen;
	int len;

	maxlen = *buflen;

	sr_dbg("Detecting packets on FD %d (timeout = %" PRIu64
	       "ms, baudrate = %d).", serial->fd, timeout_ms, baudrate);

	if (maxlen < (packet_size / 2) ) {
		sr_err("Buffer size must be at least twice the packet size.");
		return SR_ERR;
	}

	/* Assume 8n1 transmission. That is 10 bits for every byte. */
	byte_delay_us = 10 * (1000000 / baudrate);
	start = g_get_monotonic_time();

	i = ibuf = len = 0;
	while (ibuf < maxlen) {
		len = serial_read(serial, &buf[ibuf], 1);
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
			/* We have at least a packet's worth of data. */
			if (is_valid(&buf[i])) {
				sr_spew("Found valid %d-byte packet after "
					"%" PRIu64 "ms.", (ibuf - i), time);
				*buflen = ibuf;
				return SR_OK;
			} else {
				sr_spew("Got %d bytes, but not a valid "
					"packet.", (ibuf - i));
			}
			/* Not a valid packet. Continue searching. */
			i++;
		}
		if (time >= timeout_ms) {
			/* Timeout */
			sr_dbg("Detection timed out after %dms.", time);
			break;
		}
		if (len < 1)
			g_usleep(byte_delay_us);
	}

	*buflen = ibuf;

	sr_err("Didn't find a valid packet (read %d bytes).", *buflen);

	return SR_ERR;
}
