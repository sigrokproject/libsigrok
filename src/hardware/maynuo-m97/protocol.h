/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Aurelien Jacobs <aurel@gnuage.org>
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

#ifndef LIBSIGROK_HARDWARE_MAYNUO_M97_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MAYNUO_M97_PROTOCOL_H

#include <stdint.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "maynuo-m97"

struct maynuo_m97_model {
	unsigned int id;
	const char *name;
	unsigned int max_current;
	unsigned int max_voltage;
	unsigned int max_power;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct maynuo_m97_model *model;

	/* Acquisition settings */
	uint64_t limit_samples;
	uint64_t limit_msec;

	/* Operational state */
	uint64_t num_samples;
	int64_t starttime;
	int expecting_registers;
};

enum maynuo_m97_coil {
	PC1        = 0x0500,
	PC2        = 0X0501,
	TRIG       = 0x0502,
	REMOTE     = 0x0503,
	ISTATE     = 0x0510,
	TRACK      = 0x0511,
	MEMORY     = 0x0512,
	VOICEEN    = 0x0513,
	CONNECT    = 0x0514,
	ATEST      = 0x0515,
	ATESTUN    = 0x0516,
	ATESTPASS  = 0x0517,
	IOVER      = 0x0520,
	UOVER      = 0x0521,
	POVER      = 0x0522,
	HEAT       = 0x0523,
	REVERSE    = 0x0524,
	UNREG      = 0x0525,
	ERREP      = 0x0526,
	ERRCAL     = 0x0527,
};

enum maynuo_m97_register {
	CMD        = 0x0A00,
	IFIX       = 0x0A01,
	UFIX       = 0x0A03,
	PFIX       = 0x0A05,
	RFIX       = 0x0A07,
	TMCCS      = 0x0A09,
	TMCVS      = 0x0A0B,
	UCCONSET   = 0x0A0D,
	UCCOFFSET  = 0x0A0F,
	UCVONSET   = 0x0A11,
	UCVOFFSET  = 0x0A13,
	UCPONSET   = 0x0A15,
	UCPOFFSET  = 0x0A17,
	UCRONSET   = 0x0A19,
	UCROFFSET  = 0x0A1B,
	UCCCV      = 0x0A1D,
	UCRCV      = 0x0A1F,
	IA         = 0x0A21,
	IB         = 0x0A23,
	TMAWD      = 0x0A25,
	TMBWD      = 0x0A27,
	TMTRANSRIS = 0x0A29,
	TMTRANSFAL = 0x0A2B,
	MODETRAN   = 0x0A2D,
	UBATTEND   = 0x0A2E,
	BATT       = 0x0A30,
	SERLIST    = 0x0A32,
	SERATEST   = 0x0A33,
	IMAX       = 0x0A34,
	UMAX       = 0x0A36,
	PMAX       = 0x0A38,
	ILCAL      = 0x0A3A,
	IHCAL      = 0x0A3C,
	ULCAL      = 0x0A3E,
	UHCAL      = 0x0A40,
	TAGSCAL    = 0x0A42,
	U          = 0x0B00,
	I          = 0x0B02,
	SETMODE    = 0x0B04,
	INPUTMODE  = 0x0B05,
	MODEL      = 0x0B06,
	EDITION    = 0x0B07,
};

enum maynuo_m97_mode {
	CC            =  1,
	CV            =  2,
	CW            =  3,
	CR            =  4,
	CC_SOFT_START = 20,
	DYNAMIC       = 25,
	SHORT_CIRCUIT = 26,
	LIST          = 27,
	CC_L_AND_UL   = 30,
	CV_L_AND_UL   = 31,
	CW_L_AND_UL   = 32,
	CR_L_AND_UL   = 33,
	CC_TO_CV      = 34,
	CR_TO_CV      = 36,
	BATTERY_TEST  = 38,
	CV_SOFT_START = 39,
	SYSTEM_PARAM  = 41,
	INPUT_ON      = 42,
	INPUT_OFF     = 43,
};

SR_PRIV int maynuo_m97_get_bit(struct sr_modbus_dev_inst *modbus,
		enum maynuo_m97_coil address, int *value);
SR_PRIV int maynuo_m97_set_bit(struct sr_modbus_dev_inst *modbus,
		enum maynuo_m97_coil address, int value);
SR_PRIV int maynuo_m97_get_float(struct sr_modbus_dev_inst *modbus,
		enum maynuo_m97_register address, float *value);
SR_PRIV int maynuo_m97_set_float(struct sr_modbus_dev_inst *modbus,
		enum maynuo_m97_register address, float value);

SR_PRIV int maynuo_m97_get_mode(struct sr_modbus_dev_inst *modbus,
		enum maynuo_m97_mode *mode);
SR_PRIV int maynuo_m97_set_mode(struct sr_modbus_dev_inst *modbus,
		enum maynuo_m97_mode mode);
SR_PRIV int maynuo_m97_set_input(struct sr_modbus_dev_inst *modbus, int enable);
SR_PRIV int maynuo_m97_get_model_version(struct sr_modbus_dev_inst *modbus,
		uint16_t *model, uint16_t *version);

SR_PRIV const char *maynuo_m97_mode_to_str(enum maynuo_m97_mode mode);

SR_PRIV int maynuo_m97_capture_start(const struct sr_dev_inst *sdi);
SR_PRIV int maynuo_m97_receive_data(int fd, int revents, void *cb_data);

#endif
