/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
 * Copyright (C) 2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_ASIX_SIGMA_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ASIX_SIGMA_PROTOCOL_H

#include <stdint.h>
#include <stdlib.h>
#include <glib.h>
#include <ftdi.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "asix-sigma"

/*
 * Triggers are not working in this implementation. Stop claiming
 * support for the feature which effectively is not available, until
 * the implementation got fixed. Yet keep the code in place and allow
 * developers to turn on this switch during development.
 */
#define ASIX_SIGMA_WITH_TRIGGER	0

/* Experimental support for OMEGA (scan only, operation is ENOIMPL). */
#define ASIX_WITH_OMEGA 0

#define USB_VENDOR_ASIX			0xa600
#define USB_PRODUCT_SIGMA		0xa000
#define USB_PRODUCT_OMEGA		0xa004

enum asix_device_type {
	ASIX_TYPE_NONE,
	ASIX_TYPE_SIGMA,
	ASIX_TYPE_OMEGA,
};

/*
 * FPGA commands are 8bits wide. The upper nibble is a command opcode,
 * the lower nibble can carry operand values. 8bit register addresses
 * and 8bit data values get communicated in two steps.
 */

/* Register access. */
#define REG_ADDR_LOW		(0x0 << 4)
#define REG_ADDR_HIGH		(0x1 << 4)
#define REG_DATA_LOW		(0x2 << 4)
#define REG_DATA_HIGH_WRITE	(0x3 << 4)
#define REG_READ_ADDR		(0x4 << 4)
#define REG_ADDR_ADJUST		(1 << 0) /* Auto adjust register address. */
#define REG_ADDR_DOWN		(1 << 1) /* 1 decrement, 0 increment. */
#define REG_ADDR_INC		(REG_ADDR_ADJUST)
#define REG_ADDR_DEC		(REG_ADDR_ADJUST | REG_ADDR_DOWN)

/* Sample memory access. */
#define REG_DRAM_WAIT_ACK	(0x5 << 4) /* Wait for completion. */
#define REG_DRAM_BLOCK		(0x6 << 4) /* DRAM to BRAM, plus bank select. */
#define REG_DRAM_BLOCK_BEGIN	(0x8 << 4) /* Read first BRAM bytes. */
#define REG_DRAM_BLOCK_DATA	(0xa << 4) /* Read full BRAM block. */
#define REG_DRAM_SEL_N		(0x1 << 4) /* Bank select, added to 6/8/a. */
#define REG_DRAM_SEL_BOOL(b)	((b) ? REG_DRAM_SEL_N : 0)

/*
 * Registers at a specific address can have different meanings depending
 * on whether data is read or written. This is why direction is part of
 * the programming language identifiers.
 *
 * The vendor documentation suggests that in addition to the first 16
 * register addresses which implement the logic analyzer's feature set,
 * there are 240 more registers in the 16 to 255 address range which
 * are available to applications and plugin features. Can libsigrok's
 * asix-sigma driver store configuration data there, to avoid expensive
 * operations (think: firmware re-load).
 */

enum sigma_write_register {
	WRITE_CLOCK_SELECT	= 0,
	WRITE_TRIGGER_SELECT	= 1,
	WRITE_TRIGGER_SELECT2	= 2,
	WRITE_MODE		= 3,
	WRITE_MEMROW		= 4,
	WRITE_POST_TRIGGER	= 5,
	WRITE_TRIGGER_OPTION	= 6,
	WRITE_PIN_VIEW		= 7,
	/* Unassigned register locations. */
	WRITE_TEST		= 15,
};

enum sigma_read_register {
	READ_ID			= 0,
	READ_TRIGGER_POS_LOW	= 1,
	READ_TRIGGER_POS_HIGH	= 2,
	READ_TRIGGER_POS_UP	= 3,
	READ_STOP_POS_LOW	= 4,
	READ_STOP_POS_HIGH	= 5,
	READ_STOP_POS_UP	= 6,
	READ_MODE		= 7,
	READ_PIN_CHANGE_LOW	= 8,
	READ_PIN_CHANGE_HIGH	= 9,
	READ_BLOCK_LAST_TS_LOW	= 10,
	READ_BLOCK_LAST_TS_HIGH	= 11,
	READ_BLOCK_TS_OVERRUN	= 12,
	READ_PIN_VIEW		= 13,
	/* Unassigned register location. */
	READ_TEST		= 15,
};

#define HI4(b)			(((b) >> 4) & 0x0f)
#define LO4(b)			(((b) >> 0) & 0x0f)

#define BIT_MASK(l)	((1UL << (l)) - 1)

#define TRGSEL_SELC_MASK	BIT_MASK(2)
#define TRGSEL_SELC_SHIFT	0
#define TRGSEL_SELPRESC_MASK	BIT_MASK(4)
#define TRGSEL_SELPRESC_SHIFT	4
#define TRGSEL_SELINC_MASK	BIT_MASK(2)
#define TRGSEL_SELINC_SHIFT	0
#define TRGSEL_SELRES_MASK	BIT_MASK(2)
#define TRGSEL_SELRES_SHIFT	2
#define TRGSEL_SELA_MASK	BIT_MASK(2)
#define TRGSEL_SELA_SHIFT	4
#define TRGSEL_SELB_MASK	BIT_MASK(2)
#define TRGSEL_SELB_SHIFT	6

#define TRGSEL2_PINS_MASK	(0x07 << 0)
#define TRGSEL2_PINPOL_RISE	(1 << 3)
#define TRGSEL2_LUT_ADDR_MASK	(0x0f << 0)
#define TRGSEL2_LUT_WRITE	(1 << 4)
#define TRGSEL2_RESET		(1 << 5)
#define TRGSEL2_LEDSEL0		(1 << 6)
#define TRGSEL2_LEDSEL1		(1 << 7)

/* WRITE_MODE register fields. */
#define WMR_SDRAMWRITEEN	(1 << 0)
#define WMR_SDRAMREADEN		(1 << 1)
#define WMR_TRGRES		(1 << 2)
#define WMR_TRGEN		(1 << 3)
#define WMR_FORCESTOP		(1 << 4)
#define WMR_TRGSW		(1 << 5)
/* not used: bit position 6 */
#define WMR_SDRAMINIT		(1 << 7)

/* READ_MODE register fields. */
#define RMR_SDRAMWRITEEN	(1 << 0)
#define RMR_SDRAMREADEN		(1 << 1)
/* not used: bit position 2 */
#define RMR_TRGEN		(1 << 3)
#define RMR_ROUND		(1 << 4)
#define RMR_TRIGGERED		(1 << 5)
#define RMR_POSTTRIGGERED	(1 << 6)
/* not used: bit position 7 */

/*
 * Trigger options. First and second write are similar, but _some_
 * positions change their meaning.
 */
#define TRGOPT_TRGIEN		(1 << 7)
#define TRGOPT_TRGOEN		(1 << 6)
#define TRGOPT_TRGOINEN		(1 << 5) /* 1st write */
#define TRGOPT_TRGINEG		TRGOPT1_TRGOINEN /* 2nd write */
#define TRGOPT_TRGOEVNTEN	(1 << 4) /* 1st write */
#define TRGOPT_TRGOPIN		TRGOPT1_TRGOEVNTEN /* 2nd write */
#define TRGOPT_TRGOOUTEN	(1 << 3) /* 1st write */
#define TRGOPT_TRGOLONG		TRGOPT1_TRGOOUTEN /* 2nd write */
#define TRGOPT_TRGOUTR_OUT	(1 << 1)
#define TRGOPT_TRGOUTR_EN	(1 << 0)
#define TRGOPT_CLEAR_MASK	(TRGOPT_TRGOINEN | TRGOPT_TRGOEVNTEN | TRGOPT_TRGOOUTEN)

/*
 * Layout of the sample data DRAM, which will be downloaded to the PC:
 *
 * Sigma memory is organized in 32K rows. Each row contains 64 clusters.
 * Each cluster contains a timestamp (16bit) and 7 events (16bits each).
 * Events contain 16 bits of sample data (potentially taken at multiple
 * sample points, see below).
 *
 * Total memory size is 32K x 64 x 8 x 2 bytes == 32 MiB (256 Mbit). The
 * size of a memory row is 1024 bytes. Assuming x16 organization of the
 * memory array, address specs (sample count, trigger position) are kept
 * in 24bit entities. The upper 15 bit address the "row", the lower 9 bit
 * refer to the "event" within the row. Because there is one timestamp for
 * seven events each, one memory row can hold up to 64x7 == 448 events.
 *
 * Sample data is represented in 16bit quantities. The first sample in
 * the cluster corresponds to the cluster's timestamp. Each next sample
 * corresponds to the timestamp + 1, timestamp + 2, etc (the distance is
 * one sample period, according to the samplerate). In the absence of
 * pin level changes, no data is provided (RLE compression). A cluster
 * is enforced for each 64K ticks of the timestamp, to reliably handle
 * rollover and determine the next timestamp of the next cluster.
 *
 * For samplerates up to 50MHz, an event directly translates to one set
 * of sample data at a single sample point, spanning up to 16 channels.
 * For samplerates of 100MHz, there is one 16 bit entity for each 20ns
 * period (50MHz rate). The 16 bit memory contains 2 samples of up to
 * 8 channels. Bits of multiple samples are interleaved. For samplerates
 * of 200MHz one 16bit entity contains 4 samples of up to 4 channels,
 * each 5ns apart.
 */

#define ROW_COUNT		32768
#define ROW_LENGTH_BYTES	1024
#define ROW_LENGTH_U16		(ROW_LENGTH_BYTES / sizeof(uint16_t))
#define ROW_SHIFT		9 /* log2 of u16 count */
#define ROW_MASK		((1UL << ROW_SHIFT) - 1)
#define EVENTS_PER_CLUSTER	7
#define CLUSTERS_PER_ROW	(ROW_LENGTH_U16 / (1 + EVENTS_PER_CLUSTER))
#define EVENTS_PER_ROW		(CLUSTERS_PER_ROW * EVENTS_PER_CLUSTER)

struct sigma_dram_line {
	struct sigma_dram_cluster {
		uint16_t timestamp;
		uint16_t samples[EVENTS_PER_CLUSTER];
	} cluster[CLUSTERS_PER_ROW];
};

/* The effect of all these are still a bit unclear. */
struct triggerinout {
	uint8_t trgout_resistor_enable : 1;
	uint8_t trgout_resistor_pullup : 1;
	uint8_t reserved1 : 1;
	uint8_t trgout_bytrigger : 1;
	uint8_t trgout_byevent : 1;
	uint8_t trgout_bytriggerin : 1;
	uint8_t reserved2 : 2;

	/* Should be set same as the first two */
	uint8_t trgout_resistor_enable2 : 1;
	uint8_t trgout_resistor_pullup2 : 1;

	uint8_t reserved3 : 1;
	uint8_t trgout_long : 1;
	uint8_t trgout_pin : 1; /* Use 1k resistor. Pullup? */
	uint8_t trgin_negate : 1;
	uint8_t trgout_enable : 1;
	uint8_t trgin_enable : 1;
};

struct triggerlut {
	/* The actual LUTs. */
	uint16_t m0d[4], m1d[4], m2d[4];
	uint16_t m3, m3s, m4;

	/* Parameters should be sent as a single register write. */
	struct {
		uint8_t selc : 2;
		uint8_t selpresc : 6;

		uint8_t selinc : 2;
		uint8_t selres : 2;
		uint8_t sela : 2;
		uint8_t selb : 2;

		uint16_t cmpb;
		uint16_t cmpa;
	} params;
};

/* Trigger configuration */
struct sigma_trigger {
	/* Only two channels can be used in mask. */
	uint16_t risingmask;
	uint16_t fallingmask;

	/* Simple trigger support (<= 50 MHz). */
	uint16_t simplemask;
	uint16_t simplevalue;

	/* TODO: Advanced trigger support (boolean expressions). */
};

/* Events for trigger operation. */
enum triggerop {
	OP_LEVEL = 1,
	OP_NOT,
	OP_RISE,
	OP_FALL,
	OP_RISEFALL,
	OP_NOTRISE,
	OP_NOTFALL,
	OP_NOTRISEFALL,
};

/* Logical functions for trigger operation. */
enum triggerfunc {
	FUNC_AND = 1,
	FUNC_NAND,
	FUNC_OR,
	FUNC_NOR,
	FUNC_XOR,
	FUNC_NXOR,
};

struct sigma_state {
	enum {
		SIGMA_UNINITIALIZED = 0,
		SIGMA_CONFIG,
		SIGMA_IDLE,
		SIGMA_CAPTURE,
		SIGMA_STOPPING,
		SIGMA_DOWNLOAD,
	} state;
	uint16_t lastts;
	uint16_t lastsample;
};

enum sigma_firmware_idx {
	SIGMA_FW_NONE,
	SIGMA_FW_50MHZ,
	SIGMA_FW_100MHZ,
	SIGMA_FW_200MHZ,
	SIGMA_FW_SYNC,
	SIGMA_FW_FREQ,
};

struct submit_buffer;

struct dev_context {
	struct {
		uint16_t vid, pid;
		uint32_t serno;
		uint16_t prefix;
		enum asix_device_type type;
	} id;
	struct {
		struct ftdi_context ctx;
		gboolean is_open, must_close;
	} ftdi;
	uint64_t samplerate;
	struct sr_sw_limits cfg_limits; /* Configured limits (user specified). */
	struct sr_sw_limits acq_limits; /* Acquisition limits (internal use). */
	struct sr_sw_limits feed_limits; /* Datafeed limits (internal use). */
	enum sigma_firmware_idx firmware_idx;
	int num_channels;
	int samples_per_event;
	uint64_t capture_ratio;
	struct sigma_trigger trigger;
	int use_triggers;
	struct sigma_state state;
	struct submit_buffer *buffer;
};

/* "Automatic" and forced USB connection open/close support. */
SR_PRIV int sigma_check_open(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_check_close(struct dev_context *devc);
SR_PRIV int sigma_force_open(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_force_close(struct dev_context *devc);

/* Send register content (simple and complex) to the hardware. */
SR_PRIV int sigma_write_register(struct dev_context *devc,
	uint8_t reg, uint8_t *data, size_t len);
SR_PRIV int sigma_set_register(struct dev_context *devc,
	uint8_t reg, uint8_t value);
SR_PRIV int sigma_write_trigger_lut(struct dev_context *devc,
	struct triggerlut *lut);

/* Samplerate constraints check, get/set/list helpers. */
SR_PRIV int sigma_normalize_samplerate(uint64_t want_rate, uint64_t *have_rate);
SR_PRIV uint64_t sigma_get_samplerate(const struct sr_dev_inst *sdi);
SR_PRIV GVariant *sigma_get_samplerates_list(void);

/* Preparation of data acquisition, spec conversion, hardware configuration. */
SR_PRIV int sigma_set_samplerate(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_set_acquire_timeout(struct dev_context *devc);
SR_PRIV int sigma_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_build_basic_trigger(struct dev_context *devc,
	struct triggerlut *lut);

/* Callback to periodically drive acuisition progress. */
SR_PRIV int sigma_receive_data(int fd, int revents, void *cb_data);

#endif
