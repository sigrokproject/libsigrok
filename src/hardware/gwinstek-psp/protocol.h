/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 ettom <36895504+ettom@users.noreply.github.com>
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

#ifndef LIBSIGROK_HARDWARE_GWINSTEK_PSP_PROTOCOL_H
#define LIBSIGROK_HARDWARE_GWINSTEK_PSP_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "gwinstek-psp"

#define GWINSTEK_PSP_PROCESSING_TIME_MS 50
#define GWINSTEK_PSP_STATUS_POLL_TIME_MS 245 /**< 'L' query response time. */

/* Information on single model */
struct gwinstek_psp_model {
	const char *vendor;    /**< Vendor name */
	const char *name;      /**< Model name */
	const double *voltage; /**< References: Min, max, step */
	const double *current; /**< References: Min, max, step */
};

struct dev_context {
	const struct gwinstek_psp_model *model; /**< Model information. */

	struct sr_sw_limits limits;
	int64_t next_req_time;
	int64_t last_status_query_time;
	GMutex rw_mutex;

	float power;            /**< Last power value [W] read from device. */
	float current;          /**< Last current value [A] read from device. */
	float current_limit;    /**< Output current set. */
	float voltage;          /**< Last voltage value [V] read from device. */
	float voltage_or_0;     /**< Same, but 0 if output is off. */
	int voltage_limit;      /**< Output voltage limit. */

	/*< Output voltage target. The device has no means to query this
	 * directly. It's equal to the voltage if the output is disabled
	 * (detectable) or the device is in CV mode (undetectable).*/
	float voltage_target;
	int64_t voltage_target_updated; /**< When device last reported a voltage target. */

	float set_voltage_target;           /**< The last set output voltage target. */
	int64_t set_voltage_target_updated; /**< When the voltage target was last set. */

	gboolean output_enabled; /**< Is the output enabled? */
	gboolean otp_active;     /**< Is the overtemperature protection active? */

	int msg_terminator_len; /** < 2 or 3, depending on the URPSP1/2 setting */
};

SR_PRIV int gwinstek_psp_send_cmd(struct sr_serial_dev_inst *serial,
    struct dev_context *devc, const char* cmd, gboolean lock);
SR_PRIV int gwinstek_psp_check_terminator(struct sr_serial_dev_inst *serial,
    struct dev_context *devc);
SR_PRIV int gwinstek_psp_get_initial_voltage_target(struct dev_context *devc);
SR_PRIV int gwinstek_psp_get_all_values(struct sr_serial_dev_inst *serial,
	struct dev_context *devc);
SR_PRIV int gwinstek_psp_receive_data(int fd, int revents, void *cb_data);

#endif
