/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Katherine J. Temkin <k@ktemkin.com>
 * Copyright (C) 2019 Mikaela Szekely <qyriad@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_GREATFET_PROTOCOL_H
#define LIBSIGROK_HARDWARE_GREATFET_PROTOCOL_H

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "greatfet"

struct dev_context {
	struct sr_dev_inst *sdi;
	GString *usb_comm_buffer;
	char *firmware_version;
	char *serial_number;
	size_t channel_count;
	char **channel_names;
	size_t feed_unit_size;
	struct sr_sw_limits sw_limits;
	uint64_t samplerate;
	struct dev_acquisition_t {
		uint64_t bandwidth_threshold;
		size_t wire_unit_size;
		struct feed_queue_logic *feed_queue;
		size_t capture_channels;
		gboolean use_upper_pins;
		size_t channel_shift;
		size_t points_per_byte;
		uint64_t capture_samplerate;
		size_t firmware_bufsize;
		uint8_t samples_endpoint;
		uint8_t control_interface;
		uint8_t samples_interface;
		enum {
			ACQ_IDLE,
			ACQ_PREPARE,
			ACQ_RECEIVE,
			ACQ_SHUTDOWN,
		} acquisition_state;
		gboolean frame_begin_sent;
		gboolean control_interface_claimed;
		gboolean samples_interface_claimed;
		gboolean start_req_sent;
	} acquisition;
	struct dev_transfers_t {
		size_t transfer_bufsize;
		size_t transfers_count;
		uint8_t *transfer_buffer;
		struct libusb_transfer **transfers;
		size_t active_transfers;
		size_t capture_bufsize;
	} transfers;
};

SR_PRIV int greatfet_get_serial_number(const struct sr_dev_inst *sdi);
SR_PRIV int greatfet_get_version_number(const struct sr_dev_inst *sdi);

SR_PRIV int greatfet_setup_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int greatfet_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV void greatfet_abort_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int greatfet_stop_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV void greatfet_release_resources(const struct sr_dev_inst *sdi);

SR_PRIV int greatfet_receive_data(int fd, int revents, void *cb_data);

#endif
