/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Sean W Jasin <swjasin03@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_ADALM2K_DRIVER_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ADALM2K_DRIVER_PROTOCOL_H

#include <iio/iio.h>
#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "adalm2k-driver"

#define DEFAULT_NUM_LOGIC_CHANNELS               16
#define DEFAULT_NUM_ANALOG_CHANNELS               2
#define MAX_NEG_DELAY                         -8192
#define M2K_LA "m2k-logic-analyzer"
#define M2K_TX "m2k-logic-analyzer-tx"
#define M2K_RX "m2k-logic-analyzer-rx"

// NOTE: Credit to @teoperisanu github https://github.com/teoperisanu/libsigrok/blob/master/src/hardware/adalm2000/protocol.h
struct dev_context {
    struct iio_context *m2k;
    struct iio_channels_mask *mask;
    uint64_t samplerate;
    uint64_t start_time;
	int64_t spent_us;
	uint64_t limit_msec;
	uint64_t limit_frames;
	uint64_t limit_samples;
	uint64_t sent_samples;
	uint64_t buffersize;
	uint32_t logic_unitsize;
	gboolean avg;
	uint64_t avg_samples;
    
    // TODO: Learn what this stuff does 
    struct sr_datafeed_analog packet;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
};

// TODO: And this stuff
SR_PRIV int adalm2k_driver_set_samplerate(const struct sr_dev_inst *sdi);

SR_PRIV int adalm2k_driver_enable_channel(const struct sr_dev_inst *sdi, int index);

SR_PRIV int adalm2k_driver_nb_enabled_channels(const struct sr_dev_inst *sdi, int type);

SR_PRIV int adalm2000_convert_trigger(const struct sr_dev_inst *sdi);

SR_PRIV int adalm2k_driver_receive_data(int fd, int revents, void *cb_data);

/* SR_PRIV uint8_t * adalm2k_driver_get_samples(struct sr_dev_inst *sdi, long samples); */

#endif
