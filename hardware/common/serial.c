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

#include <glob.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <glib.h>

#include "sigrok.h"



char *serial_port_glob[] = {
	/* Linux */
	"/dev/ttyS*",
	"/dev/ttyUSB*",
	"/dev/ttyACM*",
	/* MacOS X */
	"/dev/ttys*",
	"/dev/tty.USB-*",
	"/dev/tty.Modem-*",
	NULL
};


GSList *list_serial_ports(void)
{
	glob_t g;
	GSList *ports;
	int i, j;

	ports = NULL;
	for(i = 0; serial_port_glob[i]; i++)
	{
		if(!glob(serial_port_glob[i], 0, NULL, &g))
		{
			for(j = 0; j < g.gl_pathc; j++)
				ports = g_slist_append(ports, strdup(g.gl_pathv[j]));
			globfree(&g);
		}
	}

	return ports;
}


int serial_open(const char *pathname, int flags)
{

	return open(pathname, flags);
}


int serial_close(int fd)
{

	return close(fd);
}


void *serial_backup_params(int fd)
{
	struct termios *term;

	term = malloc(sizeof(struct termios));
	tcgetattr(fd, term);

	return term;
}


void serial_restore_params(int fd, void *backup)
{

	tcsetattr(fd, TCSADRAIN, (struct termios *) backup);

}


/* flowcontrol 1 = rts/cts  2 = xon/xoff */
int serial_set_params(int fd, int speed, int bits, int parity, int stopbits, int flowcontrol)
{
	struct termios term;

	/* only supporting what we need really -- currently just the OLS driver */
	if(speed != 115200 || bits != 8 || parity != 0 || stopbits != 1 || flowcontrol != 2)
		return SIGROK_ERR;

	if(tcgetattr(fd, &term) < 0)
		return SIGROK_ERR;
	if(cfsetispeed(&term, B115200) < 0)
		return SIGROK_ERR;
	term.c_cflag &= ~CSIZE;
	term.c_cflag |= CS8;
	term.c_cflag &= ~CSTOPB;
	term.c_cflag |= IXON | IXOFF;
	term.c_iflag |= IGNPAR;
	if(tcsetattr(fd, TCSADRAIN, &term) < 0)
		return SIGROK_ERR;

	return SIGROK_OK;
}


