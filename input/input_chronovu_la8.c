/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sigrok.h>
#include <sigrok-internal.h>

#define NUM_PACKETS		2048
#define PACKET_SIZE		4096
#define DEFAULT_NUM_PROBES	8

/**
 * Convert the LA8 'divcount' value to the respective samplerate (in Hz).
 *
 * LA8 hardware: sample period = (divcount + 1) * 10ns.
 * Min. value for divcount: 0x00 (10ns sample period, 100MHz samplerate).
 * Max. value for divcount: 0xfe (2550ns sample period, 392.15kHz samplerate).
 *
 * @param divcount The divcount value as needed by the hardware.
 * @return The samplerate in Hz, or 0xffffffffffffffff upon errors.
 */
static uint64_t divcount_to_samplerate(uint8_t divcount)
{
	if (divcount == 0xff)
		return 0xffffffffffffffffULL;

	return SR_MHZ(100) / (divcount + 1);
}

static int format_match(const char *filename)
{
	if (!filename) {
		sr_warn("la8input: %s: filename was NULL", __func__);
		// return SR_ERR; /* FIXME */
		return FALSE;
	}

	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		sr_warn("la8input: %s: input file '%s' does not exist",
			__func__, filename);
		// return SR_ERR; /* FIXME */
		return FALSE;
	}

	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
		sr_warn("la8input: %s: input file '%s' not a regular file",
			__func__, filename);
		// return SR_ERR; /* FIXME */
		return FALSE;
	}

	/* TODO: Only accept files of length 8MB + 5 bytes. */

	/* TODO: Check for divcount != 0xff. */

	return TRUE;
}

static int init(struct sr_input *in)
{
	int num_probes;

	if (in->param && in->param[0]) {
		num_probes = strtoul(in->param, NULL, 10);
		if (num_probes < 1) {
			sr_warn("la8input: %s: strtoul failed", __func__);
			return SR_ERR;
		}
	} else {
		num_probes = DEFAULT_NUM_PROBES;
	}

	/* Create a virtual device. */
	in->vdevice = sr_device_new(NULL, 0, num_probes);

	return SR_OK;
}

static int loadfile(struct sr_input *in, const char *filename)
{
	struct sr_datafeed_header header;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint8_t buf[PACKET_SIZE], divcount;
	int i, fd, size, num_probes;
	uint64_t samplerate;

	/* TODO: Use glib functions! GIOChannel, g_fopen, etc. */
	if ((fd = open(filename, O_RDONLY)) == -1) {
		sr_warn("la8input: %s: file open failed", __func__);
		return SR_ERR;
	}

	num_probes = g_slist_length(in->vdevice->probes);

	/* Seek to the end of the file, and read the divcount byte. */
	divcount = 0x00; /* TODO: Don't hardcode! */

	/* Convert the divcount value to a samplerate. */
	samplerate = divcount_to_samplerate(divcount);
	if (samplerate == 0xffffffffffffffffULL) {
		close(fd); /* FIXME */
		return SR_ERR;
	}
	sr_dbg("la8input: %s: samplerate is %" PRIu64, __func__, samplerate);

	/* Send header packet to the session bus. */
	sr_dbg("la8input: %s: sending SR_DF_HEADER packet", __func__);
	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.num_logic_probes = num_probes;
	header.num_analog_probes = 0;
	header.samplerate = samplerate;
	sr_session_bus(in->vdevice, &packet);

	/* TODO: Handle trigger point. */

	/* Send data packets to the session bus. */
	sr_dbg("la8input: %s: sending SR_DF_LOGIC data packets", __func__);
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = (num_probes + 7) / 8;
	logic.data = buf;

	/* Send 8MB of total data to the session bus in small chunks. */
	for (i = 0; i < NUM_PACKETS; i++) {
		/* TODO: Handle errors, handle incomplete reads. */
		size = read(fd, buf, PACKET_SIZE);
		logic.length = PACKET_SIZE;
		sr_session_bus(in->vdevice, &packet);
	}
	close(fd); /* FIXME */

	/* Send end packet to the session bus. */
	sr_dbg("la8input: %s: sending SR_DF_END", __func__);
	packet.type = SR_DF_END;
	packet.payload = NULL;
	sr_session_bus(in->vdevice, &packet);

	return SR_OK;
}

struct sr_input_format input_chronovu_la8 = {
	.id = "chronovu-la8",
	.description = "ChronoVu LA8",
	.format_match = format_match,
	.init = init,
	.loadfile = loadfile,
};
