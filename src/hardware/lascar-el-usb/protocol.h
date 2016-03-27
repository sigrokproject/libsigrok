/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_LASCAR_EL_USB_PROTOCOL_H
#define LIBSIGROK_HARDWARE_LASCAR_EL_USB_PROTOCOL_H

#include <stdint.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "lascar-el-usb"

#define LASCAR_VENDOR "Lascar"
#define LASCAR_INTERFACE 0
#define LASCAR_EP_IN 0x82
#define LASCAR_EP_OUT 2
#define MAX_CONFIGBLOCK_SIZE 256

/* Max 100ms for a device to positively identify. */
#define SCAN_TIMEOUT (100 * 1000)
#define BULK_XFER_TIMEOUT (10 * 1000)
#define EVENTS_TIMEOUT (10 * 1000)
#define SLEEP_US_LONG (5 * 1000)
#define SLEEP_US_SHORT (1 * 1000)

/** Private, per-device-instance driver context. */
struct dev_context {
	const struct elusb_profile *profile;
	/* Generic EL-USB */
	unsigned char config[MAX_CONFIGBLOCK_SIZE];
	unsigned int log_size;
	unsigned int rcvd_bytes;
	unsigned int sample_size;
	unsigned int logged_samples;
	unsigned int rcvd_samples;
	uint64_t limit_samples;
	/* Model-specific */
	/* EL-USB-CO: these are something like scaling and calibration values
	 * fixed per device, used to convert the sample values to CO ppm. */
	float co_high;
	float co_low;
	/* Temperature units as stored in the device config. */
	int temp_unit;
};

enum {
	LOG_UNSUPPORTED,
	LOG_TEMP_RH,
	LOG_CO,
};

struct elusb_profile {
	int modelid;
	const char *modelname;
	int logformat;
};

SR_PRIV int lascar_get_config(libusb_device_handle *dev_hdl,
		unsigned char *configblock, int *configlen);
SR_PRIV struct sr_dev_inst *lascar_scan(int bus, int address);
SR_PRIV int lascar_el_usb_handle_events(int fd, int revents, void *cb_data);
SR_PRIV void LIBUSB_CALL lascar_el_usb_receive_transfer(struct libusb_transfer *transfer);
SR_PRIV int lascar_start_logging(const struct sr_dev_inst *sdi);
SR_PRIV int lascar_stop_logging(const struct sr_dev_inst *sdi);
SR_PRIV int lascar_is_logging(const struct sr_dev_inst *sdi);
SR_PRIV int dev_acquisition_stop(struct sr_dev_inst *sdi);

#endif
