/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Bert Vermeulen <bert@biot.com>
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

#ifndef SIGROK_SIGROK_INTERNAL_H
#define SIGROK_SIGROK_INTERNAL_H

#include <stdarg.h>
#include <glib.h>
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

/*--- hwplugin.c ------------------------------------------------------------*/

int load_hwplugins(void);

#ifdef HAVE_LIBUSB_1_0
struct sr_usb_device_instance {
	uint8_t bus;
	uint8_t address;
	struct libusb_device_handle *devhdl;
};
#endif

struct sr_serial_device_instance {
	char *port;
	int fd;
};

#ifdef HAVE_LIBUSB_1_0
/* USB-specific instances */
struct sr_usb_device_instance *sr_usb_device_instance_new(uint8_t bus,
		uint8_t address, struct libusb_device_handle *hdl);
void sr_usb_device_instance_free(struct sr_usb_device_instance *usb);
#endif

/* Serial-specific instances */
struct sr_serial_device_instance *sr_serial_device_instance_new(
					const char *port, int fd);
void sr_serial_device_instance_free(struct sr_serial_device_instance *serial);

/*--- log.c -----------------------------------------------------------------*/

int sr_log(int loglevel, const char *format, ...);
int sr_spew(const char *format, ...);
int sr_dbg(const char *format, ...);
int sr_info(const char *format, ...);
int sr_warn(const char *format, ...);
int sr_err(const char *format, ...);

/*--- hardware/common/serial.c ----------------------------------------------*/

GSList *list_serial_ports(void);
int serial_open(const char *pathname, int flags);
int serial_close(int fd);
int serial_flush(int fd);
int serial_write(int fd, const void *buf, size_t count);
int serial_read(int fd, void *buf, size_t count);
void *serial_backup_params(int fd);
void serial_restore_params(int fd, void *backup);
int serial_set_params(int fd, int speed, int bits, int parity, int stopbits,
		      int flowcontrol);

/*--- hardware/common/ezusb.c -----------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
int ezusb_reset(struct libusb_device_handle *hdl, int set_clear);
int ezusb_install_firmware(libusb_device_handle *hdl, const char *filename);
int ezusb_upload_firmware(libusb_device *dev, int configuration,
			  const char *filename);
#endif

/*--- hardware/common/misc.c ------------------------------------------------*/

#ifdef HAVE_LIBUSB_1_0
int opendev2(int device_index, struct sr_device_instance **sdi,
	     libusb_device *dev, struct libusb_device_descriptor *des,
	     int *skip, uint16_t vid, uint16_t pid, int interface);
int opendev3(struct sr_device_instance **sdi, libusb_device *dev,
	     struct libusb_device_descriptor *des,
	     uint16_t vid, uint16_t pid, int interface);
#endif

#endif
