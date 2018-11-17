/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_SCPI_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SCPI_DMM_PROTOCOL_H

#include <config.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi-dmm"

#define SCPI_DMM_MAX_CHANNELS	1

enum scpi_dmm_cmdcode {
	DMM_CMD_SETUP_REMOTE,
	DMM_CMD_SETUP_FUNC,
	DMM_CMD_QUERY_FUNC,
	DMM_CMD_START_ACQ,
	DMM_CMD_STOP_ACQ,
	DMM_CMD_QUERY_VALUE,
	DMM_CMD_QUERY_PREC,
};

struct mqopt_item {
	enum sr_mq mq;
	enum sr_mqflag mqflag;
	const char *scpi_func_setup;
	const char *scpi_func_query;
	int default_precision;
};
#define NO_DFLT_PREC	-99

struct scpi_dmm_model {
	const char *vendor;
	const char *model;
	size_t num_channels;
	ssize_t digits;
	const struct scpi_command *cmdset;
	const struct mqopt_item *mqopts;
	size_t mqopt_size;
	int (*get_measurement)(const struct sr_dev_inst *sdi, size_t ch);
	const uint32_t *devopts;
	size_t devopts_size;
};

struct dev_context {
	size_t num_channels;
	const struct scpi_command *cmdset;
	const struct scpi_dmm_model *model;
	struct sr_sw_limits limits;
	struct {
		enum sr_mq curr_mq;
		enum sr_mqflag curr_mqflag;
	} start_acq_mq;
	struct scpi_dmm_acq_info {
		float f_value;
		double d_value;
		struct sr_datafeed_packet packet;
		struct sr_datafeed_analog analog[SCPI_DMM_MAX_CHANNELS];
		struct sr_analog_encoding encoding[SCPI_DMM_MAX_CHANNELS];
		struct sr_analog_meaning meaning[SCPI_DMM_MAX_CHANNELS];
		struct sr_analog_spec spec[SCPI_DMM_MAX_CHANNELS];
	} run_acq_info;
};

SR_PRIV void scpi_dmm_cmd_delay(struct sr_scpi_dev_inst *scpi);
SR_PRIV const struct mqopt_item *scpi_dmm_lookup_mq_number(
	const struct sr_dev_inst *sdi, enum sr_mq mq, enum sr_mqflag flag);
SR_PRIV const struct mqopt_item *scpi_dmm_lookup_mq_text(
	const struct sr_dev_inst *sdi, const char *text);
SR_PRIV int scpi_dmm_get_mq(const struct sr_dev_inst *sdi,
	enum sr_mq *mq, enum sr_mqflag *flag, char **rsp,
	const struct mqopt_item **mqitem);
SR_PRIV int scpi_dmm_set_mq(const struct sr_dev_inst *sdi,
	enum sr_mq mq, enum sr_mqflag flag);
SR_PRIV int scpi_dmm_get_meas_agilent(const struct sr_dev_inst *sdi, size_t ch);
SR_PRIV int scpi_dmm_receive_data(int fd, int revents, void *cb_data);

#endif
