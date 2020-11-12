/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Martin Eitzenberger <x@cymaphore.net>
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

/**
 * @file
 *
 * APPA Transport Protocol handler
 */

#ifndef LIBSIGROK_TP_APPA_H
#define LIBSIGROK_TP_APPA_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#ifdef HAVE_SERIAL_COMM

#define SR_TP_APPA_MAX_DATA_SIZE 64
#define SR_TP_APPA_MAX_PACKET_SIZE 69

/**
 * Instance object
 *
 * Must be created by the user and retain valid for the duration of the
 * APPA protocol handling. Multiple instances can be active at the same time.
 */
struct sr_tp_appa_inst {
	struct sr_serial_dev_inst *serial;
	uint8_t buffer[SR_TP_APPA_MAX_PACKET_SIZE];
	uint8_t buffer_size;
};

/**
 * APPA transport package
 */
struct sr_tp_appa_packet {
	uint8_t command; /**< Command code, according to device documentation */
	uint8_t length; /**< Number of bytes in data */
	uint8_t data[SR_TP_APPA_MAX_DATA_SIZE]; /**< Payload data */
};

SR_PRIV int sr_tp_appa_init(struct sr_tp_appa_inst *arg_tpai,
	struct sr_serial_dev_inst *arg_serial);
SR_PRIV int sr_tp_appa_term(struct sr_tp_appa_inst *arg_tpai);

SR_PRIV int sr_tp_appa_send(struct sr_tp_appa_inst *arg_tpai,
	const struct sr_tp_appa_packet *arg_s_packet, gboolean arg_is_blocking);
SR_PRIV int sr_tp_appa_receive(struct sr_tp_appa_inst *arg_tpai,
	struct sr_tp_appa_packet *arg_r_packet, gboolean arg_is_blocking);
SR_PRIV int sr_tp_appa_send_receive(struct sr_tp_appa_inst *arg_tpai,
	const struct sr_tp_appa_packet *arg_s_packet,
	struct sr_tp_appa_packet *arg_r_packet);

#endif/*HAVE_SERIAL_COMM*/

#endif/*LIBSIGROK_TP_APPA_H*/
