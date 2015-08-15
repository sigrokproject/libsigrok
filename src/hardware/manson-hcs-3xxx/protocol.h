/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/** @file
  *  <em>Manson HCS-3xxx Series</em> power supply driver
  *  @internal
  */

#ifndef LIBSIGROK_HARDWARE_MANSON_HCS_3XXX_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MANSON_HCS_3XXX_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "manson-hcs-3xxx"

enum {
	MANSON_HCS_3100,
	MANSON_HCS_3102,
	MANSON_HCS_3104,
	MANSON_HCS_3150,
	MANSON_HCS_3200,
	MANSON_HCS_3202,
	MANSON_HCS_3204,
	MANSON_HCS_3300,
	MANSON_HCS_3302,
	MANSON_HCS_3304,
	MANSON_HCS_3400,
	MANSON_HCS_3402,
	MANSON_HCS_3404,
	MANSON_HCS_3600,
	MANSON_HCS_3602,
	MANSON_HCS_3604,
};

/** Information on a single model. */
struct hcs_model {
	int model_id;      /**< Model info */
	char *name;        /**< Model name */
	char *id;          /**< Model ID, like delivered by interface */
	double voltage[3]; /**< Min, max, step */
	double current[3]; /**< Min, max, step */
};

/** Private, per-device-instance driver context. */
struct dev_context {
	const struct hcs_model *model; /**< Model information. */

	uint64_t limit_samples;
	uint64_t limit_msec;
	uint64_t num_samples;
	int64_t starttime;
	int64_t req_sent_at;
	gboolean reply_pending;

	void *cb_data;

	float current;		/**< Last current value [A] read from device. */
	float current_max;	/**< Output current set. */
	float current_max_device;/**< Device-provided maximum output current. */
	float voltage;		/**< Last voltage value [V] read from device. */
	float voltage_max;	/**< Output voltage set. */
	float voltage_max_device;/**< Device-provided maximum output voltage. */
	gboolean cc_mode;	/**< Device is in constant current mode (otherwise constant voltage). */

	gboolean output_enabled; /**< Is the output enabled? */

	char buf[50];
	int buflen;
};

SR_PRIV int hcs_parse_volt_curr_mode(struct sr_dev_inst *sdi, char **tokens);
SR_PRIV int hcs_read_reply(struct sr_serial_dev_inst *serial, int lines, char *buf, int buflen);
SR_PRIV int hcs_send_cmd(struct sr_serial_dev_inst *serial, const char *cmd, ...);
SR_PRIV int hcs_receive_data(int fd, int revents, void *cb_data);

#endif
