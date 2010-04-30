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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include <sigrok.h>

#define CHUNKSIZE 4096


static int format_match(char *filename)
{

	filename = NULL;

	return TRUE;
}


/* TODO: number of probes hardcoded to 8 */
static int in_loadfile(char *filename)
{
	struct datafeed_header header;
	struct datafeed_packet packet;
	char buffer[CHUNKSIZE];
	int fd, size;

	if( (fd = open(filename, O_RDONLY)) == -1)
		return SIGROK_ERR;

	header.feed_version = 1;
	header.num_probes = 8;
	header.protocol_id = PROTO_RAW;
	header.samplerate = 0;
	gettimeofday(&header.starttime, NULL);
	packet.type = DF_HEADER;
	packet.length = sizeof(struct datafeed_header);
	packet.payload = &header;
	session_bus(NULL, &packet);

	packet.type = DF_LOGIC8;
	packet.payload = buffer;
	while( (size = read(fd, buffer, CHUNKSIZE)) > 0) {
		packet.length = size;
		session_bus(NULL, &packet);
	}
	close(fd);

	packet.type = DF_END;
	session_bus(NULL, &packet);


	return SIGROK_OK;
}


struct input_format input_binary = {
		"binary",
		"Raw binary",
		format_match,
		in_loadfile
};

