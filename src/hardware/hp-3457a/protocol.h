/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_HP_3457A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HP_3457A_PROTOCOL_H

#include <stdint.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hp-3457a"

/* Information about the rear card option currently installed. */
enum card_type {
	CARD_UNKNOWN,
	REAR_TERMINALS,
	HP_44491A,
	HP_44492A,
};

struct rear_card_info {
	unsigned int card_id;
	enum card_type type;
	const char *name;
	const char *cg_name;
	unsigned int num_channels;
};

/* Possible states in an acquisition. */
enum acquisition_state {
	ACQ_TRIGGERED_MEASUREMENT,
	ACQ_REQUESTED_HIRES,
	ACQ_REQUESTED_RANGE,
	ACQ_GOT_MEASUREMENT,
	ACQ_REQUESTED_CHANNEL_SYNC,
	ACQ_GOT_CHANNEL_SYNC,
};

/* Channel connector (front terminals, or rear card. */
enum channel_conn {
	CONN_FRONT,
	CONN_REAR,
};

struct dev_context {
	/* Information about rear card option, or NULL if unknown */
	const struct rear_card_info *rear_card;

	enum sr_mq measurement_mq;
	enum sr_mqflag measurement_mq_flags;
	enum sr_unit measurement_unit;
	uint64_t limit_samples;
	float nplc;
	GSList *active_channels;
	unsigned int num_active_channels;
	struct sr_channel *current_channel;

	enum acquisition_state acq_state;
	enum channel_conn input_loc;
	uint64_t num_samples;
	double base_measurement;
	double hires_register;
	double measurement_range;
	double last_channel_sync;
};

struct channel_context {
	enum channel_conn location;
	int index;
};

SR_PRIV const struct rear_card_info *hp_3457a_probe_rear_card(struct sr_scpi_dev_inst *scpi);
SR_PRIV int hp_3457a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int hp_3457a_set_mq(const struct sr_dev_inst *sdi, enum sr_mq mq,
			    enum sr_mqflag mq_flags);
SR_PRIV int hp_3457a_set_nplc(const struct sr_dev_inst *sdi, float nplc);
SR_PRIV int hp_3457a_select_input(const struct sr_dev_inst *sdi,
				  enum channel_conn loc);
SR_PRIV int hp_3457a_send_scan_list(const struct sr_dev_inst *sdi,
				    unsigned int *channels, size_t len);

#endif
