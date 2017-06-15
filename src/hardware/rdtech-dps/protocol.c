/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 James Churchill <pelrun@gmail.com>
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
#include "protocol.h"

SR_PRIV int rdtech_dps_get_reg(struct sr_modbus_dev_inst *modbus,
		uint16_t address, uint16_t *value)
{
	uint16_t registers[1];
	int ret = sr_modbus_read_holding_registers(modbus, address, 1, registers);
	*value = RB16(registers + 0);
	return ret;
}

SR_PRIV int rdtech_dps_set_reg(struct sr_modbus_dev_inst *modbus,
		uint16_t address, uint16_t value)
{
	uint16_t registers[1];
	WB16(registers, value);
	return sr_modbus_write_multiple_registers(modbus, address, 1, registers);
}

SR_PRIV int rdtech_dps_get_model_version(struct sr_modbus_dev_inst *modbus,
		uint16_t *model, uint16_t *version)
{
	uint16_t registers[2];
	int ret;
	ret = sr_modbus_read_holding_registers(modbus, REG_MODEL, 2, registers);
	if (ret == SR_OK) {
		*model = RB16(registers + 0);
		*version = RB16(registers + 1);
		sr_info("RDTech PSU model: %d version: %d", *model, *version);
	}
	return ret;
}

static void send_value(const struct sr_dev_inst *sdi, struct sr_channel *ch,
		float value, enum sr_mq mq, enum sr_unit unit, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = 1;
	analog.data = &value;
	analog.meaning->mq = mq;
	analog.meaning->unit = unit;
	analog.meaning->mqflags = SR_MQFLAG_DC;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

SR_PRIV int rdtech_dps_capture_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	int ret;

	modbus = sdi->conn;
	devc = sdi->priv;

	if ((ret = sr_modbus_read_holding_registers(modbus, REG_UOUT, 3, NULL)) == SR_OK)
		devc->expecting_registers = 2;
	return ret;
}

SR_PRIV int rdtech_dps_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	struct sr_datafeed_packet packet;
	uint16_t registers[3];

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	modbus = sdi->conn;
	devc = sdi->priv;

	devc->expecting_registers = 0;
	if (sr_modbus_read_holding_registers(modbus, -1, 3, registers) == SR_OK) {
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);

		send_value(sdi, sdi->channels->data,
			RB16(registers + 0) / 100.0f,
			SR_MQ_VOLTAGE, SR_UNIT_VOLT, 3);
		send_value(sdi, sdi->channels->next->data,
			RB16(registers + 1) / 1000.0f,
			SR_MQ_CURRENT, SR_UNIT_AMPERE, 4);
		send_value(sdi, sdi->channels->next->next->data,
			RB16(registers + 2) / 100.0f,
			SR_MQ_POWER, SR_UNIT_WATT, 3);

		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	rdtech_dps_capture_start(sdi);
	return TRUE;
}
