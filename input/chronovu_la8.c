/*
 * This file is part of the libsigrok project.
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
#include <sys/stat.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/chronovu-la8"

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
 *
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
	struct stat stat_buf;
	int ret;

	if (!filename) {
		sr_err("%s: filename was NULL", __func__);
		// return SR_ERR; /* FIXME */
		return FALSE;
	}

	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		sr_err("%s: input file '%s' does not exist",
		       __func__, filename);
		// return SR_ERR; /* FIXME */
		return FALSE;
	}

	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
		sr_err("%s: input file '%s' not a regular file",
		       __func__, filename);
		// return SR_ERR; /* FIXME */
		return FALSE;
	}

	/* Only accept files of length 8MB + 5 bytes. */
	ret = stat(filename, &stat_buf);
	if (ret != 0) {
		sr_err("%s: Error getting file size of '%s'",
		       __func__, filename);
		return FALSE;
	}
	if (stat_buf.st_size != (8 * 1024 * 1024 + 5)) {
		sr_dbg("%s: File size must be exactly 8388613 bytes ("
		       "it actually is %d bytes in size), so this is not a "
		       "ChronoVu LA8 file.", __func__, stat_buf.st_size);
		return FALSE;
	}

	/* TODO: Check for divcount != 0xff. */

	return TRUE;
}

static int init(struct sr_input *in, const char *filename)
{
	struct sr_channel *probe;
	int num_probes, i;
	char name[SR_MAX_PROBENAME_LEN + 1];
	char *param;

	(void)filename;

	num_probes = DEFAULT_NUM_PROBES;

	if (in->param) {
		param = g_hash_table_lookup(in->param, "numprobes");
		if (param) {
			num_probes = strtoul(param, NULL, 10);
			if (num_probes < 1) {
				sr_err("%s: strtoul failed", __func__);
				return SR_ERR;
			}
		}
	}

	/* Create a virtual device. */
	in->sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, NULL, NULL, NULL);

	for (i = 0; i < num_probes; i++) {
		snprintf(name, SR_MAX_PROBENAME_LEN, "%d", i);
		/* TODO: Check return value. */
		if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE, name)))
			return SR_ERR;
		in->sdi->probes = g_slist_append(in->sdi->probes, probe);
	}

	return SR_OK;
}

static int loadfile(struct sr_input *in, const char *filename)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_datafeed_logic logic;
	struct sr_config *src;
	uint8_t buf[PACKET_SIZE], divcount;
	int i, fd, size, num_probes;
	uint64_t samplerate;

	/* TODO: Use glib functions! GIOChannel, g_fopen, etc. */
	if ((fd = open(filename, O_RDONLY)) == -1) {
		sr_err("%s: file open failed", __func__);
		return SR_ERR;
	}

	num_probes = g_slist_length(in->sdi->probes);

	/* Seek to the end of the file, and read the divcount byte. */
	divcount = 0x00; /* TODO: Don't hardcode! */

	/* Convert the divcount value to a samplerate. */
	samplerate = divcount_to_samplerate(divcount);
	if (samplerate == 0xffffffffffffffffULL) {
		close(fd); /* FIXME */
		return SR_ERR;
	}
	sr_dbg("%s: samplerate is %" PRIu64, __func__, samplerate);

	/* Send header packet to the session bus. */
	std_session_send_df_header(in->sdi, LOG_PREFIX);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet.type = SR_DF_META;
	packet.payload = &meta;
	src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(samplerate));
	meta.config = g_slist_append(NULL, src);
	sr_session_send(in->sdi, &packet);
	sr_config_free(src);

	/* TODO: Handle trigger point. */

	/* Send data packets to the session bus. */
	sr_dbg("%s: sending SR_DF_LOGIC data packets", __func__);
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = (num_probes + 7) / 8;
	logic.data = buf;

	/* Send 8MB of total data to the session bus in small chunks. */
	for (i = 0; i < NUM_PACKETS; i++) {
		/* TODO: Handle errors, handle incomplete reads. */
		size = read(fd, buf, PACKET_SIZE);
		logic.length = size;
		sr_session_send(in->sdi, &packet);
	}
	close(fd); /* FIXME */

	/* Send end packet to the session bus. */
	sr_dbg("%s: sending SR_DF_END", __func__);
	packet.type = SR_DF_END;
	packet.payload = NULL;
	sr_session_send(in->sdi, &packet);

	return SR_OK;
}

SR_PRIV struct sr_input_format input_chronovu_la8 = {
	.id = "chronovu-la8",
	.description = "ChronoVu LA8",
	.format_match = format_match,
	.init = init,
	.loadfile = loadfile,
};
