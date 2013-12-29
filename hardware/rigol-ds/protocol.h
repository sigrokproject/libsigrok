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

#define DS1000_ANALOG_LIVE_WAVEFORM_SIZE 600
#define DS2000_ANALOG_LIVE_WAVEFORM_SIZE 1400
#define VS5000_ANALOG_LIVE_WAVEFORM_SIZE 2048
#define DSO1000_ANALOG_LIVE_WAVEFORM_SIZE 600
/* Needs to be made configurable later */
#define DS2000_ANALOG_MEM_WAVEFORM_SIZE_1C 14000
#define DS2000_ANALOG_MEM_WAVEFORM_SIZE_2C 7000
#define DIGITAL_WAVEFORM_SIZE 1210
/* Size of acquisition buffers */
#define ACQ_BUFFER_SIZE 32768

#define MAX_ANALOG_PROBES 4
#define MAX_DIGITAL_PROBES 16

enum rigol_ds_series {
	RIGOL_DS1000,
	RIGOL_DS1000Z,
	RIGOL_DS2000,
	RIGOL_DS4000,
	RIGOL_DS6000,
	RIGOL_VS5000,
	AGILENT_DSO1000,
};

enum rigol_protocol_flavor {
	/* Used by DS1000 and VS5000 series */
	PROTOCOL_LEGACY,
	/* Used by DS2000, DS4000, DS6000, ... series */
	PROTOCOL_IEEE488_2,
};

enum data_source {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
	DATA_SOURCE_SEGMENTED,
};

struct rigol_ds_model {
	char *vendor;
	char *name;
	enum rigol_ds_series series;
	enum rigol_protocol_flavor protocol;
	uint64_t min_timebase[2];
	uint64_t max_timebase[2];
	uint64_t min_vdiv[2];
	unsigned int analog_channels;
	bool has_digital;
	int num_horizontal_divs;
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

	/* Device settings */
	gboolean analog_channels[MAX_ANALOG_PROBES];
	gboolean digital_channels[MAX_DIGITAL_PROBES];
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
	/* Number of samples received in current frame. */
	uint64_t num_frame_samples;
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

SR_PRIV int rigol_ds_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int rigol_ds_channel_start(const struct sr_dev_inst *sdi);
SR_PRIV int rigol_ds_receive(int fd, int revents, void *cb_data);
SR_PRIV int rigol_ds_get_dev_cfg(const struct sr_dev_inst *sdi);

#endif
