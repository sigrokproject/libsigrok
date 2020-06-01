/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Timo Kokkonen <tjko@iki.fi>
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

#ifndef LIBSIGROK_HARDWARE_ITECH_IT8500_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ITECH_IT8500_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "itech-it8500"

#define ITECH_IT8500_MAX_MODEL_NAME_LEN 5

enum itech_it8500_modes {
	CC = 0,
	CV = 1,
	CW = 2,
	CR = 3 ,
};

enum itech_it8500_command {
	CMD_GET_LOAD_LIMITS = 0x01,
	CMD_SET_HW_OPP_VALUE = 0x02,
	CMD_GET_HW_OPP_VALUE = 0x03,
	CMD_SET_VON_MODE = 0x0e,
	CMD_GET_VON_MODE = 0x0f,
	CMD_SET_VON_VALUE = 0x10,
	CMD_GET_VON_VALUE = 0x11,
	RESPONSE = 0x12,  /* this is sent as response to command not returning data */
	CMD_SET_REMOTE_MODE = 0x20,
	CMD_LOAD_ON_OFF = 0x21,
	CMD_SET_MAX_VOLTAGE = 0x22,
	CMD_GET_MAX_VOLTAGE = 0x23,
	CMD_SET_MAX_CURRENT = 0x24,
	CMD_GET_MAX_CURRENT = 0x25,
	CMD_SET_MAX_POWER = 0x26,
	CMD_GET_MAX_POWER = 0x27,
	CMD_SET_MODE = 0x28,
	CMD_GET_MODE = 0x29,
	CMD_SET_CC_CURRENT = 0x2a,
	CMD_GET_CC_CURRENT = 0x2b,
	CMD_SET_CV_VOLTAGE = 0x2c,
	CMD_GET_CV_VOLTAGE = 0x2d,
	CMD_SET_CW_POWER = 0x2e,
	CMD_GET_CW_POWER = 0x2f,
	CMD_SET_CR_RESISTANCE = 0x30,
	CMD_GET_CR_RESISTANCE = 0x31,
	CMD_SET_BATTERY_MIN_VOLTAGE = 0x4e,
	CMD_GET_BATTERY_MIN_VOLTAGE = 0x4f,
	CMD_SET_LOAD_ON_TIMER = 0x50,
	CMD_GET_LOAD_ON_TIMER = 0x51,
	CMD_LOAD_ON_TIMER = 0x52,
	CMD_LOAD_ON_TIME_STATUS = 0x53,
	CMD_SET_ADDRESS = 0x54,
	CMD_LOCAL_CONTROL = 0x55,
	CMD_REMOTE_SENSING = 0x56,
	CMD_REMOTE_SENSING_STATUS = 0x57,
	CMD_SET_TRIGGER_SOURCE = 0x58,
	CMD_GET_TRIGGER_SOURCE = 0x59,
	CMD_TRIGGER = 0x5a,
	CMD_SAVE_SETTINGS = 0x5b,
	CMD_LOAD_SETTINGS = 0x5c,
	CMD_SET_FUNCTION = 0x5d,
	CMD_GET_FUNCTION = 0x5e,
	CMD_GET_STATE = 0x5f,
	CMD_GET_MODEL_INFO = 0x6a,
	CMD_GET_BARCODE_INFO = 0x6b,
	CMD_SET_OCP_VALUE = 0x80,
	CMD_GET_OCP_VALUE = 0x81,
	CMD_SET_OCP_DELAY = 0x82,
	CMD_GET_OCP_DELAY = 0x83,
	CMD_ENABLE_OCP = 0x84,
	CMD_DISABLE_OCP = 0x85,
	CMD_SET_OPP_VALUE = 0x86,
	CMD_GET_OPP_VALUE = 0x87,
	CMD_SET_OPP_DELAY = 0x88,
	CMD_GET_OPP_DELAY = 0x89,
};

/*
 * this is data structure sent to the load, must use only uint8_t to
 * keep code portable...
 */
struct itech_it8500_cmd_packet {
	uint8_t preamble;  /* must be 0xAA */
	uint8_t address;   /* unit address: 0..254 [255 = broadcast] (optional) */
	uint8_t command;   /* command number */
	uint8_t data[22];  /* command/response data (0-22 bytes) */
	uint8_t checksum;  /* checksum (modulo 256) */
};

struct dev_context {
	char model[ITECH_IT8500_MAX_MODEL_NAME_LEN + 1];
	uint8_t fw_ver_major;
	uint8_t fw_ver_minor;
	uint8_t address;
	double max_current;
	double min_voltage;
	double max_voltage;
	double max_power;
	double min_resistance;
	double max_resistance;
	double voltage;
	double current;
	double power;
	uint8_t operation_state;
	uint16_t demand_state;
	enum itech_it8500_modes mode;
	gboolean load_on;
	uint32_t max_sample_rate_idx;
	uint64_t sample_rate;
	struct sr_sw_limits limits;
};


SR_PRIV int itech_it8500_receive_data(int fd, int revents, void *cb_data);

SR_PRIV char itech_it8500_checksum(struct itech_it8500_cmd_packet *packet);
SR_PRIV const char* itech_it8500_mode_to_string(enum itech_it8500_modes mode);
SR_PRIV void itech_it8500_channel_send_value(const struct sr_dev_inst *sdi,
					     struct sr_channel *ch, float value,
					     enum sr_mq mq, enum sr_unit unit,
					     int digits);

SR_PRIV int itech_it8500_send_cmd(struct sr_serial_dev_inst *serial,
				  struct itech_it8500_cmd_packet *cmd,
				  struct itech_it8500_cmd_packet **response);
SR_PRIV int itech_it8500_get_status(struct sr_serial_dev_inst *serial,
				    struct dev_context *devc);
SR_PRIV int itech_it8500_get_int(struct sr_serial_dev_inst *serial,
				 enum itech_it8500_command command,
				 int *result);
#endif
