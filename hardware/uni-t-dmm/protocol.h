/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_HARDWARE_UNI_T_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_DMM_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libusb.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "uni-t-dmm: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define UT_D04_CABLE_USB_VID	0x1a86
#define UT_D04_CABLE_USB_DID	0xe008

#define CHUNK_SIZE		8
#define NUM_DATA_BYTES		14

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The current sampling limit (in ms). */
	uint64_t limit_msec;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received samples. */
	uint64_t num_samples;

	struct sr_usb_dev_inst *usb;

	uint8_t protocol_buf[14];
};

SR_PRIV int uni_t_dmm_receive_data(int fd, int revents, void *cb_data);

#endif
