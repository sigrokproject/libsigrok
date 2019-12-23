/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_SERIAL_LCR_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SERIAL_LCR_PROTOCOL_H

#define LOG_PREFIX "serial-lcr"

#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>
#include <stdint.h>
#include <stdlib.h>

struct lcr_info {
	struct sr_dev_driver di;
	const char *vendor;
	const char *model;
	size_t channel_count;
	const char **channel_formats;
	const char *comm;
	size_t packet_size;
	int64_t req_timeout_ms;
	int (*packet_request)(struct sr_serial_dev_inst *serial);
	gboolean (*packet_valid)(const uint8_t *pkt);
	int (*packet_parse)(const uint8_t *pkt, float *value,
		struct sr_datafeed_analog *analog, void *info);
	int (*config_get)(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg);
	int (*config_set)(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg);
	int (*config_list)(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg);
};

#define LCR_BUFSIZE	128

struct dev_context {
	const struct lcr_info *lcr_info;
	struct sr_sw_limits limits;
	uint8_t buf[LCR_BUFSIZE];
	size_t buf_rxpos, buf_rdpos;
	struct lcr_parse_info parse_info;
	uint64_t output_freq;
	const char *circuit_model;
	int64_t req_next_at;
};

SR_PRIV int lcr_receive_data(int fd, int revents, void *cb_data);

#endif
