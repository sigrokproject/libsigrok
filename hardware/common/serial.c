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

static const char *serial_port_glob[] = {
	/* Linux */
	"/dev/ttyS*",
	"/dev/ttyUSB*",
	"/dev/ttyACM*",
	/* MacOS X */
	"/dev/ttys*",
	"/dev/tty.USB-*",
	"/dev/tty.Modem-*",
	NULL,
};

SR_PRIV GSList *list_serial_ports(void)
{
	GSList *ports;

	sr_dbg("Getting list of serial ports on the system.");

#ifdef _WIN32
	/* TODO */
	ports = NULL;
	ports = g_slist_append(ports, g_strdup("COM1"));
#else
	glob_t g;
	unsigned int i, j;

	ports = NULL;
	for (i = 0; serial_port_glob[i]; i++) {
		if (glob(serial_port_glob[i], 0, NULL, &g))
			continue;
		for (j = 0; j < g.gl_pathc; j++) {
			ports = g_slist_append(ports, g_strdup(g.gl_pathv[j]));
			sr_dbg("Found serial port '%s'.", g.gl_pathv[j]);
		}
		globfree(&g);
	}
#endif

	return ports;
}

/**
 * Open the specified serial port.
 *
 * @param pathname OS-specific serial port specification. Examples:
 *                 "/dev/ttyUSB0", "/dev/ttyACM1", "/dev/tty.Modem-0", "COM1".
 * @param flags Flags to use when opening the serial port.
 *
 * @return 0 upon success, -1 upon failure.
 */
SR_PRIV int serial_open(const char *pathname, int flags)
{
	/* TODO: Abstract 'flags', currently they're OS-specific! */

	sr_dbg("Opening serial port '%s' (flags = %d).", pathname, flags);

#ifdef _WIN32
	pathname = "COM1"; /* FIXME: Don't hardcode COM1. */

	hdl = CreateFile(pathname, GENERIC_READ | GENERIC_WRITE, 0, 0,
			 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hdl == INVALID_HANDLE_VALUE) {
		sr_err("Error opening serial port '%s'.", pathname);
		return -1;
	}
	return 0;
#else
	int fd;

	if ((fd = open(pathname, flags)) < 0) {
		/*
		 * Should be sr_err(), but since some drivers try to open all
		 * ports on a system and see if they succeed, this would
		 * yield ugly output for e.g. "sigrok-cli -D".
		 */
		sr_dbg("Error opening serial port '%s': %s.", pathname,
		       strerror(errno));
	}

	return fd;
#endif
}

/**
 * Close the specified serial port.
 *
 * @param fd File descriptor of the serial port.
 *
 * @return 0 upon success, -1 upon failure.
 */
SR_PRIV int serial_close(int fd)
{
	sr_dbg("FD %d: Closing serial port.", fd);

#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	return (CloseHandle(hdl) == 0) ? -1 : 0;
#else
	int ret;

	/* Returns 0 upon success, -1 upon failure. */
	if ((ret = close(fd)) < 0) {
		sr_dbg("FD %d: Error closing serial port: %s.",
		       fd, strerror(errno));
	}

	return ret;
#endif
}

/**
 * Flush serial port buffers (if any).
 *
 * @param fd File descriptor of the serial port.
 *
 * @return 0 upon success, -1 upon failure.
 */
SR_PRIV int serial_flush(int fd)
{
	sr_dbg("FD %d: Flushing serial port.", fd);

#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	return (PurgeComm(hdl, PURGE_RXCLEAR | PURGE_TXCLEAR) == 0) ? -1 : 0;
#else
	int ret;

	/* Returns 0 upon success, -1 upon failure. */
	if ((ret = tcflush(fd, TCIOFLUSH)) < 0)
		sr_err("Error flushing serial port: %s.", strerror(errno));

	return ret;
#endif
}

/**
 * Write a number of bytes to the specified serial port.
 *
 * @param fd File descriptor of the serial port.
 * @param buf Buffer containing the bytes to write.
 * @param count Number of bytes to write.
 *
 * @return The number of bytes written, or -1 upon failure.
 */
SR_PRIV int serial_write(int fd, const void *buf, size_t count)
{
	sr_spew("FD %d: Writing %d bytes.", fd, count);

#ifdef _WIN32
	DWORD tmp = 0;

	/* FIXME */
	/* Returns non-zero upon success, 0 upon failure. */
	WriteFile(hdl, buf, count, &tmp, NULL);
#else
	ssize_t ret;

	/* Returns the number of bytes written, or -1 upon failure. */
	ret = write(fd, buf, count);
	if (ret < 0)
		sr_err("FD %d: Write error: %s.", fd, strerror(errno));
	else if ((size_t)ret != count)
		sr_spew("FD %d: Only wrote %d/%d bytes.", fd, ret, count);

	return ret;
#endif
}

/**
 * Read a number of bytes from the specified serial port.
 *
 * @param fd File descriptor of the serial port.
 * @param buf Buffer where to store the bytes that are read.
 * @param count The number of bytes to read.
 *
 * @return The number of bytes read, or -1 upon failure.
 */
SR_PRIV int serial_read(int fd, void *buf, size_t count)
{
	sr_spew("FD %d: Reading %d bytes.", fd, count);

#ifdef _WIN32
	DWORD tmp = 0;

	/* FIXME */
	/* Returns non-zero upon success, 0 upon failure. */
	return ReadFile(hdl, buf, count, &tmp, NULL);
#else
	ssize_t ret;

	/* Returns the number of bytes read, or -1 upon failure. */
	ret = read(fd, buf, count);
	if (ret < 0) {
		/*
 		 * Should be sr_err(), but that would yield lots of
		 * "Resource temporarily unavailable" messages.
		 */
		sr_spew("FD %d: Read error: %s.", fd, strerror(errno));
	} else if ((size_t)ret != count) {
		sr_spew("FD %d: Only read %d/%d bytes.", fd, ret, count);
	}

	return ret;
#endif
}

/**
 * Create a backup of the current parameters of the specified serial port.
 *
 * @param fd File descriptor of the serial port.
 *
 * @return Pointer to a struct termios upon success, NULL upon errors.
 *         It is the caller's responsibility to g_free() the pointer if no
 *         longer needed.
 */
SR_PRIV void *serial_backup_params(int fd)
{
	sr_dbg("FD %d: Creating serial parameters backup.", fd);

#ifdef _WIN32
	/* TODO */
#else
	struct termios *term;

	if (!(term = g_try_malloc(sizeof(struct termios)))) {
		sr_err("termios struct malloc failed.");
		return NULL;
	}

	/* Returns 0 upon success, -1 upon failure. */
	if (tcgetattr(fd, term) < 0) {
		sr_err("FD %d: Error getting serial parameters: %s.",
		       strerror(errno));
		g_free(term);
		return NULL;
	}

	return term;
#endif
}

/**
 * Restore serial port settings from a previously created backup.
 *
 * @param fd File descriptor of the serial port.
 * @param backup Pointer to a struct termios which contains the settings
 *               to restore.
 *
 * @return 0 upon success, -1 upon failure.
 */
SR_PRIV int serial_restore_params(int fd, void *backup)
{
	sr_dbg("FD %d: Restoring serial parameters from backup.", fd);

#ifdef _WIN32
	/* TODO */
#else
	int ret;

	/* Returns 0 upon success, -1 upon failure. */
	if ((ret = tcsetattr(fd, TCSADRAIN, (struct termios *)backup)) < 0) {
		sr_err("FD %d: Error restoring serial parameters: %s.",
		       strerror(errno));
	}

	return ret;
#endif
}

/**
 * Set serial parameters for the specified serial port.
 *
 * @param baudrate The baudrate to set.
 * @param bits The number of data bits to use.
 * @param parity The parity setting to use (0 = none, 1 = even, 2 = odd).
 * @param stopbits The number of stop bits to use (1 or 2).
 * @param flowcontrol The flow control settings to use (0 = none, 1 = RTS/CTS,
 *                    2 = XON/XOFF).
 *
 * @return SR_OK upon success, SR_ERR upon failure.
 */
SR_PRIV int serial_set_params(int fd, int baudrate, int bits, int parity,
			      int stopbits, int flowcontrol)
{
	sr_dbg("FD %d: Setting serial parameters.", fd);

#ifdef _WIN32
	DCB dcb;

	if (!GetCommState(hdl, &dcb)) {
		/* TODO: Error handling. */
		return SR_ERR;
	}

	switch (baudrate) {
	/* TODO: Support for higher baud rates. */
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
		sr_err("Unsupported baudrate: %d.", baudrate);
		return SR_ERR;
	}
	dcb.ByteSize = bits;
	dcb.Parity = NOPARITY; /* TODO: Don't hardcode. */
	dcb.StopBits = ONESTOPBIT; /* TODO: Don't hardcode. */

	if (!SetCommState(hdl, &dcb)) {
		/* TODO: Error handling. */
		return SR_ERR;
	}
#else
	struct termios term;
	speed_t baud;

	sr_dbg("FD %d: Getting terminal settings.", fd);
	if (tcgetattr(fd, &term) < 0) {
		sr_err("tcgetattr() error: %ѕ.", strerror(errno));
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

	sr_dbg("FD %d: Configuring output baudrate to %d (%d).",
		fd, baudrate, baud);
	if (cfsetospeed(&term, baud) < 0) {
		sr_err("cfsetospeed() error: %ѕ.", strerror(errno));
		return SR_ERR;
	}

	sr_dbg("FD %d: Configuring input baudrate to %d (%d).",
		fd, baudrate, baud);
	if (cfsetispeed(&term, baud) < 0) {
		sr_err("cfsetispeed() error: %ѕ.", strerror(errno));
		return SR_ERR;
	}

	sr_dbg("FD %d: Configuring %d data bits.", fd, bits);
	term.c_cflag &= ~CSIZE;
	switch (bits) {
	case 8:
		term.c_cflag |= CS8;
		break;
	case 7:
		term.c_cflag |= CS7;
		break;
	default:
		sr_err("Unsupported data bits number: %d.", bits);
		return SR_ERR;
	}

	sr_dbg("FD %d: Configuring %d stop bits.", fd, stopbits);
	term.c_cflag &= ~CSTOPB;
	switch (stopbits) {
	case 1:
		/* Do nothing, a cleared CSTOPB entry means "1 stop bit". */
		break;
	case 2:
		term.c_cflag |= CSTOPB;
		break;
	default:
		sr_err("Unsupported stopbits number: %d.", stopbits);
		return SR_ERR;
	}

	term.c_iflag &= ~(IXON | IXOFF);
	term.c_cflag &= ~CRTSCTS;
	switch (flowcontrol) {
	case 0:
		/* No flow control. */
		sr_dbg("FD %d: Configuring no flow control.", fd);
		break;
	case 1:
		sr_dbg("FD %d: Configuring RTS/CTS flow control.", fd);
		term.c_cflag |= CRTSCTS;
		break;
	case 2:
		sr_dbg("FD %d: Configuring XON/XOFF flow control.", fd);
		term.c_iflag |= IXON | IXOFF;
		break;
	default:
		sr_err("Unsupported flow control setting: %d.", flowcontrol);
		return SR_ERR;
	}

	term.c_iflag &= ~IGNPAR;
	term.c_cflag &= ~(PARODD | PARENB);
	switch (parity) {
	case SERIAL_PARITY_NONE:
		sr_dbg("FD %d: Configuring no parity.", fd);
		term.c_iflag |= IGNPAR;
		break;
	case SERIAL_PARITY_EVEN:
		sr_dbg("FD %d: Configuring even parity.", fd);
		term.c_cflag |= PARENB;
		break;
	case SERIAL_PARITY_ODD:
		sr_dbg("FD %d: Configuring odd parity.", fd);
		term.c_cflag |= PARENB | PARODD;
		break;
	default:
		sr_err("Unsupported parity setting: %d.", parity);
		return SR_ERR;
	}

	/* Do NOT translate carriage return to newline on input. */
	term.c_iflag &= ~(ICRNL);

	/* Disable canonical mode, and don't echo input characters. */
	term.c_lflag &= ~(ICANON | ECHO);

	/* Write the configured settings. */
	if (tcsetattr(fd, TCSADRAIN, &term) < 0) {
		sr_err("tcsetattr() error: %ѕ.", strerror(errno));
		return SR_ERR;
	}
#endif

	return SR_OK;
}

#define SERIAL_COMM_SPEC "^(\\d+)/([78])([neo])([12])$"
SR_PRIV int serial_set_paramstr(int fd, const char *paramstr)
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
		return serial_set_params(fd, speed, databits, parity, stopbits, 0);
	else
		return SR_ERR_ARG;
}

SR_PRIV int serial_readline(int fd, char **buf, int *buflen,
			    uint64_t timeout_ms)
{
	uint64_t start;
	int maxlen, len;

	timeout_ms *= 1000;
	start = g_get_monotonic_time();

	maxlen = *buflen;
	*buflen = len = 0;
	while(1) {
		len = maxlen - *buflen - 1;
		if (len < 1)
			break;
		len = serial_read(fd, *buf + *buflen, 1);
		if (len > 0) {
			*buflen += len;
			*(*buf + *buflen) = '\0';
			if (*buflen > 0 && *(*buf + *buflen - 1) == '\r') {
				/* Strip LF and terminate. */
				*(*buf + --*buflen) = '\0';
				break;
			}
		}
		if (g_get_monotonic_time() - start > timeout_ms)
			/* Timeout */
			break;
		g_usleep(2000);
	}
	sr_dbg("Received %d: '%s'.", *buflen, *buf);

	return SR_OK;
}
