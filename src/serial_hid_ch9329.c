/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Jochen Rueter <jo.rueter@gmail.com>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "serial_hid.h"
#include <string.h>

#define LOG_PREFIX "serial-ch9329"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_LIBHIDAPI

/**
 * @file
 *
 * Support serial-over-HID, specifically the WCH CH9329 chip.
 *
 * The CH9329 is used in the UNI-T UT-D09A cable (shipped with e.g. the
 * UT161B/D/E multimeters). It operates in "custom HID" mode, where
 * both directions use 64 byte reports without a report ID. The first
 * byte of a report holds the number of UART data bytes that follow.
 * UART parameters are part of the chip's (persistent) configuration
 * and cannot be changed by means of HID requests.
 */

#define CH9329_REPORT_SIZE		64
#define CH9329_MAX_BYTES_PER_REQUEST	(CH9329_REPORT_SIZE - 1)

static const struct vid_pid_item vid_pid_items_ch9329[] = {
	{ 0x1a86, 0xe429, },	/* CH9329, UNI-T UT-D09A */
	ALL_ZERO
};

static int ch9329_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	(void)serial;
	(void)baudrate;
	(void)bits;
	(void)parity;
	(void)stopbits;
	(void)flowcontrol;
	(void)rts;
	(void)dtr;

	/*
	 * The UART side is configured by the cable's firmware setup
	 * (9600/8n1 for the UNI-T cable). Accept any caller's spec.
	 */
	return SR_OK;
}

static int ch9329_read_bytes(struct sr_serial_dev_inst *serial,
	uint8_t *data, int space, unsigned int timeout)
{
	uint8_t buffer[CH9329_REPORT_SIZE];
	int rc;
	int count;

	/*
	 * Check for available input data from the serial port.
	 * Packet layout:
	 * @0, length 1, number of bytes
	 * @1, length N, data bytes (up to 63 bytes)
	 */
	rc = ser_hid_hidapi_get_data(serial, 0, buffer, sizeof(buffer), timeout);
	if (rc < 0)
		return SR_ERR;
	if (rc == 0)
		return 0;

	count = buffer[0];
	if (count > rc - 1 || count > CH9329_MAX_BYTES_PER_REQUEST)
		return SR_ERR;
	sr_spew("%s(), got %d UART RX bytes.", __func__, count);
	if (count > space)
		return SR_ERR;

	memcpy(data, &buffer[1], count);
	return count;
}

static int ch9329_write_bytes(struct sr_serial_dev_inst *serial,
	const uint8_t *data, int size)
{
	uint8_t buffer[1 + CH9329_REPORT_SIZE];
	int rc;

	if (size < 1)
		return 0;
	if (size > CH9329_MAX_BYTES_PER_REQUEST)
		size = CH9329_MAX_BYTES_PER_REQUEST;

	/*
	 * Packet layout to send serial data to the USB HID chip:
	 * @-1, length 1, report number (always 0, no report IDs)
	 * @0, length 1, number of bytes
	 * @1, length N, data bytes (up to 63 bytes)
	 * Always send the full report size, some platforms insist.
	 */
	memset(buffer, 0, sizeof(buffer));
	buffer[1] = size;
	memcpy(&buffer[2], data, size);
	rc = ser_hid_hidapi_set_data(serial, 0, buffer, sizeof(buffer), 0);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;
	return size;
}

static struct ser_hid_chip_functions chip_ch9329 = {
	.chipname = "ch9329",
	.chipdesc = "WCH CH9329",
	.vid_pid_items = vid_pid_items_ch9329,
	.max_bytes_per_request = CH9329_MAX_BYTES_PER_REQUEST,
	.set_params = ch9329_set_params,
	.read_bytes = ch9329_read_bytes,
	.write_bytes = ch9329_write_bytes,
};
SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_ch9329 = &chip_ch9329;

#else

SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_ch9329 = NULL;

#endif
#endif
