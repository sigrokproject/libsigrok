/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Patrick Plenefisch <simonpatp@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_TEKTRONIX_TDS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TEKTRONIX_TDS_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "tektronix-tds"

/* Mostly for general information, but also used for debug messages */
enum bandwidth
{
	BW_25MHz = 25,
	BW_30MHz = 30,
	BW_40MHz = 40,
	BW_45MHz = 45,
	BW_50MHz = 50,
	BW_60MHz = 60,
	BW_70MHz = 70,
	BW_100MHz = 100,
	BW_150MHz = 150,
	BW_200MHz = 200,
};

enum samplerate
{
	SA_500M = 500,
	SA_1G = 1000,
	SA_2G = 2000,
};

/* Describes model-specific features. See DEVICE_SPEC() macro in api.c for the
 * "constructor" */
struct device_spec {
	const char *model;
	int channels;

	enum samplerate sample_rate;
	enum bandwidth bandwidth;

	const uint64_t *probe_factors;
	int num_probe_factors;

	int timebase_start;
	int timebase_stop;

	int voltrange_start;
	int voltrange_stop;

	const char **trigger_sources;
	int num_trigger_sources;
};

/* Values that are the same for all models */
#define TEK_BUFFER_SIZE 2500
// all scopes have -5 to +5 hdivs
// and -4 to +4 vdivs
#define TEK_NUM_HDIV 10
#define TEK_NUM_VDIV 8
#define MAX_ANALOG_CHANNELS 4

/* Wave data information */

enum TEK_DATA_ENCODING
{
	ENC_ASCII,
	ENC_BINARY
};

enum TEK_DATA_FORMAT
{
	FMT_RI,
	FMT_RP
};

enum TEK_DATA_ORDERING
{
	ORDER_LSB,
	ORDER_MSB
};

enum TEK_POINT_FORMAT
{
	PT_FMT_ENV,
	PT_FMT_Y
};

enum TEK_X_UNITS
{
	XU_SECOND,
	XU_HZ
};

enum TEK_Y_UNITS
{
	YU_UNKNOWN,
	YU_UNKNOWN_MASK,
	YU_VOLTS,
	YU_DECIBELS,

	// TBS1000B/EDU, TBS1000, TDS2000C, TDS1000C-EDU, TDS2000B,
	// TDS1000B, TPS2000B, and TPS2000 Series only:
	YU_AMPS,
	YU_VV,
	YU_VA,
	YU_AA
};

struct most_recent_wave_preamble {
	// Xn = XZEro + XINcr (n - PT_OFf)
	float x_zero; // (in xunis)
	float x_incr; // seconds per point or herts per point
	enum TEK_X_UNITS x_unit;

	// value_in_YUNits = ((curve_in_dl - YOFF_in_dl) * YMUlt) +
	// YZERO_in_YUNits
	float y_mult; // (in yunits)
	float y_off; // (in digitizer levels)
	float y_zero; // (in yunits)
	enum TEK_Y_UNITS y_unit;

	int num_pts;
};

enum DRIVER_CAPTURE_MODE
{
	CAPTURE_LIVE, // reset trigger, re-enable at end
	CAPTURE_ONE_SHOT, // reset trigger, no clear
	CAPTURE_DISPLAY, // no reset, re-enable at end
	CAPTURE_MEMORY, // no reset, no clear
};

enum wait_events
{
	WAIT_CAPTURE,
	WAIT_CHANNEL,
	WAIT_DONE,
};

struct dev_context {
	/* Core information */
	struct sr_channel_group **analog_groups;
	const struct device_spec *model;

	/* Current & configured channel settings */
	gboolean analog_channels[MAX_ANALOG_CHANNELS];
	float vdiv[MAX_ANALOG_CHANNELS];
	float vert_offset[MAX_ANALOG_CHANNELS];
	float attenuation[MAX_ANALOG_CHANNELS];
	char *coupling[MAX_ANALOG_CHANNELS];

	/* Current & configured device settings */
	float timebase;
	char *trigger_source;
	float horiz_triggerpos;
	char *trigger_slope;
	float trigger_level;

	/* Current & configured acquisition settings */
	gboolean average_enabled;
	int average_samples;
	gboolean peak_enabled;
	enum DRIVER_CAPTURE_MODE capture_mode;

	/* Acquisition state */
	enum wait_events acquire_status;
	struct most_recent_wave_preamble wavepre;
	gboolean prior_state_running;
	gboolean prior_state_single;

	uint64_t limit_frames;
	uint64_t num_frames;
	GSList *enabled_channels;
	GSList *channel_entry;

	/* Acq buffers used for reading from the scope and sending data to app.
	 */
	unsigned char *buffer;
	int num_block_read;
};

SR_PRIV int tektronix_tds_config_set(
	const struct sr_dev_inst *sdi, const char *format, ...);
SR_PRIV int tektronix_tds_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds_channel_start(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds_capture_finish(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds_receive(int fd, int revents, void *cb_data);
SR_PRIV int tektronix_tds_get_dev_cfg(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds_get_dev_cfg_vertical(const struct sr_dev_inst *sdi);
SR_PRIV int tektronix_tds_get_dev_cfg_horizontal(const struct sr_dev_inst *sdi);

#endif
