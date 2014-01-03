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

#define LOG_PREFIX "serial"

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
	int sp_flags = 0;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Opening serial port '%s' (flags %d).", serial->port, flags);

	sp_get_port_by_name(serial->port, &serial->data);

	if (flags & SERIAL_RDWR)
		sp_flags = (SP_MODE_READ | SP_MODE_WRITE);
	else if (flags & SERIAL_RDONLY)
		sp_flags = SP_MODE_READ;

	serial->nonblocking = (flags & SERIAL_NONBLOCK) ? 1 : 0;

	ret = sp_open(serial->data, sp_flags);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to open serial port with invalid parameters.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error opening port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

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

	if (!serial->data) {
		sr_dbg("Cannot close unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	sr_spew("Closing serial port %s.", serial->port);

	ret = sp_close(serial->data);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to close an invalid serial port.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error closing port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	sp_free_port(serial->data);
	serial->data = NULL;

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

	if (!serial->data) {
		sr_dbg("Cannot flush unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	sr_spew("Flushing serial port %s.", serial->port);

	ret = sp_flush(serial->data, SP_BUF_BOTH);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempt to flush an invalid serial port.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error flushing port (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

static int _serial_write(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count, int nonblocking)
{
	ssize_t ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!serial->data) {
		sr_dbg("Cannot use unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	if (nonblocking)
		ret = sp_nonblocking_write(serial->data, buf, count);
	else
		ret = sp_blocking_write(serial->data, buf, count, 0);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempted serial port write with invalid arguments.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Write error (%d): %s.", sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	sr_spew("Wrote %d/%d bytes.", ret, count);

	return ret;
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
	return _serial_write(serial, buf, count, serial->nonblocking);
}

SR_PRIV int serial_write_blocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count)
{
	return _serial_write(serial, buf, count, 0);
}

SR_PRIV int serial_write_nonblocking(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count)
{
	return _serial_write(serial, buf, count, 1);
}

static int _serial_read(struct sr_serial_dev_inst *serial, void *buf,
		size_t count, int nonblocking)
{
	ssize_t ret;
	char *error;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!serial->data) {
		sr_dbg("Cannot use unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	if (nonblocking)
		ret = sp_nonblocking_read(serial->data, buf, count);
	else
		ret = sp_blocking_read(serial->data, buf, count, 0);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Attempted serial port read with invalid arguments.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Read error (%d): %s.", sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	if (ret > 0)
		sr_spew("Read %d/%d bytes.", ret, count);

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
	return _serial_read(serial, buf, count, serial->nonblocking);
}

SR_PRIV int serial_read_blocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count)
{
	return _serial_read(serial, buf, count, 0);
}

SR_PRIV int serial_read_nonblocking(struct sr_serial_dev_inst *serial, void *buf,
		size_t count)
{
	return _serial_read(serial, buf, count, 1);
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
 *                      1 = RTS/CTS, 2 = XON/XOFF).
 * @param[in] rts Status of RTS line (0 or 1; required by some interfaces).
 * @param[in] dtr Status of DTR line (0 or 1; required by some interfaces).
 *
 * @retval SR_OK Success
 * @retval SR_ERR Failure.
 */
SR_PRIV int serial_set_params(struct sr_serial_dev_inst *serial, int baudrate,
			      int bits, int parity, int stopbits,
			      int flowcontrol, int rts, int dtr)
{
	int ret;
	char *error;
	struct sp_port_config *config;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!serial->data) {
		sr_dbg("Cannot configure unopened serial port %s.", serial->port);
		return SR_ERR;
	}

	sr_spew("Setting serial parameters on port %s.", serial->port);

	sp_new_config(&config);
	sp_set_config_baudrate(config, baudrate);
	sp_set_config_bits(config, bits);
	switch (parity) {
	case 0:
		sp_set_config_parity(config, SP_PARITY_NONE);
		break;
	case 1:
		sp_set_config_parity(config, SP_PARITY_EVEN);
		break;
	case 2:
		sp_set_config_parity(config, SP_PARITY_ODD);
		break;
	default:
		return SR_ERR_ARG;
	}
	sp_set_config_stopbits(config, stopbits);
	sp_set_config_rts(config, flowcontrol == 1 ? SP_RTS_FLOW_CONTROL : rts);
	sp_set_config_cts(config, flowcontrol == 1 ? SP_CTS_FLOW_CONTROL : SP_CTS_IGNORE);
	sp_set_config_dtr(config, dtr);
	sp_set_config_dsr(config, SP_DSR_IGNORE);
	sp_set_config_xon_xoff(config, flowcontrol == 2 ? SP_XONXOFF_INOUT : SP_XONXOFF_DISABLED);

	ret = sp_set_config(serial->data, config);
	sp_free_config(config);

	switch (ret) {
	case SP_ERR_ARG:
		sr_err("Invalid arguments for setting serial port parameters.");
		return SR_ERR_ARG;
	case SP_ERR_FAIL:
		error = sp_last_error_message();
		sr_err("Error setting serial port parameters (%d): %s.",
			sp_last_error_code(), error);
		sp_free_error_message(error);
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * Set serial parameters for the specified serial port from parameter string.
 *
 * @param serial Previously initialized serial port structure.
 * @param[in] paramstr A serial communication parameters string of the form
 * "<baudrate>/<bits><parity><stopbits>{/<option>}".\n
 *  Examples: "9600/8n1", "600/7o2/dtr=1/rts=0" or "460800/8n1/flow=2".\n
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
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 */
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
		const char *paramstr)
{
#define SERIAL_COMM_SPEC "^(\\d+)/([5678])([neo])([12])(.*)$"

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
 * @param[in] timeout_ms How long to wait for a line to come in.
 *
 * Reading stops when CR of LR is found, which is stripped from the buffer.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Failure.
 */
SR_PRIV int serial_readline(struct sr_serial_dev_inst *serial, char **buf,
		int *buflen, gint64 timeout_ms)
{
	gint64 start;
	int maxlen, len;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	if (!serial->data) {
		sr_dbg("Cannot use unopened serial port %s.", serial->port);
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
 * @param buflen Size of the buffer.
 * @param[in] packet_size Size, in bytes, of a valid packet.
 * @param is_valid Callback that assesses whether the packet is valid or not.
 * @param[in] timeout_ms The timeout after which, if no packet is detected, to
 *                   abort scanning.
 * @param[in] baudrate The baudrate of the serial port. This parameter is not
 *                 critical, but it helps fine tune the serial port polling
 *                 delay.
 *
 * @retval SR_OK Valid packet was found within the given timeout
 * @retval SR_ERR Failure.
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

	sr_dbg("Detecting packets on %s (timeout = %" PRIu64
	       "ms, baudrate = %d).", serial->port, timeout_ms, baudrate);

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

/**
 * Extract the serial device and options from the options linked list.
 *
 * @param options List of options passed from the command line.
 * @param serial_device Pointer where to store the exctracted serial device.
 * @param serial_options Pointer where to store the optional extracted serial
 * options.
 *
 * @return SR_OK if a serial_device is found, SR_ERR if no device is found. The
 * returned string should not be freed by the caller.
 */
SR_PRIV int sr_serial_extract_options(GSList *options, const char **serial_device,
				      const char **serial_options)
{
	GSList *l;
	struct sr_config *src;

	*serial_device = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			*serial_device = g_variant_get_string(src->data, NULL);
			sr_dbg("Parsed serial device: %s", *serial_device);
			break;

		case SR_CONF_SERIALCOMM:
			*serial_options = g_variant_get_string(src->data, NULL);
			sr_dbg("Parsed serial options: %s", *serial_options);
			break;
		}
	}

	if (!*serial_device) {
		sr_dbg("No serial device specified");
		return SR_ERR;
	}

	return SR_OK;
}

#ifdef _WIN32
typedef HANDLE event_handle;
#else
typedef int event_handle;
#endif

SR_PRIV int serial_source_add(struct sr_serial_dev_inst *serial, int events,
		int timeout, sr_receive_data_callback_t cb, void *cb_data)
{
	enum sp_event mask = 0;
	unsigned int i;

	if (sp_new_event_set(&serial->event_set) != SP_OK)
		return SR_ERR;

	if (events & G_IO_IN)
		mask |= SP_EVENT_RX_READY;
	if (events & G_IO_OUT)
		mask |= SP_EVENT_TX_READY;
	if (events & G_IO_ERR)
		mask |= SP_EVENT_ERROR;

	if (sp_add_port_events(serial->event_set, serial->data, mask) != SP_OK) {
		sp_free_event_set(serial->event_set);
		return SR_ERR;
	}

	serial->pollfds = (GPollFD *) g_malloc0(sizeof(GPollFD) * serial->event_set->count);

	for (i = 0; i < serial->event_set->count; i++) {

		serial->pollfds[i].fd = ((event_handle *) serial->event_set->handles)[i];

		mask = serial->event_set->masks[i];

		if (mask & SP_EVENT_RX_READY)
			serial->pollfds[i].events |= G_IO_IN;
		if (mask & SP_EVENT_TX_READY)
			serial->pollfds[i].events |= G_IO_OUT;
		if (mask & SP_EVENT_ERROR)
			serial->pollfds[i].events |= G_IO_ERR;

		if (sr_session_source_add_pollfd(&serial->pollfds[i],
				timeout, cb, cb_data) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int serial_source_remove(struct sr_serial_dev_inst *serial)
{
	unsigned int i;

	for (i = 0; i < serial->event_set->count; i++)
		if (sr_session_source_remove_pollfd(&serial->pollfds[i]) != SR_OK)
			return SR_ERR;

	g_free(serial->pollfds);
	sp_free_event_set(serial->event_set);

	serial->pollfds = NULL;
	serial->event_set = NULL;

	return SR_OK;
}
