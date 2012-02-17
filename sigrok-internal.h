/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_SIGROK_INTERNAL_H
#define LIBSIGROK_SIGROK_INTERNAL_H

#include <stdarg.h>
#include <glib.h>
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#ifdef HAVE_LIBUSB_1_0
#include <libusb.h>
#endif

/*--- Macros ----------------------------------------------------------------*/

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ARRAY_AND_SIZE
#define ARRAY_AND_SIZE(a) (a), ARRAY_SIZE(a)
#endif

/* Size of a datastore chunk in units */
#define DATASTORE_CHUNKSIZE (512 * 1024)

#ifdef HAVE_LIBUSB_1_0
struct sr_usb_dev_inst {
	uint8_t bus;
	uint8_t address;
	struct libusb_device_handle *devhdl;
};
#endif

struct sr_serial_dev_inst {
	char *port;
	int fd;
};

#ifdef HAVE_LIBUSB_1_0
/* USB-specific instances */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb);
#endif

/* Serial-specific instances */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(
					const char *port, int fd);
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial);

/*--- log.c -----------------------------------------------------------------*/

SR_PRIV int sr_log(int loglevel, const char *format, ...);
SR_PRIV int sr_spew(const char *format, ...);
SR_PRIV int sr_dbg(const char *format, ...);
SR_PRIV int sr_info(const char *format, ...);
SR_PRIV int sr_warn(const char *format, ...);
SR_PRIV int sr_err(const char *format, ...);

/*--- hwplugin.c ------------------------------------------------------------*/

SR_PRIV int sr_hw_load_all(void);
SR_PRIV void sr_hw_cleanup_all(void);

/*--- session.c -------------------------------------------------------------*/

SR_PRIV int sr_session_bus(struct sr_dev *dev,
			   struct sr_datafeed_packet *packet);

/* Generic device instances */
SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version);
SR_PRIV struct sr_dev_inst *sr_dev_inst_get(GSList *dev_insts, int dev_index);
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi);

SR_PRIV void sr_source_remove(int fd);
SR_PRIV void sr_source_add(int fd, int events, int timeout,
			   sr_receive_data_callback rcv_cb, void *user_data);

/*--- hardware/common/serial.c ----------------------------------------------*/

SR_PRIV GSList *list_serial_ports(void);
SR_PRIV int serial_open(const char *pathname, int flags);
SR_PRIV int serial_close(int fd);
SR_PRIV int serial_flush(int fd);
SR_PRIV int serial_write(int fd, const void *buf, size_t count);
SR_PRIV int serial_read(int fd, void *buf, size_t count);
SR_PRIV void *serial_backup_params(int fd);
SR_PRIV void serial_restore_params(int fd, void *backup);
SR_PRIV int serial_set_params(int fd, int speed, int bits, int parity,
			      int stopbits, int flowcontrol);

/*--- hardware/common/ezusb.c -----------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
SR_PRIV int ezusb_install_firmware(libusb_device_handle *hdl,
				   const char *filename);
SR_PRIV int ezusb_upload_firmware(libusb_device *dev, int configuration,
				  const char *filename);
#endif

/*--- hardware/common/misc.c ------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
SR_PRIV int opendev2(int dev_index, struct sr_dev_inst **sdi,
		     libusb_device *dev, struct libusb_device_descriptor *des,
		     int *skip, uint16_t vid, uint16_t pid, int interface);
SR_PRIV int opendev3(struct sr_dev_inst **sdi, libusb_device *dev,
		     struct libusb_device_descriptor *des,
		     uint16_t vid, uint16_t pid, int interface);
#endif

#endif
