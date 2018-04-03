/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Sven Bursch-Osewold <sb_git@bursch.com>
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

#ifndef LIBSIGROK_HARDWARE_ZKETECH_EBD_USB_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ZKETECH_EBD_USB_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "zketech-ebd-usb"

#define MSG_LEN 19
#define MSG_CHECKSUM_POS 17
#define MSG_FRAME_BEGIN 0xfa
#define MSG_FRAME_BEGIN_POS 0
#define MSG_FRAME_END 0xf8
#define MSG_FRAME_END_POS 18

struct dev_context {
	struct sr_sw_limits limits;
	GMutex rw_mutex;
	float current_limit;
	gboolean running;
	gboolean load_activated;
};

SR_PRIV float zketech_ebd_usb_value_decode(uint8_t b1, uint8_t b2, float divisor);
SR_PRIV void zketech_ebd_usb_value_encode(float voltage, uint8_t *b1, uint8_t *b2, float divisor);

/* Communication via serial. */
SR_PRIV int zketech_ebd_usb_send(struct sr_serial_dev_inst *serial, uint8_t buf[], size_t count);
SR_PRIV int zketech_ebd_usb_sendcfg(struct sr_serial_dev_inst *serial, struct dev_context *devc);
SR_PRIV int zketech_ebd_usb_read_chars(struct sr_serial_dev_inst *serial,
		int count, uint8_t *buf);

/* Commands. */
SR_PRIV int zketech_ebd_usb_init(struct sr_serial_dev_inst *serial, struct dev_context *devc);
SR_PRIV int zketech_ebd_usb_loadstart(struct sr_serial_dev_inst *serial, struct dev_context *devc);
SR_PRIV int zketech_ebd_usb_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int zketech_ebd_usb_stop(struct sr_serial_dev_inst *serial, struct dev_context *devc);
SR_PRIV int zketech_ebd_usb_loadstop(struct sr_serial_dev_inst *serial, struct dev_context *devc);

/* Configuration. */
SR_PRIV int zketech_ebd_usb_get_current_limit(const struct sr_dev_inst *sdi,
		float *current);
SR_PRIV int zketech_ebd_usb_set_current_limit(const struct sr_dev_inst *sdi,
		float current);
SR_PRIV gboolean zketech_ebd_usb_current_is0(struct dev_context *devc);

SR_PRIV void zketech_ebd_usb_buffer_debug(const char *message, uint8_t buf[], size_t count);

#endif
