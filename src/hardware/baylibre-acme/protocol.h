/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Bartosz Golaszewski <bgolaszewski@baylibre.com>
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

#ifndef LIBSIGROK_HARDWARE_BAYLIBRE_ACME_PROTOCOL_H
#define LIBSIGROK_HARDWARE_BAYLIBRE_ACME_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "baylibre-acme"

/* We support up to 8 energy/temperature probes. */
#define MAX_PROBES		8

/*
 * Temperature probes can be connected to the last four ports on the
 * ACME cape. When scanning, first look for temperature probes starting
 * from this index.
 */
#define TEMP_PRB_START_INDEX	4

#define ENRG_PROBE_NAME		"ina226"
#define TEMP_PROBE_NAME		"tmp435"

/* For the user we number the probes starting from 1. */
#define PROBE_NUM(n) ((n) + 1)

enum probe_type {
	PROBE_ENRG = 1,
	PROBE_TEMP,
};

enum channel_type {
	ENRG_PWR = 1,
	ENRG_CURR,
	ENRG_VOL,
	TEMP_IN,
	TEMP_OUT,
};

/** Private, per-device-instance driver context. */
struct dev_context {
	uint64_t samplerate;
	uint64_t limit_samples;
	uint64_t limit_msec;

	uint32_t num_channels;
	uint64_t samples_read;
	uint64_t samples_missed;
	int64_t start_time;
	int64_t last_sample_fin;
	int timer_fd;
	GIOChannel *channel;
};

SR_PRIV uint8_t bl_acme_get_enrg_addr(int index);
SR_PRIV uint8_t bl_acme_get_temp_addr(int index);

SR_PRIV gboolean bl_acme_is_sane(void);

SR_PRIV gboolean bl_acme_detect_probe(unsigned int addr,
				      int prb_num, const char *prb_name);
SR_PRIV gboolean bl_acme_register_probe(struct sr_dev_inst *sdi, int type,
					unsigned int addr, int prb_num);

SR_PRIV int bl_acme_get_probe_type(const struct sr_channel_group *cg);
SR_PRIV int bl_acme_probe_has_pws(const struct sr_channel_group *cg);

SR_PRIV void bl_acme_maybe_set_update_interval(const struct sr_dev_inst *sdi,
					       uint64_t samplerate);

SR_PRIV int bl_acme_get_shunt(const struct sr_channel_group *cg,
			      uint64_t *shunt);
SR_PRIV int bl_acme_set_shunt(const struct sr_channel_group *cg,
			      uint64_t shunt);
SR_PRIV int bl_acme_read_power_state(const struct sr_channel_group *cg,
				     gboolean *off);
SR_PRIV int bl_acme_set_power_off(const struct sr_channel_group *cg,
				  gboolean off);

SR_PRIV int bl_acme_receive_data(int fd, int revents, void *cb_data);

SR_PRIV int bl_acme_open_channel(struct sr_channel *ch);

SR_PRIV void bl_acme_close_channel(struct sr_channel *ch);
#endif
