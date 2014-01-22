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

#define LOG_PREFIX "rigol-ds"

/* Size of acquisition buffers */
#define ACQ_BUFFER_SIZE 32768

#define MAX_ANALOG_PROBES 4
#define MAX_DIGITAL_PROBES 16

enum protocol_version {
	PROTOCOL_V1, /* VS5000 */
	PROTOCOL_V2, /* DS1000 */
	PROTOCOL_V3, /* DS2000, DSO1000 */
};

enum data_format {
	/* Used by DS1000 versions up to 2.02, and VS5000 series */
	FORMAT_RAW,
	/* Used by DS1000 versions from 2.04 onwards and all later series */
	FORMAT_IEEE488_2,
};

enum data_source {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
	DATA_SOURCE_SEGMENTED,
};

struct rigol_ds_vendor {
	const char *name;
	const char *full_name;
};

struct rigol_ds_series {
	const struct rigol_ds_vendor *vendor;
	const char *name;
	enum protocol_version protocol;
	enum data_format format;
	uint64_t max_timebase[2];
	uint64_t min_vdiv[2];
	int num_horizontal_divs;
	int live_samples;
	int buffer_samples;
};

struct rigol_ds_model {
	const struct rigol_ds_series *series;
	const char *name;
	uint64_t min_timebase[2];
	unsigned int analog_channels;
	bool has_digital;
};

enum wait_events {
	WAIT_NONE,    /* Don't wait */
	WAIT_TRIGGER, /* Wait for trigger (only live capture) */
	WAIT_BLOCK,   /* Wait for block data (only when reading sample mem) */
	WAIT_STOP,    /* Wait for scope stopping (only single shots) */
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Device model */
	const struct rigol_ds_model *model;
	enum data_format format;

	/* Device properties */
	const uint64_t (*timebases)[2];
	uint64_t num_timebases;
	const uint64_t (*vdivs)[2];
	uint64_t num_vdivs;

	/* Probe groups */
	struct sr_probe_group analog_groups[MAX_ANALOG_PROBES];
	struct sr_probe_group digital_group;

	/* Acquisition settings */
	GSList *enabled_analog_probes;
	GSList *enabled_digital_probes;
	uint64_t limit_frames;
	void *cb_data;
	enum data_source data_source;
	uint64_t analog_frame_size;
	uint64_t digital_frame_size;

	/* Device settings */
	gboolean analog_channels[MAX_ANALOG_PROBES];
	gboolean digital_channels[MAX_DIGITAL_PROBES];
	gboolean la_enabled;
	float timebase;
	float vdiv[MAX_ANALOG_PROBES];
	int vert_reference[MAX_ANALOG_PROBES];
	float vert_offset[MAX_ANALOG_PROBES];
	char *trigger_source;
	float horiz_triggerpos;
	char *trigger_slope;
	char *coupling[MAX_ANALOG_PROBES];

	/* Operational state */

	/* Number of frames received in total. */
	uint64_t num_frames;
	/* GSList entry for the current channel. */
	GSList *channel_entry;
	/* Number of bytes received for current channel. */
	uint64_t num_channel_bytes;
	/* Number of bytes of block header read */
	uint64_t num_header_bytes;
	/* Number of bytes in current data block, if 0 block header expected */
	uint64_t num_block_bytes;
	/* Number of data block bytes already read */
	uint64_t num_block_read;
	/* What to wait for in *_receive */
	enum wait_events wait_event;
	/* Trigger/block copying/stop waiting status */
	int wait_status;
	/* Acq buffers used for reading from the scope and sending data to app */
	unsigned char *buffer;
	float *data;
};

SR_PRIV int rigol_ds_config_set(const struct sr_dev_inst *sdi, const char *format, ...);
SR_PRIV int rigol_ds_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int rigol_ds_channel_start(const struct sr_dev_inst *sdi);
SR_PRIV int rigol_ds_receive(int fd, int revents, void *cb_data);
SR_PRIV int rigol_ds_get_dev_cfg(const struct sr_dev_inst *sdi);

#endif
