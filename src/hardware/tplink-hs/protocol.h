/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Francois Gervais <francoisgervais@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_TPLINK_HS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TPLINK_HS_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "tplink-hs"

struct channel_spec {
	const char *name;
	int type;
	enum sr_mq mq;
	enum sr_unit unit;
};

struct tplink_dev_info {
	char *model;
	char *sw_ver;
	char *device_id;

	const struct channel_spec *channels;
};

struct dev_context {
	struct tplink_dev_info dev_info;
	const struct tplink_hs_ops *ops;
	struct sr_sw_limits limits;

	char *address;
	char *port;
	int socket;
	unsigned int read_timeout;

	GPollFD pollfd;

	float current;
	float voltage;

	int64_t cmd_sent_at;
};

SR_PRIV int tplink_hs_probe(struct dev_context  *devc);
SR_PRIV int tplink_hs_receive_data(int fd, int revents, void *cb_data);

#endif
