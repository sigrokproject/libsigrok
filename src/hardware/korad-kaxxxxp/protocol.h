/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
 * Copyright (C) 2018-2019 Frank Stettner <frank-stettner@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_KORAD_KAXXXXP_PROTOCOL_H
#define LIBSIGROK_HARDWARE_KORAD_KAXXXXP_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "korad-kaxxxxp"

#define KAXXXXP_POLL_INTERVAL_MS 80

/* Maximum number of outputs any supported model provides. */
#define KAXXXXP_MAX_OUTPUTS 2

enum korad_quirks_flag {
	KORAD_QUIRK_NONE = 0,
	KORAD_QUIRK_LABPS_OVP_EN = 1UL << 0,
	KORAD_QUIRK_ID_NO_VENDOR = 1UL << 1,
	KORAD_QUIRK_ID_TRAILING = 1UL << 2,
	KORAD_QUIRK_ID_OPT_VERSION = 1UL << 3,
	KORAD_QUIRK_SLOW_PROCESSING = 1UL << 4,
	/*
	 * The KA3305P family reports the OVP enable state on STATUS bit 4,
	 * which the device documentation lists as unused, and it has no
	 * status bit for the beeper at all (toggling the beeper changes no
	 * bit whatsoever). On the other models bit 4 carries the beeper
	 * state and OVP uses bit 7.
	 */
	KORAD_QUIRK_STATUS_OVP_BIT4 = 1UL << 5,
	KORAD_QUIRK_ALL = (1UL << 6) - 1,
};

/* Information on single model */
struct korad_kaxxxxp_model {
	const char *vendor; /**< Vendor name */
	const char *name; /**< Model name */
	const char *id; /**< Model ID, as delivered by interface */
	int channels; /**< Number of channels */
	const double *voltage; /**< References: Min, max, step */
	const double *current; /**< References: Min, max, step */
	enum korad_quirks_flag quirks;
};

/* Reply targets */
enum {
	KAXXXXP_CURRENT,
	KAXXXXP_CURRENT_LIMIT,
	KAXXXXP_VOLTAGE,
	KAXXXXP_VOLTAGE_TARGET,
	KAXXXXP_STATUS,
	KAXXXXP_OUTPUT,
	KAXXXXP_BEEP,
	KAXXXXP_OCP,
	KAXXXXP_OVP,
	KAXXXXP_SAVE,
	KAXXXXP_RECALL,
};

struct dev_context {
	const struct korad_kaxxxxp_model *model; /**< Model information. */

	struct sr_sw_limits limits;
	int64_t next_req_time;
	GMutex rw_mutex;

	int num_outputs;         /**< Number of outputs the model provides. */

	/*
	 * Per-output values. Indices here are 0-based, while the device
	 * protocol uses 1-based channel numbers. Commands append "1" or
	 * "2" to VSET/ISET/VOUT/IOUT, so @c out_idx + 1 is used on the wire.
	 */
	float current[KAXXXXP_MAX_OUTPUTS];        /**< Last current [A]. */
	float current_limit[KAXXXXP_MAX_OUTPUTS];  /**< Output current set. */
	float voltage[KAXXXXP_MAX_OUTPUTS];        /**< Last voltage [V]. */
	float voltage_target[KAXXXXP_MAX_OUTPUTS]; /**< Output voltage set. */
	gboolean cc_mode[KAXXXXP_MAX_OUTPUTS];     /**< In CC mode (else CV). */

	/*
	 * Global device state. Output enable is a single global setting on the
	 * KA3305P family ("OUT0"/"OUT1"), it is not per output; only the
	 * KD3305P variant has individually switchable outputs. Likewise OCP,
	 * OVP and the beeper are global settings.
	 */
	gboolean output_enabled; /**< Is the output enabled? */
	gboolean beep_enabled;   /**< Enable beeper. */
	gboolean ocp_enabled;    /**< Output current protection enabled. */
	gboolean ovp_enabled;    /**< Output voltage protection enabled. */

	gboolean cc_mode_changed[KAXXXXP_MAX_OUTPUTS]; /**< CC mode changed. */
	gboolean output_enabled_changed; /**< Output enabled state has changed. */
	gboolean ocp_enabled_changed;    /**< OCP enabled state has changed. */
	gboolean ovp_enabled_changed;    /**< OVP enabled state has changed. */

	int acquisition_target;  /**< What reply to expect. */
	int acquisition_out;     /**< Output the next reply belongs to. */
	int program;             /**< Program to store or recall. */

	float set_current_limit[KAXXXXP_MAX_OUTPUTS];  /**< New current to set. */
	float set_voltage_target[KAXXXXP_MAX_OUTPUTS]; /**< New voltage to set. */
	gboolean set_output_enabled; /**< New output enabled to set. */
	gboolean set_beep_enabled;   /**< New enable beeper to set. */
	gboolean set_ocp_enabled;    /**< New OCP enabled to set. */
	gboolean set_ovp_enabled;    /**< New OVP enabled to set. */
};

SR_PRIV int korad_kaxxxxp_send_cmd(struct sr_serial_dev_inst *serial,
		const char *cmd);
SR_PRIV int korad_kaxxxxp_read_chars(struct sr_serial_dev_inst *serial,
		size_t count, char *buf);
SR_PRIV int korad_kaxxxxp_set_value(struct sr_serial_dev_inst *serial,
		int target, int out_idx, struct dev_context *devc);
SR_PRIV int korad_kaxxxxp_get_value(struct sr_serial_dev_inst *serial,
		int target, int out_idx, struct dev_context *devc);
SR_PRIV int korad_kaxxxxp_get_all_values(struct sr_serial_dev_inst *serial,
		struct dev_context *devc);
SR_PRIV int korad_kaxxxxp_receive_data(int fd, int revents, void *cb_data);

#endif
