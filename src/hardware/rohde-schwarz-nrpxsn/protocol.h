/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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

#ifndef LIBSIGROK_HARDWARE_ROHDE_SCHWARZ_NRPXSN_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ROHDE_SCHWARZ_NRPXSN_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "rohde-schwarz-nrpxsn"


struct rohde_schwarz_nrpxsn_device_model {
	const char *model_str;
	double freq_min;
	double freq_max;
	double power_min;
	double power_max;
};

enum MEAS_STATES {
	IDLE,
	WAITING_MEASUREMENT,
};

struct dev_context {
	struct sr_sw_limits limits;
	int trigger_source;
	int trigger_source_changed;
	uint64_t curr_freq;
	int curr_freq_changed;
	int measurement_state;

	const struct rohde_schwarz_nrpxsn_device_model *model_config;
};

SR_PRIV int rohde_schwarz_nrpxsn_receive_data(
		int fd, int revents, void *cb_data);
SR_PRIV int rohde_schwarz_nrpxsn_init(
		struct sr_scpi_dev_inst *scpi, struct dev_context *devc);
SR_PRIV int rohde_schwarz_nrpxsn_update_trigger_source(
		struct sr_scpi_dev_inst *scpi, struct dev_context *devc);
SR_PRIV int rohde_schwarz_nrpxsn_update_curr_freq(
		struct sr_scpi_dev_inst *scpi, struct dev_context *devc);

#endif
