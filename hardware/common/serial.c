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
#include <conio.h>
#else
#include <glob.h>
#include <termios.h>
#endif
#include <stdlib.h>
#include <glib.h>
#include <sigrok.h>

// FIXME: Must be moved, or rather passed as function argument.
#ifdef _WIN32
HANDLE hdl;
#endif

char *serial_port_glob[] = {
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
#ifdef _WIN32
	/* TODO */
#else
	glob_t g;
	GSList *ports;
	unsigned int i, j;

	ports = NULL;
	for (i = 0; serial_port_glob[i]; i++) {
		if (glob(serial_port_glob[i], 0, NULL, &g))
			continue;
		for (j = 0; j < g.gl_pathc; j++)
			ports = g_slist_append(ports, strdup(g.gl_pathv[j]));
		globfree(&g);
	}

	return ports;
#endif
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

int serial_close(int fd)
{
#ifdef _WIN32
	CloseHandle(hdl);
#else
	return close(fd);
#endif
}

int serial_flush(int fd)
{

	tcflush(fd, TCIOFLUSH);

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

/* flowcontrol 1 = rts/cts  2 = xon/xoff */
int serial_set_params(int fd, int speed, int bits, int parity, int stopbits,
		      int flowcontrol)
{
#ifdef _WIN32
	DCB dcb;

	if (!GetCommState(hdl, &dcb)) {
		/* TODO: Error handling. */
	}

	/* TODO: Rename 'speed' to 'baudrate'. */
	switch(speed) {
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
	}
#else
	struct termios term;

	/* Only supporting what we need really, currently the OLS driver. */
	if (speed != 115200 || bits != 8 || parity != 0 || stopbits != 1
	    || flowcontrol != 2)
		return SIGROK_ERR;

	if (tcgetattr(fd, &term) < 0)
		return SIGROK_ERR;
	if (cfsetispeed(&term, B115200) < 0)
		return SIGROK_ERR;
	term.c_cflag &= ~CSIZE;
	term.c_cflag |= CS8;
	term.c_cflag &= ~CSTOPB;
	term.c_cflag |= IXON | IXOFF;
	term.c_iflag |= IGNPAR;
	if (tcsetattr(fd, TCSADRAIN, &term) < 0)
		return SIGROK_ERR;
#endif

	return SIGROK_OK;
}
