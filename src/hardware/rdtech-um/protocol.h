/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018-2020 Andreas Sandberg <andreas@sandberg.pp.se>
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

#ifndef LIBSIGROK_HARDWARE_RDTECH_UM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RDTECH_UM_PROTOCOL_H

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rdtech-um"

#define RDTECH_UM_BUFSIZE 256

enum rdtech_um_model_id {
	RDTECH_UM24C = 0x0963,
	RDTECH_UM25C = 0x09c9,
	RDTECH_UM34C = 0x0d4c,
};

struct rdtech_um_channel_desc {
	const char *name;
	struct binary_value_spec spec;
	struct sr_rational scale;
	int digits;
	enum sr_mq mq;
	enum sr_unit unit;
};

struct rdtech_um_profile {
	const char *model_name;
	enum rdtech_um_model_id model_id;
	const struct rdtech_um_channel_desc *channels;
	size_t channel_count;
	gboolean (*csum_ok)(const uint8_t *buf, size_t len);
};

struct dev_context {
	const struct rdtech_um_profile *profile;
	struct sr_sw_limits limits;
	struct feed_queue_analog **feeds;
	uint8_t buf[RDTECH_UM_BUFSIZE];
	size_t buflen;
	int64_t cmd_sent_at;
};

SR_PRIV const struct rdtech_um_profile *rdtech_um_probe(struct sr_serial_dev_inst *serial);
SR_PRIV int rdtech_um_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int rdtech_um_poll(const struct sr_dev_inst *sdi, gboolean force);

#endif
