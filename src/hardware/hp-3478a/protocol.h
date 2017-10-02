/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Frank Stettner <frank-stettner@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_HP_3478A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HP_3478A_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hp-3478a"

#define SB1_FUNCTION_BLOCK	0b11100000
#define SB1_RANGE_BLOCK		0b00011100
#define SB1_DIGITS_BLOCK	0b00000011

/* Status Byte 1 (Function) */
enum sb1_function {
	FUNCTION_VDC			= 0b00100000,
	FUNCTION_VAC			= 0b01000000,
	FUNCTION_2WR			= 0b01100000,
	FUNCTION_4WR			= 0b10000000,
	FUNCTION_ADC			= 0b10100000,
	FUNCTION_AAC			= 0b11000000,
	FUNCTION_EXR			= 0b11100000,
};

/* Status Byte 1 (Range V DC) */
enum sb1_range_vdc {
	RANGE_VDC_30MV			= 0b00000100,
	RANGE_VDC_300MV			= 0b00001000,
	RANGE_VDC_3V			= 0b00001100,
	RANGE_VDC_30V			= 0b00010000,
	RANGE_VDC_300V			= 0b00010100,
};

/* Status Byte 1 (Range V AC) */
enum sb1_range_vac {
	RANGE_VAC_300MV			= 0b00000100,
	RANGE_VAC_3V			= 0b00001000,
	RANGE_VAC_30V			= 0b00001100,
	RANGE_VAC_300V			= 0b00010000,
};

/* Status Byte 1 (Range A) */
enum sb1_range_a {
	RANGE_A_300MA			= 0b00000100,
	RANGE_A_3A			= 0b00001000,
};

/* Status Byte 1 (Range Ohm) */
enum sb1_range_ohm {
	RANGE_OHM_30R			= 0b00000100,
	RANGE_OHM_300R			= 0b00001000,
	RANGE_OHM_3KR			= 0b00001100,
	RANGE_OHM_30KR			= 0b00010000,
	RANGE_OHM_300KR			= 0b00010100,
	RANGE_OHM_3MR			= 0b00011000,
	RANGE_OHM_30MR			= 0b00011100,
};

/* Status Byte 1 (Digits) */
enum sb1_digits {
	DIGITS_5_5			= 0b00000001,
	DIGITS_4_5			= 0b00000010,
	DIGITS_3_5			= 0b00000011,
};

/* Status Byte 2 */
enum sb2_status {
	STATUS_INT_TRIGGER		= (1 << 0),
	STATUS_AUTO_RANGE		= (1 << 1),
	STATUS_AUTO_ZERO		= (1 << 2),
	STATUS_50HZ			= (1 << 3),
	STATUS_FRONT_TERMINAL		= (1 << 4),
	STATUS_CAL_RAM			= (1 << 5),
	STATUS_EXT_TRIGGER		= (1 << 6),
};

/* Status Byte 3 (Serial Poll Mask) */
enum sb3_srq {
	SRQ_BUS_AVAIL			= (1 << 0),
	SRQ_SYNTAX_ERR			= (1 << 2),
	SRQ_HARDWARE_ERR		= (1 << 3),
	SRQ_KEYBORD			= (1 << 4),
	SRQ_CAL_FAILED			= (1 << 5),
	SRQ_POWER_ON			= (1 << 7),
};

/* Status Byte 4 (Error) */
enum sb4_error {
	ERROR_SELF_TEST			= (1 << 0),
	ERROR_RAM_SELF_TEST		= (1 << 1),
	ERROR_ROM_SELF_TEST		= (1 << 2),
	ERROR_AD_SLOPE			= (1 << 3),
	ERROR_AD_SELF_TEST		= (1 << 4),
	ERROR_AD_LINK			= (1 << 5),
};

/* Channel connector (front terminals or rear terminals. */
enum terminal_connector {
	TERMINAL_FRONT,
	TERMINAL_REAR,
};

/* Possible triggers */
enum trigger_state {
	TRIGGER_UNDEFINED,
	TRIGGER_EXTERNAL,
	TRIGGER_INTERNAL,
};

/* Possible line frequencies */
enum line_freq {
	LINE_50HZ,
	LINE_60HZ,
};

struct dev_context {
	struct sr_sw_limits limits;

	double measurement;
	enum sr_mq measurement_mq;
	enum sr_mqflag measurement_mq_flags;
	enum sr_unit measurement_unit;
	uint8_t enc_digits;
	uint8_t spec_digits;

	enum terminal_connector terminal;
	enum trigger_state trigger;
	enum line_freq line;
	gboolean auto_zero;
	gboolean calibration;
};

struct channel_context {
	int index;
	enum terminal_connector location;
};

SR_PRIV int hp_3478a_set_mq(const struct sr_dev_inst *sdi, enum sr_mq mq,
				enum sr_mqflag mq_flags);
SR_PRIV int hp_3478a_get_status_bytes(const struct sr_dev_inst *sdi);
SR_PRIV int hp_3478a_receive_data(int fd, int revents, void *cb_data);

#endif
