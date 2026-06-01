/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013-2017 DreamSourceLab <support@dreamsourcelab.com>
 * Copyright (C) 2026 Larry Hernandez <l.gr@dartmouth.edu>
 *
 * Constants, structs, and prototypes for the DSLogic envelope (v2)
 * protocol used by newer-firmware DSLogic hardware (PID 2a0e:0034
 * and any later device tagged DSL_PROTO_V2).
 *
 * Wire format mirrors DSView 1.3.2's libsigrok4DSL/hardware/DSL/.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v3.
 */

#ifndef LIBSIGROK_HARDWARE_DREAMSOURCELAB_DSLOGIC_PROTOCOL_V2_H
#define LIBSIGROK_HARDWARE_DREAMSOURCELAB_DSLOGIC_PROTOCOL_V2_H

#include <stdint.h>
#include <libusb.h>
#include "protocol.h"

/* Envelope opcodes (carried in libusb_control_transfer's bRequest). */
#define CMD_CTL_WR              0xb0
#define CMD_CTL_RD_PRE          0xb1
#define CMD_CTL_RD              0xb2

/* DSL_CTL destination codes (selector in ctl_header.dest). */
#define DSL_CTL_FW_VERSION      0
#define DSL_CTL_REVID_VERSION   1
#define DSL_CTL_HW_STATUS       2
#define DSL_CTL_PROG_B          3
#define DSL_CTL_SYS             4
#define DSL_CTL_LED             5
#define DSL_CTL_INTRDY          6
#define DSL_CTL_WORDWIDE        7
#define DSL_CTL_START           8
#define DSL_CTL_STOP            9
#define DSL_CTL_BULK_WR        10
#define DSL_CTL_REG            11
#define DSL_CTL_NVM            12
#define DSL_CTL_I2C_REG        14
#define DSL_CTL_I2C_STATUS     15

/* Status bits in DSL_CTL_HW_STATUS read byte. */
#define bmGPIF_DONE     (1 << 7)
#define bmFPGA_DONE     (1 << 6)
#define bmFPGA_INIT_B   (1 << 5)
#define bmSYS_OVERFLOW  (1 << 4)
#define bmSYS_CLR       (1 << 3)
#define bmSYS_EN        (1 << 2)

/* Bits written via DSL_CTL_INTRDY / DSL_CTL_WORDWIDE / DSL_CTL_PROG_B. */
#define bmWR_INTRDY     (1 << 7)
#define bmWR_WORDWIDE   (1 << 0)
#define bmWR_PROG_B     (1 << 2)

/* Bits written via DSL_CTL_LED. */
#define bmLED_GREEN     (1 << 0)
#define bmLED_RED       (1 << 1)

/* I2C-mapped FPGA register addresses. */
#define VTH_ADDR        0x78
#define SEC_DATA_ADDR   0x75
#define SEC_CTRL_ADDR   0x73
#define CTR0_ADDR       0x70

/* CTR0_ADDR bits (mirrors DSView command.h). */
#define bmFORCE_RDY     (1 << 1)

/* Security check (mirrors DSView command.c). */
#define bmSECU_READY    (1 << 3)
#define bmSECU_PASS     (1 << 4)
#define SECU_STEPS      8
#define SECU_START      0x0513
#define SECU_CHECK      0x0219
#define SECU_EEP_ADDR   0x3C00
#define SECU_TRY_CNT    8

/* Trigger / setting blob (mirrors DSView dsl.h). */
#ifndef NUM_TRIGGER_STAGES
#define NUM_TRIGGER_STAGES   16
#endif

#pragma pack(push, 1)

struct ctl_header {
	uint8_t dest;
	uint16_t offset;
	uint8_t size;
};

struct ctl_wr_cmd {
	struct ctl_header header;
	uint8_t data[60];
};

struct ctl_rd_cmd {
	struct ctl_header header;
	uint8_t *data;
};

struct DSL_setting {
	uint32_t sync;
	uint16_t mode_header;       uint16_t mode;
	uint16_t divider_header;    uint16_t div_l, div_h;
	uint16_t count_header;      uint16_t cnt_l, cnt_h;
	uint16_t trig_pos_header;   uint16_t tpos_l, tpos_h;
	uint16_t trig_glb_header;   uint16_t trig_glb;
	uint16_t dso_count_header;  uint16_t dso_cnt_l, dso_cnt_h;
	uint16_t ch_en_header;      uint16_t ch_en_l, ch_en_h;
	uint16_t fgain_header;      uint16_t fgain;

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

/* Sync markers used by DSL_setting (mirrors DSView dsl.c constants). */
#define DSL_SETTING_SYNC      0xf5a5f5a5
#define DSL_SETTING_END_SYNC  0xfa5afa5a

/*
 * Bit positions within DSL_setting.mode (mirrors DSView dsl.c).
 * Only the ones the V2 LOGIC path actually uses are listed here.
 */
#define DS_MODE_TRIG_EN_BIT     0
#define DS_MODE_CLK_TYPE_BIT    1
#define DS_MODE_CLK_EDGE_BIT    2
#define DS_MODE_RLE_MODE_BIT    3
#define DS_MODE_HALF_MODE_BIT   5
#define DS_MODE_QUAR_MODE_BIT   6
#define DS_MODE_FILTER_BIT      8
#define DS_MODE_STRIG_MODE_BIT 11
#define DS_MODE_STREAM_MODE_BIT 12

/*
 * Channel-mode table. One entry per DSLogic Plus channel-count /
 * samplerate preset that the FPGA supports. Mirrors DSView's
 * struct DSL_channels with only the fields the LOGIC capture path needs.
 *
 * `id` is a stable numeric handle exposed via SR_CONF_CHANNEL_MODE so
 * the user can pick "16 channels buffered at up to 100 MHz" vs
 * "3 channels streamed at up to 100 MHz", etc.
 */
struct dslogic_channel_mode {
	uint8_t  id;
	gboolean stream;
	uint16_t num_channels;
	uint64_t min_samplerate;
	uint64_t max_samplerate;
	uint64_t hw_max_samplerate;
	uint8_t  pre_div;
	const char *descr;
};

SR_PRIV const struct dslogic_channel_mode *dslogic_plus_channel_modes(size_t *count);
SR_PRIV const struct dslogic_channel_mode *dslogic_plus_channel_mode_default(void);
SR_PRIV const struct dslogic_channel_mode *dslogic_plus_channel_mode_by_id(uint8_t id);
SR_PRIV uint8_t dslogic_plus_auto_pick_mode_id(uint64_t samplerate,
		gboolean continuous, gboolean rle, unsigned int need_channels);

/* Transport primitives. */
SR_PRIV int command_ctl_wr_v2(libusb_device_handle *devhdl, struct ctl_wr_cmd cmd);
SR_PRIV int command_ctl_rd_v2(libusb_device_handle *devhdl, struct ctl_rd_cmd cmd);

/* Register / NVM access (envelope wrappers). */
SR_PRIV int dsl_wr_reg_v2(const struct sr_dev_inst *sdi, uint16_t addr, uint8_t value);
SR_PRIV int dsl_rd_reg_v2(const struct sr_dev_inst *sdi, uint16_t addr, uint8_t *value);
SR_PRIV int dsl_rd_nvm_v2(const struct sr_dev_inst *sdi, uint8_t *buf, uint16_t addr, uint8_t len);

/* HW status polling helper. */
SR_PRIV int dsl_wait_hw_status_bit_v2(libusb_device_handle *hdl, uint8_t bit_mask, gboolean want_set, unsigned timeout_ms);

#endif
