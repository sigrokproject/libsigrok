/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2015 Google, Inc.
 * (Written by Alexandru Gagniuc <mrnuke@google.com> for Google, Inc.)
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
#include <strings.h>
#include "protocol.h"

#define CH_IDX(x) (1 << x)
#define FREQ_DC_ONLY {0, 0, 0}

const char *pps_vendors[][2] = {
	{ "RIGOL TECHNOLOGIES", "Rigol" },
	{ "HEWLETT-PACKARD", "HP" },
	{ "PHILIPS", "Philips" },
	{ "Chroma ATE", "Chroma" },
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

/* Chroma 61600 series AC source */
static const uint32_t chroma_61604_devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
};

static const uint32_t chroma_61604_devopts_cg[] = {
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_VOLTAGE | SR_CONF_GET,
	SR_CONF_OUTPUT_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET,
	SR_CONF_OUTPUT_FREQUENCY_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_CURRENT | SR_CONF_GET,
	SR_CONF_OUTPUT_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

const struct channel_spec chroma_61604_ch[] = {
	{ "1", { 0, 300, 0.1 }, { 0, 16, 0.1 }, { 1.0, 1000.0, 0.01 } },
};

const struct channel_group_spec chroma_61604_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
};

const struct scpi_command chroma_61604_cmd[] = {
	{ SCPI_CMD_REMOTE, "SYST:REM" },
	{ SCPI_CMD_LOCAL, "SYST:LOC" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":FETC:VOLT:ACDC?" },
	{ SCPI_CMD_GET_MEAS_FREQUENCY, ":FETC:FREQ?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, ":FETC:CURR:AC?" },
	{ SCPI_CMD_GET_MEAS_POWER, ":FETC:POW:AC?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT:AC?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT:AC %.1f" },
	{ SCPI_CMD_GET_FREQUENCY_TARGET, ":SOUR:FREQ?" },
	{ SCPI_CMD_SET_FREQUENCY_TARGET, ":SOUR:FREQ %.2f" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, ":OUTP?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, ":OUTP ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, ":OUTP OFF" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":SOUR:VOLT:LIM:AC?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":SOUR:VOLT:LIM:AC %.1f" },
	/* This is not a current limit mode. It is overcurrent protection */
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR:LIM?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR:LIM %.2f" },
};

/* Rigol DP800 series */
static const uint32_t rigol_dp800_devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
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

const struct channel_spec rigol_dp821a_ch[] = {
	{ "1", { 0, 60, 0.001 }, { 0, 1, 0.0001 }, FREQ_DC_ONLY },
	{ "2", { 0, 8, 0.001 }, { 0, 10, 0.001 }, FREQ_DC_ONLY },
};

const struct channel_spec rigol_dp831_ch[] = {
	{ "1", { 0, 8, 0.001 }, { 0, 5, 0.0003 }, FREQ_DC_ONLY },
	{ "2", { 0, 30, 0.001 }, { 0, 2, 0.0001 }, FREQ_DC_ONLY },
	{ "3", { 0, -30, 0.001 }, { 0, 2, 0.0001 }, FREQ_DC_ONLY },
};

const struct channel_spec rigol_dp832_ch[] = {
	{ "1", { 0, 30, 0.001 }, { 0, 3, 0.001 }, FREQ_DC_ONLY },
	{ "2", { 0, 30, 0.001 }, { 0, 3, 0.001 }, FREQ_DC_ONLY },
	{ "3", { 0, 5, 0.001 }, { 0, 3, 0.001 }, FREQ_DC_ONLY },
};

const struct channel_group_spec rigol_dp820_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
	{ "2", CH_IDX(1), PPS_OVP | PPS_OCP },
};

const struct channel_group_spec rigol_dp830_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
	{ "2", CH_IDX(1), PPS_OVP | PPS_OCP },
	{ "3", CH_IDX(2), PPS_OVP | PPS_OCP },
};

const struct scpi_command rigol_dp800_cmd[] = {
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
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_OUTPUT_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_VOLTAGE | SR_CONF_GET,
	SR_CONF_OUTPUT_CURRENT | SR_CONF_GET,
	SR_CONF_OUTPUT_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

const struct channel_spec hp_6632b_ch[] = {
	{ "1", { 0, 20.475, 0.005 }, { 0, 5.1188, 0.00132 }, FREQ_DC_ONLY },
};

const struct channel_group_spec hp_6632b_cg[] = {
	{ "1", CH_IDX(0), 0 },
};

const struct scpi_command hp_6632b_cmd[] = {
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

/* Philips/Fluke PM2800 series */
static const uint32_t philips_pm2800_devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
};

static const uint32_t philips_pm2800_devopts_cg[] = {
	SR_CONF_OUTPUT_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_VOLTAGE | SR_CONF_GET,
	SR_CONF_OUTPUT_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_CURRENT | SR_CONF_GET,
	SR_CONF_OUTPUT_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OUTPUT_REGULATION | SR_CONF_GET,
};

enum philips_pm2800_modules {
	PM2800_MOD_30V_10A = 1,
	PM2800_MOD_60V_5A,
	PM2800_MOD_60V_10A,
	PM2800_MOD_8V_15A,
	PM2800_MOD_60V_2A,
	PM2800_MOD_120V_1A,
};

static const struct philips_pm2800_module_spec {
	/* Min, max, programming resolution. */
	float voltage[3];
	float current[3];
} philips_pm2800_module_specs[] = {
	/* Autoranging modules. */
	[PM2800_MOD_30V_10A] = { { 0, 30, 0.0075 }, { 0, 10, 0.0025 } },
	[PM2800_MOD_60V_5A] = { { 0, 60, 0.015 }, { 0, 5, 0.00125 } },
	[PM2800_MOD_60V_10A] = { { 0, 60, 0.015 }, { 0, 10, 0.0025 } },
	/* Linear modules. */
	[PM2800_MOD_8V_15A] = { { 0, 8, 0.002 }, { -15, 15, 0.00375 } },
	[PM2800_MOD_60V_2A] = { { 0, 60, 0.015 }, { -2, 2, 0.0005 } },
	[PM2800_MOD_120V_1A] = { { 0, 120, 0.030 }, { -1, 1, 0.00025 } },
};

static const struct philips_pm2800_model {
	unsigned int chassis;
	unsigned int num_modules;
	unsigned int set;
	unsigned int modules[3];
} philips_pm2800_matrix[] = {
	/* Autoranging chassis. */
	{ 1, 1, 0, { PM2800_MOD_30V_10A, 0, 0 } },
	{ 1, 1, 1, { PM2800_MOD_60V_5A, 0, 0 } },
	{ 1, 2, 0, { PM2800_MOD_30V_10A, PM2800_MOD_30V_10A, 0 } },
	{ 1, 2, 1, { PM2800_MOD_60V_5A, PM2800_MOD_60V_5A, 0 } },
	{ 1, 2, 2, { PM2800_MOD_30V_10A, PM2800_MOD_60V_5A, 0 } },
	{ 1, 2, 3, { PM2800_MOD_30V_10A, PM2800_MOD_60V_10A, 0 } },
	{ 1, 2, 4, { PM2800_MOD_60V_5A, PM2800_MOD_60V_10A, 0 } },
	{ 1, 3, 0, { PM2800_MOD_30V_10A, PM2800_MOD_30V_10A, PM2800_MOD_30V_10A } },
	{ 1, 3, 1, { PM2800_MOD_60V_5A, PM2800_MOD_60V_5A, PM2800_MOD_60V_5A } },
	{ 1, 3, 2, { PM2800_MOD_30V_10A, PM2800_MOD_30V_10A, PM2800_MOD_60V_5A } },
	{ 1, 3, 3, { PM2800_MOD_30V_10A, PM2800_MOD_60V_5A, PM2800_MOD_60V_5A } },
	/* Linear chassis. */
	{ 3, 1, 0, { PM2800_MOD_60V_2A, 0, 0 } },
	{ 3, 1, 1, { PM2800_MOD_120V_1A, 0, 0 } },
	{ 3, 1, 2, { PM2800_MOD_8V_15A, 0, 0 } },
	{ 3, 2, 0, { PM2800_MOD_60V_2A, 0, 0 } },
	{ 3, 2, 1, { PM2800_MOD_120V_1A, 0, 0 } },
	{ 3, 2, 2, { PM2800_MOD_60V_2A, PM2800_MOD_120V_1A, 0 } },
	{ 3, 2, 3, { PM2800_MOD_8V_15A, PM2800_MOD_8V_15A, 0 } },
};

static const char *philips_pm2800_names[] = { "1", "2", "3" };

static int philips_pm2800_probe_channels(struct sr_dev_inst *sdi,
		struct sr_scpi_hw_info *hw_info,
		struct channel_spec **channels, unsigned int *num_channels,
		struct channel_group_spec **channel_groups, unsigned int *num_channel_groups)
{
	const struct philips_pm2800_model *model;
	const struct philips_pm2800_module_spec *spec;
	unsigned int chassis, num_modules, set, module, m, i;

	(void)sdi;

	/*
	 * The model number as reported by *IDN? looks like e.g. PM2813/11,
	 * Where "PM28" is fixed, followed by the chassis code (1 = autoranging,
	 * 3 = linear series) and the number of modules: 1-3 for autoranging,
	 * 1-2 for linear.
	 * After the slash, the first digit denotes the module set. The
	 * digit after that denotes front (5) or rear (1) binding posts.
	 */
	chassis = hw_info->model[4] - 0x30;
	num_modules = hw_info->model[5] - 0x30;
	set = hw_info->model[7] - 0x30;
	for (m = 0; m < ARRAY_SIZE(philips_pm2800_matrix); m++) {
		model = &philips_pm2800_matrix[m];
		if (model->chassis == chassis && model->num_modules == num_modules
				&& model->set == set)
			break;
	}
	if (m == ARRAY_SIZE(philips_pm2800_matrix)) {
		sr_dbg("Model %s not found in matrix.", hw_info->model);
		return SR_ERR;
	}

	sr_dbg("Found %d output channel%s:", num_modules, num_modules > 1 ? "s" : "");
	*channels = g_malloc0(sizeof(struct channel_spec) * num_modules);
	*channel_groups = g_malloc0(sizeof(struct channel_group_spec) * num_modules);
	for (i = 0; i < num_modules; i++) {
		module = model->modules[i];
		spec = &philips_pm2800_module_specs[module];
		sr_dbg("output %d: %.0f - %.0fV, %.0f - %.0fA", i + 1,
				spec->voltage[0], spec->voltage[1],
				spec->current[0], spec->current[1]);
		(*channels)[i].name = (char *)philips_pm2800_names[i];
		memcpy(&((*channels)[i].voltage), spec, sizeof(float) * 6);
		(*channel_groups)[i].name = (char *)philips_pm2800_names[i];
		(*channel_groups)[i].channel_index_mask = 1 << i;
		(*channel_groups)[i].features = PPS_OTP | PPS_OVP | PPS_OCP;
	}
	*num_channels = *num_channel_groups = num_modules;

	return SR_OK;
}

const struct scpi_command philips_pm2800_cmd[] = {
	{ SCPI_CMD_SELECT_CHANNEL, ":INST:NSEL %s" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, ":MEAS:CURR?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT %.6f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, ":SOUR:CURR %.6f" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, ":OUTP?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, ":OUTP ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, ":OUTP OFF" },
	{ SCPI_CMD_GET_OUTPUT_REGULATION, ":SOUR:FUNC:MODE?" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE, ":SOUR:VOLT:PROT:TRIP?" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":SOUR:VOLT:PROT:LEV?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":SOUR:VOLT:PROT:LEV %.6f" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED, ":SOUR:CURR:PROT:STAT?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE, ":SOUR:CURR:PROT:STAT ON" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE, ":SOUR:CURR:PROT:STAT OFF" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE, ":SOUR:CURR:PROT:TRIP?" },
};

SR_PRIV const struct scpi_pps pps_profiles[] = {
	/* Chroma 61604 */
	{ "Chroma", "61604", 0,
		ARRAY_AND_SIZE(chroma_61604_devopts),
		ARRAY_AND_SIZE(chroma_61604_devopts_cg),
		ARRAY_AND_SIZE(chroma_61604_ch),
		ARRAY_AND_SIZE(chroma_61604_cg),
		ARRAY_AND_SIZE(chroma_61604_cmd),
		.probe_channels = NULL,
	},
	/* HP 6632B */
	{ "HP", "6632B", 0,
		ARRAY_AND_SIZE(hp_6632b_devopts),
		ARRAY_AND_SIZE(devopts_none),
		ARRAY_AND_SIZE(hp_6632b_ch),
		ARRAY_AND_SIZE(hp_6632b_cg),
		ARRAY_AND_SIZE(hp_6632b_cmd),
		.probe_channels = NULL,
	},

	/* Rigol DP800 series */
	{ "Rigol", "^DP821A$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp821a_ch),
		ARRAY_AND_SIZE(rigol_dp820_cg),
		ARRAY_AND_SIZE(rigol_dp800_cmd),
		.probe_channels = NULL,
	},
	{ "Rigol", "^DP831A$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp831_ch),
		ARRAY_AND_SIZE(rigol_dp830_cg),
		ARRAY_AND_SIZE(rigol_dp800_cmd),
		.probe_channels = NULL,
	},
	{ "Rigol", "^(DP832|DP832A)$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp832_ch),
		ARRAY_AND_SIZE(rigol_dp830_cg),
		ARRAY_AND_SIZE(rigol_dp800_cmd),
		.probe_channels = NULL,
	},

	/* Philips/Fluke PM2800 series */
	{ "Philips", "^PM28[13][123]/[01234]{1,2}$", 0,
		ARRAY_AND_SIZE(philips_pm2800_devopts),
		ARRAY_AND_SIZE(philips_pm2800_devopts_cg),
		NULL, 0,
		NULL, 0,
		ARRAY_AND_SIZE(philips_pm2800_cmd),
		philips_pm2800_probe_channels,
	},
};

SR_PRIV unsigned int num_pps_profiles = ARRAY_SIZE(pps_profiles);
