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

#define LOG_PREFIX "serial-ch9329"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_LIBHIDAPI

/**
 * @file
 *
 * Support serial-over-HID for the WCH CH9329 found in some UNI-T meter
 * USB cables (USB VID:PID 1a86:e429), running in the chip's custom-HID
 * mode (chip work mode 3, serial mode 2 per the CH9329 datasheet): a
 * plain bidirectional UART tunnel over a vendor-defined HID interface
 * (usage page 0xffa0), no keyboard/mouse emulation involved.
 */

#define CH9329_REPORT_SIZE		64
#define CH9329_MAX_BYTES_PER_REQUEST	(CH9329_REPORT_SIZE - 1)

static const struct vid_pid_item vid_pid_items_ch9329[] = {
	{ 0x1a86, 0xe429, },	/* WCH CH9329 in UNI-T meter USB cable (custom-HID mode) */
	ALL_ZERO
};

static int ch9329_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	/*
	 * The CH9329's UART line coding is persistent chip configuration,
	 * not a per-session USB transaction: it is programmed through a
	 * dedicated parameter block that is gated behind the chip's SET
	 * pin and only takes effect after a power-on/reset (see the
	 * @file comment above). There is nothing to negotiate here over
	 * the plain HID tunnel this driver uses, and the vendor software
	 * itself never issues a rate change on this path either. The
	 * meter and the cable both run fixed at the chip's 9600 8N1
	 * default, so this is intentionally a no-op.
	 */
	(void)serial;
	(void)baudrate;
	(void)bits;
	(void)parity;
	(void)stopbits;
	(void)flowcontrol;
	(void)rts;
	(void)dtr;

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
	 * Packet layout (no report-ID placeholder needed for reads):
	 * @0, length 1, number of bytes (0..63, not masked)
	 * @1, length N, data bytes (up to 63 bytes)
	 */
	rc = ser_hid_hidapi_get_data(serial, 2, buffer, sizeof(buffer), timeout);
	if (rc < 0)
		return SR_ERR;
	if (rc == 0)
		return 0;
	sr_dbg("DBG: %s() got report len %d, count %d.", __func__, rc, buffer[0]);

	count = buffer[0];
	if (count > CH9329_MAX_BYTES_PER_REQUEST)
		return SR_ERR;
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

	sr_dbg("DBG: %s() shall send UART TX data, len %d.", __func__, size);

	if (size < 1)
		return 0;
	if (size > CH9329_MAX_BYTES_PER_REQUEST) {
		size = CH9329_MAX_BYTES_PER_REQUEST;
		sr_dbg("DBG: %s() capping size to %d.", __func__, size);
	}

	/*
	 * Packet layout to send serial data to the USB HID chip. This
	 * device has no report IDs, but hid_write() still requires the
	 * mandatory report-ID placeholder byte ahead of the actual report
	 * content (length = report size + 1):
	 * (@-1, length 1, report-ID placeholder, always 0)
	 * @0, length 1, number of bytes (0..63, not masked)
	 * @1, length N, data bytes (up to 63 bytes)
	 */
	memset(buffer, 0, sizeof(buffer));
	buffer[1] = size;
	memcpy(&buffer[2], data, size);
	rc = ser_hid_hidapi_set_data(serial, 2, buffer, sizeof(buffer), 0);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;
	return size;
}

static struct ser_hid_chip_functions chip_ch9329 = {
	.chipname = "ch9329",
	.chipdesc = "WCH CH9329, custom-HID mode (UNI-T meter cable, 1a86:e429)",
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
