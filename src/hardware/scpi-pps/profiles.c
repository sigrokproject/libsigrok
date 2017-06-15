/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2015 Google, Inc.
 * (Written by Alexandru Gagniuc <mrnuke@google.com> for Google, Inc.)
 * Copyright (C) 2017 Frank Stettner <frank-stettner@gmx.net>
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
#include <string.h>
#include <strings.h>
#include "protocol.h"

#define CH_IDX(x) (1 << x)
#define FREQ_DC_ONLY {0, 0, 0, 0, 0}
#define NO_OVP_LIMITS {0, 0, 0, 0, 0}
#define NO_OCP_LIMITS {0, 0, 0, 0, 0}

/* Agilent/Keysight N5700A series */
static const uint32_t agilent_n5700a_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t agilent_n5700a_devopts_cg[] = {
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_group_spec agilent_n5700a_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
};

static const struct channel_spec agilent_n5767a_ch[] = {
	{ "1", { 0, 60, 0.0072, 3, 4 }, { 0, 25, 0.003, 3, 4 }, { 0, 1500 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

static const struct channel_spec agilent_n5763a_ch[] = {
	{ "1", { 0, 12.5, 0.0015, 3, 4 }, { 0, 120, 0.0144, 3, 4 }, { 0, 1500 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

/*
 * TODO: OVER_CURRENT_PROTECTION_ACTIVE status can be determined by the OC bit
 * in STAT:QUES:EVEN?, but this is not implemented.
 */
static const struct scpi_command agilent_n5700a_cmd[] = {
	{ SCPI_CMD_REMOTE, "SYST:COMM:RLST REM" },
	{ SCPI_CMD_LOCAL, "SYST:COMM:RLST LOC" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, "MEAS:CURR?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT %.6f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, ":SOUR:CURR %.6f" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, ":OUTP:STAT?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, ":OUTP ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, ":OUTP OFF" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":VOLT:PROT?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":VOLT:PROT %.6f" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED, ":CURR:PROT:STAT?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE, ":CURR:PROT:STAT ON?"},
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE, ":CURR:PROT:STAT OFF?"},
	/* Current limit (CC mode) and OCP are set using the same command. */
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR %.6f" },
	ALL_ZERO
};

/* Chroma 61600 series AC source */
static const uint32_t chroma_61604_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t chroma_61604_devopts_cg[] = {
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET,
	SR_CONF_OUTPUT_FREQUENCY_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_spec chroma_61604_ch[] = {
	{ "1", { 0, 300, 0.1, 1, 1 }, { 0, 16, 0.1, 2, 2 }, { 0, 2000, 0, 1, 1 }, { 1.0, 1000.0, 0.01 }, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

static const struct channel_group_spec chroma_61604_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
};

static const struct scpi_command chroma_61604_cmd[] = {
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
	/* This is not a current limit mode. It is overcurrent protection. */
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR:LIM?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR:LIM %.2f" },
	ALL_ZERO
};

/* Chroma 62000 series DC source */
static const uint32_t chroma_62000_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t chroma_62000_devopts_cg[] = {
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_group_spec chroma_62000_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
};

static const struct scpi_command chroma_62000_cmd[] = {
	{ SCPI_CMD_REMOTE, ":CONF:REM ON" },
	{ SCPI_CMD_LOCAL, ":CONF:REM OFF" },
	{ SCPI_CMD_BEEPER, ":CONF:BEEP?" },
	{ SCPI_CMD_BEEPER_ENABLE, ":CONF:BEEP ON" },
	{ SCPI_CMD_BEEPER_DISABLE, ":CONF:BEEP OFF" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, ":MEAS:CURR?" },
	{ SCPI_CMD_GET_MEAS_POWER, ":MEAS:POW?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT %.2f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, ":SOUR:CURR %.6f" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, ":CONF:OUTP?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, ":CONF:OUTP ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, ":CONF:OUTP OFF" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":SOUR:VOLT:PROT:HIGH?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":SOUR:VOLT:PROT:HIGH %.6f" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR:PROT:HIGH?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD, ":SOUR:CURR:PROT:HIGH %.6f" },
	ALL_ZERO
};

static int chroma_62000p_probe_channels(struct sr_dev_inst *sdi,
		struct sr_scpi_hw_info *hw_info,
		struct channel_spec **channels, unsigned int *num_channels,
		struct channel_group_spec **channel_groups,
		unsigned int *num_channel_groups)
{
	unsigned int volts, amps, watts;
	struct channel_spec *channel;

	(void)sdi;

	sscanf(hw_info->model, "620%uP-%u-%u", &watts, &volts, &amps);
	watts *= 100;
	sr_dbg("Found device rated for %d V, %d A and %d W", volts, amps, watts);

	if (volts > 600) {
		sr_err("Probed max voltage of %u V is out of spec.", volts);
		return SR_ERR_BUG;
	}

	if (amps > 120) {
		sr_err("Probed max current of %u A is out of spec.", amps);
		return SR_ERR_BUG;
	}

	if (watts > 5000) {
		sr_err("Probed max power of %u W is out of spec.", watts);
		return SR_ERR_BUG;
	}

	channel = g_malloc0(sizeof(struct channel_spec));
	channel->name = "1";
	channel->voltage[0] = channel->current[0] = channel->power[0] = 0.0;
	channel->voltage[1] = volts;
	channel->current[1] = amps;
	channel->power[1]   = watts;
	channel->voltage[2] = channel->current[2] = 0.01;
	channel->voltage[3] = channel->voltage[4] = 3;
	channel->current[3] = channel->current[4] = 4;
	*channels = channel;
	*num_channels = 1;

	*channel_groups = g_malloc(sizeof(struct channel_group_spec));
	**channel_groups = chroma_62000_cg[0];
	*num_channel_groups = 1;

	return SR_OK;
}

/* Rigol DP700 series */
static const uint32_t rigol_dp700_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t rigol_dp700_devopts_cg[] = {
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_spec rigol_dp711_ch[] = {
	{ "1", { 0, 30, 0.01, 3, 3 }, { 0, 5, 0.01, 3, 3 }, { 0, 150, 0, 3, 3 }, FREQ_DC_ONLY, { 0.01, 33, 0.01}, { 0.01, 5.5, 0.01 } },
};

static const struct channel_spec rigol_dp712_ch[] = {
	{ "1", { 0, 50, 0.01, 3, 3 }, { 0, 3, 0.01, 3, 3 }, { 0, 150, 0, 3, 3 }, FREQ_DC_ONLY, { 0.01, 55, 0.01}, { 0.01, 3.3, 0.01 } },
};

static const struct channel_group_spec rigol_dp700_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
};

/* Same as the DP800 series, except for the missing :SYST:OTP* commands. */
static const struct scpi_command rigol_dp700_cmd[] = {
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
	ALL_ZERO
};

/* Rigol DP800 series */
static const uint32_t rigol_dp800_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t rigol_dp800_devopts_cg[] = {
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_spec rigol_dp821a_ch[] = {
	{ "1", { 0, 60, 0.001, 3, 3 }, { 0, 1, 0.0001, 4, 4 }, { 0, 60, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "2", { 0,  8, 0.001, 3, 3 }, { 0, 10, 0.001, 3, 3 }, { 0, 80, 0, 3, 3 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

static const struct channel_spec rigol_dp831_ch[] = {
	{ "1", { 0,   8, 0.001, 3, 4 }, { 0, 5, 0.0003, 3, 4 }, { 0, 40, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "2", { 0,  30, 0.001, 3, 4 }, { 0, 2, 0.0001, 3, 4 }, { 0, 60, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "3", { 0, -30, 0.001, 3, 4 }, { 0, 2, 0.0001, 3, 4 }, { 0, 60, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

static const struct channel_spec rigol_dp832_ch[] = {
	{ "1", { 0, 30, 0.001, 3, 4 }, { 0, 3, 0.001, 3, 4 }, { 0, 90, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "2", { 0, 30, 0.001, 3, 4 }, { 0, 3, 0.001, 3, 4 }, { 0, 90, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "3", { 0,  5, 0.001, 3, 4 }, { 0, 3, 0.001, 3, 4 }, { 0, 90, 0, 3, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

static const struct channel_group_spec rigol_dp820_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
	{ "2", CH_IDX(1), PPS_OVP | PPS_OCP },
};

static const struct channel_group_spec rigol_dp830_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP | PPS_OCP },
	{ "2", CH_IDX(1), PPS_OVP | PPS_OCP },
	{ "3", CH_IDX(2), PPS_OVP | PPS_OCP },
};

static const struct scpi_command rigol_dp800_cmd[] = {
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
	ALL_ZERO
};

/* HP 663xx series */
static const uint32_t hp_6630a_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t hp_6630a_devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT_LIMIT | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_SET,
};

static const uint32_t hp_6630b_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t hp_6630b_devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_spec hp_6633a_ch[] = {
	{ "1", { 0, 51.188, 0.0125, 3, 4 }, { 0, 2.0475, 0.0005, 4, 5 }, { 0, 104.80743 }, FREQ_DC_ONLY, { 0, 55, 0.25 }, NO_OCP_LIMITS },
};

static const struct channel_spec hp_6631b_ch[] = {
	{ "1", { 0, 8.19, 0.002, 3, 4 }, { 0, 10.237, 0.00263, 4, 5 }, { 0, 83.84103 }, FREQ_DC_ONLY, { 0, 12, 0.06 }, NO_OCP_LIMITS },
};

static const struct channel_spec hp_6632b_ch[] = {
	{ "1", { 0, 20.475, 0.005, 3, 4 }, { 0, 5.1188, 0.00132, 4, 5 }, { 0, 104.80743 }, FREQ_DC_ONLY, { 0, 22, 0.1 }, NO_OCP_LIMITS },
};

static const struct channel_spec hp_66332a_ch[] = {
	{ "1", { 0, 20.475, 0.005, 3, 4 }, { 0, 5.1188, 0.00132, 4, 5 }, { 0, 104.80743 }, FREQ_DC_ONLY, { 0, 22, 0.1 }, NO_OCP_LIMITS },
};

static const struct channel_spec hp_6633b_ch[] = {
	{ "1", { 0, 51.188, 0.0125, 3, 4 }, { 0, 2.0475, 0.000526, 4, 5 }, { 0, 104.80743 }, FREQ_DC_ONLY, { 0, 55, 0.25 }, NO_OCP_LIMITS },
};

static const struct channel_spec hp_6634b_ch[] = {
	{ "1", { 0, 102.38, 0.025, 3, 4 }, { 0, 1.0238, 0.000263, 4, 5 }, { 0, 104.81664 }, FREQ_DC_ONLY, { 0, 110, 0.5 }, NO_OCP_LIMITS },
};

static const struct channel_group_spec hp_663xx_cg[] = {
	{ "1", CH_IDX(0), 0 },
};

static const struct scpi_command hp_6630a_cmd[] = {
	{ SCPI_CMD_SET_OUTPUT_ENABLE, "OUT 1" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, "OUT 0" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, "VOUT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, "IOUT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, "VSET %.4f" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, "ISET %.4f" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE, "OCP 1" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE, "OCP 0" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, "OVSET %.4f" },
	ALL_ZERO
};

static const struct scpi_command hp_6630b_cmd[] = {
	{ SCPI_CMD_REMOTE, "SYST:REM" },
	{ SCPI_CMD_LOCAL, "SYST:LOC" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, "OUTP:STAT?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, "OUTP:STAT ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, "OUTP:STAT OFF" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, ":MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, ":MEAS:CURR?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, ":SOUR:VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, ":SOUR:VOLT %.6f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, ":SOUR:CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, ":SOUR:CURR %.6f" },
	{ SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED, ":CURR:PROT:STAT?" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE, ":CURR:PROT:STAT 1" },
	{ SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE, ":CURR:PROT:STAT 0" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":VOLT:PROT?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, ":VOLT:PROT %.6f" },
	ALL_ZERO
};

/* Philips/Fluke PM2800 series */
static const uint32_t philips_pm2800_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t philips_pm2800_devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_REGULATION | SR_CONF_GET,
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
	double voltage[5];
	double current[5];
	double power[5];
} philips_pm2800_module_specs[] = {
	/* Autoranging modules. */
	[PM2800_MOD_30V_10A] = { { 0, 30, 0.0075, 2, 4 }, { 0, 10, 0.0025, 2, 4 }, { 0, 60 } },
	[PM2800_MOD_60V_5A] = { { 0, 60, 0.015, 2, 3 }, { 0, 5, 0.00125, 2, 5 }, { 0, 60 } },
	[PM2800_MOD_60V_10A] = { { 0, 60, 0.015, 2, 3 }, { 0, 10, 0.0025, 2, 5 }, { 0, 120 } },
	/* Linear modules. */
	[PM2800_MOD_8V_15A] = { { 0, 8, 0.002, 3, 3 }, { -15, 15, 0.00375, 3, 5 }, { 0, 120 } },
	[PM2800_MOD_60V_2A] = { { 0, 60, 0.015, 2, 3 }, { -2, 2, 0.0005, 3, 4 }, { 0, 120 } },
	[PM2800_MOD_120V_1A] = { { 0, 120, 0.030, 2, 2 }, { -1, 1, 0.00025, 3, 5 }, { 0, 120 } },
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
		sr_dbg("output %d: %.0f - %.0fV, %.0f - %.0fA, %.0f - %.0fW", i + 1,
				spec->voltage[0], spec->voltage[1],
				spec->current[0], spec->current[1],
				spec->power[0], spec->power[1]);
		(*channels)[i].name = (char *)philips_pm2800_names[i];
		memcpy(&((*channels)[i].voltage), spec, sizeof(double) * 15);
		(*channel_groups)[i].name = (char *)philips_pm2800_names[i];
		(*channel_groups)[i].channel_index_mask = 1 << i;
		(*channel_groups)[i].features = PPS_OTP | PPS_OVP | PPS_OCP;
	}
	*num_channels = *num_channel_groups = num_modules;

	return SR_OK;
}

static const struct scpi_command philips_pm2800_cmd[] = {
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
	ALL_ZERO
};

static const uint32_t rs_hmc8043_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t rs_hmc8043_devopts_cg[] = {
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct channel_spec rs_hmc8043_ch[] = {
	{ "1", { 0, 32.050, 0.001, 3, 4 }, { 0.001, 3, 0.001, 3, 4 }, { 0, 0, 0, 0, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "2", { 0, 32.050, 0.001, 3, 4 }, { 0.001, 3, 0.001, 3, 4 }, { 0, 0, 0, 0, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
	{ "3", { 0, 32.050, 0.001, 3, 4 }, { 0.001, 3, 0.001, 3, 4 }, { 0, 0, 0, 0, 4 }, FREQ_DC_ONLY, NO_OVP_LIMITS, NO_OCP_LIMITS },
};

static const struct channel_group_spec rs_hmc8043_cg[] = {
	{ "1", CH_IDX(0), PPS_OVP },
	{ "2", CH_IDX(1), PPS_OVP },
	{ "3", CH_IDX(2), PPS_OVP },
};

static const struct scpi_command rs_hmc8043_cmd[] = {
	{ SCPI_CMD_SELECT_CHANNEL, "INST:NSEL %s" },
	{ SCPI_CMD_GET_MEAS_VOLTAGE, "MEAS:VOLT?" },
	{ SCPI_CMD_GET_MEAS_CURRENT, "MEAS:CURR?" },
	{ SCPI_CMD_GET_VOLTAGE_TARGET, "VOLT?" },
	{ SCPI_CMD_SET_VOLTAGE_TARGET, "VOLT %.6f" },
	{ SCPI_CMD_GET_CURRENT_LIMIT, "CURR?" },
	{ SCPI_CMD_SET_CURRENT_LIMIT, "CURR %.6f" },
	{ SCPI_CMD_GET_OUTPUT_ENABLED, "OUTP?" },
	{ SCPI_CMD_SET_OUTPUT_ENABLE, "OUTP ON" },
	{ SCPI_CMD_SET_OUTPUT_DISABLE, "OUTP OFF" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE, "VOLT:PROT:TRIP?" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD, "VOLT:PROT:LEV?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, "VOLT:PROT:LEV %.6f" },
	{ SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ENABLED, "VOLT:PROT:STAT?" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_ENABLE, "VOLT:PROT:STAT ON" },
	{ SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_DISABLE, "VOLT:PROT:STAT OFF" },
	ALL_ZERO
};

SR_PRIV const struct scpi_pps pps_profiles[] = {
	/* Agilent N5763A */
	{ "Agilent", "N5763A", 0,
		ARRAY_AND_SIZE(agilent_n5700a_devopts),
		ARRAY_AND_SIZE(agilent_n5700a_devopts_cg),
		ARRAY_AND_SIZE(agilent_n5763a_ch),
		ARRAY_AND_SIZE(agilent_n5700a_cg),
		agilent_n5700a_cmd,
		.probe_channels = NULL,
	},

	/* Agilent N5767A */
	{ "Agilent", "N5767A", 0,
		ARRAY_AND_SIZE(agilent_n5700a_devopts),
		ARRAY_AND_SIZE(agilent_n5700a_devopts_cg),
		ARRAY_AND_SIZE(agilent_n5767a_ch),
		ARRAY_AND_SIZE(agilent_n5700a_cg),
		agilent_n5700a_cmd,
		.probe_channels = NULL,
	},

	/* Chroma 61604 */
	{ "Chroma", "61604", 0,
		ARRAY_AND_SIZE(chroma_61604_devopts),
		ARRAY_AND_SIZE(chroma_61604_devopts_cg),
		ARRAY_AND_SIZE(chroma_61604_ch),
		ARRAY_AND_SIZE(chroma_61604_cg),
		chroma_61604_cmd,
		.probe_channels = NULL,
	},

	/* Chroma 62000 series */
	{ "Chroma", "620[0-9]{2}P-[0-9]{2,3}-[0-9]{1,3}", 0,
		ARRAY_AND_SIZE(chroma_62000_devopts),
		ARRAY_AND_SIZE(chroma_62000_devopts_cg),
		NULL, 0,
		NULL, 0,
		chroma_62000_cmd,
		.probe_channels = chroma_62000p_probe_channels,
	},

	/* HP 6633A */
	{ "HP", "6633A", 0,
		ARRAY_AND_SIZE(hp_6630a_devopts),
		ARRAY_AND_SIZE(hp_6630a_devopts_cg),
		ARRAY_AND_SIZE(hp_6633a_ch),
		ARRAY_AND_SIZE(hp_663xx_cg),
		hp_6630a_cmd,
		.probe_channels = NULL,
	},

	/* HP 6631B */
	{ "HP", "6631B", PPS_OVP | PPS_OCP | PPS_OTP,
		ARRAY_AND_SIZE(hp_6630b_devopts),
		ARRAY_AND_SIZE(hp_6630b_devopts_cg),
		ARRAY_AND_SIZE(hp_6631b_ch),
		ARRAY_AND_SIZE(hp_663xx_cg),
		hp_6630b_cmd,
		.probe_channels = NULL,
	},

	/* HP 6632B */
	{ "HP", "6632B", PPS_OVP | PPS_OCP | PPS_OTP,
		ARRAY_AND_SIZE(hp_6630b_devopts),
		ARRAY_AND_SIZE(hp_6630b_devopts_cg),
		ARRAY_AND_SIZE(hp_6632b_ch),
		ARRAY_AND_SIZE(hp_663xx_cg),
		hp_6630b_cmd,
		.probe_channels = NULL,
	},

	/* HP 66332A */
	{ "HP", "66332A", PPS_OVP | PPS_OCP | PPS_OTP,
		ARRAY_AND_SIZE(hp_6630b_devopts),
		ARRAY_AND_SIZE(hp_6630b_devopts_cg),
		ARRAY_AND_SIZE(hp_66332a_ch),
		ARRAY_AND_SIZE(hp_663xx_cg),
		hp_6630b_cmd,
		.probe_channels = NULL,
	},

	/* HP 6633B */
	{ "HP", "6633B", PPS_OVP | PPS_OCP | PPS_OTP,
		ARRAY_AND_SIZE(hp_6630b_devopts),
		ARRAY_AND_SIZE(hp_6630b_devopts_cg),
		ARRAY_AND_SIZE(hp_6633b_ch),
		ARRAY_AND_SIZE(hp_663xx_cg),
		hp_6630b_cmd,
		.probe_channels = NULL,
	},

	/* HP 6634B */
	{ "HP", "6634B", PPS_OVP | PPS_OCP | PPS_OTP,
		ARRAY_AND_SIZE(hp_6630b_devopts),
		ARRAY_AND_SIZE(hp_6630b_devopts_cg),
		ARRAY_AND_SIZE(hp_6634b_ch),
		ARRAY_AND_SIZE(hp_663xx_cg),
		hp_6630b_cmd,
		.probe_channels = NULL,
	},

	/* Rigol DP700 series */
	{ "Rigol", "^DP711$", 0,
		ARRAY_AND_SIZE(rigol_dp700_devopts),
		ARRAY_AND_SIZE(rigol_dp700_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp711_ch),
		ARRAY_AND_SIZE(rigol_dp700_cg),
		rigol_dp700_cmd,
		.probe_channels = NULL,
	},
	{ "Rigol", "^DP712$", 0,
		ARRAY_AND_SIZE(rigol_dp700_devopts),
		ARRAY_AND_SIZE(rigol_dp700_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp712_ch),
		ARRAY_AND_SIZE(rigol_dp700_cg),
		rigol_dp700_cmd,
		.probe_channels = NULL,
	},

	/* Rigol DP800 series */
	{ "Rigol", "^DP821A$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp821a_ch),
		ARRAY_AND_SIZE(rigol_dp820_cg),
		rigol_dp800_cmd,
		.probe_channels = NULL,
	},
	{ "Rigol", "^DP831A$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp831_ch),
		ARRAY_AND_SIZE(rigol_dp830_cg),
		rigol_dp800_cmd,
		.probe_channels = NULL,
	},
	{ "Rigol", "^(DP832|DP832A)$", PPS_OTP,
		ARRAY_AND_SIZE(rigol_dp800_devopts),
		ARRAY_AND_SIZE(rigol_dp800_devopts_cg),
		ARRAY_AND_SIZE(rigol_dp832_ch),
		ARRAY_AND_SIZE(rigol_dp830_cg),
		rigol_dp800_cmd,
		.probe_channels = NULL,
	},

	/* Philips/Fluke PM2800 series */
	{ "Philips", "^PM28[13][123]/[01234]{1,2}$", 0,
		ARRAY_AND_SIZE(philips_pm2800_devopts),
		ARRAY_AND_SIZE(philips_pm2800_devopts_cg),
		NULL, 0,
		NULL, 0,
		philips_pm2800_cmd,
		philips_pm2800_probe_channels,
	},

	/* Rohde & Schwarz HMC8043 */
	{ "Rohde&Schwarz", "HMC8043", 0,
		ARRAY_AND_SIZE(rs_hmc8043_devopts),
		ARRAY_AND_SIZE(rs_hmc8043_devopts_cg),
		ARRAY_AND_SIZE(rs_hmc8043_ch),
		ARRAY_AND_SIZE(rs_hmc8043_cg),
		rs_hmc8043_cmd,
		.probe_channels = NULL,
	},
};

SR_PRIV unsigned int num_pps_profiles = ARRAY_SIZE(pps_profiles);
