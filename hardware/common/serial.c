/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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
#include <glib.h>
#include <sigrok.h>
#include <sigrok-internal.h>

// FIXME: Must be moved, or rather passed as function argument.
#ifdef _WIN32
HANDLE hdl;
#endif

const char *serial_port_glob[] = {
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

GSList *list_serial_ports(void)
{
	GSList *ports;

#ifdef _WIN32
	/* TODO */
	ports = NULL;
	ports = g_slist_append(ports, strdup("COM1"));
#else
	glob_t g;
	unsigned int i, j;

	ports = NULL;
	for (i = 0; serial_port_glob[i]; i++) {
		if (glob(serial_port_glob[i], 0, NULL, &g))
			continue;
		for (j = 0; j < g.gl_pathc; j++)
			ports = g_slist_append(ports, strdup(g.gl_pathv[j]));
		globfree(&g);
	}
#endif

	return ports;
}

int serial_open(const char *pathname, int flags)
{
#ifdef _WIN32
	/* FIXME: Don't hardcode COM1. */
	hdl = CreateFile("COM1", GENERIC_READ | GENERIC_WRITE, 0, 0,
			 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (hdl == INVALID_HANDLE_VALUE) {
		/* TODO: Error handling. */
	}
	return 0;
#else
	return open(pathname, flags);
#endif
}

/*
 * Close the serial port.
 * Returns 0 upon success, -1 upon failure.
 */
int serial_close(int fd)
{
#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	return (CloseHandle(hdl) == 0) ? -1 : 0;
#else
	/* Returns 0 upon success, -1 upon failure. */
	return close(fd);
#endif
}

/*
 * Flush serial port buffers (if any).
 * Returns 0 upon success, -1 upon failure.
 */
int serial_flush(int fd)
{
#ifdef _WIN32
	/* Returns non-zero upon success, 0 upon failure. */
	return (PurgeComm(hdl, PURGE_RXCLEAR | PURGE_TXCLEAR) == 0) ? -1 : 0;
#else
	/* Returns 0 upon success, -1 upon failure. */
	return tcflush(fd, TCIOFLUSH);
#endif
}

/*
 * Write a number of bytes to the specified serial port.
 * Returns the number of bytes written, or -1 upon failure.
 */
int serial_write(int fd, const void *buf, size_t count)
{
#ifdef _WIN32
	DWORD tmp = 0;

	/* FIXME */
	/* Returns non-zero upon success, 0 upon failure. */
	WriteFile(hdl, buf, count, &tmp, NULL);
#else
	/* Returns the number of bytes written, or -1 upon failure. */
	return write(fd, buf, count);
#endif
}

/*
 * Read a number of bytes from the specified serial port.
 * Returns the number of bytes read, or -1 upon failure.
 */
int serial_read(int fd, void *buf, size_t count)
{
#ifdef _WIN32
	DWORD tmp = 0;

	/* FIXME */
	/* Returns non-zero upon success, 0 upon failure. */
	return ReadFile(hdl, buf, count, &tmp, NULL);
#else
	/* Returns the number of bytes read, or -1 upon failure. */
	return read(fd, buf, count);
#endif
}

void *serial_backup_params(int fd)
{
#ifdef _WIN32
	/* TODO */
#else
	struct termios *term;

	term = malloc(sizeof(struct termios));
	tcgetattr(fd, term);

	return term;
#endif
}

void serial_restore_params(int fd, void *backup)
{
#ifdef _WIN32
	/* TODO */
#else
	tcsetattr(fd, TCSADRAIN, (struct termios *)backup);
#endif
}

/*
 * Set serial parameters.
 *
 * flowcontrol: 1 = rts/cts, 2 = xon/xoff
 * parity: 0 = none, 1 = even, 2 = odd
 */
int serial_set_params(int fd, int speed, int bits, int parity, int stopbits,
		      int flowcontrol)
{
#ifdef _WIN32
	DCB dcb;

	if (!GetCommState(hdl, &dcb)) {
		/* TODO: Error handling. */
		return SR_ERR;
	}

	/* TODO: Rename 'speed' to 'baudrate'. */
	switch(speed) {
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
	default:
		/* TODO: Error handling. */
		break;
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

	switch (speed) {
	case 9600:
		baud = B9600;
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
#if 0
	case 460800:
		baud = B460800;
		break;
#endif
	default:
		return SR_ERR;
	}

	if (tcgetattr(fd, &term) < 0)
		return SR_ERR;
	if (cfsetispeed(&term, baud) < 0)
		return SR_ERR;

	term.c_cflag &= ~CSIZE;
	switch (bits) {
	case 8:
		term.c_cflag |= CS8;
		break;
	case 7:
		term.c_cflag |= CS7;
		break;
	default:
		return SR_ERR;
	}

	term.c_cflag &= ~CSTOPB;
	switch (stopbits) {
	case 1:
		break;
	case 2:
		term.c_cflag |= CSTOPB;
	default:
		return SR_ERR;
	}

	term.c_cflag &= ~(IXON | IXOFF | CRTSCTS);
	switch (flowcontrol) {
	case 2:
		term.c_cflag |= IXON | IXOFF;
		break;
	case 1:
		term.c_cflag |= CRTSCTS;
	default:
		return SR_ERR;
	}

	term.c_iflag &= ~IGNPAR;
	term.c_cflag &= ~(PARODD | PARENB);
	switch (parity) {
	case 0:
		term.c_iflag |= IGNPAR;
		break;
	case 1:
		term.c_cflag |= PARENB;
		break;
	case 2:
		term.c_cflag |= PARENB | PARODD;
		break;
	default:
		return SR_ERR;
	}

	if (tcsetattr(fd, TCSADRAIN, &term) < 0)
		return SR_ERR;
#endif

	return SR_OK;
}
