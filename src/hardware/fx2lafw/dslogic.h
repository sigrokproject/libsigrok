/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#ifndef LIBSIGROK_HARDWARE_FX2LAFW_DSLOGIC_H
#define LIBSIGROK_HARDWARE_FX2LAFW_DSLOGIC_H

/* Modified protocol commands & flags used by DSLogic */
#define DS_CMD_GET_FW_VERSION		0xb0
#define DS_CMD_GET_REVID_VERSION	0xb1
#define DS_CMD_START			0xb2
#define DS_CMD_FPGA_FW			0xb3
#define DS_CMD_CONFIG			0xb4
#define DS_CMD_VTH				0xb8

#define DS_NUM_TRIGGER_STAGES		16
#define DS_START_FLAGS_STOP		(1 << 7)
#define DS_START_FLAGS_CLK_48MHZ	(1 << 6)
#define DS_START_FLAGS_SAMPLE_WIDE	(1 << 5)
#define DS_START_FLAGS_MODE_LA		(1 << 4)

enum dslogic_operation_modes {
	DS_OP_NORMAL,
	DS_OP_INTERNAL_TEST,
	DS_OP_EXTERNAL_TEST,
	DS_OP_LOOPBACK_TEST,
};

enum  {
	    DS_VOLTAGE_RANGE_18_33_V,	/* 1.8V and 3.3V logic */
	    DS_VOLTAGE_RANGE_5_V,	/* 5V logic */
};

enum{
	DS_EDGE_RISING,
	DS_EDGE_FALLING
};

struct dslogic_version {
	uint8_t major;
	uint8_t minor;
};

struct dslogic_mode {
	uint8_t flags;
	uint8_t sample_delay_h;
	uint8_t sample_delay_l;
};

struct dslogic_trigger_pos {
	uint32_t real_pos;
	uint32_t ram_saddr;
	uint32_t remain_cnt;
	uint8_t first_block[500];
};

/*
 * The FPGA is configured with TLV tuples. Length is specified as the
 * number of 16-bit words, and the (type, length) header is in some
 * cases padded with 0xffff.
 */
#define _DS_CFG(variable, wordcnt) ((variable << 8) | wordcnt)
#define _DS_CFG_PAD(variable, wordcnt) ((_DS_CFG(variable, wordcnt) << 16) | 0xffff)
#define DS_CFG_START		0xf5a5f5a5
#define DS_CFG_MODE		_DS_CFG(0, 1)
#define DS_CFG_DIVIDER		_DS_CFG_PAD(1, 2)
#define DS_CFG_COUNT		_DS_CFG_PAD(3, 2)
#define DS_CFG_TRIG_POS		_DS_CFG_PAD(5, 2)
#define DS_CFG_TRIG_GLB		_DS_CFG(7, 1)
#define DS_CFG_TRIG_ADP		_DS_CFG_PAD(10, 2)
#define DS_CFG_TRIG_SDA		_DS_CFG_PAD(12, 2)
#define DS_CFG_TRIG_MASK0	_DS_CFG_PAD(16, 16)
#define DS_CFG_TRIG_MASK1	_DS_CFG_PAD(17, 16)
#define DS_CFG_TRIG_VALUE0	_DS_CFG_PAD(20, 16)
#define DS_CFG_TRIG_VALUE1	_DS_CFG_PAD(21, 16)
#define DS_CFG_TRIG_EDGE0	_DS_CFG_PAD(24, 16)
#define DS_CFG_TRIG_EDGE1	_DS_CFG_PAD(25, 16)
#define DS_CFG_TRIG_COUNT0	_DS_CFG_PAD(28, 16)
#define DS_CFG_TRIG_COUNT1	_DS_CFG_PAD(29, 16)
#define DS_CFG_TRIG_LOGIC0	_DS_CFG_PAD(32, 16)
#define DS_CFG_TRIG_LOGIC1	_DS_CFG_PAD(33, 16)
#define DS_CFG_END		0xfa5afa5a

struct dslogic_fpga_config {
	uint32_t sync;
	uint16_t mode_header;
	uint16_t mode;
	uint32_t divider_header;
	uint32_t divider;
	uint32_t count_header;
	uint32_t count;
	uint32_t trig_pos_header;
	uint32_t trig_pos;
	uint16_t trig_glb_header;
	uint16_t trig_glb;
	uint32_t trig_adp_header;
	uint32_t trig_adp;
	uint32_t trig_sda_header;
	uint32_t trig_sda;
	uint32_t trig_mask0_header;
	uint16_t trig_mask0[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_mask1_header;
	uint16_t trig_mask1[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_value0_header;
	uint16_t trig_value0[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_value1_header;
	uint16_t trig_value1[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_edge0_header;
	uint16_t trig_edge0[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_edge1_header;
	uint16_t trig_edge1[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_count0_header;
	uint16_t trig_count0[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_count1_header;
	uint16_t trig_count1[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_logic0_header;
	uint16_t trig_logic0[DS_NUM_TRIGGER_STAGES];
	uint32_t trig_logic1_header;
	uint16_t trig_logic1[DS_NUM_TRIGGER_STAGES];
	uint32_t end_sync;
};

SR_PRIV int dslogic_fpga_firmware_upload(const struct sr_dev_inst *sdi,
		const char *name);
SR_PRIV int dslogic_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_stop_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_fpga_configure(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_set_vth(const struct sr_dev_inst *sdi, double vth);

#endif
