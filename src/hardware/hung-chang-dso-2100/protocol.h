/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Daniel Glöckner <daniel-gl@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_HUNG_CHANG_DSO_2100_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HUNG_CHANG_DSO_2100_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hung-chang-dso-2100"

#define MAX_RETRIES 4
#define NUM_CHANNELS 2

struct dev_context {
	GSList *enabled_channel;
	uint8_t channel;
	uint8_t rate;
	uint8_t cctl[2];
	uint8_t edge;
	uint8_t tlevel;
	uint8_t pos[2];
	uint8_t offset[2];
	uint8_t gain[2];

	uint64_t frame_limit;
	uint64_t frame;
	uint64_t probe[2];
	uint8_t step;
	uint8_t last_step;
	uint8_t retries;
	gboolean adc2;

	float *samples;
	float factor;
	gboolean state_known;
};

SR_PRIV void hung_chang_dso_2100_reset_port(struct parport *port);
SR_PRIV gboolean hung_chang_dso_2100_check_id(struct parport *port);
SR_PRIV void hung_chang_dso_2100_write_mbox(struct parport *port, uint8_t val);
SR_PRIV uint8_t hung_chang_dso_2100_read_mbox(struct parport *port, float timeout);
SR_PRIV int hung_chang_dso_2100_move_to(const struct sr_dev_inst *sdi, uint8_t target);
SR_PRIV int hung_chang_dso_2100_poll(int fd, int revents, void *cb_data);

#endif
