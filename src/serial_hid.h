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

#ifndef LIBSIGROK_SERIAL_HID_H
#define LIBSIGROK_SERIAL_HID_H

/* The prefix for port names which are HID based. */
#define SER_HID_CONN_PREFIX	"hid"
#define SER_HID_USB_PREFIX	"usb="
#define SER_HID_RAW_PREFIX	"raw="
#define SER_HID_IOKIT_PREFIX	"iokit="
#define SER_HID_SNR_PREFIX	"sn="

/*
 * The maximum number of bytes any supported HID chip can communicate
 * within a single request.
 *
 * Brymen BU-86X: up to 8 bytes
 * SiLabs CP2110: up to 63 bytes
 * Victor DMM:    up to 14 bytes
 * WCH CH9325:    up to 7 bytes
 */
#define SER_HID_CHUNK_SIZE	64

/*
 * Routines to get/set reports/data, provided by serial_hid.c and used
 * in serial_hid_<chip>.c files.
 */
SR_PRIV int ser_hid_hidapi_get_report(struct sr_serial_dev_inst *serial,
	uint8_t *data, size_t len);
SR_PRIV int ser_hid_hidapi_set_report(struct sr_serial_dev_inst *serial,
	const uint8_t *data, size_t len);
SR_PRIV int ser_hid_hidapi_get_data(struct sr_serial_dev_inst *serial,
	uint8_t ep, uint8_t *data, size_t len, int timeout);
SR_PRIV int ser_hid_hidapi_set_data(struct sr_serial_dev_inst *serial,
	uint8_t ep, const uint8_t *data, size_t len, int timeout);

#endif
