/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_ASIX_OMEGA_RTM_CLI_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ASIX_OMEGA_RTM_CLI_PROTOCOL_H

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "asix-omega-rtm-cli"

#define RTMCLI_STDOUT_CHUNKSIZE (1024 * 1024)
#define FEED_QUEUE_DEPTH (256 * 1024)

struct dev_context {
	struct sr_sw_limits limits;
	struct {
		gchar **argv;
		GSpawnFlags flags;
		gboolean running;
		GPid pid;
		gint fd_stdin_write;
		gint fd_stdout_read;
	} child;
	struct {
		uint8_t buff[RTMCLI_STDOUT_CHUNKSIZE];
		size_t fill;
	} rawdata;
	struct {
		struct feed_queue_logic *queue;
		uint8_t last_sample[sizeof(uint16_t)];
		uint64_t remain_count;
		gboolean check_count;
	} samples;
};

SR_PRIV int omega_rtm_cli_open(const struct sr_dev_inst *sdi);
SR_PRIV int omega_rtm_cli_close(const struct sr_dev_inst *sdi);
SR_PRIV int omega_rtm_cli_receive_data(int fd, int revents, void *cb_data);

#endif
