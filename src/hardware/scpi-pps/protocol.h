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

#ifndef LIBSIGROK_HARDWARE_SCPI_PPS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SCPI_PPS_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "scpi-pps"

enum pps_scpi_cmds {
	SCPI_CMD_KEY_UNLOCK,
	SCPI_CMD_GET_MEAS_VOLTAGE,
	SCPI_CMD_GET_MEAS_CURRENT,
	SCPI_CMD_GET_MEAS_POWER,
	SCPI_CMD_GET_VOLTAGE_MAX,
	SCPI_CMD_SET_VOLTAGE_MAX,
	SCPI_CMD_GET_CURRENT_MAX,
	SCPI_CMD_SET_CURRENT_MAX,
	SCPI_CMD_GET_OUTPUT_ENABLED,
	SCPI_CMD_SET_OUTPUT_ENABLED,
	SCPI_CMD_GET_OUTPUT_REGULATION,
	SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION,
	SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION,
	SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ENABLED,
	SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_ENABLED,
	SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE,
	SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD,
	SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD,
	SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED,
	SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLED,
	SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE,
	SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD,
	SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD,
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
	char *vendor;
	char *model;
	uint64_t features;
	const int32_t *devopts;
	unsigned int num_devopts;
	const int32_t *devopts_cg;
	unsigned int num_devopts_cg;
	struct channel_spec *channels;
	unsigned int num_channels;
	struct channel_group_spec *channel_groups;
	unsigned int num_channel_groups;
	struct scpi_command *commands;
	unsigned int num_commands;
};

struct channel_spec {
	char *name;
	/* Min, max, programming resolution. */
	float voltage[3];
	float current[3];
};

struct scpi_command {
	int command;
	char *string;
};

struct channel_group_spec {
	char *name;
	uint64_t channel_index_mask;
	uint64_t features;
};

struct pps_channel_group {
	uint64_t features;
};

enum acq_states {
	STATE_VOLTAGE,
	STATE_CURRENT,
	STATE_STOP,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct scpi_pps *device;

	/* Acquisition settings */
	void *cb_data;

	/* Operational state */

	/* Temporary state across callbacks */
	int state;
	struct sr_channel *cur_channel;
};

const char *get_vendor(const char *raw_vendor);
SR_PRIV char *scpi_cmd_get(const struct sr_dev_inst *sdi, int command);
SR_PRIV int scpi_cmd(const struct sr_dev_inst *sdi, int command, ...);
SR_PRIV int scpi_cmd_resp(const struct sr_dev_inst *sdi, GVariant **gvar,
		const GVariantType *gvtype, int command, ...);
SR_PRIV int scpi_pps_receive_data(int fd, int revents, void *cb_data);

#endif
