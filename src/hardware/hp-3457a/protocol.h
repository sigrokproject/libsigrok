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
};

/* Possible states in an acquisition. */
enum acquisition_state {
	ACQ_TRIGGERED_MEASUREMENT,
	ACQ_REQUESTED_HIRES,
	ACQ_REQUESTED_RANGE,
	ACQ_GOT_MEASUREMENT,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	/* Information about rear card option, or NULL if unknown */
	const struct rear_card_info *rear_card;

	/* Acquisition settings */
	enum sr_mq measurement_mq;
	enum sr_unit measurement_unit;
	uint64_t limit_samples;
	float nplc;

	/* Operational state */
	enum acquisition_state acq_state;
	uint64_t num_samples;
	double base_measurement;
	double hires_register;
	double measurement_range;
};

SR_PRIV const struct rear_card_info *hp_3457a_probe_rear_card(struct sr_scpi_dev_inst *scpi);
SR_PRIV int hp_3457a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int hp_3457a_set_mq(const struct sr_dev_inst *sdi, enum sr_mq mq);
SR_PRIV int hp_3457a_set_nplc(const struct sr_dev_inst *sdi, float nplc);

#endif
