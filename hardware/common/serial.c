/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2010-2012 Uwe Hermann <uwe@hermann-uwe.de>
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
 * @param flags Flags to use when opening the serial port.
 * TODO: Abstract 'flags', currently they're OS-specific!
 *
 * If the serial structure contains a serialcomm string, it will be
 * passed to serial_set_paramstr() after the port is opened.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int serial_open(struct sr_serial_dev_inst *serial, int flags)
{

	if (!serial) {
		sr_dbg("Invalid serial port.");
		return SR_ERR;
	}

	sr_spew("Opening serial port '%s' (flags %d).", serial->port, flags);

#ifdef _WIN32
	hdl = CreateFile(serial->port, GENERIC_READ | GENERIC_WRITE, 0, 0,
			 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hdl == INVALID_HANDLE_VALUE) {
		sr_err("Error opening serial port '%s'.", serial->port);
		return SR_ERR;
	}
#else
	if ((serial->fd = open(serial->port, flags)) < 0) {
		sr_err("Error opening serial port '%s': %s.", serial->port,
		       strerror(errno));
		return SR_ERR;
	} else
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
	if (ret < 0)
		/*
 		 * Should be sr_err(), but that would yield lots of
		 * "Resource temporarily unavailable" messages.
		 */
		sr_spew("Read error: %s (fd %d).", strerror(errno), serial->fd);
	else
		sr_spew("Read %d/%d bytes (fd %d).", ret, count, serial->fd);
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
		int bits, int parity, int stopbits, int flowcontrol)
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

	if (!GetCommState(hdl, &dcb))
		return SR_ERR;

	switch (baudrate) {
	case 115200:
		dcb.BaudRate = CBR_115200;
		break;
	case 57600:
		dcb.BaudRate = CBR_57600;
		break;
	case 38400:
		dcb.BaudRate = CBR_38400;
		break;
	case 19200:
		dcb.BaudRate = CBR_19200;
		break;
	case 9600:
		dcb.BaudRate = CBR_9600;
		break;
	case 4800:
		dcb.BaudRate = CBR_4800;
		break;
	case 2400:
		dcb.BaudRate = CBR_2400;
		break;
	default:
		sr_err("Unsupported baudrate %d.", baudrate);
		return SR_ERR;
	}
	dcb.ByteSize = bits;
	dcb.Parity = NOPARITY; /* TODO: Don't hardcode. */
	dcb.StopBits = ONESTOPBIT; /* TODO: Don't hardcode. */

	if (!SetCommState(hdl, &dcb))
		return SR_ERR;
#else
	struct termios term;
	speed_t baud;

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
		sr_err("Unsupported baudrate %d.", baudrate);
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
#endif

	return SR_OK;
}

/**
 * Set serial parameters for the specified serial port.
 *
 * @param serial Previously initialized serial port structure.
 * @param paramstr A serial communication parameters string, in the form
 * of <speed>/<data bits><parity><stopbits>, for example "9600/8n1" or
 * "600/7o2".
 *
 * @return SR_OK upon success, SR_ERR upon failure.
 */
#define SERIAL_COMM_SPEC "^(\\d+)/([78])([neo])([12])$"
SR_PRIV int serial_set_paramstr(struct sr_serial_dev_inst *serial,
		const char *paramstr)
{
	GRegex *reg;
	GMatchInfo *match;
	int speed, databits, parity, stopbits;
	char *mstr;

	speed = databits = parity = stopbits = 0;
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
	}
	g_match_info_unref(match);
	g_regex_unref(reg);

	if (speed)
		return serial_set_params(serial, speed, databits, parity, stopbits, 0);
	else
		return SR_ERR_ARG;
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
