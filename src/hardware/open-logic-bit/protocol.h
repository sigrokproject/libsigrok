/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2021 Ultra-Embedded <admin@ultra-embedded.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_OPEN_LOGIC_BIT_PROTOCOL_H
#define LIBSIGROK_HARDWARE_OPEN_LOGIC_BIT_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <ftdi.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "openlb"

#define DATA_BUF_SIZE (64 * 1024)

struct dev_context {
	struct ftdi_context *ftdic;
	uint64_t limit_samples;
	uint32_t sample_rate;

	uint64_t num_samples;
	uint16_t *data_buf;
	uint32_t data_pos;

	uint16_t seq_num;

	uint32_t trigger_enable;
	uint32_t trigger_sense;
	uint32_t trigger_level;
};

SR_PRIV int openlb_convert_triggers(const struct sr_dev_inst *sdi);
SR_PRIV int openlb_close(struct dev_context *devc);
SR_PRIV int openlb_start_acquisition(struct dev_context *devc);
SR_PRIV int openlb_receive_data(int fd, int revents, void *cb_data);

#endif
