/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Aleksandr Orlov <orlovaleksandr7922@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_MY_OSC_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MY_OSC_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "my-osc"

#define BUFSIZE 100

//Commands
#define CMD_SCAN 0x00
#define CMD_START 0x01
#define CMD_STOP 0x02
#define CMD_RESET 0x03
#define CMD_SET_SAMPLE_RATE 0x04
#define CMD_SET_LIMIT_SAMPLES 0x05

//Limit sample range
#define MIN_NUM_SAMPLES 100
#define MAX_NUM_SAMPLES 1000000

struct dev_context {
    struct sr_sw_limits limits;
    uint64_t cur_samplerate;

    char buf[BUFSIZE];
	int buflen;

	float voltage;
	float current;

    /* GSList entry for the current channel. */
	GSList *channel_entry;

};

SR_PRIV int my_osc_receive_data(int fd, int revents, void *cb_data);

//Set parameters
void my_osc_osc_set_samplerate(struct sr_serial_dev_inst *serial, uint64_t val);
void my_osc_set_limit_samples(struct sr_serial_dev_inst *serial, uint64_t val);

#endif
