/*
 * This file is part of the sigrok project.
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


#ifndef LIBSIGROK_GENERICDMM_H
#define LIBSIGROK_GENERICDMM_H

/* SR_HWCAP_CONN takes one of these: */
#define DMM_CONN_USB_VIDPID    "^([0--9a-z]{1,4}):([0--9a-z]{1,4})$"
#define DMM_CONN_USB_BUSADDR   "^(\\d+)\\.(\\d+)$"
#define DMM_CONN_SERIALPORT    "^([a-z0-9/\\-_]+)$"

/* SR_HWCAP_SERIALCOMM like 2400/8n1 */
#define DMM_CONN_SERIALCOMM    "^(\\d+)/(\\d)([neo])(\\d)$"

enum {
	DMM_TRANSPORT_SERIAL,
	DMM_TRANSPORT_USBHID,
};


struct dev_profile {
	char *modelid;
	char *vendor;
	char *model;
	struct dmmchip *chip;
	/* Only use when the VID:PID is really specific to a DMM. */
	uint16_t vid;
	uint16_t pid;
	int transport;
};

struct context {
	struct dev_profile *profile;
	uint64_t limit_samples;
	uint64_t limit_msec;

	/* Opaque pointer passed in by the frontend. */
	void *cb_data;

	/* Only used for USB-connected devices. */
	struct sr_usb_dev_inst *usb;

	/* Only used for serial-connected devices. */
	struct sr_serial_dev_inst *serial;
	int serial_speed;
	int serial_databits;
	int serial_parity;
	int serial_stopbits;

	/* Runtime. */
	uint64_t num_samples;

	/* DMM chip-specific data, if needed. */
	void *priv;
};

struct dmmchip {
	/* Optional, called once before measurement starts. */
	int (*init) (struct context *ctx);
	/* Called whenever a chunk of data arrives. */
	int (*data) (struct context *ctx, unsigned char *data);
};


#endif /* LIBSIGROK_GENERICDMM_H */
