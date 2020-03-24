/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017-2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#define LOG_PREFIX "serial-ch9325"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_LIBHIDAPI

/**
 * @file
 *
 * Support serial-over-HID, specifically the WCH CH9325 chip.
 */

#define CH9325_MAX_BYTES_PER_REQUEST	7

static const struct vid_pid_item vid_pid_items_ch9325[] = {
	{ 0x1a86, 0xe008, },	/* CH9325 */
	/*
	 * Strictly speaking Hoitek HE2325U is a different chip, but
	 * shares the programming model with WCH CH9325, and works
	 * with the same support code.
	 */
	{ 0x04fa, 0x2490, },	/* HE2325U */
	ALL_ZERO
};

static int ch9325_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	uint8_t report[6];
	int replen;
	int rc;
	GString *text;

	(void)parity;
	(void)stopbits;
	(void)flowcontrol;
	(void)rts;
	(void)dtr;

	/*
	 * Setup bitrate and frame format. Report layout:
	 * (@-1, length 1, report number)
	 * @0, length 2, bitrate (little endian format)
	 * @2, length 1, unknown (parity? stop bits?)
	 * @3, length 1, unknown (parity? stop bits?)
	 * @4, length 1, data bits (0: 5, 1: 6, etc, 3: 8)
	 */
	replen = 0;
	report[replen++] = 0;
	WL16(&report[replen], baudrate);
	replen += sizeof(uint16_t);
	report[replen++] = 0x00;
	report[replen++] = 0x00;
	report[replen++] = bits - 5;
	rc = ser_hid_hidapi_set_report(serial, report, replen);
	text = sr_hexdump_new(report, replen);
	sr_dbg("DBG: %s() report %s => rc %d", __func__, text->str, rc);
	sr_hexdump_free(text);
	if (rc < 0)
		return SR_ERR;
	if (rc != replen)
		return SR_ERR;

	return SR_OK;
}

static int ch9325_read_bytes(struct sr_serial_dev_inst *serial,
	uint8_t *data, int space, unsigned int timeout)
{
	uint8_t buffer[1 + CH9325_MAX_BYTES_PER_REQUEST];
	int rc;
	int count;

	/*
	 * Check for available input data from the serial port.
	 * Packet layout:
	 * @0, length 1, number of bytes, OR-ed with 0xf0
	 * @1, length N, data bytes (up to 7 bytes)
	 */
	rc = ser_hid_hidapi_get_data(serial, 2, buffer, sizeof(buffer), timeout);
	if (rc < 0)
		return SR_ERR;
	if (rc == 0)
		return 0;
	sr_dbg("DBG: %s() got report len %d, 0x%02x.", __func__, rc, buffer[0]);

	/* Check the length spec, get the byte count. */
	count = buffer[0];
	if ((count & 0xf0) != 0xf0)
		return SR_ERR;
	count &= 0x0f;
	sr_dbg("DBG: %s(), got %d UART RX bytes.", __func__, count);
	if (count > space)
		return SR_ERR;

	/* Pass received data bytes and their count to the caller. */
	memcpy(data, &buffer[1], count);
	return count;
}

static int ch9325_write_bytes(struct sr_serial_dev_inst *serial,
	const uint8_t *data, int size)
{
	uint8_t buffer[1 + CH9325_MAX_BYTES_PER_REQUEST];
	int rc;

	sr_dbg("DBG: %s() shall send UART TX data, len %d.", __func__, size);

	if (size < 1)
		return 0;
	if (size > CH9325_MAX_BYTES_PER_REQUEST) {
		size = CH9325_MAX_BYTES_PER_REQUEST;
		sr_dbg("DBG: %s() capping size to %d.", __func__, size);
	}

	/*
	 * Packet layout to send serial data to the USB HID chip:
	 * (@-1, length 1, report number)
	 * @0, length 1, number of bytes, OR-ed with 0xf0
	 * @1, length N, data bytes (up to 7 bytes)
	 */
	buffer[0] = size;	/* YES! TX is *without* 0xf0! */
	memcpy(&buffer[1], data, size);
	rc = ser_hid_hidapi_set_data(serial, 2, buffer, sizeof(buffer), 0);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;
	return size;
}

static struct ser_hid_chip_functions chip_ch9325 = {
	.chipname = "ch9325",
	.chipdesc = "WCH CH9325",
	.vid_pid_items = vid_pid_items_ch9325,
	.max_bytes_per_request = CH9325_MAX_BYTES_PER_REQUEST,
	.set_params = ch9325_set_params,
	.read_bytes = ch9325_read_bytes,
	.write_bytes = ch9325_write_bytes,
};
SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_ch9325 = &chip_ch9325;

#else

SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_ch9325 = NULL;

#endif
#endif
