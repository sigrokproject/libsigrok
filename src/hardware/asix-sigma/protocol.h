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

/* Mask to isolate one bit, mask to span a number of bits. */
#define BIT(pos)		(1UL << (pos))
#define BITS_MASK(count)	((1UL << (count)) - 1)

#define HI4(b)			(((b) >> 4) & 0x0f)
#define LO4(b)			(((b) >> 0) & 0x0f)

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
#define REG_ADDR_ADJUST		BIT(0) /* Auto adjust register address. */
#define REG_ADDR_DOWN		BIT(1) /* 1 decrement, 0 increment. */
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
 *
 * Update: The documentation may be incorrect, or the FPGA netlist may
 * be incomplete. Experiments show that registers beyond 0x0f can get
 * accessed, USB communication passes, but data bytes are always 0xff.
 * Are several firmware versions around, and the documentation does not
 * match the one that ships with sigrok?
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
	/* Reserved for plugin features. */
	REG_PLUGIN_START	= 16,
	REG_PLUGIN_STOP		= 256,
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
	/* Reserved for plugin features. See above. */
};

#define CLKSEL_CLKSEL8		BIT(0)
#define CLKSEL_PINMASK		BITS_MASK(4)
#define CLKSEL_RISING		BIT(4)
#define CLKSEL_FALLING		BIT(5)

#define TRGSEL_SELINC_MASK	BITS_MASK(2)
#define TRGSEL_SELINC_SHIFT	0
#define TRGSEL_SELRES_MASK	BITS_MASK(2)
#define TRGSEL_SELRES_SHIFT	2
#define TRGSEL_SELA_MASK	BITS_MASK(2)
#define TRGSEL_SELA_SHIFT	4
#define TRGSEL_SELB_MASK	BITS_MASK(2)
#define TRGSEL_SELB_SHIFT	6
#define TRGSEL_SELC_MASK	BITS_MASK(2)
#define TRGSEL_SELC_SHIFT	8
#define TRGSEL_SELPRESC_MASK	BITS_MASK(4)
#define TRGSEL_SELPRESC_SHIFT	12

enum trgsel_selcode_t {
	TRGSEL_SELCODE_LEVEL = 0,
	TRGSEL_SELCODE_FALL = 1,
	TRGSEL_SELCODE_RISE = 2,
	TRGSEL_SELCODE_EVENT = 3,
	TRGSEL_SELCODE_NEVER = 3,
};

#define TRGSEL2_PINS_MASK	BITS_MASK(3)
#define TRGSEL2_PINPOL_RISE	BIT(3)
#define TRGSEL2_LUT_ADDR_MASK	BITS_MASK(4)
#define TRGSEL2_LUT_WRITE	BIT(4)
#define TRGSEL2_RESET		BIT(5)
#define TRGSEL2_LEDSEL0		BIT(6)
#define TRGSEL2_LEDSEL1		BIT(7)

/* WRITE_MODE register fields. */
#define WMR_SDRAMWRITEEN	BIT(0)
#define WMR_SDRAMREADEN		BIT(1)
#define WMR_TRGRES		BIT(2)
#define WMR_TRGEN		BIT(3)
#define WMR_FORCESTOP		BIT(4)
#define WMR_TRGSW		BIT(5)
/* not used: bit position 6 */
#define WMR_SDRAMINIT		BIT(7)

/* READ_MODE register fields. */
#define RMR_SDRAMWRITEEN	BIT(0)
#define RMR_SDRAMREADEN		BIT(1)
/* not used: bit position 2 */
#define RMR_TRGEN		BIT(3)
#define RMR_ROUND		BIT(4)
#define RMR_TRIGGERED		BIT(5)
#define RMR_POSTTRIGGERED	BIT(6)
/* not used: bit position 7 */

/*
 * Trigger options. First and second write are similar, but _some_
 * positions change their meaning.
 */
#define TRGOPT_TRGIEN		BIT(7)
#define TRGOPT_TRGOEN		BIT(6)
#define TRGOPT_TRGOINEN		BIT(5) /* 1st write */
#define TRGOPT_TRGINEG		TRGOPT1_TRGOINEN /* 2nd write */
#define TRGOPT_TRGOEVNTEN	BIT(4) /* 1st write */
#define TRGOPT_TRGOPIN		TRGOPT1_TRGOEVNTEN /* 2nd write */
#define TRGOPT_TRGOOUTEN	BIT(3) /* 1st write */
#define TRGOPT_TRGOLONG		TRGOPT1_TRGOOUTEN /* 2nd write */
#define TRGOPT_TRGOUTR_OUT	BIT(1)
#define TRGOPT_TRGOUTR_EN	BIT(0)
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
#define ROW_MASK		BITS_MASK(ROW_SHIFT)
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
	gboolean trgout_resistor_enable, trgout_resistor_pullup;
	gboolean trgout_resistor_enable2, trgout_resistor_pullup2;
	gboolean trgout_bytrigger, trgout_byevent, trgout_bytriggerin;
	gboolean trgout_long, trgout_pin; /* 1ms pulse, 1k resistor */
	gboolean trgin_negate, trgout_enable, trgin_enable;
};

struct triggerlut {
	uint16_t m0d[4], m1d[4], m2d[4];
	uint16_t m3q, m3s, m4;
	struct {
		uint8_t selpresc;
		uint8_t sela, selb, selc;
		uint8_t selinc, selres;
		uint16_t cmpa, cmpb;
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

enum sigma_firmware_idx {
	SIGMA_FW_NONE,
	SIGMA_FW_50MHZ,
	SIGMA_FW_100MHZ,
	SIGMA_FW_200MHZ,
	SIGMA_FW_SYNC,
	SIGMA_FW_FREQ,
};

enum ext_clock_edge_t {
	SIGMA_CLOCK_EDGE_RISING,
	SIGMA_CLOCK_EDGE_FALLING,
	SIGMA_CLOCK_EDGE_EITHER,
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
	struct {
		uint64_t samplerate;
		gboolean use_ext_clock;
		size_t clock_pin;
		enum ext_clock_edge_t clock_edge;
	} clock;
	struct {
		/*
		 * User specified configuration values, in contrast to
		 * internal arrangement of acquisition, and submission
		 * to the session feed.
		 */
		struct sr_sw_limits config;
		struct sr_sw_limits acquire;
		struct sr_sw_limits submit;
	} limit;
	enum sigma_firmware_idx firmware_idx;
	struct sigma_sample_interp {
		/* Interpretation of sample memory. */
		size_t num_channels;
		size_t samples_per_event;
		struct {
			uint16_t ts;
			uint16_t sample;
		} last;
		struct sigma_location {
			size_t raw, line, cluster, event;
		} start, stop, trig, iter, trig_arm;
		struct {
			size_t lines_total, lines_done;
			size_t lines_per_read; /* USB transfer limit */
			size_t lines_rcvd;
			struct sigma_dram_line *rcvd_lines;
			struct sigma_dram_line *curr_line;
		} fetch;
		struct {
			gboolean armed;
			gboolean matched;
			size_t evt_remain;
		} trig_chk;
	} interp;
	uint64_t capture_ratio;
	struct sigma_trigger trigger;
	gboolean use_triggers;
	gboolean late_trigger_timeout;
	enum {
		SIGMA_UNINITIALIZED = 0,
		SIGMA_CONFIG,
		SIGMA_IDLE,
		SIGMA_CAPTURE,
		SIGMA_STOPPING,
		SIGMA_DOWNLOAD,
	} state;
	struct submit_buffer *buffer;
};

/* "Automatic" and forced USB connection open/close support. */
SR_PRIV int sigma_check_open(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_check_close(struct dev_context *devc);
SR_PRIV int sigma_force_open(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_force_close(struct dev_context *devc);

/* Save configuration across sessions, to reduce cost of continuation. */
SR_PRIV int sigma_store_hw_config(const struct sr_dev_inst *sdi);
SR_PRIV int sigma_fetch_hw_config(const struct sr_dev_inst *sdi);

/* Send register content (simple and complex) to the hardware. */
SR_PRIV int sigma_write_register(struct dev_context *devc,
	uint8_t reg, uint8_t *data, size_t len);
SR_PRIV int sigma_set_register(struct dev_context *devc,
	uint8_t reg, uint8_t value);
SR_PRIV int sigma_write_trigger_lut(struct dev_context *devc,
	struct triggerlut *lut);

/* Samplerate constraints check, get/set/list helpers. */
SR_PRIV int sigma_normalize_samplerate(uint64_t want_rate, uint64_t *have_rate);
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
