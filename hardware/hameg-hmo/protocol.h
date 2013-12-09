/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_HAMEG_HMO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HAMEG_HMO_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "hameg-hmo: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

#define MAX_INSTRUMENT_VERSIONS 10
#define MAX_COMMAND_SIZE 31

struct scope_config {
	const char *name[MAX_INSTRUMENT_VERSIONS];
	const uint8_t analog_channels;
	const uint8_t digital_channels;
	const uint8_t digital_pods;

	const char *(*analog_names)[];
	const char *(*digital_names)[];

	const int32_t (*hw_caps)[];
	const uint8_t num_hwcaps;

	const int32_t (*analog_hwcaps)[];
	const uint8_t num_analog_hwcaps;

	const char *(*coupling_options)[];
	const uint8_t num_coupling_options;

	const char *(*trigger_sources)[];
	const uint8_t num_trigger_sources;

	const char *(*trigger_slopes)[];

	const uint64_t (*timebases)[][2];
	const uint8_t num_timebases;

	const uint64_t (*vdivs)[][2];
	const uint8_t num_vdivs;

	const uint8_t num_xdivs;
	const uint8_t num_ydivs;

	const char *(*scpi_dialect)[];
};

struct analog_channel_state {
	int coupling;

	float vdiv;
	float vertical_offset;

	gboolean state;
};

struct scope_state {
	struct analog_channel_state *analog_channels;
	gboolean *digital_channels;
	gboolean *digital_pods;

	float timebase;
	float horiz_triggerpos;

	int trigger_source;
	int trigger_slope;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	void *model_config;
	void *model_state;

	struct sr_probe_group *analog_groups;
	struct sr_probe_group *digital_groups;

	GSList *enabled_probes;
	GSList *current_probe;
	uint64_t num_frames;

	uint64_t frame_limit;
};

SR_PRIV int hmo_init_device(struct sr_dev_inst *sdi);
SR_PRIV int hmo_request_data(const struct sr_dev_inst *sdi);
SR_PRIV int hmo_receive_data(int fd, int revents, void *cb_data);

SR_PRIV struct scope_state *hmo_scope_state_new(struct scope_config *config);
SR_PRIV void hmo_scope_state_free(struct scope_state *state);
SR_PRIV int hmo_scope_state_get(struct sr_dev_inst *sdi);

#endif
