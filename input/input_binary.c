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

static int format_match(const char *filename)
{
	filename = NULL;
	return TRUE;
}

static int in_loadfile(const char *filename)
{
	struct datafeed_header header;
	struct datafeed_packet packet;
	struct device *device;
	char buffer[CHUNKSIZE];
	int fd, size, num_probes;

	if ((fd = open(filename, O_RDONLY)) == -1)
		return SIGROK_ERR;

	/* TODO: Number of probes is hardcoded to 8. */
	num_probes = 8;
	device = device_new(NULL, 0, num_probes);

	header.feed_version = 1;
	header.num_probes = num_probes;
	header.protocol_id = PROTO_RAW;
	header.samplerate = 0;
	gettimeofday(&header.starttime, NULL);
	packet.type = DF_HEADER;
	packet.length = sizeof(struct datafeed_header);
	packet.payload = &header;
	session_bus(device, &packet);

	packet.type = DF_LOGIC8;
	packet.payload = buffer;
	while ((size = read(fd, buffer, CHUNKSIZE)) > 0) {
		packet.length = size;
		session_bus(device, &packet);
	}
	close(fd);

	packet.type = DF_END;
	packet.length = 0;
	session_bus(device, &packet);

	return SIGROK_OK;
}

struct input_format input_binary = {
	"binary",
	"Raw binary",
	format_match,
	in_loadfile,
};
