/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 James Churchill <pelrun@gmail.com>
 * Copyright (C) 2019 Frank Stettner <frank-stettner@gmx.net>
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

#include "config.h"

#include <math.h>
#include <string.h>

#include "protocol.h"

/* These are the Modbus RTU registers for the XY-SK120 device. */
enum xy_sk120_register {
	REG_XY_USET       = 0x00,
	REG_XY_ISET       = 0x01,
	REG_XY_UOUT       = 0x02,
	REG_XY_IOUT       = 0x03,
	REG_XY_POWER      = 0x04,
	REG_XY_UIN        = 0x05,
	REG_XY_LOCK       = 0x0F,
	REG_XY_PROTECT    = 0x10,
	REG_XY_CV_CC      = 0x11,
	REG_XY_ENABLE     = 0x12,
	REG_XY_BACKLIGHT  = 0x14,
	REG_XY_MODEL      = 0x16,
	REG_XY_VERSION    = 0x17,

	REG_XY_PRESET     = 0x1D, /* selected preset */

	/*
	 * Add (preset * PRE_XY_STRIDE) to each of the following, for preset 1-9.
	 * Preset 0 is below
	 */
	PRE_XY_USET       = 0x50,
	PRE_XY_ISET       = 0x51,
	PRE_XY_LVPSET	   = 0x52,
	PRE_XY_OVPSET     = 0x53, 
	PRE_XY_OCPSET     = 0x54,
	PRE_XY_OPPSET     = 0x55,
	PRE_XY_OHPSET	   = 0x56, /*time limit*/
	PRE_XY_OAHSET	   = 0x58, /*charge limmit*/
	PRE_XY_OPHSET	   = 0x5A, /*energy limit*/
	PRE_XY_OTPSET	   = 0x5c, /*internal temperature limit*/
	PRE_XY_BOOT        = 0x5D, //* Enable output at boot if 1. */
};
#define PRE_XY_STRIDE 0x10 //TODO adapt to XY-SK120

enum xy_sk120_protect_state {
	STATE_NORMAL = 0,
	STATE_OVP    = 2,
	STATE_OCP    = 3,
	STATE_OPP    = 1,
};

enum xy_sk120_regulation_mode {
	MODE_CV      = 0,
	MODE_CC      = 1,
};

/* Retries failed modbus read attempts for improved reliability. */
static int xy_sk120_read_holding_registers(struct sr_modbus_dev_inst *modbus,
	int address, int nb_registers, uint16_t *registers)
{
	size_t retries;
	int ret;

	retries = 3;
	while (retries--) {
		ret = sr_modbus_read_holding_registers(modbus,
			address, nb_registers, registers);
		if (ret == SR_OK)
			return ret;
	}

	return ret;
}

/* Set one 16bit register. LE format for DPS devices. */
static int xy_sk120_set_reg(const struct sr_dev_inst *sdi,
	uint16_t address, uint16_t value)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	uint16_t registers[1];
	int ret;
	uint8_t *wrptr;

	devc = sdi->priv;
	modbus = sdi->conn;

	wrptr = (void *)registers;
	write_u16be(wrptr, value);

	g_mutex_lock(&devc->rw_mutex);
	ret = sr_modbus_write_multiple_registers(modbus, address,
		ARRAY_SIZE(registers), registers);
	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

/* Set one 16bit register. BE format for RD devices. */
static int rdtech_rd_set_reg(const struct sr_dev_inst *sdi,
	uint16_t address, uint16_t value)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	uint16_t registers[1];
	int ret;
	uint8_t *wrptr;

	devc = sdi->priv;
	modbus = sdi->conn;

	wrptr = (void *)registers;
	write_u16be(wrptr, value);

	g_mutex_lock(&devc->rw_mutex);
	ret = sr_modbus_write_multiple_registers(modbus, address,
		ARRAY_SIZE(registers), registers);
	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

/* Get DPS model number and firmware version from a connected device. */
SR_PRIV int xy_sk120_get_model_version(struct sr_modbus_dev_inst *modbus,
	enum xy_sk120_model_type model_type,
	uint16_t *model, uint16_t *version, uint32_t *serno)
{
	uint16_t registers[4];
	int ret;
	const uint8_t *rdptr;

	/*
	 * No mutex here because when the routine executes then the
	 * device instance was not created yet (probe phase).
	 */
	switch (model_type) {
	case MODEL_XY_SK:
		/* Get the MODEL and VERSION registers. */
		// Assumption: REG_DPS_VERSION == REG_DPS_MODEL +1
		ret = xy_sk120_read_holding_registers(modbus,
			REG_XY_MODEL, 2, registers);
			if (ret != SR_OK)
			return ret;
		rdptr = (void *)registers;
		*model = read_u16be_inc(&rdptr);
		*version = read_u16be_inc(&rdptr); 


		*serno = 0;
		sr_info("XY-SK model: %u version: %u",
			*model, *version);
		return SR_OK;
	default:
		sr_err("Unexpected RDTech PSU device type. Programming error?");
		return SR_ERR_ARG;
	}
	/* UNREACH */
}

SR_PRIV void xy_sk120_update_multipliers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct xy_sk120_range *range;

	devc = sdi->priv;
	range = &devc->model->ranges[devc->curr_range];
	devc->current_multiplier = pow(10.0, range->current_digits);
	devc->voltage_multiplier = pow(10.0, range->voltage_digits);
}

/*
 * Determine range of connected device. Don't do anything once
 * acquisition has started (since the range will then be tracked).
 */
SR_PRIV int xy_sk120_update_range(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint16_t range;
	int ret;

	devc = sdi->priv;

	/*
	 * Only update range if there are multiple ranges and data
	 * acquisition hasn't started.
	 */
	if (devc->model->n_ranges <= 1 || devc->acquisition_started)
		return SR_OK;
	else /* If it's a model with multiple ranges which is not implemented */
		return SR_ERR;
}

/* Send a measured value to the session feed. */
static int send_value(const struct sr_dev_inst *sdi,
	struct sr_channel *ch, float value,
	enum sr_mq mq, enum sr_mqflag mqflags,
	enum sr_unit unit, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	int ret;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = 1;
	analog.data = &value;
	analog.meaning->mq = mq;
	analog.meaning->mqflags = mqflags;
	analog.meaning->unit = unit;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	ret = sr_session_send(sdi, &packet);

	g_slist_free(analog.meaning->channels);

	return ret;
}

/*
 * Get the device's current state. Exhaustively, relentlessly.
 * Concentrate all details of communication in the physical transport,
 * register layout interpretation, and potential model dependency in
 * this central spot, to simplify maintenance.
 *
 * TODO Optionally limit the transfer volume depending on caller's spec
 * which detail level is desired? Is 10 registers each 16bits an issue
 * when the UART bitrate is only 9600bps?
 */
SR_PRIV int xy_sk120_get_state(const struct sr_dev_inst *sdi,
	struct xy_sk120_state *state, enum xy_sk120_state_context reason)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	gboolean get_config, get_init_state, get_curr_meas;
	uint16_t registers[20];
	int ret;
	const uint8_t *rdptr;
	uint16_t uset_raw, iset_raw, uout_raw, iout_raw, power_raw, charge_raw, energy_raw, temp_internal_raw, uin_raw, preset_raw;
	uint16_t reg_val, reg_state, out_state, ovpset_raw, ocpset_raw;
	gboolean is_lock, is_out_enabled, is_reg_cc;
	gboolean uses_ovp, uses_ocp;
	gboolean have_range;
	uint16_t range;
	float volt_target, curr_limit;
	float ovp_threshold, ocp_threshold;
	float curr_voltage, curr_current, curr_power;
	float charge, energy;
	float volt_source, temp_internal;

	if (!sdi || !sdi->priv || !sdi->conn)
		return SR_ERR_ARG;
	devc = sdi->priv;
	modbus = sdi->conn;
	if (!state)
		return SR_ERR_ARG;

	/* Determine the requested level of response detail. */
	get_config = FALSE;
	get_init_state = FALSE;
	get_curr_meas = FALSE;
	switch (reason) {
	case ST_CTX_CONFIG:
		get_config = TRUE;
		get_init_state = TRUE;
		get_curr_meas = TRUE;
		break;
	case ST_CTX_PRE_ACQ:
		get_init_state = TRUE;
		get_curr_meas = TRUE;
		break;
	case ST_CTX_IN_ACQ:
		get_curr_meas = TRUE;
		break;
	default:
		/* EMPTY */
		break;
	}
	/*
	 * TODO Make use of this information to reduce the transfer
	 * volume, especially on low bitrate serial connections. Though
	 * the device firmware's samplerate is probably more limiting
	 * than communication bandwidth is.
	 */
	(void)get_config;
	(void)get_init_state;
	(void)get_curr_meas;

	have_range = devc->model->n_ranges > 1;
	if (!have_range)
		range = 0;

	switch (devc->model->model_type) {
	case MODEL_XY_SK:
		/*
		 * Transfer a chunk of registers in a single call. It's
		 * unfortunate that the model dependency and the sparse
		 * register map force us to open code addresses, sizes,
		 * and the sequence of the registers and how to interpret
		 * their bit fields. But then this is not too unusual for
		 * a hardware specific device driver ...
		 */
		g_mutex_lock(&devc->rw_mutex);
		//Assumption: certain order of the register addresses
		ret = xy_sk120_read_holding_registers(modbus,
			REG_XY_USET, REG_XY_ENABLE - REG_XY_USET + 1,
			registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;

		/* Interpret the registers' values. */
		// Assumption: certain order of the register addresses
		rdptr = (const void *)registers;
		uset_raw = read_u16be_inc(&rdptr);
		volt_target = uset_raw / devc->voltage_multiplier;
		iset_raw = read_u16be_inc(&rdptr);
		curr_limit = iset_raw / devc->current_multiplier;
		uout_raw = read_u16be_inc(&rdptr);
		curr_voltage = uout_raw / devc->voltage_multiplier;
		iout_raw = read_u16be_inc(&rdptr);
		curr_current = iout_raw / devc->current_multiplier;
		power_raw = read_u16be_inc(&rdptr);
		curr_power = power_raw / 100.0f;
		uin_raw = read_u16be_inc(&rdptr);
		volt_source = uin_raw / devc->voltage_multiplier;
		charge_raw = read_u16be_inc(&rdptr); /* 2 word (low, high) */
		charge_raw += read_u16be_inc(&rdptr) * 65534; //(2^16)
		charge = charge_raw / 1000.0f;
		energy_raw = read_u16be_inc(&rdptr);
		energy_raw += read_u16be_inc(&rdptr) *65534; //(2^16)
		energy = energy_raw / 1000.0f;
		for (int i = 0; i<3; i++){
			(void)read_u16be_inc(&rdptr); //Skip 3x Time, 
		}
		temp_internal_raw = read_u16be_inc(&rdptr);
		temp_internal = temp_internal_raw/10.0f;
		(void)read_u16be_inc(&rdptr); //Skip 1x Temp_external
		reg_val = read_u16be_inc(&rdptr); /* LOCK */
		is_lock = reg_val != 0;
		reg_val = read_u16be_inc(&rdptr); /* PROTECT */
		uses_ovp = reg_val == STATE_OVP;
		uses_ocp = reg_val == STATE_OCP;
		reg_state = read_u16be_inc(&rdptr); /* CV_CC */
		is_reg_cc = reg_state == MODE_CC;
		out_state = read_u16be_inc(&rdptr); /* ENABLE */
		is_out_enabled = out_state != 0;
		
		/* Find out which preset is used */
		g_mutex_lock(&devc->rw_mutex);
		ret = xy_sk120_read_holding_registers(modbus,
			REG_XY_PRESET, 1, registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;

		preset_raw = read_u16be((const void *)registers);
		if (preset_raw >= 10){
			sr_err("Preset value is bigger than expected %u", preset_raw);
			return SR_ERR_DATA;
		}

		/* Transfer another chunk of registers in a single call. */
		g_mutex_lock(&devc->rw_mutex);
		ret = xy_sk120_read_holding_registers(modbus,
			PRE_XY_OVPSET + preset_raw*PRE_XY_STRIDE,
			3, registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;
		
		/* Interpret the second registers chunk's values. */
		rdptr = (const void *)registers;
		ovpset_raw = read_u16be_inc(&rdptr); /* PRE OVPSET */
		ovp_threshold = ovpset_raw / devc->voltage_multiplier;
		ocpset_raw = read_u16be_inc(&rdptr); /* PRE OCPSET */
		ocp_threshold = ocpset_raw / devc->current_multiplier;
		
		break;
	default:
		/* ShouldNotHappen(TM). Probe should have failed. */
		return SR_ERR_ARG;
	}

	/*
	 * Store gathered details in the high level container.
	 *
	 * TODO Make use of the caller's context. The register access
	 * code path above need not have gathered every detail in every
	 * invocation.
	 */
	memset(state, 0, sizeof(*state));
	state->lock = is_lock;
	state->mask |= STATE_LOCK;
	state->output_enabled = is_out_enabled;
	state->mask |= STATE_OUTPUT_ENABLED;
	state->regulation_cc = is_reg_cc;
	state->mask |= STATE_REGULATION_CC;
	state->protect_ovp = uses_ovp;
	state->mask |= STATE_PROTECT_OVP;
	state->protect_ocp = uses_ocp;
	state->mask |= STATE_PROTECT_OCP;
	state->protect_enabled = TRUE;
	state->mask |= STATE_PROTECT_ENABLED;
	state->voltage_target = volt_target;
	state->mask |= STATE_VOLTAGE_TARGET;
	state->current_limit = curr_limit;
	state->mask |= STATE_CURRENT_LIMIT;
	state->ovp_threshold = ovp_threshold;
	state->mask |= STATE_OVP_THRESHOLD;
	state->ocp_threshold = ocp_threshold;
	state->mask |= STATE_OCP_THRESHOLD;
	state->voltage = curr_voltage;
	state->mask |= STATE_VOLTAGE;
	state->current = curr_current;
	state->mask |= STATE_CURRENT;
	state->power = curr_power;
	state->mask |= STATE_POWER;
	
	//TODO: add state logic? (Why? Is it only needed when writing to the device?)
	state->charge = charge;
	state->energy = energy;
	state->voltage_source = volt_source;
	state->temperature_internal = temp_internal;

	if (have_range) {
		state->range = range;
		state->mask |= STATE_RANGE;
	}

	return SR_OK;
}

/* Setup device's parameters. Selectively, from caller specs. */
SR_PRIV int xy_sk120_set_state(const struct sr_dev_inst *sdi,
	struct xy_sk120_state *state)
{
	struct dev_context *devc;
	uint16_t reg_value;
	int ret;

	if (!sdi || !sdi->priv || !sdi->conn)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!state)
		return SR_ERR_ARG;

	/* Only a subset of known values is settable. */
	if (state->mask & STATE_OUTPUT_ENABLED) {
		reg_value = state->output_enabled ? 1 : 0;
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			ret = xy_sk120_set_reg(sdi, REG_XY_ENABLE, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_VOLTAGE_TARGET) {
		reg_value = state->voltage_target * devc->voltage_multiplier;
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			ret = xy_sk120_set_reg(sdi, REG_XY_USET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_CURRENT_LIMIT) {
		reg_value = state->current_limit * devc->current_multiplier;
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			ret = xy_sk120_set_reg(sdi, REG_XY_ISET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_OVP_THRESHOLD) {
		reg_value = state->ovp_threshold * devc->voltage_multiplier;
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			ret = xy_sk120_set_reg(sdi, PRE_XY_OVPSET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_OCP_THRESHOLD) {
		reg_value = state->ocp_threshold * devc->current_multiplier;
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			ret = xy_sk120_set_reg(sdi, PRE_XY_OCPSET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_LOCK) {
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			reg_value = state->lock ? 1 : 0;
			ret = xy_sk120_set_reg(sdi, REG_XY_LOCK, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_RANGE) {
		reg_value = state->range;
		switch (devc->model->model_type) {
		case MODEL_XY_SK:
			/* DPS models don't support current ranges at all. */
			if (reg_value > 0)
				return SR_ERR_ARG;
			break;
		default:
			return SR_ERR_ARG;
		}
	}

	return SR_OK;
}

/* Get the current state when acquisition starts. */
SR_PRIV int xy_sk120_seed_receive(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct xy_sk120_state state;
	int ret;

	if (!sdi || !sdi->priv)
		return SR_ERR_ARG;
	devc = sdi->priv;

	ret = xy_sk120_get_state(sdi, &state, ST_CTX_PRE_ACQ);
	if (ret != SR_OK)
		return ret;

	if (state.mask & STATE_PROTECT_OVP)
		devc->curr_ovp_state = state.protect_ovp;
	if (state.mask & STATE_PROTECT_OCP)
		devc->curr_ocp_state = state.protect_ocp;
	if (state.mask & STATE_REGULATION_CC)
		devc->curr_cc_state = state.regulation_cc;
	if (state.mask & STATE_OUTPUT_ENABLED)
		devc->curr_out_state = state.output_enabled;
	if (state.mask & STATE_RANGE) {
		devc->curr_range = state.range;
		xy_sk120_update_multipliers(sdi);
	}

	return SR_OK;
}

/* Get measurements, track state changes during acquisition. */
SR_PRIV int xy_sk120_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct xy_sk120_state state;
	int ret;
	struct sr_channel *ch;
	const char *regulation_text, *range_text;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	devc = sdi->priv;

	/* Get the device's current state. */
	ret = xy_sk120_get_state(sdi, &state, ST_CTX_IN_ACQ);
	if (ret != SR_OK)
		return ret;


	/* Submit measurement data to the session feed. */
	std_session_send_df_frame_begin(sdi);
	ch = g_slist_nth_data(sdi->channels, 0);
	send_value(sdi, ch, state.voltage,
		SR_MQ_VOLTAGE, SR_MQFLAG_DC, SR_UNIT_VOLT,
		devc->model->ranges[devc->curr_range].voltage_digits);
	ch = g_slist_nth_data(sdi->channels, 1);
	send_value(sdi, ch, state.current,
		SR_MQ_CURRENT, SR_MQFLAG_DC, SR_UNIT_AMPERE,
		devc->model->ranges[devc->curr_range].current_digits);
	ch = g_slist_nth_data(sdi->channels, 2);
	send_value(sdi, ch, state.power,
		SR_MQ_POWER, 0, SR_UNIT_WATT, 2);

	ch = g_slist_nth_data(sdi->channels, 3);
		send_value(sdi, ch, state.charge,
			SR_MQ_ELECTRIC_CHARGE, 0, SR_UNIT_AMPERE_HOUR, 3);
	ch = g_slist_nth_data(sdi->channels, 4);
	send_value(sdi, ch, state.energy,
		SR_MQ_ENERGY, 0, SR_UNIT_WATT_HOUR, 3);
	
	ch = g_slist_nth_data(sdi->channels, 5);
	send_value(sdi, ch, state.voltage_source,
		SR_MQ_VOLTAGE, SR_MQFLAG_DC, SR_UNIT_VOLT,
		devc->model->ranges[devc->curr_range].voltage_digits);

	ch = g_slist_nth_data(sdi->channels, 6);
	send_value(sdi, ch, state.temperature_internal,
		SR_MQ_TEMPERATURE, 0, SR_UNIT_CELSIUS, //TODO: adjust unit based on the device settings
		1);


	std_session_send_df_frame_end(sdi);

	/* Check for state changes. */
	if (devc->curr_ovp_state != state.protect_ovp) {
		(void)sr_session_send_meta(sdi,
			SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE,
			g_variant_new_boolean(state.protect_ovp));
		devc->curr_ovp_state = state.protect_ovp;
	}
	if (devc->curr_ocp_state != state.protect_ocp) {
		(void)sr_session_send_meta(sdi,
			SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,
			g_variant_new_boolean(state.protect_ocp));
		devc->curr_ocp_state = state.protect_ocp;
	}
	if (devc->curr_cc_state != state.regulation_cc) {
		regulation_text = state.regulation_cc ? "CC" : "CV";
		(void)sr_session_send_meta(sdi, SR_CONF_REGULATION,
			g_variant_new_string(regulation_text));
		devc->curr_cc_state = state.regulation_cc;
	}
	if (devc->curr_out_state != state.output_enabled) {
		(void)sr_session_send_meta(sdi, SR_CONF_ENABLED,
			g_variant_new_boolean(state.output_enabled));
		devc->curr_out_state = state.output_enabled;
	}
	if (devc->curr_range != state.range) {
		range_text = devc->model->ranges[state.range].range_str;
		(void)sr_session_send_meta(sdi, SR_CONF_RANGE,
			g_variant_new_string(range_text));
		devc->curr_range = state.range;
		xy_sk120_update_multipliers(sdi);
	}

	/* Check optional acquisition limits. */
	sr_sw_limits_update_samples_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	return TRUE;
}
