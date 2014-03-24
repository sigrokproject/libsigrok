/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_HARDWARE_ALSA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ALSA_PROTOCOL_H

#include <stdint.h>
#include <alsa/asoundlib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "alsa"

/** Private, per-device-instance driver context. */
struct dev_context {
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t num_samples;
	uint8_t num_channels;
	uint64_t *samplerates;
	char *hwdev;
	snd_pcm_t *capture_handle;
	snd_pcm_hw_params_t *hw_params;
	struct pollfd *ufds;
	void *cb_data;
};

SR_PRIV GSList *alsa_scan(GSList *options, struct sr_dev_driver *di);
SR_PRIV int alsa_set_samplerate(const struct sr_dev_inst *sdi,
				uint64_t newrate);
SR_PRIV int alsa_receive_data(int fd, int revents, void *cb_data);

#endif
