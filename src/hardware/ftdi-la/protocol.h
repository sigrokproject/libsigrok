/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Sergey Alirzaev <zl29ah@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_FTID_LA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FTID_LA_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ftdi-la"

/* Number of interfaces on largest supported chip. Used to size arrays of
 * per-interface parameters. */
#define MAX_IFACES 4

struct ftdi_chip_desc {
	uint16_t vendor;
	uint16_t product;

	/* Set to TRUE for chips that expect an interface to be specified for
	 * commands like baud rate selection, even if the specific chip only
	 * has a single one (e.g. FT232H). */
	gboolean multi_iface;
	unsigned int num_ifaces; /* No effect if multi_iface is FALSE. */

	uint32_t base_clock;
	uint32_t bitbang_divisor;
	uint32_t max_sample_rates[MAX_IFACES];

	char *channel_names[]; /* 8 channel names for each interface */
};

struct dev_context {
	const struct ftdi_chip_desc *desc;

	uint16_t usb_iface_idx;
	uint16_t ftdi_iface_idx; /* 1-indexed because FTDI hates us */

	uint16_t in_ep_pkt_size;
	uint8_t in_ep_addr;

	uint32_t cur_samplerate;

	struct libusb_transfer **transfers;
	size_t num_transfers;
	size_t active_transfers;

	uint64_t limit_samples;
	uint64_t samples_sent;
	gboolean acq_aborted;
};

SR_PRIV int ftdi_la_set_samplerate(const struct sr_dev_inst *sdi, uint64_t requested_rate);
SR_PRIV int ftdi_la_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int ftdi_la_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int ftdi_la_stop_acquisition(struct sr_dev_inst *sdi);

#endif
