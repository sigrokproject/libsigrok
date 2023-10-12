/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_JUNTEK_JDS6600_PROTOCOL_H
#define LIBSIGROK_HARDWARE_JUNTEK_JDS6600_PROTOCOL_H

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "juntek-jds6600"

#define MAX_GEN_CHANNELS	2

struct dev_context {
	struct devc_dev {
		unsigned int device_type;
		char *serial_number;
		uint64_t max_output_frequency;
		size_t channel_count_gen;
	} device;
	struct devc_wave {
		size_t builtin_count;
		size_t arbitrary_count;
		size_t names_count;
		const char **names;
		uint32_t *fw_codes;
	} waveforms;
	struct devc_chan {
		gboolean enabled;
		uint32_t waveform_code;
		size_t waveform_index;
		double output_frequency;
		double amplitude;
		double offset;
		double dutycycle;
	} channel_config[MAX_GEN_CHANNELS];
	double channels_phase;
	GString *quick_req;
};

SR_PRIV int jds6600_identify(struct sr_dev_inst *sdi);
SR_PRIV int jds6600_setup_devc(struct sr_dev_inst *sdi);

SR_PRIV int jds6600_get_chans_enable(const struct sr_dev_inst *sdi);
SR_PRIV int jds6600_get_waveform(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_get_frequency(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_get_amplitude(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_get_offset(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_get_dutycycle(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_get_phase_chans(const struct sr_dev_inst *sdi);

SR_PRIV int jds6600_set_chans_enable(const struct sr_dev_inst *sdi);
SR_PRIV int jds6600_set_waveform(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_set_frequency(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_set_amplitude(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_set_offset(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_set_dutycycle(const struct sr_dev_inst *sdi, size_t ch_idx);
SR_PRIV int jds6600_set_phase_chans(const struct sr_dev_inst *sdi);

#endif
