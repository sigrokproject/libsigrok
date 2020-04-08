/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 James Churchill <pelrun@gmail.com>
 * Copyright (C) 2019 Frank Stettner <frank-stettner@gmx.net>
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

SR_PRIV int rdtech_dps_read_holding_registers(struct sr_modbus_dev_inst *modbus,
		int address, int nb_registers, uint16_t *registers)
{
	int i, ret;

	i = 0;
	do {
		ret = sr_modbus_read_holding_registers(modbus,
			address, nb_registers, registers);
		++i;
	} while (ret != SR_OK && i < 3);

	return ret;
}

SR_PRIV int rdtech_dps_get_reg(const struct sr_dev_inst *sdi,
		uint16_t address, uint16_t *value)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	uint16_t registers[1];
	int ret;

	devc = sdi->priv;
	modbus = sdi->conn;

	g_mutex_lock(&devc->rw_mutex);
	ret = rdtech_dps_read_holding_registers(modbus, address, 1, registers);
	g_mutex_unlock(&devc->rw_mutex);
	*value = RB16(registers + 0);
	return ret;
}

SR_PRIV int rdtech_dps_set_reg(const struct sr_dev_inst *sdi,
		uint16_t address, uint16_t value)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	uint16_t registers[1];
	int ret;

	devc = sdi->priv;
	modbus = sdi->conn;

	WB16(registers, value);
	g_mutex_lock(&devc->rw_mutex);
	ret = sr_modbus_write_multiple_registers(modbus, address, 1, registers);
	g_mutex_unlock(&devc->rw_mutex);
	return ret;
}

SR_PRIV int rdtech_dps_get_model_version(struct sr_modbus_dev_inst *modbus,
		uint16_t *model, uint16_t *version)
{
	uint16_t registers[2];
	int ret;

	/*
	 * No mutex here, because there is no sr_dev_inst when this function
	 * is called.
	 */
	ret = rdtech_dps_read_holding_registers(modbus, REG_MODEL, 2, registers);
	if (ret == SR_OK) {
		*model = RB16(registers + 0);
		*version = RB16(registers + 1);
		sr_info("RDTech PSU model: %d version: %d", *model, *version);
	}
	return ret;
}

static void send_value(const struct sr_dev_inst *sdi, struct sr_channel *ch,
		float value, enum sr_mq mq, enum sr_mqflag mqflags,
		enum sr_unit unit, int digits)
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
	analog.meaning->mqflags = mqflags;
	analog.meaning->unit = unit;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

SR_PRIV int rdtech_dps_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	uint16_t registers[8];
	int ret;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	modbus = sdi->conn;
	devc = sdi->priv;

	g_mutex_lock(&devc->rw_mutex);
	/*
	 * Using the libsigrok function here, because it doesn't matter if the
	 * reading fails. It will be done again in the next acquision cycle anyways.
	 */
	ret = sr_modbus_read_holding_registers(modbus, REG_UOUT, 8, registers);
	g_mutex_unlock(&devc->rw_mutex);

	if (ret == SR_OK) {
		/* Send channel values */
		std_session_send_df_frame_begin(sdi);

		send_value(sdi, sdi->channels->data,
			RB16(registers + 0) / devc->voltage_multiplier,
			SR_MQ_VOLTAGE, SR_MQFLAG_DC, SR_UNIT_VOLT,
			devc->model->voltage_digits);
		send_value(sdi, sdi->channels->next->data,
			RB16(registers + 1) / devc->current_multiplier,
			SR_MQ_CURRENT, SR_MQFLAG_DC, SR_UNIT_AMPERE,
			devc->model->current_digits);
		send_value(sdi, sdi->channels->next->next->data,
			RB16(registers + 2) / 100.0f,
			SR_MQ_POWER, 0, SR_UNIT_WATT, 2);

		std_session_send_df_frame_end(sdi);

		/* Check for state changes */
		if (devc->actual_ovp_state != (RB16(registers + 5) == STATE_OVP)) {
			devc->actual_ovp_state = RB16(registers + 5) == STATE_OVP;
			sr_session_send_meta(sdi, SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE,
				g_variant_new_boolean(devc->actual_ovp_state));
		}
		if (devc->actual_ocp_state != (RB16(registers + 5) == STATE_OCP)) {
			devc->actual_ocp_state = RB16(registers + 5) == STATE_OCP;
			sr_session_send_meta(sdi, SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,
				g_variant_new_boolean(devc->actual_ocp_state));
		}
		if (devc->actual_regulation_state != RB16(registers + 6)) {
			devc->actual_regulation_state = RB16(registers + 6);
			sr_session_send_meta(sdi, SR_CONF_REGULATION,
				g_variant_new_string(
					devc->actual_regulation_state == MODE_CC ? "CC" : "CV"));
		}
		if (devc->actual_output_state != RB16(registers + 7)) {
			devc->actual_output_state = RB16(registers + 7);
			sr_session_send_meta(sdi, SR_CONF_ENABLED,
				g_variant_new_boolean(devc->actual_output_state));
		}

		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	return TRUE;
}
