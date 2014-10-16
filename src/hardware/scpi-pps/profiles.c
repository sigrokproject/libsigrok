/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

#include <string.h>
#include "protocol.h"

#define CH_IDX(x) (1 << x)

const char *pps_vendors[][2] = {
	{ "RIGOL TECHNOLOGIES", "Rigol" },
	{ "HEWLETT-PACKARD", "HP" },
};

const char *get_vendor(const char *raw_vendor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pps_vendors); i++) {
		if (!strcasecmp(raw_vendor, pps_vendors[i][0]))
			return pps_vendors[i][1];
	}

	return raw_vendor;
}

static const uint32_t devopts_none[] = { };

/* Rigol DP800 series */
static const uint32_t rigol_dp800_devopts[] = {
	SR_CONF_POWER_SUPPLY,
	SR_CONF_CONTINUOUS,
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t rigol_dp800_devopts_cg[] = {
	SR_CONF_OUTPUT_REGULATION | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_VOLTAGE | SR_CONF_GET,
	SR_CONF_OUTPUT_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_CURRENT | SR_CONF_GET,
	SR_CONF_OUTPUT_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

struct channel_spec rigol_dp831_ch[] = {
	{ "1", { 0, 8, 0.001 }, { 0, 5, 0.0003 } },
	{ "2", { 0, 30, 0.001 }, { 0, 2, 0.0001 } },
	{ "3", { 0, -30, 0.001 }, { 0, 2, 0.0001 } },
};

struct channel_spec rigol_dp832_ch[] = {
	{ "1", { 0, 30, 0.001 }, { 0, 3, 0.001 } },
	{ "2", { 0, 30, 0.001 }, { 0, 3, 0.001 } },
	{ "3", { 0, 5, 0.001 }, { 0, 3, 0.001 } },
};

struct channel_group_spec rigol_dp800_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
	{ "2", CH_IDX(1), PPS_OVP | PPS_OCP },
	{ "3", CH_IDX(2), PPS_OVP | PPS_OCP },
};

struct scpi_command rigol_dp800_cmd[] = {
	{ SCPI_CMD_REMOTE, "SYST:REMOTE" },
	{ SCPI_CMD_LOCAL, "SYST:LOCAL" },
	{ SCPI_CMD_BEEPER, "SYST:BEEP:STAT?" },
	{ SCPI_CMD_BEEPER_ENABLE, "SYST:BEEP:STAT ON" },
	{ SCPI_CMD_BEEPER_DISABLE, "SYST:BEEP:STAT OFF" },
	{ SCPI_CMD_SELECT_CHANNEL, ":INST:NSEL %s" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, ":MEAS:CURR?" },
	{ SCPI_CMD_GET_MEAS_POWER, ":MEAS:POWE?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT %.6f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, ":SOUR:CURR %.6f" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, ":OUTP?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, ":OUTP ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, ":OUTP OFF" },
	{ SCPI_CMD_GET_OUTPUT_REGULATION, ":OUTP:MODE?" },
	{ SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION, ":SYST:OTP?" },
	{ SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION_ENABLE, ":SYST:OTP ON" },
	{ SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION_DISABLE, ":SYST:OTP OFF" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ENABLED, ":OUTP:OVP?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_ENABLE, ":OUTP:OVP ON" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_DISABLE, ":OUTP:OVP OFF" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE, ":OUTP:OVP:QUES?" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":OUTP:OVP:VAL?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":OUTP:OVP:VAL %.6f" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED, ":OUTP:OCP?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE, ":OUTP:OCP:STAT ON" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE, ":OUTP:OCP:STAT OFF" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE, ":OUTP:OCP:QUES?" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD, ":OUTP:OCP:VAL?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD, ":OUTP:OCP:VAL %.6f" },
};

/* HP 663xx series */
static const uint32_t hp_6632b_devopts[] = {
	SR_CONF_POWER_SUPPLY,
	SR_CONF_CONTINUOUS,
	SR_CONF_OUTPUT_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_VOLTAGE | SR_CONF_GET,
	SR_CONF_OUTPUT_CURRENT | SR_CONF_GET,
	SR_CONF_OUTPUT_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

struct channel_spec hp_6632b_ch[] = {
	{ "1", { 0, 20.475, 0.005 }, { 0, 5.1188, 0.00132 } },
};

struct channel_group_spec hp_6632b_cg[] = {
	{ "1", CH_IDX(0), 0 },
};

struct scpi_command hp_6632b_cmd[] = {
	{ SCPI_CMD_GET_OUTPUT_ENABLED, "OUTP:STAT?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, "OUTP:STAT ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, "OUTP:STAT OFF" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, ":MEAS:CURR?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT %.6f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, ":SOUR:CURR %.6f" },
};


SR_PRIV const struct scpi_pps pps_profiles[] = {
	/* HP 6632B */
	{ "HP", "6632B", 0,
		ARRAY_AND_SIZE(hp_6632b_devopts),
		ARRAY_AND_SIZE(devopts_none),
		ARRAY_AND_SIZE(hp_6632b_ch),
		ARRAY_AND_SIZE(hp_6632b_cg),
		ARRAY_AND_SIZE(hp_6632b_cmd),
	},

	/* Rigol DP800 series */
	{ "Rigol", "^DP831A$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp831_ch),
		ARRAY_AND_SIZE(rigol_dp800_cg),
		ARRAY_AND_SIZE(rigol_dp800_cmd),
	},
	{ "Rigol", "^(DP832|DP832A)$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp832_ch),
		ARRAY_AND_SIZE(rigol_dp800_cg),
		ARRAY_AND_SIZE(rigol_dp800_cmd),
	},
};
SR_PRIV unsigned int num_pps_profiles = ARRAY_SIZE(pps_profiles);

