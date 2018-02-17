/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 mhooijboer <marchelh@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_SIGLENT_SDS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SIGLENT_SDS_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "siglent-sds"

/* Size of acquisition buffers */
//#define ACQ_BUFFER_SIZE (6000000)
#define ACQ_BUFFER_SIZE (18000000)

/* Maximum number of samples to retrieve at once. */
#define ACQ_BLOCK_SIZE (30 * 1000)

#define MAX_ANALOG_CHANNELS 4
#define MAX_DIGITAL_CHANNELS 16

#define DEVICE_STATE_STOPPED  0     /* Scope is in stopped state */
#define DEVICE_STATE_DATA_ACQ 1     /* A new signal has been acquired */
#define DEVICE_STATE_TRIG_RDY 8192  /* Trigger is ready */

enum protocol_version {
	SPO_MODEL,
	NON_SPO_MODEL,
};

enum data_source {
	DATA_SOURCE_SCREEN,
	DATA_SOURCE_HISTORY,
};

struct siglent_sds_vendor {
	const char *name;
	const char *full_name;
};

struct siglent_sds_series {
	const struct siglent_sds_vendor *vendor;
	const char *name;
	enum protocol_version protocol;
	uint64_t max_timebase[2];
	uint64_t min_vdiv[2];
	int num_horizontal_divs;
	int num_vertical_divs;
	int buffer_samples;
};

struct siglent_sds_model {
	const struct siglent_sds_series *series;
	const char *name;
	uint64_t min_timebase[2];
	unsigned int analog_channels;
	bool has_digital;
	unsigned int digital_channels;
};

enum wait_events {
	WAIT_NONE,	/* Don't wait */
	WAIT_TRIGGER,	/* Wait for trigger */
	WAIT_BLOCK,	/* Wait for block data (only when reading sample mem) */
	WAIT_STOP,	/* Wait for scope stopping (only single shots) */
};

struct dev_context {
	/* Device model */
	const struct siglent_sds_model *model;

	/* Device properties */
	const uint64_t (*timebases)[2];
	uint64_t num_timebases;
	const uint64_t (*vdivs)[2];
	uint64_t num_vdivs;

	/* Channel groups */
	struct sr_channel_group **analog_groups;
	struct sr_channel_group *digital_group;

	/* Acquisition settings */
	GSList *enabled_channels;
	uint64_t limit_frames;
	enum data_source data_source;
	uint64_t analog_frame_size;
	uint64_t digital_frame_size;
	uint64_t num_samples;
	long block_header_size;
	float samplerate;

	/* Device settings */
	gboolean analog_channels[MAX_ANALOG_CHANNELS];
	gboolean digital_channels[MAX_DIGITAL_CHANNELS];
	gboolean la_enabled;
	float timebase;
	float attenuation[MAX_ANALOG_CHANNELS];
	float vdiv[MAX_ANALOG_CHANNELS];
	int vert_reference[MAX_ANALOG_CHANNELS];
	float vert_offset[MAX_ANALOG_CHANNELS];
	char *trigger_source;
	float horiz_triggerpos;
	char *trigger_slope;
	float trigger_level;
	char *coupling[MAX_ANALOG_CHANNELS];

	/* Operational state */

	/* Number of frames received in total. */
	uint64_t num_frames;
	/* GSList entry for the current channel. */
	GSList *channel_entry;
	/* Number of bytes received for current channel. */
	uint64_t num_channel_bytes;
	/* Number of bytes of block header read. */
	uint64_t num_header_bytes;
	/* Number of bytes in current data block, if 0 block header expected. */
	uint64_t num_block_bytes;
	/* Number of data block bytes already read. */
	uint64_t num_block_read;
	/* What to wait for in *_receive. */
	enum wait_events wait_event;
	/* Trigger/block copying/stop waiting status. */
	int wait_status;
	/* Acq buffers used for reading from the scope and sending data to app. */
	unsigned char *buffer;
	float *data;
};

SR_PRIV int siglent_sds_config_set(const struct sr_dev_inst *sdi,
	const char *format, ...);
SR_PRIV int siglent_sds_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_channel_start(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_receive(int fd, int revents, void *cb_data);
SR_PRIV int siglent_sds_get_dev_cfg(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_get_dev_cfg_vertical(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_get_dev_cfg_horizontal(const struct sr_dev_inst *sdi);

#endif
