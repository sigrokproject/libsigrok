/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Martin Lederhilger <martin.lederhilger@gmx.at>
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

#ifndef LIBSIGROK_HARDWARE_GWINSTEK_GDS_800_PROTOCOL_H
#define LIBSIGROK_HARDWARE_GWINSTEK_GDS_800_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "gwinstek-gds-800"

#define MAX_SAMPLES 125000
#define MAX_RCV_BUFFER_SIZE (MAX_SAMPLES * 2)

enum gds_state
{
	START_ACQUISITION,
	START_TRANSFER_OF_CHANNEL_DATA,
	WAIT_FOR_TRANSFER_OF_BEGIN_TRANSMISSION_COMPLETE,
	WAIT_FOR_TRANSFER_OF_DATA_SIZE_DIGIT_COMPLETE,
	WAIT_FOR_TRANSFER_OF_DATA_SIZE_COMPLETE,
	WAIT_FOR_TRANSFER_OF_SAMPLE_RATE_COMPLETE,
	WAIT_FOR_TRANSFER_OF_CHANNEL_INDICATOR_COMPLETE,
	WAIT_FOR_TRANSFER_OF_RESERVED_DATA_COMPLETE,
	WAIT_FOR_TRANSFER_OF_CHANNEL_DATA_COMPLETE,
};

struct dev_context {
	enum gds_state state;
	uint64_t cur_acq_frame;
	uint64_t frame_limit;
	int cur_acq_channel;
	int cur_rcv_buffer_position;
	char rcv_buffer[MAX_RCV_BUFFER_SIZE];
	int data_size_digits;
	int data_size;
	float sample_rate;
	gboolean df_started;
};

SR_PRIV int gwinstek_gds_800_receive_data(int fd, int revents, void *cb_data);

#endif
