/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Bastian Schmitz <bastian.schmitz@udo.edu>
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

#ifndef LIBSIGROK_HARDWARE_GWINSTEK_GPD_PROTOCOL_H
#define LIBSIGROK_HARDWARE_GWINSTEK_GPD_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "gwinstek-gpd"

enum {
	GPD_2303S,
};

/* Maximum number of output channels handled by this driver. */
#define MAX_CHANNELS 2

#define CHANMODE_INDEPENDENT (1 << 0)
#define CHANMODE_SERIES      (1 << 1)
#define CHANMODE_PARALLEL    (1 << 2)

struct channel_spec {
	/* Min, max, step. */
	gdouble voltage[3];
	gdouble current[3];
};

struct gpd_model {
	int			modelid;
	const char		*name;
	int			channel_modes;
	unsigned int		num_channels;
	struct channel_spec	channels[MAX_CHANNELS];
};

struct per_channel_config {
	/* Received from device. */
	gfloat	output_voltage_last;
	gfloat	output_current_last;
	/* Set by frontend. */
	gfloat	output_voltage_max;
	gfloat	output_current_max;
};

struct dev_context {
	/* Received from device. */
	gboolean			output_enabled;
	int64_t				req_sent_at;
	gboolean			reply_pending;

	struct sr_sw_limits		limits;
	int				channel_mode;
	struct per_channel_config	*config;
	const struct gpd_model		*model;
};


SR_PRIV int gpd_send_cmd(struct sr_serial_dev_inst *serial,
		const char *cmd, ...);

SR_PRIV int gpd_receive_data(int fd, int revents, void *cb_data);

SR_PRIV int gpd_receive_reply(struct sr_serial_dev_inst *serial, char *buf,
		int buflen);

#endif
