/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Timo Kokkonen <tjko@iki.fi>
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

#ifndef LIBSIGROK_HARDWARE_RIGOL_DG_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RIGOL_DG_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rigol-dg"

/* Device/firmware specific quirks. */
#define RIGOL_DG_COUNTER_BUG           (1UL << 0)
#define RIGOL_DG_COUNTER_CH2_CONFLICT  (1UL << 1)

#define RIGOL_DG_COUNTER_BUG_DELAY (1000 * 1000)

enum psg_commands {
	PSG_CMD_SETUP_REMOTE,
	PSG_CMD_SETUP_LOCAL,
	PSG_CMD_SELECT_CHANNEL,
	PSG_CMD_GET_CHANNEL,
	PSG_CMD_GET_ENABLED,
	PSG_CMD_SET_ENABLE,
	PSG_CMD_SET_DISABLE,
	PSG_CMD_GET_SOURCE,
	PSG_CMD_SET_SOURCE,
	PSG_CMD_SET_FREQUENCY,
	PSG_CMD_GET_FREQUENCY,
	PSG_CMD_SET_AMPLITUDE,
	PSG_CMD_GET_AMPLITUDE,
	PSG_CMD_GET_OFFSET,
	PSG_CMD_SET_OFFSET,
	PSG_CMD_GET_PHASE,
	PSG_CMD_SET_PHASE,
	PSG_CMD_GET_DCYCL_PULSE,
	PSG_CMD_SET_DCYCL_PULSE,
	PSG_CMD_GET_DCYCL_SQUARE,
	PSG_CMD_SET_DCYCL_SQUARE,
	PSG_CMD_COUNTER_GET_ENABLED,
	PSG_CMD_COUNTER_SET_ENABLE,
	PSG_CMD_COUNTER_SET_DISABLE,
	PSG_CMD_COUNTER_MEASURE,
};

enum waveform_type {
	WF_DC = 0,
	WF_SINE,
	WF_SQUARE,
	WF_RAMP,
	WF_PULSE,
	WF_NOISE,
	WF_ARB,
};

enum waveform_options {
	WFO_FREQUENCY = 1,
	WFO_AMPLITUDE = 2,
	WFO_OFFSET = 4,
	WFO_PHASE = 8,
	WFO_DUTY_CYCLE = 16,
};

struct waveform_spec {
	const char *name;
	enum waveform_type waveform;
	double freq_min;
	double freq_max;
	double freq_step;
	uint32_t opts;
};

struct channel_spec {
	const char *name;
	const struct waveform_spec *waveforms;
	uint32_t num_waveforms;
};

struct channel_status {
	enum waveform_type wf;
	const struct waveform_spec *wf_spec;
	double freq;
	double ampl;
	double offset;
	double phase;
};

struct device_spec {
	const char *vendor;
	const char *model;
	const uint32_t *devopts;
	const uint32_t num_devopts;
	const uint32_t *devopts_cg;
	const uint32_t num_devopts_cg;
	const struct channel_spec *channels;
	const uint32_t num_channels;
	const struct scpi_command *cmdset;
};

struct dev_context {
	const struct scpi_command *cmdset;
	const struct device_spec *device;
	struct channel_status *ch_status;
	struct sr_sw_limits limits;
	gboolean counter_enabled;
	uint32_t quirks;
};

SR_PRIV const char *rigol_dg_waveform_to_string(enum waveform_type type);
SR_PRIV const struct waveform_spec *rigol_dg_get_waveform_spec(
		const struct channel_spec *ch, enum waveform_type wf);
SR_PRIV int rigol_dg_get_channel_state(const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg);
SR_PRIV int rigol_dg_receive_data(int fd, int revents, void *cb_data);

#endif
