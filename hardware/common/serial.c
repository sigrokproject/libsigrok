/*
 * This file is part of the sigrok project.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif
#include <stdlib.h>
#include <errno.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "serial: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

// FIXME: Must be moved, or rather passed as function argument.
#ifdef _WIN32
static HANDLE hdl;
#endif

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
	int flags_local = 0;
#ifdef _WIN32
	DWORD desired_access = 0, flags_and_attributes = 0;
#endif

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Opening serial port '%s' (flags %d).", serial->port, flags);

#ifdef _WIN32
	/* Map 'flags' to the OS-specific settings. */
	desired_access |= GENERIC_READ;
	flags_and_attributes = FILE_ATTRIBUTE_NORMAL;
	if (flags & SERIAL_RDWR)
		desired_access |= GENERIC_WRITE;
	if (flags & SERIAL_NONBLOCK)
		flags_and_attributes |= FILE_FLAG_OVERLAPPED;

	hdl = CreateFile(serial->port, desired_access, 0, 0,
			 OPEN_EXISTING, flags_and_attributes, 0);
	if (hdl == INVALID_HANDLE_VALUE) {
		sr_err("Error opening serial port '%s'.", serial->port);
		return SR_ERR;
	}
#else
	/* Map 'flags' to the OS-specific settings. */
	if (flags & SERIAL_RDWR)
		flags_local |= O_RDWR;
	if (flags & SERIAL_RDONLY)
		flags_local |= O_RDONLY;
	if (flags & SERIAL_NONBLOCK)
		flags_local |= O_NONBLOCK;

	if ((serial->fd = open(serial->port, flags_local)) < 0) {
		sr_err("Error opening serial port '%s': %s.", serial->port,
		       strerror(errno));
		return SR_ERR;
	}

	sr_spew("Opened serial port '%s' (fd %d).", serial->port, serial->fd);
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
	ret = SR_OK;

#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	if (CloseHandle(hdl) == 0)
		ret = SR_ERR;
#else
	/* Returns 0 upon success, -1 upon failure. */
	if (close(serial->fd) < 0) {
		sr_err("Error closing serial port: %s (fd %d).", strerror(errno),
				serial->fd);
		ret = SR_ERR;
	}
#endif

	serial->fd = -1;

	return ret;
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
	ret = SR_OK;

#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	if (PurgeComm(hdl, PURGE_RXCLEAR | PURGE_TXCLEAR) == 0) {
		sr_err("Error flushing serial port: %s.", strerror(errno));
		ret = SR_ERR;
	}
#else
	/* Returns 0 upon success, -1 upon failure. */
	if (tcflush(serial->fd, TCIOFLUSH) < 0) {
		sr_err("Error flushing serial port: %s.", strerror(errno));
		ret = SR_ERR;
	}

	return ret;
#endif
}

/**
 * Write a number of bytes to the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer containing the bytes to write.
 * @param count Number of bytes to write.
 *
 * @return The number of bytes written, or -1 upon failure.
 */
SR_PRIV int serial_write(struct sr_serial_dev_inst *serial,
		const void *buf, size_t count)
{
	ssize_t ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return -1;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot use unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return -1;
	}

#ifdef _WIN32
	DWORD tmp = 0;

	/* FIXME */
	/* Returns non-zero upon success, 0 upon failure. */
	WriteFile(hdl, buf, count, &tmp, NULL);
#else
	/* Returns the number of bytes written, or -1 upon failure. */
	ret = write(serial->fd, buf, count);
	if (ret < 0)
		sr_err("Write error: %s.", strerror(errno));
	else
		sr_spew("Wrote %d/%d bytes (fd %d).", ret, count, serial->fd);
#endif

	return ret;
}

/**
 * Read a number of bytes from the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer where to store the bytes that are read.
 * @param count The number of bytes to read.
 *
 * @return The number of bytes read, or -1 upon failure.
 */
SR_PRIV int serial_read(struct sr_serial_dev_inst *serial, void *buf,
		size_t count)
{
	ssize_t ret;

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return -1;
	}

	if (serial->fd == -1) {
		sr_dbg("Cannot use unopened serial port %s (fd %d).",
				serial->port, serial->fd);
		return -1;
	}

#ifdef _WIN32
	DWORD tmp = 0;

	/* FIXME */
	/* Returns non-zero upon success, 0 upon failure. */
	return ReadFile(hdl, buf, count, &tmp, NULL);
#else
	/* Returns the number of bytes read, or -1 upon failure. */
	ret = read(serial->fd, buf, count);
#endif

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

#ifdef _WIN32
	DCB dcb;

	if (!GetCommState(hdl, &dcb)) {
		sr_err("Failed to get comm state on port %s (fd %d): %d.",
		       serial->port, serial->fd, GetLastError());
		return SR_ERR;
	}

	switch (baudrate) {
	/*
	 * The baudrates 50/75/134/150/200/1800/230400/460800 do not seem to
	 * have documented CBR_* macros.
	 */
	case 110:
		dcb.BaudRate = CBR_110;
		break;
	case 300:
		dcb.BaudRate = CBR_300;
		break;
	case 600:
		dcb.BaudRate = CBR_600;
		break;
	case 1200:
		dcb.BaudRate = CBR_1200;
		break;
	case 2400:
		dcb.BaudRate = CBR_2400;
		break;
	case 4800:
		dcb.BaudRate = CBR_4800;
		break;
	case 9600:
		dcb.BaudRate = CBR_9600;
		break;
	case 14400:
		dcb.BaudRate = CBR_14400; /* Not available on Unix? */
		break;
	case 19200:
		dcb.BaudRate = CBR_19200;
		break;
	case 38400:
		dcb.BaudRate = CBR_38400;
		break;
	case 57600:
		dcb.BaudRate = CBR_57600;
		break;
	case 115200:
		dcb.BaudRate = CBR_115200;
		break;
	case 128000:
		dcb.BaudRate = CBR_128000; /* Not available on Unix? */
		break;
	case 256000:
		dcb.BaudRate = CBR_256000; /* Not available on Unix? */
		break;
	default:
		sr_err("Unsupported baudrate: %d.", baudrate);
		return SR_ERR;
	}
	sr_spew("Configuring baudrate to %d (%d).", baudrate, dcb.BaudRate);

	sr_spew("Configuring %d data bits.", bits);
	dcb.ByteSize = bits;

	sr_spew("Configuring %d stop bits.", stopbits);
	switch (stopbits) {
	/* Note: There's also ONE5STOPBITS == 1.5 (unneeded so far). */
	case 1:
		dcb.StopBits = ONESTOPBIT;
		break;
	case 2:
		dcb.StopBits = TWOSTOPBITS;
		break;
	default:
		sr_err("Unsupported stopbits number: %d.", stopbits);
		return SR_ERR;
	}

	switch (parity) {
	/* Note: There's also SPACEPARITY, MARKPARITY (unneeded so far). */
	case SERIAL_PARITY_NONE:
		sr_spew("Configuring no parity.");
		dcb.Parity = NOPARITY;
		break;
	case SERIAL_PARITY_EVEN:
		sr_spew("Configuring even parity.");
		dcb.Parity = EVENPARITY;
		break;
	case SERIAL_PARITY_ODD:
		sr_spew("Configuring odd parity.");
		dcb.Parity = ODDPARITY;
		break;
	default:
		sr_err("Unsupported parity setting: %d.", parity);
		return SR_ERR;
	}

	if (rts != -1) {
		sr_spew("Setting RTS %s.", rts ? "high" : "low");
		if (rts)
			dcb.fRtsControl = RTS_CONTROL_ENABLE;
		else
			dcb.fRtsControl = RTS_CONTROL_DISABLE;
	}

	if (dtr != -1) {
		sr_spew("Setting DTR %s.", dtr ? "high" : "low");
		if (rts)
			dcb.fDtrControl = DTR_CONTROL_ENABLE;
		else
			dcb.fDtrControl = DTR_CONTROL_DISABLE;
	}

	if (!SetCommState(hdl, &dcb)) {
		sr_err("Failed to set comm state on port %s (fd %d): %d.",
		       serial->port, serial->fd, GetLastError());
		return SR_ERR;
	}
#else
	struct termios term;
	speed_t baud;
	int ret, controlbits;

	if (tcgetattr(serial->fd, &term) < 0) {
		sr_err("tcgetattr() error on port %s (fd %d): %s.",
				serial->port, serial->fd, strerror(errno));
		return SR_ERR;
	}

	switch (baudrate) {
	case 50:
		baud = B50;
		break;
	case 75:
		baud = B75;
		break;
	case 110:
		baud = B110;
		break;
	case 134:
		baud = B134;
		break;
	case 150:
		baud = B150;
		break;
	case 200:
		baud = B200;
		break;
	case 300:
		baud = B300;
		break;
	case 600:
		baud = B600;
		break;
	case 1200:
		baud = B1200;
		break;
	case 1800:
		baud = B1800;
		break;
	case 2400:
		baud = B2400;
		break;
	case 4800:
		baud = B4800;
		break;
	case 9600:
		baud = B9600;
		break;
	case 19200:
		baud = B19200;
		break;
	case 38400:
		baud = B38400;
		break;
	case 57600:
		baud = B57600;
		break;
	case 115200:
		baud = B115200;
		break;
	case 230400:
		baud = B230400;
		break;
#ifndef __APPLE__
	case 460800:
		baud = B460800;
		break;
#endif
	default:
		sr_err("Unsupported baudrate: %d.", baudrate);
		return SR_ERR;
	}

	sr_spew("Configuring output baudrate to %d (%d).", baudrate, baud);
	if (cfsetospeed(&term, baud) < 0) {
		sr_err("cfsetospeed() error: %s.", strerror(errno));
		return SR_ERR;
	}

	sr_spew("Configuring input baudrate to %d (%d).", baudrate, baud);
	if (cfsetispeed(&term, baud) < 0) {
		sr_err("cfsetispeed() error: %s.", strerror(errno));
		return SR_ERR;
	}

	sr_spew("Configuring %d data bits.", bits);
	term.c_cflag &= ~CSIZE;
	switch (bits) {
	case 8:
		term.c_cflag |= CS8;
		break;
	case 7:
		term.c_cflag |= CS7;
		break;
	default:
		sr_err("Unsupported data bits number %d.", bits);
		return SR_ERR;
	}

	sr_spew("Configuring %d stop bits.", stopbits);
	term.c_cflag &= ~CSTOPB;
	switch (stopbits) {
	case 1:
		term.c_cflag &= ~CSTOPB;
		break;
	case 2:
		term.c_cflag |= CSTOPB;
		break;
	default:
		sr_err("Unsupported stopbits number %d.", stopbits);
		return SR_ERR;
	}

	term.c_iflag &= ~(IXON | IXOFF);
	term.c_cflag &= ~CRTSCTS;
	switch (flowcontrol) {
	case 0:
		/* No flow control. */
		sr_spew("Configuring no flow control.");
		break;
	case 1:
		sr_spew("Configuring RTS/CTS flow control.");
		term.c_cflag |= CRTSCTS;
		break;
	case 2:
		sr_spew("Configuring XON/XOFF flow control.");
		term.c_iflag |= IXON | IXOFF;
		break;
	default:
		sr_err("Unsupported flow control setting %d.", flowcontrol);
		return SR_ERR;
	}

	term.c_iflag &= ~IGNPAR;
	term.c_cflag &= ~(PARODD | PARENB);
	switch (parity) {
	case SERIAL_PARITY_NONE:
		sr_spew("Configuring no parity.");
		term.c_iflag |= IGNPAR;
		break;
	case SERIAL_PARITY_EVEN:
		sr_spew("Configuring even parity.");
		term.c_cflag |= PARENB;
		break;
	case SERIAL_PARITY_ODD:
		sr_spew("Configuring odd parity.");
		term.c_cflag |= PARENB | PARODD;
		break;
	default:
		sr_err("Unsupported parity setting %d.", parity);
		return SR_ERR;
	}

	/* Do not translate carriage return to newline on input. */
	term.c_iflag &= ~(ICRNL);

	/* Disable canonical mode, and don't echo input characters. */
	term.c_lflag &= ~(ICANON | ECHO);

	/* Write the configured settings. */
	if (tcsetattr(serial->fd, TCSADRAIN, &term) < 0) {
		sr_err("tcsetattr() error: %s.", strerror(errno));
		return SR_ERR;
	}

	if (rts != -1) {
		sr_spew("Setting RTS %s.", rts ? "high" : "low");
		controlbits = TIOCM_RTS;
		if ((ret = ioctl(serial->fd, rts ? TIOCMBIS : TIOCMBIC,
				&controlbits)) < 0) {
			sr_err("Error setting RTS: %s.", strerror(errno));
			return SR_ERR;
		}
	}

	if (dtr != -1) {
		sr_spew("Setting DTR %s.", dtr ? "high" : "low");
		controlbits = TIOCM_DTR;
		if ((ret = ioctl(serial->fd, dtr ? TIOCMBIS : TIOCMBIC,
				&controlbits)) < 0) {
			sr_err("Error setting DTR: %s.", strerror(errno));
			return SR_ERR;
		}
	}

#endif

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
		g_usleep(byte_delay_us);
	}

	*buflen = ibuf;

	sr_err("Didn't find a valid packet (read %d bytes).", *buflen);

	return SR_ERR;
}
