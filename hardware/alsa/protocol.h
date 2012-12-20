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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <alsa/asoundlib.h>
#include <stdint.h>

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "alsa: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

/** Private, per-device-instance driver context. */
struct dev_context {
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t num_samples;
	uint8_t num_probes;
	const char *hwdev;
	snd_pcm_t *capture_handle;
	snd_pcm_hw_params_t *hw_params;
	struct pollfd *ufds;
	void *cb_data;
};
SR_PRIV GSList *alsa_scan(GSList *options, struct sr_dev_driver *di);
SR_PRIV void alsa_dev_inst_clear(struct sr_dev_inst *sdi);

SR_PRIV int alsa_receive_data(int fd, int revents, void *cb_data);

#endif /* LIBSIGROK_HARDWARE_ALSA_PROTOCOL_H */
