/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <config.h>
#include "protocol.h"

/*
 * Logic level thresholds.
 *
 * For each of the two channel groups (1-4 and 5-9), the logic level
 * threshold can be set independently.
 *
 * The threshold can be set to values that are usable for systems with
 * different voltage levels, e.g. for 1.8V or 3.3V systems.
 *
 * The actual threshold value is always the middle of the values below.
 * E.g. for a system voltage level of 1.8V, the threshold is at 0.9V. That
 * means that values <= 0.9V are considered to be a logic 0/low, and
 * values > 0.9V are considered to be a logic 1/high.
 *
 *  - 1.2V system: threshold = 0.6V
 *  - 1.5V system: threshold = 0.75V
 *  - 1.8V system: threshold = 0.9V
 *  - 2.8V system: threshold = 1.4V
 *  - 3.3V system: threshold = 1.65V
 */
#define THRESHOLD_1_2V_SYSTEM 0x2e
#define THRESHOLD_1_5V_SYSTEM 0x39
#define THRESHOLD_1_8V_SYSTEM 0x45
#define THRESHOLD_2_8V_SYSTEM 0x6c
#define THRESHOLD_3_3V_SYSTEM 0x7f

static int scanaplus_write(struct dev_context *devc, uint8_t *buf, int size)
{
	int i, bytes_written;
	GString *s;

	/* Note: Caller checks devc, devc->ftdic, buf, size. */

	s = g_string_sized_new(100);
	g_string_printf(s, "Writing %d bytes: ", size);
	for (i = 0; i < size; i++)
		g_string_append_printf(s, "0x%02x ", buf[i]);
	sr_spew("%s", s->str);
	g_string_free(s, TRUE);

	bytes_written = ftdi_write_data(devc->ftdic, buf, size);
	if (bytes_written < 0) {
		sr_err("Failed to write FTDI data (%d): %s.",
		       bytes_written, ftdi_get_error_string(devc->ftdic));
	} else if (bytes_written != size) {
		sr_err("FTDI write error, only %d/%d bytes written: %s.",
		       bytes_written, size, ftdi_get_error_string(devc->ftdic));
	}

	return bytes_written;
}

SR_PRIV int scanaplus_close(struct dev_context *devc)
{
	int ret;

	/* Note: Caller checks devc and devc->ftdic. */

	if ((ret = ftdi_usb_close(devc->ftdic)) < 0) {
		sr_err("Failed to close FTDI device (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	return SR_OK;
}

static void scanaplus_uncompress_block(struct dev_context *devc,
				       uint64_t num_bytes)
{
	uint64_t i, j;
	uint8_t num_samples, low, high;

	for (i = 0; i < num_bytes; i += 2) {
		num_samples = devc->compressed_buf[i + 0] >> 1;

		low = devc->compressed_buf[i + 0] & (1 << 0);
		high = devc->compressed_buf[i + 1];

		for (j = 0; j < num_samples; j++) {
			devc->sample_buf[devc->bytes_received++] = high;
			devc->sample_buf[devc->bytes_received++] = low;
		}
	}
}

static void send_samples(struct dev_context *devc, uint64_t samples_to_send)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	sr_spew("Sending %" PRIu64 " samples.", samples_to_send);

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = samples_to_send * 2;
	logic.unitsize = 2; /* We need 2 bytes for 9 channels. */
	logic.data = devc->sample_buf;
	sr_session_send(devc->cb_data, &packet);

	devc->samples_sent += samples_to_send;
	devc->bytes_received -= samples_to_send * 2;
}

SR_PRIV int scanaplus_get_device_id(struct dev_context *devc)
{
	int ret;
	uint16_t val1, val2;

	/* FTDI EEPROM indices 16+17 contain the 3 device ID bytes. */
	if ((ret = ftdi_read_eeprom_location(devc->ftdic, 16, &val1)) < 0) {
		sr_err("Failed to read EEPROM index 16 (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	if ((ret = ftdi_read_eeprom_location(devc->ftdic, 17, &val2)) < 0) {
		sr_err("Failed to read EEPROM index 17 (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	/*
	 * Note: Bit 7 of the three bytes must not be used, apparently.
	 *
	 * Even though the three bits can be either 0 or 1 (we've seen both
	 * in actual ScanaPLUS devices), the device ID as sent to the FPGA
	 * has bit 7 of each byte zero'd out.
	 *
	 * It is unknown whether bit 7 of these bytes has any meaning,
	 * whether it's used somewhere, or whether it can be simply ignored.
	 */
	devc->devid[0] = ((val1 >> 0) & 0xff) & ~(1 << 7);
	devc->devid[1] = ((val1 >> 8) & 0xff) & ~(1 << 7);
	devc->devid[2] = ((val2 >> 0) & 0xff) & ~(1 << 7);

	return SR_OK;
}

static int scanaplus_clear_device_id(struct dev_context *devc)
{
	uint8_t buf[2];

	buf[0] = 0x8c;
	buf[1] = 0x00;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x8e;
	buf[1] = 0x00;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x8f;
	buf[1] = 0x00;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	return SR_OK;
}

static int scanaplus_send_device_id(struct dev_context *devc)
{
	uint8_t buf[2];

	buf[0] = 0x8c;
	buf[1] = devc->devid[0];
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x8e;
	buf[1] = devc->devid[1];
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x8f;
	buf[1] = devc->devid[2];
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int scanaplus_init(struct dev_context *devc)
{
	int i;
	uint8_t buf[8];

	buf[0] = 0x88;
	buf[1] = 0x41;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x89;
	buf[1] = 0x64;
	buf[2] = 0x8a;
	buf[3] = 0x64;
	if (scanaplus_write(devc, (uint8_t *)&buf, 4) < 0)
		return SR_ERR;

	buf[0] = 0x88;
	buf[1] = 0x41;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x88;
	buf[1] = 0x40;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	buf[0] = 0x8d;
	buf[1] = 0x01;
	buf[2] = 0x8d;
	buf[3] = 0x05;
	buf[4] = 0x8d;
	buf[5] = 0x01;
	buf[6] = 0x8d;
	buf[7] = 0x02;
	if (scanaplus_write(devc, (uint8_t *)&buf, 8) < 0)
		return SR_ERR;

	for (i = 0; i < 57; i++) {
		buf[0] = 0x8d;
		buf[1] = 0x06;
		if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
			return SR_ERR;

		buf[0] = 0x8d;
		buf[1] = 0x02;
		if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
			return SR_ERR;
	}

	if (scanaplus_send_device_id(devc) < 0)
		return SR_ERR;

	buf[0] = 0x88;
	buf[1] = 0x40;
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int scanaplus_start_acquisition(struct dev_context *devc)
{
	uint8_t buf[4];

	/* Threshold and differential channel settings not yet implemented. */

	buf[0] = 0x89;
	buf[1] = 0x7f; /* Logic level threshold for channels 1-4. */
	buf[2] = 0x8a;
	buf[3] = 0x7f; /* Logic level threshold for channels 5-9. */
	if (scanaplus_write(devc, (uint8_t *)&buf, 4) < 0)
		return SR_ERR;

	buf[0] = 0x88;
	buf[1] = 0x40; /* Special config of channels 5/6 and 7/8. */
	/* 0x40: normal, 0x50: ch56 diff, 0x48: ch78 diff, 0x58: ch5678 diff */
	if (scanaplus_write(devc, (uint8_t *)&buf, 2) < 0)
		return SR_ERR;

	if (scanaplus_clear_device_id(devc) < 0)
		return SR_ERR;

	if (scanaplus_send_device_id(devc) < 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int scanaplus_receive_data(int fd, int revents, void *cb_data)
{
	int bytes_read;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint64_t max, n;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (!devc->ftdic)
		return TRUE;

	/* Get a block of data. */
	bytes_read = ftdi_read_data(devc->ftdic, devc->compressed_buf,
				    COMPRESSED_BUF_SIZE);
	if (bytes_read < 0) {
		sr_err("Failed to read FTDI data (%d): %s.",
		       bytes_read, ftdi_get_error_string(devc->ftdic));
		sdi->driver->dev_acquisition_stop(sdi, sdi);
		return FALSE;
	}
	if (bytes_read == 0) {
		sr_spew("Received 0 bytes, nothing to do.");
		return TRUE;
	}

	/*
	 * After a ScanaPLUS acquisition starts, a bunch of samples will be
	 * returned as all-zero, no matter which signals are actually present
	 * on the channels. This is probably due to the FPGA reconfiguring some
	 * of its internal state/config during this time.
	 *
	 * As far as we know there is apparently no way for the PC-side to
	 * know when this "reconfiguration" starts or ends. The FTDI chip
	 * will return all-zero "dummy" samples during this time, which is
	 * indistinguishable from actual all-zero samples.
	 *
	 * We currently simply ignore the first 64kB of data after an
	 * acquisition starts. Empirical tests have shown that the
	 * "reconfigure" time is a lot less than that usually.
	 */
	if (devc->compressed_bytes_ignored < COMPRESSED_BUF_SIZE) {
		/* Ignore the first 64kB of data of every acquisition. */
		sr_spew("Ignoring first 64kB chunk of data.");
		devc->compressed_bytes_ignored += COMPRESSED_BUF_SIZE;
		return TRUE;
	}

	/* TODO: Handle bytes_read which is not a multiple of 2? */
	scanaplus_uncompress_block(devc, bytes_read);

	n = devc->samples_sent + (devc->bytes_received / 2);
	max = (SR_MHZ(100) / 1000) * devc->limit_msec;

	if (devc->limit_samples && (n >= devc->limit_samples)) {
		send_samples(devc, devc->limit_samples - devc->samples_sent);
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	} else if (devc->limit_msec && (n >= max)) {
		send_samples(devc, max - devc->samples_sent);
		sr_info("Requested time limit reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	} else {
		send_samples(devc, devc->bytes_received / 2);
	}

	return TRUE;
}
