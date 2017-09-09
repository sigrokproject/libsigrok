/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2017 John Chajecki <subs@qcontinuum.plus.com>
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

#ifndef LIBSIGROK_HARDWARE_FLUKE_45_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FLUKE_45_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <scpi.h>

#define LOG_PREFIX "fluke-45"

#define FLUKEDMM_BUFSIZE 256

/* Always USB-serial, 1ms is plenty. */
#define SERIAL_WRITE_TIMEOUT_MS 1

enum data_format {
	/* Fluke 45 uses IEEE488v2. */
	FORMAT_IEEE488_2,
};

enum dmm_scpi_cmds {
	SCPI_CMD_CLS,
	SCPI_CMD_RST,
	SCPI_CMD_REMS,
	SCPI_CMD_RWLS,
	SCPI_CMD_LOCS,
	SCPI_CMD_LWLS,
	SCPI_CMD_REMOTE,
	SCPI_CMD_LOCAL,
	SCPI_CMD_SET_ACVOLTAGE,
	SCPI_CMD_SET_ACDCVOLTAGE,
	SCPI_CMD_SET_DCVOLTAGE,
	SCPI_CMD_SET_ACCURRENT,
	SCPI_CMD_SET_ACDCCURRENT,
	SCPI_CMD_SET_DCCURRENT,
	SCPI_CMD_SET_FREQUENCY,
	SCPI_CMD_SET_RESISTANCE,
	SCPI_CMD_SET_CONTINUITY,
	SCPI_CMD_SET_DIODE,
	SCPI_CMD_SET_AUTO,
	SCPI_CMD_GET_AUTO,
	SCPI_CMD_SET_FIXED,
	SCPI_CMD_SET_RANGE,
	SCPI_CMD_GET_RANGE_D1,
	SCPI_CMD_GET_RANGE_D2,
	SCPI_CMD_SET_DB,
	SCPI_CMD_SET_DBCLR,
	SCPI_CMD_SET_DBPOWER,
	SCPI_CMD_SET_DBREF,
	SCPI_CMD_GET_DBREF,
	SCPI_CMD_SET_HOLD,
	SCPI_CMD_SET_HOLDCLR,
	SCPI_CMD_SET_MAX,
	SCPI_CMD_SET_MIN,
	SCPI_CMD_SET_MMCLR,
	SCPI_CMD_SET_REL,
	SCPI_CMD_SET_RELCLR,
	SCPI_CMD_GET_MEAS_DD,
	SCPI_CMD_GET_MEAS_D1,
	SCPI_CMD_GET_MEAS_D2,
	SCPI_CMD_GET_RATE,
	SCPI_CMD_SET_RATE,
	SCPI_CMD_SET_TRIGGER,
	SCPI_CMD_GET_TRIGGER,
};

static const struct scpi_command fluke_45_cmdset[] = {
	{ SCPI_CMD_CLS, "*CLS" },
	{ SCPI_CMD_RST, "*RST" },
	{ SCPI_CMD_REMS, "*REMS" },
	{ SCPI_CMD_RWLS, "*RWLS" },
	{ SCPI_CMD_LOCS, "LOCS" },
	{ SCPI_CMD_LWLS, "LWLS" },
	{ SCPI_CMD_REMOTE, "REMS" },
	{ SCPI_CMD_LOCAL, "LOCS" },
	{ SCPI_CMD_SET_ACVOLTAGE, "VAC" },
	{ SCPI_CMD_SET_ACDCVOLTAGE, "VACDC" },
	{ SCPI_CMD_SET_DCVOLTAGE, "VDC" },
	{ SCPI_CMD_SET_ACCURRENT, "AAC" },
	{ SCPI_CMD_SET_ACDCCURRENT, "AACDC" },
	{ SCPI_CMD_SET_DCCURRENT, "ADC" },
	{ SCPI_CMD_SET_FREQUENCY, "FREQ" },
	{ SCPI_CMD_SET_RESISTANCE, "OHMS" },
	{ SCPI_CMD_SET_CONTINUITY, "CONT" },
	{ SCPI_CMD_SET_DIODE, "DIODE" },
	{ SCPI_CMD_SET_AUTO, "AUTO" },
	{ SCPI_CMD_GET_AUTO, "AUTO?" },
	{ SCPI_CMD_SET_FIXED, "FIXED" },
	{ SCPI_CMD_SET_RANGE, "RANGE" },
	{ SCPI_CMD_GET_RANGE_D1, "RANGE1?" },
	{ SCPI_CMD_GET_RANGE_D2, "RANGE2?" },
	{ SCPI_CMD_SET_DB, "DB" },
	{ SCPI_CMD_SET_DBCLR, "DBCLR" },
	{ SCPI_CMD_SET_DBPOWER, "DBPOWER" },
	{ SCPI_CMD_SET_DBREF, "DBREF" },
	{ SCPI_CMD_GET_DBREF, "DBREF?" },
	{ SCPI_CMD_SET_HOLD, "HOLD" },
	{ SCPI_CMD_SET_HOLDCLR, "HOLDCLR" },
	{ SCPI_CMD_SET_MAX, "MAX" },
	{ SCPI_CMD_SET_MIN, "MIN" },
	{ SCPI_CMD_SET_MMCLR, "MMCLR" },
	{ SCPI_CMD_SET_REL, "REL" },
	{ SCPI_CMD_SET_RELCLR, "RELCLR" },
	{ SCPI_CMD_GET_MEAS_DD, "MEAS?" },
	{ SCPI_CMD_GET_MEAS_D1, "MEAS1?" },
	{ SCPI_CMD_GET_MEAS_D2, "MEAS2?" },
	{ SCPI_CMD_SET_RATE, "RATE" },
	{ SCPI_CMD_GET_RATE, "RATE?" },
	{ SCPI_CMD_SET_TRIGGER, "TRIGGER" },
	{ SCPI_CMD_GET_TRIGGER, "TRIGGER?" },
	ALL_ZERO
};

struct fluke_scpi_dmm_model {
	const char *vendor;
	const char *model;
	int num_channels;
	int poll_period; /* How often to poll, in ms. */
};

struct channel_spec {
	const char *name;
	/* Min, max, programming resolution, spec digits, encoding digits. */
	double voltage[5];
	double current[5];
	double resistance[5];
	double capacitance[5];
	double conductance[5];
	double diode[5];
};

struct channel_group_spec {
	const char *name;
	uint64_t channel_index_mask;
	uint64_t features;
};

struct dmm_channel {
	enum sr_mq mq;
	unsigned int hw_output_idx;
	const char *hwname;
	int digits;
};

struct dmm_channel_instance {
	enum sr_mq mq;
	int command;
	const char *prefix;
};

struct dmm_channel_group {
	uint64_t features;
};

struct dev_context {
	struct sr_sw_limits limits;
	unsigned int num_channels;
	const struct scpi_command *cmdset;
	char *response;
	const char *mode1;
	const char *mode2;
	long range1;
	long range2;
	long autorng;
	const char *rate;
	long modifiers;
	long trigmode;
};

int get_reading_dd(char *reading, size_t size);

SR_PRIV extern const struct fluke_scpi_dmm_model dmm_profiles[];

SR_PRIV int fl45_scpi_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int fl45_scpi_get_response(const struct sr_dev_inst *sdi,  char *cmd);
SR_PRIV int fl45_get_status(const struct sr_dev_inst *sdi,
		struct sr_datafeed_analog *analog, int idx);
SR_PRIV int fl45_get_modifiers(const struct sr_dev_inst *sdi,
		struct sr_datafeed_analog *analog, int idx);

#endif
