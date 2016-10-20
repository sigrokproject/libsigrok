/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011-2014 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_CHRONOVU_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_CHRONOVU_LA_PROTOCOL_H

#include <glib.h>
#include <libusb.h>
#include <ftdi.h>
#include <stdint.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "la8/la16"

#define SDRAM_SIZE			(8 * 1024 * 1024)
#define MAX_NUM_SAMPLES			SDRAM_SIZE

#define BS				4096 /* Block size */
#define NUM_BLOCKS			2048 /* Number of blocks */

enum {
	CHRONOVU_LA8,
	CHRONOVU_LA16,
};

struct cv_profile {
	int model;
	const char *modelname;
	const char *iproduct; /* USB iProduct string */
	unsigned int num_channels;
	uint64_t max_samplerate;
	const int num_trigger_matches;
	float trigger_constant;
};

/* Private, per-device-instance driver context. */
struct dev_context {
	/** Device profile struct for this device. */
	const struct cv_profile *prof;

	/** FTDI device context (used by libftdi). */
	struct ftdi_context *ftdic;

	/** The currently configured samplerate of the device. */
	uint64_t cur_samplerate;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/**
	 * A buffer containing some (mangled) samples from the device.
	 * Format: Pretty mangled-up (due to hardware reasons), see code.
	 */
	uint8_t mangled_buf[BS];

	/**
	 * An 8MB buffer where we'll store the de-mangled samples.
	 * LA8: Each sample is 1 byte, MSB is channel 7, LSB is channel 0.
	 * LA16: Each sample is 2 bytes, MSB is channel 15, LSB is channel 0.
	 */
	uint8_t *final_buf;

	/**
	 * Trigger pattern.
	 * A 1 bit matches a high signal, 0 matches a low signal on a channel.
	 *
	 * If the resp. 'trigger_edgemask' bit is set, 1 means "rising edge",
	 * and 0 means "falling edge".
	 */
	uint16_t trigger_pattern;

	/**
	 * Trigger mask.
	 * A 1 bit means "must match trigger_pattern", 0 means "don't care".
	 */
	uint16_t trigger_mask;

	/**
	 * Trigger edge mask.
	 * A 1 bit means "edge triggered", 0 means "state triggered".
	 *
	 * Edge triggering is only supported on LA16 (but not LA8).
	 */
	uint16_t trigger_edgemask;

	/** Tells us whether an SR_DF_TRIGGER packet was already sent. */
	int trigger_found;

	/** Used for keeping track how much time has passed. */
	gint64 done;

	/** Counter/index for the data block to be read. */
	int block_counter;

	/** The divcount value (determines the sample period). */
	uint8_t divcount;

	/** This ChronoVu device's USB VID/PID. */
	uint16_t usb_vid;
	uint16_t usb_pid;

	/** Samplerates supported by this device. */
	uint64_t samplerates[255];
};

/* protocol.c */
extern SR_PRIV const char *cv_channel_names[];
extern const struct cv_profile cv_profiles[];
SR_PRIV void cv_fill_samplerates_if_needed(const struct sr_dev_inst *sdi);
SR_PRIV uint8_t cv_samplerate_to_divcount(const struct sr_dev_inst *sdi,
					  uint64_t samplerate);
SR_PRIV int cv_write(struct dev_context *devc, uint8_t *buf, int size);
SR_PRIV int cv_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int cv_set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate);
SR_PRIV int cv_read_block(struct dev_context *devc);
SR_PRIV void cv_send_block_to_session_bus(const struct sr_dev_inst *sdi, int block);

#endif
