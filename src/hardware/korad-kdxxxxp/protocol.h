/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
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

/**
 * @file
 * Korad KDxxxxP power supply driver
 * @internal
 */

#ifndef LIBSIGROK_HARDWARE_KORAD_KDXXXXP_PROTOCOL_H
#define LIBSIGROK_HARDWARE_KORAD_KDXXXXP_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "korad-kdxxxxp"

#define KDXXXXP_POLL_INTERVAL_MS 80

enum {
	VELLEMAN_LABPS_3005D,
	/* Support for future devices with this protocol. */
};

/* Information on single model */
struct korad_kdxxxxp_model {
	int model_id; /**< Model info */
	char *vendor; /**< Vendor name */
	char *name; /**< Model name */
	char *id; /**< Model ID, as delivered by interface */
	int channels; /**< Number of channels */
	double voltage[3]; /**< Min, max, step */
	double current[3]; /**< Min, max, step */
};

/* Reply targets */
enum {
	KDXXXXP_CURRENT,
	KDXXXXP_CURRENT_MAX,
	KDXXXXP_VOLTAGE,
	KDXXXXP_VOLTAGE_MAX,
	KDXXXXP_STATUS,
	KDXXXXP_OUTPUT,
	KDXXXXP_BEEP,
	KDXXXXP_OCP,
	KDXXXXP_OVP,
	KDXXXXP_SAVE,
	KDXXXXP_RECALL,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct korad_kdxxxxp_model *model; /**< Model information. */

	/* Acquisition settings */
	uint64_t limit_samples;
	uint64_t limit_msec;
	uint64_t num_samples;
	int64_t starttime;
	int64_t req_sent_at;
	gboolean reply_pending;

	void *cb_data;

	/* Operational state */
	float current;          /**< Last current value [A] read from device. */
	float current_max;      /**< Output current set. */
	float voltage;          /**< Last voltage value [V] read from device. */
	float voltage_max;      /**< Output voltage set. */
	gboolean cc_mode[2];    /**< Device is in CC mode (otherwise CV). */

	gboolean output_enabled; /**< Is the output enabled? */
	gboolean beep_enabled;   /**< Enable beeper. */
	gboolean ocp_enabled;    /**< Output current protection enabled. */
	gboolean ovp_enabled;    /**< Output voltage protection enabled. */

	/* Temporary state across callbacks */
	int target;              /**< What reply to expect. */
	int program;             /**< Program to store or recall. */
	char reply[6];
};

SR_PRIV int korad_kdxxxxp_send_cmd(struct sr_serial_dev_inst *serial,
					const char *cmd);
SR_PRIV int korad_kdxxxxp_read_chars(struct sr_serial_dev_inst *serial,
					int count, char *buf);
SR_PRIV int korad_kdxxxxp_set_value(struct sr_serial_dev_inst *serial,
					struct dev_context *devc);
SR_PRIV int korad_kdxxxxp_query_value(struct sr_serial_dev_inst *serial,
					struct dev_context *devc);
SR_PRIV int korad_kdxxxxp_get_reply(struct sr_serial_dev_inst *serial,
					struct dev_context *devc);
SR_PRIV int korad_kdxxxxp_get_all_values(struct sr_serial_dev_inst *serial,
					struct dev_context *devc);
SR_PRIV int korad_kdxxxxp_receive_data(int fd, int revents, void *cb_data);

#endif
