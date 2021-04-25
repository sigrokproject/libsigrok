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

#ifndef LIBSIGROK_HARDWARE_RDTECH_DPS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RDTECH_DPS_PROTOCOL_H

#include <config.h>

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "rdtech-dps"

enum rdtech_dps_model_type {
	MODEL_NONE,
	MODEL_DPS,
	MODEL_RD,
};

struct rdtech_dps_model {
	enum rdtech_dps_model_type model_type;
	unsigned int id;
	const char *name;
	unsigned int max_current;
	unsigned int max_voltage;
	unsigned int max_power;
	unsigned int current_digits;
	unsigned int voltage_digits;
};

struct dev_context {
	const struct rdtech_dps_model *model;
	double current_multiplier;
	double voltage_multiplier;
	struct sr_sw_limits limits;
	GMutex rw_mutex;
	gboolean curr_ovp_state;
	gboolean curr_ocp_state;
	gboolean curr_cc_state;
	gboolean curr_out_state;
};

/* Container to get and set parameter values. */
struct rdtech_dps_state {
	enum rdtech_dps_state_mask {
		STATE_LOCK = 1 << 0,
		STATE_OUTPUT_ENABLED = 1 << 1,
		STATE_REGULATION_CC = 1 << 2,
		STATE_PROTECT_OVP = 1 << 3,
		STATE_PROTECT_OCP = 1 << 4,
		STATE_PROTECT_ENABLED = 1 << 5,
		STATE_VOLTAGE_TARGET = 1 << 6,
		STATE_CURRENT_LIMIT = 1 << 7,
		STATE_OVP_THRESHOLD = 1 << 8,
		STATE_OCP_THRESHOLD = 1 << 9,
		STATE_VOLTAGE = 1 << 10,
		STATE_CURRENT = 1 << 11,
		STATE_POWER = 1 << 12,
	} mask;
	gboolean lock;
	gboolean output_enabled, regulation_cc;
	gboolean protect_ovp, protect_ocp, protect_enabled;
	float voltage_target, current_limit;
	float ovp_threshold, ocp_threshold;
	float voltage, current, power;
};

enum rdtech_dps_state_context {
	ST_CTX_NONE,
	ST_CTX_CONFIG,
	ST_CTX_PRE_ACQ,
	ST_CTX_IN_ACQ,
};
SR_PRIV int rdtech_dps_get_state(const struct sr_dev_inst *sdi,
	struct rdtech_dps_state *state, enum rdtech_dps_state_context reason);
SR_PRIV int rdtech_dps_set_state(const struct sr_dev_inst *sdi,
	struct rdtech_dps_state *state);

SR_PRIV int rdtech_dps_get_model_version(struct sr_modbus_dev_inst *modbus,
	enum rdtech_dps_model_type model_type,
	uint16_t *model, uint16_t *version, uint32_t *serno);
SR_PRIV int rdtech_dps_seed_receive(const struct sr_dev_inst *sdi);
SR_PRIV int rdtech_dps_receive_data(int fd, int revents, void *cb_data);

#endif
