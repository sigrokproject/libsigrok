/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_RIGOL_DS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RIGOL_DS_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "rigol-ds: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

#define DS1000_ANALOG_WAVEFORM_SIZE 600
#define DS2000_ANALOG_WAVEFORM_SIZE 1400
#define DIGITAL_WAVEFORM_SIZE 1210

struct rigol_ds_model {
	char *name;
	unsigned int series;
	uint64_t min_timebase[2];
	uint64_t max_timebase[2];
	uint64_t min_vdiv[2];
	bool has_digital;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Device model */
	const struct rigol_ds_model *model;

	/* Device properties */
	const uint64_t (*timebases)[2];
	uint64_t num_timebases;
	const uint64_t (*vdivs)[2];
	uint64_t num_vdivs;

	/* Probe groups */
	struct sr_probe_group analog_groups[2];
	struct sr_probe_group digital_group;

	/* Acquisition settings */
	GSList *enabled_analog_probes;
	GSList *enabled_digital_probes;
	uint64_t limit_frames;
	void *cb_data;

	/* Device settings */
	gboolean analog_channels[2];
	gboolean digital_channels[16];
	float timebase;
	float vdiv[2];
	int vert_reference[2];
	float vert_offset[2];
	char *trigger_source;
	float horiz_triggerpos;
	char *trigger_slope;
	char *coupling[2];

	/* Operational state */
	uint64_t num_frames;
	/* FIXME: misnomer, actually this is number of frame samples? */
	uint64_t num_frame_bytes;
	struct sr_probe *channel_frame;
	/* Number of bytes in current data block, if 0 block header expected */
	uint64_t num_block_bytes;
	/* Number of data block bytes already read */
	uint64_t num_block_read;
	/* Trigger waiting status, 0 - don't wait */
	int trigger_wait_status;
};

SR_PRIV int rigol_ds2xx2_acquisition_start(const struct sr_dev_inst *sdi, gboolean wait_for_trigger);
SR_PRIV int rigol_ds_receive(int fd, int revents, void *cb_data);
SR_PRIV int rigol_ds_send(const struct sr_dev_inst *sdi, const char *format, ...);
SR_PRIV int rigol_ds_get_dev_cfg(const struct sr_dev_inst *sdi);

#endif
