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
#include <glib.h>


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


