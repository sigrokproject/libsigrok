/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Daniel <1824222@stud.hs-mannheim.de>
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

#ifndef LIBSIGROK_HARDWARE_AGILENT_54621D_PROTOCOL_H
#define LIBSIGROK_HARDWARE_AGILENT_54621D_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "agilent-54621d"
#define MAX_COMMAND_SIZE		128

#define DIGITAL_CHANNELS_PER_POD    8
#define MAX_INSTRUMENT_VERSIONS		10
#define MAX_ANALOG_CHANNEL_COUNT    4
#define MAX_DIGITAL_CHANNEL_COUNT   16
#define MAX_DIGITAL_POD_COUNT       2
#define MAX_DIGITAL_GROUP_COUNT		2


enum data_source {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_MEMORY,
};

struct scope_config {
	const char *name[MAX_INSTRUMENT_VERSIONS];
	const uint8_t analog_channels;
	const uint8_t digital_channels;
	uint8_t digital_pods;

	const char *(*analog_names)[];
	const char *(*digital_names)[];

	const uint32_t (*devopts)[];
	const uint8_t num_devopts;

	const uint32_t (*devopts_cg_analog)[];
	const uint8_t num_devopts_cg_analog;

	const uint32_t (*devopts_cg_digital)[];
	const uint8_t num_devopts_cg_digital;

	const char *(*coupling_options)[];
	const uint8_t num_coupling_options;

	const char *(*logic_threshold)[];
	const uint8_t num_logic_threshold;
	const gboolean logic_threshold_for_pod;

	const char *(*trigger_sources)[];
	const uint8_t num_trigger_sources;

	const char *(*trigger_slopes)[];
	const uint8_t num_trigger_slopes;

	const uint64_t (*timebases)[][2];
	const uint8_t num_timebases;

	const uint64_t (*vdivs)[][2];
	const uint8_t num_vdivs;

	unsigned int num_xdivs;
	const unsigned int num_ydivs;

	const char *(*scpi_dialect)[];
};

struct analog_channel_state {
    int coupling;

    int vdiv;
    float vertical_offset;

    gboolean state;
    char probe_unit;
};

struct analog_channel_transfer_info {
	int yReference;
	float yOrigin;
	float yIncrement;
	gboolean is_unsigned;
	gboolean is_eightbit;
};

struct digital_pod_state {
    gboolean state;

    int threshold;
    float user_threshold;
};

struct scope_state {
	struct analog_channel_state *analog_channels;
	gboolean *digital_channels;
	struct digital_pod_state *digital_pods;

	int timebase;
	float horiz_triggerpos;

	int trigger_source;
	int trigger_slope;
	char trigger_pattern[MAX_ANALOG_CHANNEL_COUNT + MAX_DIGITAL_CHANNEL_COUNT + 1];

	gboolean high_resolution;
	gboolean peak_detection;

	uint64_t sample_rate;
};

struct dev_context {
    const void *model_config;
    void *model_state;

	/* Device properties */
	const float minTimebase;
	const float maxTimebase;

    struct sr_channel_group **analog_groups;
    struct sr_channel_group **digital_groups;

	enum data_source data_source;

    GSList *enabled_channels;
    GSList *current_channel;
    uint64_t num_samples;
    uint64_t num_frames;

    uint64_t samples_limit;
    uint64_t frame_limit;

    size_t pod_count;

	//helper variables for data download
	uint64_t sample_rate_limit;
    GByteArray *buffer;
	GSList *channels_to_download;
	int num_block_to_download;
	int num_blocks_downloaded;
	float block_deltaT;
	float timebaseLbound;
	gboolean headerSent;
	float *data;
	int failcount;
	GByteArray *logic_data;
};

SR_PRIV int agilent_54621d_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int agilent_54621d_request_data(const struct sr_dev_inst *sdi);
SR_PRIV int agilent_54621d_init_device(struct sr_dev_inst *sdi);
SR_PRIV int agilent_54621d_scope_state_get(struct sr_dev_inst *sdi);
SR_PRIV int agilent_54621d_update_sample_rate(const struct sr_dev_inst *sdi);
static struct scope_state *scope_state_new(const struct scope_config *config);

static int analog_channel_state_get(struct sr_dev_inst *sdi, const struct scope_config *config, struct scope_state *state);
static int digital_channel_state_get(struct sr_dev_inst *sdi, const struct scope_config *config, struct scope_state *state);
static int array_float_get(gchar *value, const uint64_t array[][2], int array_len, unsigned int *result);
static void scope_state_dump(const struct scope_config *config, struct scope_state *state);
static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi, const char *command, const char *(*array)[], unsigned int n, int *result);
//static int wait_for_capture_complete(const struct sr_dev_inst *sdi);

#endif
