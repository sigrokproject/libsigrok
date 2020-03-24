/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * This implements serial communication primitives for the Brymen BU-86X
 * infrared adapter for handheld multimeters. The vendor's protocol spec
 * http://brymen.com/product-html/images/DownloadList/ProtocolList/BM860-BM860s_List/BM860-BM860s-500000-count-dual-display-DMMs-protocol.pdf
 * suggests that HID reports get communicated, but only report number 0
 * is involved, which carries a mere byte stream in 8 byte chunks each.
 * The frame format and bitrate are fixed, and need not get configured.
 *
 * The meter's packet consists of 24 bytes which get received in three
 * HID reports. Packet reception gets initiated by sending a short HID
 * report to the meter. It's uncertain which parts of this exchange are
 * specific to the adapter and to the meter. Using the IR adapter with
 * other devices, or using the meter with other cables/adapters may need
 * a little more adjustment with respect to layering.
 */

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "serial_hid.h"
#include <string.h>

#define LOG_PREFIX "serial-bu86x"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_LIBHIDAPI

/**
 * @file
 *
 * Support serial-over-HID, specifically the Brymen BU-86X infrared adapter.
 */

#define BU86X_MAX_BYTES_PER_REQUEST	8

static const struct vid_pid_item vid_pid_items_bu86x[] = {
	{ 0x0820, 0x0001, },
	ALL_ZERO
};

static int bu86x_read_bytes(struct sr_serial_dev_inst *serial,
	uint8_t *data, int space, unsigned int timeout)
{
	int rc;

	if (space > BU86X_MAX_BYTES_PER_REQUEST)
		space = BU86X_MAX_BYTES_PER_REQUEST;
	rc = ser_hid_hidapi_get_data(serial, 0, data, space, timeout);
	if (rc == SR_ERR_TIMEOUT)
		return 0;
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;
	return rc;
}

static int bu86x_write_bytes(struct sr_serial_dev_inst *serial,
	const uint8_t *data, int size)
{
	return ser_hid_hidapi_set_data(serial, 0, data, size, 0);
}

static struct ser_hid_chip_functions chip_bu86x = {
	.chipname = "bu86x",
	.chipdesc = "Brymen BU-86X",
	.vid_pid_items = vid_pid_items_bu86x,
	.max_bytes_per_request = BU86X_MAX_BYTES_PER_REQUEST,
	/*
	 * The IR adapter's communication parameters are fixed and need
	 * not get configured. Just silently ignore the caller's spec.
	 */
	.set_params = std_dummy_set_params,
	.read_bytes = bu86x_read_bytes,
	.write_bytes = bu86x_write_bytes,
};
SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_bu86x = &chip_bu86x;

#else

SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_bu86x = NULL;

#endif
#endif
