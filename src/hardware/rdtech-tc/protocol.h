/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Andreas Sandberg <andreas@sandberg.pp.se>
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

#ifndef LIBSIGROK_HARDWARE_RDTECH_TC_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RDTECH_TC_PROTOCOL_H

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rdtech-tc"

#define RDTECH_TC_BUFSIZE 256

struct rdtech_dev_info {
	char *model_name;
	char *fw_ver;
	uint32_t serial_num;
};

struct dev_context {
	struct rdtech_dev_info dev_info;
	const struct binary_analog_channel *channels;
	struct sr_sw_limits limits;

	uint8_t buf[RDTECH_TC_BUFSIZE];
	int buflen;
	int64_t cmd_sent_at;
};

SR_PRIV int rdtech_tc_probe(struct sr_serial_dev_inst *serial, struct dev_context  *devc);
SR_PRIV int rdtech_tc_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int rdtech_tc_poll(const struct sr_dev_inst *sdi);

#endif
