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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_AIM_TTI_DPS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_AIM_TTI_DPS_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "aim-tti-dps"
#define MAX_CHANNELS 2

/* Information on single model */
struct aim_tti_dps_model {
	const char *name;  /** Model name */
	int channels;      /** Number of channels */
	double maxpower;   /** max power per channel */
	double voltage[3]; /** Min, max, step */
	double current[3]; /** Min, max, step */
};

struct per_channel_dev_context {
	float voltage_target;
	float current_limit;
	float actual_voltage;
	float actual_current;
	float over_voltage_protection_threshold;
	float over_current_protection_threshold;

	gboolean output_enabled;
	int mode; /* cc cv ur */
	gboolean ocp_active;
	gboolean ovp_active;

	gboolean mode_changed;
	gboolean ocp_active_changed;
	gboolean ovp_active_changed;
};

struct dev_context {
	struct sr_sw_limits limits;
	const struct aim_tti_dps_model *model_config;

	struct per_channel_dev_context *config;

	int acquisition_param;
	int acquisition_channel;
	gboolean tracking_enabled;
	GMutex rw_mutex;
};

enum {
	AIM_TTI_VOLTAGE,
	AIM_TTI_VOLTAGE_TARGET,
	AIM_TTI_CURRENT,
	AIM_TTI_CURRENT_LIMIT,
	AIM_TTI_OUTPUT_ENABLE,
	AIM_TTI_OCP_THRESHOLD,
	AIM_TTI_OVP_THRESHOLD,
	AIM_TTI_STATUS,
	AIM_TTI_LAST_CHANNEL_PARAM,
	AIM_TTI_OUTPUT_ENABLE_DUAL,
	AIM_TTI_TRACKING_ENABLE
};

enum {
	AIM_TTI_CC,
	AIM_TTI_CV,
	AIM_TTI_UR
};

SR_PRIV int aim_tti_dps_set_value(struct sr_scpi_dev_inst *scpi,
			 		struct dev_context *devc, int param, size_t channel);
SR_PRIV int aim_tti_dps_get_value(struct sr_scpi_dev_inst *scpi,
			 		struct dev_context *devc, int param, size_t channel);
SR_PRIV int aim_tti_dps_sync_state(struct sr_scpi_dev_inst *scpi,
			 		struct dev_context *devc);
SR_PRIV void aim_tti_dps_next_acqusition(struct dev_context *devc);
SR_PRIV int aim_tti_dps_receive_data(int fd, int revents, void *cb_data);
#endif

