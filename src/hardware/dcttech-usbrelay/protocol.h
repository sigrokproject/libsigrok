/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_DCTTECH_USBRELAY_PROTOCOL_H
#define LIBSIGROK_HARDWARE_DCTTECH_USBRELAY_PROTOCOL_H

#include <glib.h>
#include <hidapi.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "dcttech-usbrelay"

/* USB identification. */
#define VENDOR_ID 0x16c0
#define PRODUCT_ID 0x05df
#define VENDOR_STRING "www.dcttech.com"
#define PRODUCT_STRING_PREFIX "USBRelay"

/* HID report layout. */
#define REPORT_NUMBER 0
#define REPORT_BYTECOUNT 8
#define SERNO_LENGTH 5
#define STATE_INDEX 7

struct dev_context {
	char *hid_path;
	uint16_t usb_vid, usb_pid;
	hid_device *hid_dev;
	size_t relay_count;
	uint32_t relay_mask;
	uint32_t relay_state;
};

struct channel_group_context {
	size_t number;
};

SR_PRIV int dcttech_usbrelay_update_state(const struct sr_dev_inst *sdi);
SR_PRIV int dcttech_usbrelay_switch_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean on);
SR_PRIV int dcttech_usbrelay_query_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean *on);

#endif
