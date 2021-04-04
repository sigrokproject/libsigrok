/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 AlexEagleC <orlovaleksandr7922@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_EXAMPLE_PROTOCOL_H
#define LIBSIGROK_HARDWARE_EXAMPLE_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "example"

#define CMD_CONF 0x00
#define CMD_SCAN 0x01
#define CMD_START 0x02

#define MAX_CHANNELS 2
#define BUFSIZE 100

struct dev_context {
    struct sr_sw_limits limits;
    int data_source;
    uint64_t cur_samplerate;
    int cur_mq[MAX_CHANNELS];

    char buf[BUFSIZE];
	int buflen;

    GMutex acquisition_mutex;

	float current_limit;
	float voltage;
	float current;
};

SR_PRIV int my_dmm_receive_data(int fd, int revents, void *cb_data);

#endif
