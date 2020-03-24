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
 * This implements serial transport primitives for Victor DMM cables,
 * which forward normal DMM chips' protocols, but scramble the data in
 * the process of forwarding. Just undoing the cable's scrambling at
 * the serial communication level allows full re-use of existing DMM
 * drivers, instead of creating Victor DMM specific support code.
 *
 * The cable's scrambling is somewhat complex:
 * - The order of bits within the bytes gets reversed.
 * - The order of bytes within the packet gets shuffled (randomly).
 * - The byte values randomly get mapped to other values by adding a
 *   sequence of magic values to packet's byte values.
 * None of this adds any value to the DMM chip vendor's protocol. It's
 * mere obfuscation and extra complexity for the receiving application.
 */

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "serial_hid.h"
#include <string.h>

#define LOG_PREFIX "serial-victor"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_LIBHIDAPI

/**
 * @file
 *
 * Support serial-over-HID, specifically the Victor 70/86 DMM cables.
 */

#define VICTOR_DMM_PACKET_LENGTH	14

static const struct vid_pid_item vid_pid_items_victor[] = {
	{ 0x1244, 0xd237, },
	ALL_ZERO
};

/* Reverse bits within a byte. */
static uint8_t bit_reverse(uint8_t b)
{
	static const uint8_t rev_nibble[] = {
		0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e,
		0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f,
	};

	return (rev_nibble[(b >> 4) & 0xf]) |
		(rev_nibble[b & 0xf] << 4);
}

/*
 * The cable receives data by means of HID reports (simple data stream,
 * HID report encapsulation was already trimmed). Assume that received
 * data "is aligned", cope with zero or one 14-byte packets here, but
 * don't try to even bother with odd-length reception units. Also drop
 * the "all-zero" packets here which victor_dmm_receive_data() used to
 * eliminate at the device driver level in the past.
 */
static int victor_unobfuscate(uint8_t *rx_buf, size_t rx_len,
	uint8_t *ret_buf)
{
	static const uint8_t obfuscation[VICTOR_DMM_PACKET_LENGTH] = {
		'j', 'o', 'd', 'e', 'n', 'x', 'u', 'n', 'i', 'c', 'k', 'x', 'i', 'a',
	};
	static const uint8_t shuffle[VICTOR_DMM_PACKET_LENGTH] = {
		6, 13, 5, 11, 2, 7, 9, 8, 3, 10, 12, 0, 4, 1
	};

	GString *txt;
	int is_zero;
	size_t idx, to_idx;

	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		txt = sr_hexdump_new(rx_buf, rx_len);
		sr_spew("Received %zu bytes: %s.", rx_len, txt->str);
		sr_hexdump_free(txt);
	}

	/* Pass unexpected data in verbatim form. */
	if (rx_len != VICTOR_DMM_PACKET_LENGTH) {
		memcpy(ret_buf, rx_buf, rx_len);
		return rx_len;
	}

	/* Check for and discard all-zero packets. */
	is_zero = 1;
	for (idx = 0; is_zero && idx < VICTOR_DMM_PACKET_LENGTH; idx++) {
		if (rx_buf[idx])
			is_zero = 0;
	}
	if (is_zero) {
		sr_dbg("Received all zeroes packet, discarding.");
		return 0;
	}

	/*
	 * Unobfuscate data bytes by subtracting a magic pattern, shuffle
	 * the bits and bytes into the DMM chip's original order.
	 */
	for (idx = 0; idx < VICTOR_DMM_PACKET_LENGTH; idx++) {
		to_idx = VICTOR_DMM_PACKET_LENGTH - 1 - shuffle[idx];
		ret_buf[to_idx] = bit_reverse(rx_buf[idx] - obfuscation[idx]);
	}

	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		txt = sr_hexdump_new(ret_buf, idx);
		sr_spew("Deobfuscated: %s.", txt->str);
		sr_hexdump_free(txt);
	}

	return rx_len;
}

/*
 * Read into a local buffer, and unobfuscate into the caller's buffer.
 * Always receive full DMM packets.
 */
static int victor_read_bytes(struct sr_serial_dev_inst *serial,
	uint8_t *data, int space, unsigned int timeout)
{
	uint8_t buf[VICTOR_DMM_PACKET_LENGTH];
	int rc;

	if (space != sizeof(buf))
		space = sizeof(buf);
	rc = ser_hid_hidapi_get_data(serial, 0, buf, space, timeout);
	if (rc == SR_ERR_TIMEOUT)
		return 0;
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;

	return victor_unobfuscate(buf, rc, data);
}

/* Victor DMM cables are read-only. Just pretend successful transmission. */
static int victor_write_bytes(struct sr_serial_dev_inst *serial,
	const uint8_t *data, int size)
{
	(void)serial;
	(void)data;

	return size;
}

static struct ser_hid_chip_functions chip_victor = {
	.chipname = "victor",
	.chipdesc = "Victor DMM scrambler",
	.vid_pid_items = vid_pid_items_victor,
	.max_bytes_per_request = VICTOR_DMM_PACKET_LENGTH,
	/*
	 * The USB HID connection has no concept of UART bitrate or
	 * frame format. Silently ignore the parameters.
	 */
	.set_params = std_dummy_set_params,
	.read_bytes = victor_read_bytes,
	.write_bytes = victor_write_bytes,
};
SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_victor = &chip_victor;

#else

SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_victor = NULL;

#endif
#endif
