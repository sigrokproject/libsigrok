/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Kevin Matocha <kmatocha@icloud.com>
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

#ifndef LIBSIGROK_HARDWARE_TINY_LOGIC_FRIEND_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TINY_LOGIC_FRIEND_LA_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "tiny-logic-friend-la"

#define TLF_CHANNEL_COUNT_MAX 16 // maximum number of channels allowed
#define TLF_CHANNEL_CHAR_MAX 6   // maximum number of characters for the channel names

#define TRIGGER_MATCHES_COUNT 5 // maximum number of trigger matches

#define RECEIVE_BUFFER_SIZE 4096

/** Private, per-device-instance driver context. */

// dev_context is where all the device specific variables should go that are private
// should hold state variables for the device
//
struct dev_context {
	//const struct tlf_device_model *model_config; // what is this? *** todo

	int channels; // actual number of channels
	char chan_names[TLF_CHANNEL_COUNT_MAX][TLF_CHANNEL_COUNT_MAX + 1];// = { // channel names, initialize with default value

	uint64_t samplerate_range[3]; // sample rate storage: min, max, step size (all in Hz)
	uint64_t cur_samplerate; // currently set sample rate

	uint32_t max_samples; // maximum number of samples the device will measure
	uint32_t cur_samples; // currently set samples to measure

	int32_t trigger_matches[TRIGGER_MATCHES_COUNT];
									// See the list of trigger option constants, used in tlf_trigger_list
									// SR_TRIGGER_ZERO,      "0"
									// SR_TRIGGER_ONE,       "1"
									// SR_TRIGGER_RISING,    "R"
									// SR_TRIGGER_FALLING,   "F"
									// SR_TRIGGER_EDGE,      "E"
	size_t trigger_matches_count;
	gboolean channel_state[TLF_CHANNEL_COUNT_MAX]; // set TRUE for enable, FALSE for disable

	char receive_buffer[RECEIVE_BUFFER_SIZE];
	gboolean data_pending; // state variable if data is pending to be measured

	size_t measured_samples;
	size_t pending_samples;
	size_t num_samples;

	uint16_t last_sample;
	int32_t last_timestamp; // must store -1 for handling the 16-bit timer

	uint16_t * raw_sample_buf;

	int RLE_mode; // set TRUE if uses Run Length Encoding

};


SR_PRIV int tlf_samplerates_list(const struct sr_dev_inst *sdi); // gets sample rates
SR_PRIV int tlf_samplerate_set(const struct sr_dev_inst *sdi, uint64_t sample_rate); // set sample rate
SR_PRIV int tlf_samplerate_get(const struct sr_dev_inst *sdi, uint64_t *sample_rate); // get sample rate

SR_PRIV int tlf_samples_set(const struct sr_dev_inst *sdi, int32_t samples); // set samples count
SR_PRIV int tlf_samples_get(const struct sr_dev_inst *sdi, int32_t *samples); // get samples count
SR_PRIV int tlf_maxsamples_get(const struct sr_dev_inst *sdi); // get max samples, store in device context

SR_PRIV int tlf_channel_state_set(const struct sr_dev_inst *sdi, int32_t channel_index, gboolean enabled); // set channel status
SR_PRIV int tlf_channel_state_get(const struct sr_dev_inst *sdi, int32_t channel_index, gboolean *enabled); // get channel status
SR_PRIV int tlf_channels_list(struct sr_dev_inst *sdi); // gets channel names from device

SR_PRIV int tlf_trigger_list(const struct sr_dev_inst *sdi); // get list of trigger options
SR_PRIV int tlf_trigger_set(const struct sr_dev_inst *sdi, int32_t channel_index, char * trigger); // set trigger

SR_PRIV int tlf_RLE_mode_get(const struct sr_dev_inst *sdi); // get data mode, Run Length Encoded or Clock

SR_PRIV int tlf_exec_run(const struct sr_dev_inst *sdi); // start measurement
SR_PRIV int tlf_exec_stop(const struct sr_dev_inst *sdi); // stop measurement

SR_PRIV int tlf_receive_data(int fd, int revents, void *cb_data);

#endif
