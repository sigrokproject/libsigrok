/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Karikay Sharma <sharma.kartik2107@gmail.com>
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
#include "protocol.h"

SR_PRIV int pslab_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}

SR_PRIV char* pslab_get_version(struct sr_serial_dev_inst* serial, uint8_t c1, uint8_t c2 )
{
	char *buffer = g_malloc0(16);
	int len = 15;
	serial_write_blocking(serial,&c1, sizeof(c1), serial_timeout(serial, sizeof(c1)));
	serial_write_blocking(serial,&c2, sizeof(c2),serial_timeout(serial, sizeof(c2)));
	serial_readline(serial, &buffer, &len, serial_timeout(serial, sizeof(buffer)));
	return buffer;
}