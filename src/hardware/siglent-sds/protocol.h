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

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "siglent-sds"

/* Size of acquisition buffers */
//#define ACQ_BUFFER_SIZE (6000000)
#define ACQ_BUFFER_SIZE (18000000)

#define SIGLENT_HEADER_SIZE 363
#define SIGLENT_DIG_HEADER_SIZE 346

/* Maximum number of samples to retrieve at once. */
#define ACQ_BLOCK_SIZE (30 * 1000)

#define MAX_ANALOG_CHANNELS 4
#define MAX_DIGITAL_CHANNELS 16

#define DEVICE_STATE_STOPPED 0		/* Scope is in stopped state bit */
#define DEVICE_STATE_DATA_ACQ 1		/* A new signal has been acquired bit */
#define DEVICE_STATE_TRIG_RDY 8192	/* Trigger is ready bit */
#define DEVICE_STATE_DATA_TRIG_RDY 8193	/* Trigger is ready bit */

enum protocol_version {
	SPO_MODEL = 10000,
	NON_SPO_MODEL,
	ESERIES,
};

enum data_source {
	DATA_SOURCE_SCREEN = 10000,
	DATA_SOURCE_HISTORY,
	DATA_SOURCE_SINGLE,
	DATA_SOURCE_READ_ONLY,
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
	gboolean has_digital;
	unsigned int digital_channels;
};

enum wait_events {
	WAIT_NONE = 10000,	/* Don't wait */
	WAIT_TRIGGER,		/* Wait for trigger */
	WAIT_BLOCK,		/* Wait for block data (only when reading sample mem) */
	WAIT_STOP,		/* Wait for scope stopping (only single shots) */
	WAIT_INIT		/* Wait for waveform request init */
};

enum acq_states {
	ACQ_SETUP = 10000,	   /* Perform necessary setup SCPI commands before waveform read */
	ACQ_WAIT_STOP,             /* Wait for device to stop */
	ACQ_REQUEST_WAVEFORM,      /* Request the waveform, prepare SCPI read */
	ACQ_READ_WAVEFORM_HEADER,  /* Read and parse the header */
	ACQ_READ_WAVEFORM_DATA,    /* Read the waveform data */
	ACQ_FINALIZE_WAVEFORM,
	ACQ_DONE,	           /* All done */
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
	uint64_t average_samples;
	gboolean average_enabled;
	enum data_source data_source;
	uint64_t analog_frame_size;
	uint64_t digital_frame_size;
	uint64_t num_samples;
	uint64_t memory_depth_analog;
	uint64_t memory_depth_digital;
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
	/* Number of data blocks bytes already read. */
	uint64_t num_block_bytes;
	/* Number of data blocks read. */
	int num_block_read;
	/* What to wait for in *_receive. */
	enum wait_events wait_event;
	/* Trigger/block copying/stop waiting status. */
	int wait_status;
	/* Acq buffers used for reading from the scope and sending data to app. */
	unsigned char *buffer;
	float *data;
	GArray *dig_buffer;

	/* Eseries specific */
	enum acq_states acq_state;
	/* Close history after acquistion is done if history was not open when acqusition started  */
	gboolean close_history;
	/* General retry counter to allow bailing out of infinite loops */
	int retry_count;
	gboolean acq_error;
};

SR_PRIV int siglent_sds_config_set(const struct sr_dev_inst *sdi,
	const char *format, ...);
SR_PRIV int siglent_sds_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_channel_start(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_receive(int fd, int revents, void *cb_data);
SR_PRIV int siglent_sds_get_dev_cfg(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_get_dev_cfg_vertical(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_get_dev_cfg_horizontal(const struct sr_dev_inst *sdi);

SR_PRIV int siglent_sds_eseries_receive(int fd, int revents, void *cb_data);
SR_PRIV int siglent_sds_eseries_acq_setup(const struct sr_dev_inst *sdi, struct dev_context *devc);
SR_PRIV int siglent_sds_eseries_acq_wait_stop(const struct sr_dev_inst *sdi, struct dev_context *devc);
SR_PRIV int siglent_sds_eseries_acq_request_waveform(const struct sr_dev_inst *sdi, struct dev_context *devc);
SR_PRIV int siglent_sds_eseries_acq_read_waveform_header(const struct sr_dev_inst *sdi, struct dev_context *devc);
SR_PRIV int siglent_sds_eseries_acq_read_waveform_data(const struct sr_dev_inst *sdi, struct dev_context *devc);
SR_PRIV int siglent_sds_eseries_send_waveform(const struct sr_dev_inst *sdi, struct dev_context *devc, guint bytes_read);
SR_PRIV int siglent_sds_eseries_acq_finalize_waveform(const struct sr_dev_inst *sdi, struct dev_context *devc);
SR_PRIV int siglent_sds_eseries_stop_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int siglent_sds_eseries_acq_error(struct dev_context *devc);
SR_PRIV int siglent_sds_flush_buffers(const struct sr_dev_inst *sdi);

#endif
