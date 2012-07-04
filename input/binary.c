/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define CHUNKSIZE             (512 * 1024)
#define DEFAULT_NUM_PROBES    8

static int format_match(const char *filename)
{
	/* suppress compiler warning */
	(void)filename;

	/* this module will handle anything you throw at it */
	return TRUE;
}

static int init(struct sr_input *in)
{
	int num_probes, i;
	char name[SR_MAX_PROBENAME_LEN + 1];

	if (in->param && in->param[0]) {
		num_probes = strtoul(in->param, NULL, 10);
		if (num_probes < 1)
			return SR_ERR;
	} else {
		num_probes = DEFAULT_NUM_PROBES;
	}

	/* Create a virtual device. */
	in->vdev = sr_dev_new(NULL, 0);

	for (i = 0; i < num_probes; i++) {
		snprintf(name, SR_MAX_PROBENAME_LEN, "%d", i);
		/* TODO: Check return value. */
		sr_dev_probe_add(in->vdev, name);
	}

	return SR_OK;
}

static int loadfile(struct sr_input *in, const char *filename)
{
	struct sr_datafeed_header header;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta_logic meta;
	struct sr_datafeed_logic logic;
	unsigned char buffer[CHUNKSIZE];
	int fd, size, num_probes;

	if ((fd = open(filename, O_RDONLY)) == -1)
		return SR_ERR;

	num_probes = g_slist_length(in->vdev->probes);

	/* send header */
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	sr_session_send(in->vdev, &packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet.type = SR_DF_META_LOGIC;
	packet.payload = &meta;
	meta.samplerate = 0;
	meta.num_probes = num_probes;
	sr_session_send(in->vdev, &packet);

	/* chop up the input file into chunks and feed it into the session bus */
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = (num_probes + 7) / 8;
	logic.data = buffer;
	while ((size = read(fd, buffer, CHUNKSIZE)) > 0) {
		logic.length = size;
		sr_session_send(in->vdev, &packet);
	}
	close(fd);

	/* end of stream */
	packet.type = SR_DF_END;
	sr_session_send(in->vdev, &packet);

	return SR_OK;
}

SR_PRIV struct sr_input_format input_binary = {
	.id = "binary",
	.description = "Raw binary",
	.format_match = format_match,
	.init = init,
	.loadfile = loadfile,
};
