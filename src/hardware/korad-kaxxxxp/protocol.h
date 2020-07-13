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

enum {
	VELLEMAN_PS3005D,
	VELLEMAN_LABPS3005D,
	KORAD_KA3005P,
	KORAD_KA3005P_0X01,
	KORAD_KA3005P_0XBC,
	KORAD_KA3005P_V42,
	KORAD_KA3005P_V55,
	KORAD_KD3005P,
	KORAD_KD3005P_V20_NOSP,
	RND_320_KD3005P,
	RND_320_KA3005P,
	RND_320K30PV,
	TENMA_72_2550_V2,
	TENMA_72_2540_V20,
	TENMA_72_2540_V21,
	TENMA_72_2540_V52,
	TENMA_72_2535_V21,
	STAMOS_SLS31_V20,
	KORAD_KD6005P,
	/* Support for future devices with this protocol. */
};

/* Information on single model */
struct korad_kaxxxxp_model {
	int model_id; /**< Model info */
	const char *vendor; /**< Vendor name */
	const char *name; /**< Model name */
	const char *id; /**< Model ID, as delivered by interface */
	int channels; /**< Number of channels */
	double voltage[3]; /**< Min, max, step */
	double current[3]; /**< Min, max, step */
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
	int64_t req_sent_at;
	GMutex rw_mutex;

	float current;          /**< Last current value [A] read from device. */
	float current_limit;    /**< Output current set. */
	float voltage;          /**< Last voltage value [V] read from device. */
	float voltage_target;   /**< Output voltage set. */
	gboolean cc_mode[2];    /**< Device is in CC mode (otherwise CV). */

	gboolean output_enabled; /**< Is the output enabled? */
	gboolean beep_enabled;   /**< Enable beeper. */
	gboolean ocp_enabled;    /**< Output current protection enabled. */
	gboolean ovp_enabled;    /**< Output voltage protection enabled. */

	gboolean cc_mode_1_changed;      /**< CC mode of channel 1 has changed. */
	gboolean cc_mode_2_changed;      /**< CC mode of channel 2 has changed. */
	gboolean output_enabled_changed; /**< Output enabled state has changed. */
	gboolean ocp_enabled_changed;    /**< OCP enabled state has changed. */
	gboolean ovp_enabled_changed;    /**< OVP enabled state has changed. */

	int acquisition_target;  /**< What reply to expect. */
	int program;             /**< Program to store or recall. */

	float set_current_limit;     /**< New output current to set. */
	float set_voltage_target;    /**< New output voltage to set. */
	gboolean set_output_enabled; /**< New output enabled to set. */
	gboolean set_beep_enabled;   /**< New enable beeper to set. */
	gboolean set_ocp_enabled;    /**< New OCP enabled to set. */
	gboolean set_ovp_enabled;    /**< New OVP enabled to set. */
};

SR_PRIV int korad_kaxxxxp_send_cmd(struct sr_serial_dev_inst *serial,
					const char *cmd);
SR_PRIV int korad_kaxxxxp_read_chars(struct sr_serial_dev_inst *serial,
					int count, char *buf);
SR_PRIV int korad_kaxxxxp_set_value(struct sr_serial_dev_inst *serial,
					int target, struct dev_context *devc);
SR_PRIV int korad_kaxxxxp_get_value(struct sr_serial_dev_inst *serial,
					int target, struct dev_context *devc);
SR_PRIV int korad_kaxxxxp_get_all_values(struct sr_serial_dev_inst *serial,
					struct dev_context *devc);
SR_PRIV int korad_kaxxxxp_receive_data(int fd, int revents, void *cb_data);

#endif
