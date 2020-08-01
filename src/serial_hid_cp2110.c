/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Carl-Fredrik Sundström <audio.cf@gmail.com>
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

#define LOG_PREFIX "serial-cp2110"

#ifdef HAVE_SERIAL_COMM
#ifdef HAVE_LIBHIDAPI

/**
 * @file
 *
 * Support serial-over-HID, specifically the SiLabs CP2110 chip.
 */

#define CP2110_MAX_BYTES_PER_REQUEST	63

static const struct vid_pid_item vid_pid_items_cp2110[] = {
	{ 0x10c4, 0xea80, },
	ALL_ZERO
};

enum cp2110_report_id {
	CP2110_UART_ENDIS = 0x41,
	CP2110_UART_STATUS = 0x42,
	CP2110_FIFO_PURGE = 0x43,
	CP2110_UART_CONFIG = 0x50,
};

enum cp2110_uart_enable_param {
	CP2110_UART_DISABLE = 0,
	CP2110_UART_ENABLE = 1,
};

enum cp2110_fifo_purge_flag {
	CP2110_FIFO_PURGE_TX = (1 << 0),
	CP2110_FIFO_PURGE_RX = (1 << 1),
};

enum cp2110_uart_config_bitrate {
	CP2110_BAUDRATE_MIN = 300,
	CP2110_BAUDRATE_MAX = 1000000,
};

enum cp2110_uart_config_databits {
	CP2110_DATABITS_MIN = 5,
	CP2110_DATABITS_MAX = 8,
};

enum cp2110_uart_config_parity {
	CP2110_PARITY_NONE = 0,
	CP2110_PARITY_EVEN = 1,
	CP2110_PARITY_ODD = 2,
	CP2110_PARITY_MARK = 3,
	CP2110_PARITY_SPACE = 4,
};

enum cp2110_uart_config_stopbits {
	CP2110_STOPBITS_SHORT = 0,
	CP2110_STOPBITS_LONG = 1,
};

/* Hardware flow control on CP2110 is RTS/CTS only. */
enum cp2110_uart_config_flowctrl {
	CP2110_FLOWCTRL_NONE = 0,
	CP2110_FLOWCTRL_HARD = 1,
};

static int cp2110_set_params(struct sr_serial_dev_inst *serial,
	int baudrate, int bits, int parity, int stopbits,
	int flowcontrol, int rts, int dtr)
{
	uint8_t report[9];
	int replen;
	int rc;

	/* Map serial API specs to CP2110 register values. Check ranges. */
	if (baudrate < CP2110_BAUDRATE_MIN || baudrate > CP2110_BAUDRATE_MAX) {
		sr_err("CP2110: baudrate %d out of range", baudrate);
		return SR_ERR_ARG;
	}
	if (bits < CP2110_DATABITS_MIN || bits > CP2110_DATABITS_MAX) {
		sr_err("CP2110: %d databits out of range", bits);
		return SR_ERR_ARG;
	}
	bits -= CP2110_DATABITS_MIN;
	switch (parity) {
	case SP_PARITY_NONE:
		parity = CP2110_PARITY_NONE;
		break;
	case SP_PARITY_ODD:
		parity = CP2110_PARITY_ODD;
		break;
	case SP_PARITY_EVEN:
		parity = CP2110_PARITY_EVEN;
		break;
	case SP_PARITY_MARK:
		parity = CP2110_PARITY_MARK;
		break;
	case SP_PARITY_SPACE:
		parity = CP2110_PARITY_SPACE;
		break;
	default:
		sr_err("CP2110: unknown parity spec %d", parity);
		return SR_ERR_ARG;
	}
	switch (stopbits) {
	case 1:
		stopbits = CP2110_STOPBITS_SHORT;
		break;
	case 2:
		stopbits = CP2110_STOPBITS_LONG;
		break;
	default:
		sr_err("CP2110: unknown stop bits spec %d", stopbits);
		return SR_ERR_ARG;
	}
	switch (flowcontrol) {
	case SP_FLOWCONTROL_NONE:
		flowcontrol = CP2110_FLOWCTRL_NONE;
		break;
	case SP_FLOWCONTROL_XONXOFF:
		sr_err("CP2110: unsupported XON/XOFF flow control spec");
		return SR_ERR_ARG;
	case SP_FLOWCONTROL_RTSCTS:
		flowcontrol = CP2110_FLOWCTRL_HARD;
		break;
	default:
		sr_err("CP2110: unknown flow control spec %d", flowcontrol);
		return SR_ERR_ARG;
	}

	/*
	 * Enable the UART. Report layout:
	 * @0, length 1, enabled state (0: disable, 1: enable)
	 */
	replen = 0;
	report[replen++] = CP2110_UART_ENDIS;
	report[replen++] = CP2110_UART_ENABLE;
	rc = ser_hid_hidapi_set_report(serial, report, replen);
	if (rc < 0)
		return SR_ERR;
	if (rc != replen)
		return SR_ERR;

	/*
	 * Setup bitrate and frame format. Report layout:
	 * (@-1, length 1, report number)
	 * @0, length 4, bitrate (big endian format)
	 * @4, length 1, parity
	 * @5, length 1, flow control
	 * @6, length 1, data bits (0: 5, 1: 6, 2: 7, 3: 8)
	 * @7, length 1, stop bits
	 */
	replen = 0;
	report[replen++] = CP2110_UART_CONFIG;
	WB32(&report[replen], baudrate);
	replen += sizeof(uint32_t);
	report[replen++] = parity;
	report[replen++] = flowcontrol;
	report[replen++] = bits;
	report[replen++] = stopbits;
	rc = ser_hid_hidapi_set_report(serial, report, replen);
	if (rc < 0)
		return SR_ERR;
	if (rc != replen)
		return SR_ERR;

	/*
	 * Currently not implemented: Control RTS and DTR state.
	 * TODO are these controlled via GPIO requests?
	 * GPIO.1 == RTS, can't find DTR in AN433 table 4.3
	 */
	(void)rts;
	(void)dtr;

	return SR_OK;
}

static int cp2110_read_bytes(struct sr_serial_dev_inst *serial,
	uint8_t *data, int space, unsigned int timeout)
{
	uint8_t buffer[1 + CP2110_MAX_BYTES_PER_REQUEST];
	int rc;
	int count;

	(void)timeout;

	/*
	 * Check for available input data from the serial port.
	 * Packet layout:
	 * @0, length 1, number of bytes, range 0-63
	 * @1, length N, data bytes
	 */
	memset(buffer, 0, sizeof(buffer));
	rc = ser_hid_hidapi_get_data(serial, 0, buffer, sizeof(buffer), timeout);
	if (rc == SR_ERR_TIMEOUT)
		return 0;
	if (rc < 0)
		return SR_ERR;
	if (rc == 0)
		return 0;
	sr_dbg("DBG: %s() got report len %d, 0x%02x.", __func__, rc, buffer[0]);

	/* Check the length spec, get the byte count. */
	count = buffer[0];
	if (!count)
		return 0;
	if (count > CP2110_MAX_BYTES_PER_REQUEST)
		return SR_ERR;
	sr_dbg("DBG: %s(), got %d UART RX bytes.", __func__, count);
	if (count > space)
		return SR_ERR;

	/* Pass received data bytes and their count to the caller. */
	memcpy(data, &buffer[1], count);
	return count;
}

static int cp2110_write_bytes(struct sr_serial_dev_inst *serial,
	const uint8_t *data, int size)
{
	uint8_t buffer[1 + CP2110_MAX_BYTES_PER_REQUEST];
	int rc;

	sr_dbg("DBG: %s() shall send UART TX data, len %d.", __func__, size);

	if (size < 1)
		return 0;
	if (size > CP2110_MAX_BYTES_PER_REQUEST) {
		size = CP2110_MAX_BYTES_PER_REQUEST;
		sr_dbg("DBG: %s() capping size to %d.", __func__, size);
	}

	/*
	 * Packet layout to send serial data to the USB HID chip:
	 * @0, length 1, number of bytes, range 0-63
	 * @1, length N, data bytes
	 */
	buffer[0] = size;
	memcpy(&buffer[1], data, size);
	rc = ser_hid_hidapi_set_data(serial, 0, buffer, sizeof(buffer), 0);
	if (rc < 0)
		return rc;
	if (rc == 0)
		return 0;
	return size;
}

static int cp2110_flush(struct sr_serial_dev_inst *serial)
{
	uint8_t buffer[2];
	int rc;

	sr_dbg("DBG: %s() discarding RX and TX FIFO data.", __func__);

	buffer[0] = CP2110_FIFO_PURGE;
	buffer[1] = CP2110_FIFO_PURGE_TX | CP2110_FIFO_PURGE_RX;
	rc = ser_hid_hidapi_set_data(serial, 0, buffer, sizeof(buffer), 0);
	if (rc != sizeof(buffer))
		return SR_ERR;
	return SR_OK;
}

static int cp2110_drain(struct sr_serial_dev_inst *serial)
{
	uint8_t buffer[7];
	int rc;
	uint16_t tx_fill, rx_fill;

	sr_dbg("DBG: %s() waiting for TX data to drain.", __func__);

	/*
	 * Keep retrieving the UART status until the FIFO is found empty,
	 * or an error occured.
	 * Packet layout:
	 * @0, length 1, report ID
	 * @1, length 2, number of bytes in the TX FIFO (up to 480)
	 * @3, length 2, number of bytes in the RX FIFO (up to 480)
	 * @5, length 1, error status (parity and overrun error flags)
	 * @6, length 1, line break status
	 */
	rx_fill = ~0;
	do {
		memset(buffer, 0, sizeof(buffer));
		buffer[0] = CP2110_UART_STATUS;
		rc = ser_hid_hidapi_get_data(serial, 0, buffer, sizeof(buffer), 0);
		if (rc != sizeof(buffer)) {
			rc = SR_ERR_DATA;
			break;
		}
		if (buffer[0] != CP2110_UART_STATUS) {
			rc = SR_ERR_DATA;
			break;
		}
		rx_fill = RB16(&buffer[1]);
		tx_fill = RB16(&buffer[3]);
		if (!tx_fill) {
			rc = SR_OK;
			break;
		}
		g_usleep(2000);
	} while (1);

	sr_dbg("DBG: %s() TX drained, rc %d, RX fill %u, returning.",
		__func__, rc, (unsigned int)rx_fill);
	return rc;
}

static struct ser_hid_chip_functions chip_cp2110 = {
	.chipname = "cp2110",
	.chipdesc = "SiLabs CP2110",
	.vid_pid_items = vid_pid_items_cp2110,
	.max_bytes_per_request = CP2110_MAX_BYTES_PER_REQUEST,
	.set_params = cp2110_set_params,
	.read_bytes = cp2110_read_bytes,
	.write_bytes = cp2110_write_bytes,
	.flush = cp2110_flush,
	.drain = cp2110_drain,
};
SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_cp2110 = &chip_cp2110;

#else

SR_PRIV struct ser_hid_chip_functions *ser_hid_chip_funcs_cp2110 = NULL;

#endif
#endif
