/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2017,2019 Frank Stettner <frank-stettner@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_SCPI_PPS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SCPI_PPS_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi-pps"

enum pps_scpi_cmds {
	SCPI_CMD_REMOTE = 1,
	SCPI_CMD_LOCAL,
	SCPI_CMD_BEEPER,
	SCPI_CMD_BEEPER_ENABLE,
	SCPI_CMD_BEEPER_DISABLE,
	SCPI_CMD_SELECT_CHANNEL,
	SCPI_CMD_GET_MEAS_VOLTAGE,
	SCPI_CMD_GET_MEAS_CURRENT,
	SCPI_CMD_GET_MEAS_POWER,
	SCPI_CMD_GET_MEAS_FREQUENCY,
	SCPI_CMD_GET_VOLTAGE_TARGET,
	SCPI_CMD_SET_VOLTAGE_TARGET,
	SCPI_CMD_GET_FREQUENCY_TARGET,
	SCPI_CMD_SET_FREQUENCY_TARGET,
	SCPI_CMD_GET_CURRENT_LIMIT,
	SCPI_CMD_SET_CURRENT_LIMIT,
	SCPI_CMD_GET_OUTPUT_ENABLED,
	SCPI_CMD_SET_OUTPUT_ENABLE,
	SCPI_CMD_SET_OUTPUT_DISABLE,
	SCPI_CMD_GET_OUTPUT_REGULATION,
	SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION,
	SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION_ENABLE,
	SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION_DISABLE,
	SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION_ACTIVE,
	SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ENABLED,
	SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_ENABLE,
	SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_DISABLE,
	SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE,
	SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD,
	SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD,
	SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED,
	SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE,
	SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE,
	SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE,
	SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD,
	SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD,
	SCPI_CMD_GET_CHANNEL_CONFIG,
	SCPI_CMD_SET_CHANNEL_CONFIG,
};

/* Defines the SCPI dialect */
enum pps_scpi_dialect {
	SCPI_DIALECT_UNKNOWN = 1,
	SCPI_DIALECT_HP_COMP,
	SCPI_DIALECT_HP_66XXB,
	SCPI_DIALECT_PHILIPS,
	SCPI_DIALECT_SIGLENT,
};

/*
 * These are bit values denoting features a device can have either globally,
 * in scpi_pps.features, or on a per-channel-group basis in
 * channel_group_spec.features.
 */
enum pps_features {
	PPS_OTP           = (1 << 0),
	PPS_OVP           = (1 << 1),
	PPS_OCP           = (1 << 2),
	PPS_INDEPENDENT   = (1 << 3),
	PPS_SERIES        = (1 << 4),
	PPS_PARALLEL      = (1 << 5),
};

struct scpi_pps {
	const char *vendor;
	const char *model;
	const enum pps_scpi_dialect dialect;
	uint64_t features;
	const uint32_t *devopts;
	unsigned int num_devopts;
	const uint32_t *devopts_cg;
	unsigned int num_devopts_cg;
	const struct channel_spec *channels;
	unsigned int num_channels;
	const struct channel_group_spec *channel_groups;
	unsigned int num_channel_groups;
	const struct scpi_command *commands;
	int (*probe_channels) (struct sr_dev_inst *sdi, struct sr_scpi_hw_info *hwinfo,
		struct channel_spec **channels, unsigned int *num_channels,
		struct channel_group_spec **channel_groups, unsigned int *num_channel_groups);
	int (*init_acquisition) (const struct sr_dev_inst *sdi);
	int (*update_status) (const struct sr_dev_inst *sdi);
};

struct channel_spec {
	const char *name;
	/* Min, max, programming resolution, spec digits, encoding digits. */
	double voltage[5];
	double current[5];
	double power[5];
	double frequency[5];
	double ovp[5];
	double ocp[5];
};

struct channel_group_spec {
	const char *name;
	uint64_t channel_index_mask;
	uint64_t features;
	/* The mqflags will only be applied to voltage and current channels! */
	enum sr_mqflag mqflags;
};

struct pps_channel {
	enum sr_mq mq;
	enum sr_mqflag mqflags;
	unsigned int hw_output_idx;
	const char *hwname;
	int digits;
};

struct pps_channel_instance {
	enum sr_mq mq;
	int command;
	const char *prefix;
};

struct pps_channel_group {
	uint64_t features;
};

enum acq_states {
	STATE_VOLTAGE,
	STATE_CURRENT,
	STATE_STOP,
};

struct dev_context {
	const struct scpi_pps *device;

	gboolean beeper_was_set;
	struct channel_spec *channels;
	struct channel_group_spec *channel_groups;

	struct sr_channel *cur_acquisition_channel;
	struct sr_sw_limits limits;

	uint32_t priv_status; /* device specific status data */
};

SR_PRIV extern unsigned int num_pps_profiles;
SR_PRIV extern const struct scpi_pps pps_profiles[];

SR_PRIV int select_channel(const struct sr_dev_inst *sdi, struct sr_channel *ch);
SR_PRIV int scpi_pps_receive_data(int fd, int revents, void *cb_data);

#endif
