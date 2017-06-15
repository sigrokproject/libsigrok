/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 James Churchill <pelrun@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_RDTECH_DPS_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RDTECH_DPS_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rdtech-dps"

struct rdtech_dps_model {
	unsigned int id;
	const char *name;
	unsigned int max_current;
	unsigned int max_voltage;
	unsigned int max_power;
};

struct dev_context {
	const struct rdtech_dps_model *model;
	struct sr_sw_limits limits;
	int expecting_registers;
};

enum rdtech_dps_register {
	REG_USET       = 0x00, /* Mirror of 0x50 */
	REG_ISET       = 0x01, /* Mirror of 0x51 */
	REG_UOUT       = 0x02,
	REG_IOUT       = 0x03,
	REG_POWER      = 0x04,
	REG_UIN        = 0x05,
	REG_LOCK       = 0x06,
	REG_PROTECT    = 0x07,
	REG_CV_CC      = 0x08,
	REG_ENABLE     = 0x09,
	REG_BACKLIGHT  = 0x0A, /* Mirror of 0x55 */
	REG_MODEL      = 0x0B,
	REG_VERSION    = 0x0C,

	REG_PRESET     = 0x23, /* Loads a preset into preset 0. */

/*
 * Add (preset * 0x10) to each of the following, for preset 1-9.
 * Preset 0 regs below are the active output settings.
 */
	PRE_USET       = 0x50,
	PRE_ISET       = 0x51,
	PRE_OVPSET     = 0x52,
	PRE_OCPSET     = 0x53,
	PRE_OPPSET     = 0x54,
	PRE_BACKLIGHT  = 0x55,
	PRE_DISABLE    = 0x56, /* Disable output if 0 is copied here from a preset (1 is no change). */
	PRE_BOOT       = 0x57, /* Enable output at boot if 1. */
};

enum rdtech_dps_state {
	STATE_NORMAL = 0,
	STATE_OVP    = 1,
	STATE_OCP    = 2,
	STATE_OPP    = 3,
};

enum rdtech_dps_mode {
	MODE_CV      = 0,
	MODE_CC      = 1,
};

SR_PRIV int rdtech_dps_get_reg(struct sr_modbus_dev_inst *modbus, uint16_t address, uint16_t *value);
SR_PRIV int rdtech_dps_set_reg(struct sr_modbus_dev_inst *modbus, uint16_t address, uint16_t value);

SR_PRIV int rdtech_dps_get_model_version(struct sr_modbus_dev_inst *modbus,
		uint16_t *model, uint16_t *version);

SR_PRIV int rdtech_dps_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int rdtech_dps_receive_data(int fd, int revents, void *cb_data);

#endif
