/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Aurelien Jacobs <aurel@gnuage.org>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#define LOG_PREFIX "modbus_serial"

#define BUFFER_SIZE 1024

struct modbus_serial_rtu {
	struct sr_serial_dev_inst *serial;
	uint8_t slave_addr;
	uint16_t crc;
};

static int modbus_serial_rtu_dev_inst_new(void *priv, const char *resource,
		char **params, const char *serialcomm, int modbusaddr)
{
	struct modbus_serial_rtu *modbus = priv;

	(void) params;

	modbus->serial = sr_serial_dev_inst_new(resource, serialcomm);
	modbus->slave_addr = modbusaddr;

	return SR_OK;
}

static int modbus_serial_rtu_open(void *priv)
{
	struct modbus_serial_rtu *modbus = priv;
	struct sr_serial_dev_inst *serial = modbus->serial;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	if (serial_flush(serial) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int modbus_serial_rtu_source_add(struct sr_session *session, void *priv,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data)
{
	struct modbus_serial_rtu *modbus = priv;
	struct sr_serial_dev_inst *serial = modbus->serial;

	return serial_source_add(session, serial, events, timeout, cb, cb_data);
}

static int modbus_serial_rtu_source_remove(struct sr_session *session,
		void *priv)
{
	struct modbus_serial_rtu *modbus = priv;
	struct sr_serial_dev_inst *serial = modbus->serial;

	return serial_source_remove(session, serial);
}

static uint16_t modbus_serial_rtu_crc(uint16_t crc,
		const uint8_t *buffer, int len)
{
	int i;

	if (!buffer || len < 0)
		return crc;

	while (len--) {
		crc ^= *buffer++;
		for (i = 0; i < 8; i++) {
			int carry = crc & 1;
			crc >>= 1;
			if (carry)
				crc ^= 0xA001;
		}
	}
	return crc;
}

static int modbus_serial_rtu_send(void *priv,
		const uint8_t *buffer, int buffer_size)
{
	int result;
	struct modbus_serial_rtu *modbus = priv;
	struct sr_serial_dev_inst *serial = modbus->serial;
	uint8_t slave_addr = modbus->slave_addr;
	uint16_t crc;

	result = serial_write_nonblocking(serial, &slave_addr, sizeof(slave_addr));
	if (result < 0)
		return result;

	result = serial_write_nonblocking(serial, buffer, buffer_size);
	if (result < 0)
		return result;

	crc = modbus_serial_rtu_crc(0xFFFF, &slave_addr, sizeof(slave_addr));
	crc = modbus_serial_rtu_crc(crc, buffer, buffer_size);

	result = serial_write_nonblocking(serial, &crc, sizeof(crc));
	if (result < 0)
		return result;

	return SR_OK;
}

static int modbus_serial_rtu_read_begin(void *priv, uint8_t *function_code)
{
	struct modbus_serial_rtu *modbus = priv;
	uint8_t slave_addr;
	int ret;

	ret = serial_read_blocking(modbus->serial, &slave_addr, 1, 100);
	if (ret != 1 || slave_addr != modbus->slave_addr)
		return ret;

	ret = serial_read_blocking(modbus->serial, function_code, 1, 100);
	if (ret != 1)
		return ret;

	modbus->crc = modbus_serial_rtu_crc(0xFFFF, &slave_addr, sizeof(slave_addr));
	modbus->crc = modbus_serial_rtu_crc(modbus->crc, function_code, 1);

	return SR_OK;
}

static int modbus_serial_rtu_read_data(void *priv, uint8_t *buf, int maxlen)
{
	struct modbus_serial_rtu *modbus = priv;
	int ret;

	ret = serial_read_nonblocking(modbus->serial, buf, maxlen); 
	if (ret < 0)
		return ret;
	modbus->crc = modbus_serial_rtu_crc(modbus->crc, buf, ret); 
	return ret; 
}

static int modbus_serial_rtu_read_end(void *priv)
{
	struct modbus_serial_rtu *modbus = priv;
	uint16_t crc;
	int ret;

	ret = serial_read_blocking(modbus->serial, &crc, sizeof(crc), 100);
	if (ret != 2)
		return ret;

	if (crc != modbus->crc) {
		sr_err("CRC error (0x%04X vs 0x%04X).", crc, modbus->crc);
		return SR_ERR_DATA;
	}

	return SR_OK;
}

static int modbus_serial_rtu_close(void *priv)
{
	struct modbus_serial_rtu *modbus = priv;

	return serial_close(modbus->serial);
}

static void modbus_serial_rtu_free(void *priv)
{
	struct modbus_serial_rtu *modbus = priv;

	sr_serial_dev_inst_free(modbus->serial);
}

SR_PRIV const struct sr_modbus_dev_inst modbus_serial_rtu_dev = {
	.name          = "serial_rtu",
	.prefix        = "",
	.priv_size     = sizeof(struct modbus_serial_rtu),
	.scan          = NULL,
	.dev_inst_new  = modbus_serial_rtu_dev_inst_new,
	.open          = modbus_serial_rtu_open,
	.source_add    = modbus_serial_rtu_source_add,
	.source_remove = modbus_serial_rtu_source_remove,
	.send          = modbus_serial_rtu_send,
	.read_begin    = modbus_serial_rtu_read_begin,
	.read_data     = modbus_serial_rtu_read_data,
	.read_end      = modbus_serial_rtu_read_end,
	.close         = modbus_serial_rtu_close,
	.free          = modbus_serial_rtu_free,
};
