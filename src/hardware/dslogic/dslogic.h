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

#ifndef LIBSIGROK_HARDWARE_DSLOGIC_DSLOGIC_H
#define LIBSIGROK_HARDWARE_DSLOGIC_DSLOGIC_H

/* Modified protocol commands & flags used by DSLogic */
#define DS_CMD_GET_FW_VERSION		0xb0
#define DS_CMD_GET_REVID_VERSION	0xb1
#define DS_CMD_START			0xb2
#define DS_CMD_CONFIG			0xb3
#define DS_CMD_SETTING			0xb4
#define DS_CMD_CONTROL			0xb5
#define DS_CMD_STATUS			0xb6
#define DS_CMD_STATUS_INFO		0xb7
#define DS_CMD_WR_REG			0xb8
#define DS_CMD_WR_NVM			0xb9
#define DS_CMD_RD_NVM			0xba
#define DS_CMD_RD_NVM_PRE		0xbb
#define DS_CMD_GET_HW_INFO		0xbc

#define DS_START_FLAGS_STOP		(1 << 7)
#define DS_START_FLAGS_CLK_48MHZ	(1 << 6)
#define DS_START_FLAGS_SAMPLE_WIDE	(1 << 5)
#define DS_START_FLAGS_MODE_LA		(1 << 4)

#define DS_ADDR_COMB			0x68
#define DS_ADDR_EEWP			0x70
#define DS_ADDR_VTH			0x78

#define DS_MAX_LOGIC_DEPTH		SR_MHZ(16)
#define DS_MAX_LOGIC_SAMPLERATE		SR_MHZ(100)
#define DS_MAX_TRIG_PERCENT		90

#define DS_MODE_TRIG_EN			(1 << 0)
#define DS_MODE_CLK_TYPE		(1 << 1)
#define DS_MODE_CLK_EDGE		(1 << 2)
#define DS_MODE_RLE_MODE		(1 << 3)
#define DS_MODE_DSO_MODE		(1 << 4)
#define DS_MODE_HALF_MODE		(1 << 5)
#define DS_MODE_QUAR_MODE		(1 << 6)
#define DS_MODE_ANALOG_MODE		(1 << 7)
#define DS_MODE_FILTER			(1 << 8)
#define DS_MODE_INSTANT			(1 << 9)
#define DS_MODE_STRIG_MODE		(1 << 11)
#define DS_MODE_STREAM_MODE		(1 << 12)
#define DS_MODE_LPB_TEST		(1 << 13)
#define DS_MODE_EXT_TEST		(1 << 14)
#define DS_MODE_INT_TEST		(1 << 15)

#define DSLOGIC_ATOMIC_SAMPLES		(1 << 6)

enum dslogic_operation_modes {
	DS_OP_NORMAL,
	DS_OP_INTERNAL_TEST,
	DS_OP_EXTERNAL_TEST,
	DS_OP_LOOPBACK_TEST,
};

enum {
	DS_EDGE_RISING,
	DS_EDGE_FALLING,
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
 * number of 16-bit words.
 */
#define _DS_CFG(variable, wordcnt) ((variable << 8) | wordcnt)
#define DS_CFG_START		0xf5a5f5a5
#define DS_CFG_MODE		_DS_CFG(0, 1)
#define DS_CFG_DIVIDER		_DS_CFG(1, 2)
#define DS_CFG_COUNT		_DS_CFG(3, 2)
#define DS_CFG_TRIG_POS		_DS_CFG(5, 2)
#define DS_CFG_TRIG_GLB		_DS_CFG(7, 1)
#define DS_CFG_CH_EN		_DS_CFG(8, 1)
#define DS_CFG_TRIG		_DS_CFG(64, 160)
#define DS_CFG_END		0xfa5afa5a

#pragma pack(push, 1)

struct dslogic_fpga_config {
	uint32_t sync;

	uint16_t mode_header;
	uint16_t mode;
	uint16_t divider_header;
	uint32_t divider;
	uint16_t count_header;
	uint32_t count;
	uint16_t trig_pos_header;
	uint32_t trig_pos;
	uint16_t trig_glb_header;
	uint16_t trig_glb;
	uint16_t ch_en_header;
	uint16_t ch_en;

	uint16_t trig_header;
	uint16_t trig_mask0[NUM_TRIGGER_STAGES];
	uint16_t trig_mask1[NUM_TRIGGER_STAGES];
	uint16_t trig_value0[NUM_TRIGGER_STAGES];
	uint16_t trig_value1[NUM_TRIGGER_STAGES];
	uint16_t trig_edge0[NUM_TRIGGER_STAGES];
	uint16_t trig_edge1[NUM_TRIGGER_STAGES];
	uint16_t trig_logic0[NUM_TRIGGER_STAGES];
	uint16_t trig_logic1[NUM_TRIGGER_STAGES];
	uint32_t trig_count[NUM_TRIGGER_STAGES];

	uint32_t end_sync;
};

#pragma pack(pop)

SR_PRIV int dslogic_fpga_firmware_upload(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_stop_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_fpga_configure(const struct sr_dev_inst *sdi);
SR_PRIV int dslogic_get_number_of_transfers(struct dev_context *devc);

#endif
