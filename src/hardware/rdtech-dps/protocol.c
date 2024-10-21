/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 James Churchill <pelrun@gmail.com>
 * Copyright (C) 2019 Frank Stettner <frank-stettner@gmx.net>
 * Copyright (C) 2021 Gerhard Sittig <gerhard.sittig@gmx.net>
 * Copyright (C) 2021 Constantin Wenger <constantin.wenger@gmail.com>
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

/* These are the Modbus RTU registers for the DPS family of devices. */
enum rdtech_dps_register {
	REG_DPS_USET       = 0x00, /* Mirror of 0x50 */
	REG_DPS_ISET       = 0x01, /* Mirror of 0x51 */
	REG_DPS_UOUT       = 0x02,
	REG_DPS_IOUT       = 0x03,
	REG_DPS_POWER      = 0x04,
	REG_DPS_UIN        = 0x05,
	REG_DPS_LOCK       = 0x06,
	REG_DPS_PROTECT    = 0x07,
	REG_DPS_CV_CC      = 0x08,
	REG_DPS_ENABLE     = 0x09,
	REG_DPS_BACKLIGHT  = 0x0A, /* Mirror of 0x55 */
	REG_DPS_MODEL      = 0x0B,
	REG_DPS_VERSION    = 0x0C,

	REG_DPS_PRESET     = 0x23, /* Loads a preset into preset 0. */

	/*
	 * Add (preset * 0x10) to each of the following, for preset 1-9.
	 * Preset 0 regs below are the active output settings.
	 */
	PRE_DPS_USET       = 0x50,
	PRE_DPS_ISET       = 0x51,
	PRE_DPS_OVPSET     = 0x52,
	PRE_DPS_OCPSET     = 0x53,
	PRE_DPS_OPPSET     = 0x54,
	PRE_DPS_BACKLIGHT  = 0x55,
	PRE_DPS_DISABLE    = 0x56, /* Disable output if 0 is copied here from a preset (1 is no change). */
	PRE_DPS_BOOT       = 0x57, /* Enable output at boot if 1. */
};
#define PRE_DPS_STRIDE 0x10

enum rdtech_dps_protect_state {
	STATE_NORMAL = 0,
	STATE_OVP    = 1,
	STATE_OCP    = 2,
	STATE_OPP    = 3,
};

enum rdtech_dps_regulation_mode {
	MODE_CV      = 0,
	MODE_CC      = 1,
};

/*
 * These are the Modbus RTU registers for the RD family of devices.
 * Some registers are device specific, like REG_RD_RANGE of RD6012P
 * which could be battery related in other devices.
 */
enum rdtech_rd_register {
	REG_RD_MODEL = 0, /* u16 */
	REG_RD_SERIAL = 1, /* u32 */
	REG_RD_FIRMWARE = 3, /* u16 */
	REG_RD_TEMP_INT = 4, /* 2x u16 */
	REG_RD_TEMP_INT_F = 6, /* 2x u16 */
	REG_RD_VOLT_TGT = 8, /* u16 */
	REG_RD_CURR_LIM = 9, /* u16 */
	REG_RD_VOLTAGE = 10, /* u16 */
	REG_RD_CURRENT = 11, /* u16 */
	REG_RD_ENERGY = 12, /* u16 */
	REG_RD_POWER = 13, /* u16 */
	REG_RD_VOLT_IN = 14, /* u16 */
	REG_RD_PROTECT = 16, /* u16 */
	REG_RD_REGULATION = 17, /* u16 */
	REG_RD_ENABLE = 18, /* u16 */
	REG_RD_PRESET = 19, /* u16 */
	REG_RD_RANGE = 20, /* u16 */
	/*
	 * Battery at 32 == 0x20 pp:
	 * Mode, voltage, temperature, capacity, energy.
	 */
	/*
	 * Date/time at 48 == 0x30 pp:
	 * Year, month, day, hour, minute, second.
	 */
	/* Backlight at 72 == 0x48. */
	REG_RD_OVP_THR = 82, /* 0x52 */
	REG_RD_OCP_THR = 83, /* 0x53 */
	/* One "live" slot and 9 "memory" positions. */
	REG_RD_START_MEM = 84, /* 0x54 */
};

enum etommens_etm_xxxxp_register {
	REG_ETM_ENABLE = 0x0001,
	REG_ETM_PROTECTION = 0x0002,
	REG_ETM_MODEL = 0x0003,
	REG_ETM_CLASS = 0x0004,
	REG_ETM_DECIMALS = 0x0005,
	REG_ETM_UOUT = 0x0010,
	REG_ETM_IOUT = 0x0011,
	REG_ETM_POWER1 = 0x0012, // W power has 2x16bit
	REG_ETM_POWER2 = 0x0013,
	REG_ETM_POWERCAL = 0x0014,
	REG_ETM_OVP_VALUE = 0x0020,
	REG_ETM_OCP_VALUE = 0x0021,
	REG_ETM_OPP_VALUE1 = 0x0022, // W power has 2x16bits
	REG_ETM_OPP_VALUE2 = 0x0023,
	REG_ETM_USET = 0x0030,
	REG_ETM_ISET = 0x0031,
	REG_ETM_U_CEIL = 0xC11E,
	REG_ETM_I_CEIL = 0xC12E,
};

/* Retries failed modbus read attempts for improved reliability. */
static int rdtech_dps_read_holding_registers(struct sr_modbus_dev_inst *modbus,
	int address, int nb_registers, uint16_t *registers)
{
	size_t retries;
	int ret = SR_ERR;

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
static int rdtech_dps_set_reg(const struct sr_dev_inst *sdi,
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
SR_PRIV int rdtech_dps_get_model_version(struct sr_modbus_dev_inst *modbus,
	enum rdtech_dps_model_type model_type,
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
	case MODEL_DPS:
		/* Get the MODEL and VERSION registers. */
		ret = rdtech_dps_read_holding_registers(modbus,
			REG_DPS_MODEL, 2, registers);
		if (ret != SR_OK)
			return ret;
		rdptr = (void *)registers;
		*model = read_u16be_inc(&rdptr);
		*version = read_u16be_inc(&rdptr);
		*serno = 0;
		sr_info("RDTech DPS/DPH model: %u version: %u",
			*model, *version);
		return SR_OK;
	case MODEL_RD:
		/* Get the MODEL, SERIAL, and FIRMWARE registers. */
		ret = rdtech_dps_read_holding_registers(modbus,
			REG_RD_MODEL, 4, registers);
		if (ret != SR_OK)
			return ret;
		rdptr = (void *)registers;
		*model = read_u16be_inc(&rdptr);
		*serno = read_u32be_inc(&rdptr);
		*version = read_u16be_inc(&rdptr);
		sr_info("RDTech RD model: %u version: %u, serno %u",
			*model, *version, *serno);
		return SR_OK;
	default:
		sr_err("Unexpected RDTech PSU device type. Programming error?");
		return SR_ERR_ARG;
	}
	/* UNREACH */
}

SR_PRIV void rdtech_dps_update_multipliers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->current_multiplier = pow(10.0, devc->curr_range.current_digits);
	devc->voltage_multiplier = pow(10.0, devc->curr_range.voltage_digits);
}

/*
 * Determine range of connected device. Don't do anything once
 * acquisition has started (since the range will then be tracked).
 */
SR_PRIV int rdtech_dps_update_range(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint16_t range;
	int ret;

	devc = sdi->priv;

	/*
	 * Only update range if there are multiple ranges and data
	 * acquisition hasn't started.
	 */
	if (devc->model.rdtech_model->n_ranges <= 1 || devc->acquisition_started)
		return SR_OK;
	if (devc->model.rdtech_model->model_type != MODEL_RD)
		return SR_ERR;

	ret = rdtech_dps_read_holding_registers(sdi->conn,
		REG_RD_RANGE, 1, &range);
	if (ret != SR_OK)
		return ret;
	range = range ? 1 : 0;
	devc->curr_range_index = range;
	if (range >= devc->model.rdtech_model->n_ranges)
		return SR_ERR;

	devc->curr_range = devc->model.rdtech_model->ranges[range];
	rdtech_dps_update_multipliers(sdi);

	return SR_OK;
}

/**
 * @private
 * Read model info from the etommens device
 *
 * @param[in] modbus The modbus interface
 * @param[out] model buffer for the model to be written into
 * @param[out] dclass device class, this is used in manufacturer software to load info
 * @param[out] max_voltage upper limit for voltage using device internal format
 * @param[out] max_current upper limit for current using device internal format
 * @param[out] digits_voltage how many digits are used for voltage (use this to divide device format with 10^digits_voltage)
 * @param[out] digits_current how many digits are used for current (see digits_voltage)
 * @param[out] digits_power how many digits are used for power (see digits_voltage)
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments,
 *         SR_ERR_DATA upon invalid data, or SR_ERR on failure.
 */
SR_PRIV int etommens_etm_xxxxp_device_info_get(
		struct sr_modbus_dev_inst *modbus, uint16_t *model,
		uint16_t *dclass, uint16_t *max_voltage, uint16_t *max_current,
		uint16_t *digits_voltage, uint16_t *digits_current,
		uint16_t *digits_power)
{
	uint16_t registers[3];
	uint16_t registersumax[1];
	uint16_t registersimax[1];
	uint16_t decimals;
	int ret;

	ret = sr_modbus_read_holding_registers(modbus, REG_ETM_MODEL, 3, registers);
	if (ret == SR_OK) {
		*model = RB16(registers);
		*dclass = RB16(registers + 1);
		decimals = RB16(registers + 2);
		*digits_voltage = (decimals >> 8) & 0x000F;
		*digits_current = (decimals >> 4) & 0x000F;
		*digits_power = decimals & 0x000F;
	} else {
		return ret;
	}

	ret = sr_modbus_read_holding_registers(modbus, REG_ETM_U_CEIL, 1,
			registersumax);
	if (ret == SR_OK)
		*max_voltage = RB16(registersumax);
	else
		return ret;
	ret = sr_modbus_read_holding_registers(modbus, REG_ETM_I_CEIL, 1,
			registersimax);
	if (ret == SR_OK)
		*max_current = RB16(registersimax);
	else
		return ret;
	sr_dbg("Decimals: 0x%X", decimals);
	sr_dbg("decimals for voltage 0x%X current 0x%X power 0x%X",
			*digits_voltage, *digits_current, *digits_power);
	sr_dbg("Max voltage %d, %d Max current", *max_voltage, *max_current);
	return SR_OK;
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
SR_PRIV int rdtech_dps_get_state(const struct sr_dev_inst *sdi,
	struct rdtech_dps_state *state, enum rdtech_dps_state_context reason)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	gboolean get_config, get_init_state, get_curr_meas;
	uint16_t registers[14];
	int ret;
	const uint8_t *rdptr;
	uint16_t uset_raw, iset_raw, uout_raw, iout_raw, power_raw;
	uint16_t reg_val, reg_state, out_state, ovpset_raw, ocpset_raw;
	gboolean is_lock, is_out_enabled, is_reg_cc;
	gboolean uses_ovp, uses_ocp, uses_otp;
	gboolean have_range;
	uint16_t range;
	double curr_voltage, curr_current, curr_power;
	float volt_target, curr_limit;
	float ovp_threshold, ocp_threshold;

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

	switch (devc->model_type) {
	case MODEL_RD:
		have_range = devc->model.rdtech_model->n_ranges > 1;
		break;
	case MODEL_DPS:
	case MODEL_ETOMMENS:
	default:
		have_range = FALSE;
	}
	range = 0;

	switch (devc->model_type) {
	case MODEL_DPS:
		/*
		 * Transfer a chunk of registers in a single call. It's
		 * unfortunate that the model dependency and the sparse
		 * register map force us to open code addresses, sizes,
		 * and the sequence of the registers and how to interpret
		 * their bit fields. But then this is not too unusual for
		 * a hardware specific device driver ...
		 */
		g_mutex_lock(&devc->rw_mutex);
		ret = rdtech_dps_read_holding_registers(modbus,
			REG_DPS_USET, REG_DPS_ENABLE - REG_DPS_USET + 1,
			registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;

		/* Interpret the registers' values. */
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
		(void)read_u16be_inc(&rdptr); /* UIN */
		reg_val = read_u16be_inc(&rdptr); /* LOCK */
		is_lock = reg_val != 0;
		reg_val = read_u16be_inc(&rdptr); /* PROTECT */
		uses_ovp = reg_val == STATE_OVP;
		uses_ocp = reg_val == STATE_OCP;
		reg_state = read_u16be_inc(&rdptr); /* CV_CC */
		uses_otp = 0; /* we can't query this */
		is_reg_cc = reg_state == MODE_CC;
		out_state = read_u16be_inc(&rdptr); /* ENABLE */
		is_out_enabled = out_state != 0;

		/* Transfer another chunk of registers in a single call. */
		g_mutex_lock(&devc->rw_mutex);
		ret = rdtech_dps_read_holding_registers(modbus,
			PRE_DPS_OVPSET, 2, registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;

		/* Interpret the second registers chunk's values. */
		rdptr = (const void *)registers;
		ovpset_raw = read_u16be_inc(&rdptr); /* PRE OVPSET */
		ovp_threshold = ovpset_raw * devc->voltage_multiplier;
		ocpset_raw = read_u16be_inc(&rdptr); /* PRE OCPSET */
		ocp_threshold = ocpset_raw * devc->current_multiplier;

		break;

	case MODEL_RD:
		/* Retrieve a set of adjacent registers. */
		g_mutex_lock(&devc->rw_mutex);
		ret = rdtech_dps_read_holding_registers(modbus,
			REG_RD_VOLT_TGT,
			devc->model.rdtech_model->n_ranges > 1
				? REG_RD_RANGE - REG_RD_VOLT_TGT + 1
				: REG_RD_ENABLE - REG_RD_VOLT_TGT + 1,
			registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;

		/* Interpret the registers' raw content. */
		rdptr = (const void *)registers;
		uset_raw = read_u16be_inc(&rdptr); /* USET */
		volt_target = uset_raw / devc->voltage_multiplier;
		iset_raw = read_u16be_inc(&rdptr); /* ISET */
		curr_limit = iset_raw / devc->current_multiplier;
		uout_raw = read_u16be_inc(&rdptr); /* UOUT */
		curr_voltage = uout_raw / devc->voltage_multiplier;
		iout_raw = read_u16be_inc(&rdptr); /* IOUT */
		curr_current = iout_raw / devc->current_multiplier;
		(void)read_u16be_inc(&rdptr); /* ENERGY */
		power_raw = read_u16be_inc(&rdptr); /* POWER */
		curr_power = power_raw / 100.0f;
		(void)read_u16be_inc(&rdptr); /* VOLT_IN */
		(void)read_u16be_inc(&rdptr);
		reg_val = read_u16be_inc(&rdptr); /* PROTECT */
		uses_ovp = reg_val == STATE_OVP;
		uses_ocp = reg_val == STATE_OCP;
		uses_otp = 0; /* we can't query this */
		reg_state = read_u16be_inc(&rdptr); /* REGULATION */
		is_reg_cc = reg_state == MODE_CC;
		out_state = read_u16be_inc(&rdptr); /* ENABLE */
		is_out_enabled = out_state != 0;
		if (have_range) {
			(void)read_u16be_inc(&rdptr); /* PRESET */
			range = read_u16be_inc(&rdptr) ? 1 : 0; /* RANGE */
		}

		/* Retrieve a set of adjacent registers. */
		g_mutex_lock(&devc->rw_mutex);
		ret = rdtech_dps_read_holding_registers(modbus,
			REG_RD_OVP_THR, 2, registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;

		/* Interpret the registers' raw content. */
		rdptr = (const void *)registers;
		ovpset_raw = read_u16be_inc(&rdptr); /* OVP THR */
		ovp_threshold = ovpset_raw / devc->voltage_multiplier;
		ocpset_raw = read_u16be_inc(&rdptr); /* OCP THR */
		ocp_threshold = ocpset_raw / devc->current_multiplier;

		/* Details which we cannot query from the device. */
		is_lock = FALSE;

		break;

	case MODEL_ETOMMENS:
		g_mutex_lock(&devc->rw_mutex);
		ret = sr_modbus_read_holding_registers(modbus, REG_ETM_USET, 2, registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;
		volt_target = RB16(registers + 0) / devc->voltage_multiplier;
		curr_limit = RB16(registers + 1) / devc->current_multiplier;

		g_mutex_lock(&devc->rw_mutex);
		ret = sr_modbus_read_holding_registers(modbus, REG_ETM_UOUT, 5,
				registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;
		curr_voltage = RB16(registers + 0) / devc->voltage_multiplier;
		curr_current = RB16(registers + 1) / devc->current_multiplier;
		curr_power = RB32(registers + 2) / devc->power_multiplier;
		// cv = 0x0002, cc = 0x0003, output disabled = 0x0000
		is_reg_cc = (RB32(registers + 4) & 0x0001) == 0x0001;

		g_mutex_lock(&devc->rw_mutex);
		ret = sr_modbus_read_holding_registers(modbus, REG_ETM_ENABLE, 2,
				registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;
		is_out_enabled = RB16(registers + 0) != 0x0;
		uses_ovp = RB16(registers + 1) & 0x0001;
		uses_ocp = RB16(registers + 1) & 0x0002;
		uses_otp = RB16(registers + 1) & 0x0008;

		g_mutex_lock(&devc->rw_mutex);
		ret = sr_modbus_read_holding_registers(modbus, REG_ETM_OVP_VALUE, 4,
				registers);
		g_mutex_unlock(&devc->rw_mutex);
		if (ret != SR_OK)
			return ret;
		ovp_threshold = RB16(registers + 0) / devc->voltage_multiplier;
		ocp_threshold = RB16(registers + 1) / devc->current_multiplier;
		is_lock = FALSE; // we can't query this from device

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
	state->protect_otp = uses_otp;
	state->mask |= STATE_PROTECT_OTP;
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
	if (have_range) {
		state->range = range;
		state->mask |= STATE_RANGE;
	}

	return SR_OK;
}

/* Setup device's parameters. Selectively, from caller specs. */
SR_PRIV int rdtech_dps_set_state(const struct sr_dev_inst *sdi,
	struct rdtech_dps_state *state)
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
		switch (devc->model_type) {
		case MODEL_DPS:
			ret = rdtech_dps_set_reg(sdi, REG_DPS_ENABLE, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_RD:
			ret = rdtech_rd_set_reg(sdi, REG_RD_ENABLE, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_ETOMMENS:
			ret = rdtech_rd_set_reg(sdi, REG_ETM_ENABLE, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_VOLTAGE_TARGET) {
		reg_value = state->voltage_target * devc->voltage_multiplier;
		switch (devc->model_type) {
		case MODEL_DPS:
			ret = rdtech_dps_set_reg(sdi, REG_DPS_USET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_RD:
			ret = rdtech_rd_set_reg(sdi, REG_RD_VOLT_TGT, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_ETOMMENS:
			ret = rdtech_rd_set_reg(sdi, REG_ETM_USET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_CURRENT_LIMIT) {
		reg_value = state->current_limit * devc->current_multiplier;
		switch (devc->model_type) {
		case MODEL_DPS:
			ret = rdtech_dps_set_reg(sdi, REG_DPS_ISET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_RD:
			ret = rdtech_rd_set_reg(sdi, REG_RD_CURR_LIM, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_ETOMMENS:
			ret = rdtech_rd_set_reg(sdi, REG_ETM_ISET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_OVP_THRESHOLD) {
		reg_value = state->ovp_threshold * devc->voltage_multiplier;
		switch (devc->model_type) {
		case MODEL_DPS:
			ret = rdtech_dps_set_reg(sdi, PRE_DPS_OVPSET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_RD:
			ret = rdtech_rd_set_reg(sdi, REG_RD_OVP_THR, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_ETOMMENS:
			ret = rdtech_rd_set_reg(sdi, REG_ETM_OVP_VALUE, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_OCP_THRESHOLD) {
		reg_value = state->ocp_threshold * devc->current_multiplier;
		switch (devc->model_type) {
		case MODEL_DPS:
			ret = rdtech_dps_set_reg(sdi, PRE_DPS_OCPSET, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_RD:
			ret = rdtech_rd_set_reg(sdi, REG_RD_OCP_THR, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_ETOMMENS:
			ret = rdtech_rd_set_reg(sdi, REG_ETM_OCP_VALUE, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_LOCK) {
		switch (devc->model_type) {
		case MODEL_DPS:
			reg_value = state->lock ? 1 : 0;
			ret = rdtech_dps_set_reg(sdi, REG_DPS_LOCK, reg_value);
			if (ret != SR_OK)
				return ret;
			break;
		case MODEL_RD:
			/* Do nothing, _and_ silently succeed. */
			break;
		case MODEL_ETOMMENS:
			break; // we can't set this
		default:
			return SR_ERR_ARG;
		}
	}
	if (state->mask & STATE_RANGE) {
		reg_value = state->range;
		switch (devc->model_type) {
		case MODEL_DPS:
			/* DPS models don't support current ranges at all. */
			if (reg_value > 0)
				return SR_ERR_ARG;
			break;
		case MODEL_RD:
			/*
			 * Reject unsupported range indices.
			 * Need not set the range when the device only
			 * supports a single fixed range.
			 */
			if (reg_value >= devc->model.rdtech_model->n_ranges)
				return SR_ERR_NA;
			if (devc->model.rdtech_model->n_ranges <= 1)
				return SR_OK;
			ret = rdtech_rd_set_reg(sdi, REG_RD_RANGE, reg_value);
			if (ret != SR_OK)
				return ret;
			/*
			 * Immediately update internal state outside of
			 * an acquisition. Assume that in-acquisition
			 * activity will update internal state. This is
			 * essential for meta package emission.
			 */
			if (!devc->acquisition_started) {
				if (reg_value >= devc->model.rdtech_model->n_ranges)
					return SR_ERR;

				devc->curr_range_index = reg_value;
				devc->curr_range = devc->model.rdtech_model->ranges[reg_value];
				rdtech_dps_update_multipliers(sdi);
			}
			break;
		default:
			return SR_ERR_ARG;
		}
	}

	return SR_OK;
}

/* Get the current state when acquisition starts. */
SR_PRIV int rdtech_dps_seed_receive(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct rdtech_dps_state state;
	int ret;

	if (!sdi || !sdi->priv)
		return SR_ERR_ARG;
	devc = sdi->priv;

	ret = rdtech_dps_get_state(sdi, &state, ST_CTX_PRE_ACQ);
	if (ret != SR_OK)
		return ret;

	if (state.mask & STATE_PROTECT_OVP)
		devc->curr_ovp_state = state.protect_ovp;
	if (state.mask & STATE_PROTECT_OCP)
		devc->curr_ocp_state = state.protect_ocp;
	if (state.mask & STATE_PROTECT_OTP)
		devc->curr_otp_state = state.protect_otp;
	if (state.mask & STATE_REGULATION_CC)
		devc->curr_cc_state = state.regulation_cc;
	if (state.mask & STATE_OUTPUT_ENABLED)
		devc->curr_out_state = state.output_enabled;
	if (state.mask & STATE_RANGE) {
		if (state.range >= devc->model.rdtech_model->n_ranges)
			return SR_ERR;

		devc->curr_range_index = state.range;
		devc->curr_range = devc->model.rdtech_model->ranges[state.range];
		rdtech_dps_update_multipliers(sdi);
	}

	return SR_OK;
}

/* Get measurements, track state changes during acquisition. */
SR_PRIV int rdtech_dps_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct rdtech_dps_state state;
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
	ret = rdtech_dps_get_state(sdi, &state, ST_CTX_IN_ACQ);
	if (ret != SR_OK)
		return ret;


	/* Submit measurement data to the session feed. */
	std_session_send_df_frame_begin(sdi);
	ch = g_slist_nth_data(sdi->channels, 0);
	send_value(sdi, ch, state.voltage,
		SR_MQ_VOLTAGE, SR_MQFLAG_DC, SR_UNIT_VOLT,
		devc->curr_range.voltage_digits);
	ch = g_slist_nth_data(sdi->channels, 1);
	send_value(sdi, ch, state.current,
		 SR_MQ_CURRENT, SR_MQFLAG_DC, SR_UNIT_AMPERE,
		devc->curr_range.current_digits);
	ch = g_slist_nth_data(sdi->channels, 2);
	send_value(sdi, ch, state.power,
		SR_MQ_POWER, 0, SR_UNIT_WATT, 2);
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
	if (devc->curr_otp_state != state.protect_otp) {
		(void)sr_session_send_meta(sdi,
			SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE,
			g_variant_new_boolean(state.protect_otp));
		devc->curr_otp_state = state.protect_otp;
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
	if (devc->curr_range_index != state.range) {
		if (state.range >= devc->model.rdtech_model->n_ranges)
			return TRUE;

		range_text = devc->model.rdtech_model->ranges[state.range].range_str;
		(void)sr_session_send_meta(sdi, SR_CONF_RANGE,
			g_variant_new_string(range_text));
		devc->curr_range_index = state.range;
		devc->curr_range = devc->model.rdtech_model->ranges[state.range];
		rdtech_dps_update_multipliers(sdi);
	}

	/* Check optional acquisition limits. */
	sr_sw_limits_update_samples_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	return TRUE;
}

