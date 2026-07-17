/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
 * Copyright (C) 2013 Lior Elazary <lelazary@yahoo.com>
 * Copyright (C) 2022 Paul Kasemir <paul.kasemir@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_LINK_MSO19_PROTOCOL_H
#define LIBSIGROK_HARDWARE_LINK_MSO19_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "link-mso19"

#define USB_VENDOR		0x3195
#define USB_PRODUCT		0xf190

#define SERIALCOMM		"460800/8n1/flow=2"
#define MSO_NUM_SAMPLES		1024
#define MSO_NUM_LOGIC_CHANNELS	8

#define CG_IS_DIGITAL(cg) (cg && cg->name[0] == 'L')
#define CG_IS_ANALOG(cg) (cg && cg->name[0] == 'D')

/* Structure for the pattern generator state */
struct mso_patgen {
	/* Pattern generator clock config */
	uint16_t clock;
	/* Buffer start address */
	uint16_t start;
	/* Buffer end address */
	uint16_t end;
	/* Pattern generator config */
	uint8_t config;
	/* Samples buffer */
	uint8_t buffer[MSO_NUM_SAMPLES];
	/* Input/output configuration for the samples buffer (?) */
	uint8_t io[MSO_NUM_SAMPLES];
	/* Number of loops for the pattern generator */
	uint8_t loops;
	/* Bit enable mask for the I/O lines */
	uint8_t mask;
};

/* Data structure for the protocol trigger state */
struct mso_prototrig {
	/* Word match buffer */
	uint8_t word[4];
	/* Masks for the wordmatch buffer */
	uint8_t mask[4];
	/* SPI mode 0, 1, 2, 3. Set to 0 for I2C */
	uint8_t spimode;
};

struct dev_context {
	/* info */
	uint8_t hwmodel;
	uint8_t hwrev;
	struct sr_serial_dev_inst *serial;
//      uint8_t num_sample_rates;
	/* calibration */
	double vbit;
	uint16_t dac_offset;
	double offset_vbit;
	uint64_t limit_samples;
	uint64_t num_samples;

	/* register cache */
	uint8_t ctlbase1;
	uint8_t ctlbase2;
	uint16_t ctltrig_pos;
	uint8_t status;

	uint8_t logic_threshold;
	uint16_t logic_threshold_value;
	uint64_t cur_rate;
	const char *coupling;
	uint16_t dso_probe_factor;
	uint8_t trigger_source;
	uint8_t dso_trigger_slope;
	uint8_t trigger_outsrc;
	uint8_t la_trigger_slope;
	uint8_t la_trigger;
	uint8_t la_trigger_mask;
	double horiz_triggerpos;
	double dso_trigger_level;
	double dso_trigger_adjusted;
	double dso_offset;
	double dso_offset_adjusted;
	uint16_t dso_offset_value;
	uint16_t dso_trigger_width;
	struct mso_prototrig protocol_trigger;
	uint16_t buffer_n;
	char buffer[4096];
};

/* from api.c */
SR_PRIV uint8_t mso_calc_trigger_register(struct dev_context *devc,
		uint16_t threshold_value);

/* from protocol.c */
SR_PRIV int mso_parse_serial(const char *serial_num, const char *product,
			     struct dev_context *ctx);
SR_PRIV int mso_read_status(struct sr_serial_dev_inst *serial,
			    uint8_t *status);
SR_PRIV int mso_reset_adc(const struct sr_dev_inst *sdi);
SR_PRIV int mso_clkrate_out(struct sr_serial_dev_inst *serial, uint16_t val);
SR_PRIV int mso_configure_rate(const struct sr_dev_inst *sdi, uint32_t rate);
SR_PRIV int mso_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int mso_configure_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int mso_configure_threshold_level(const struct sr_dev_inst *sdi);
SR_PRIV int mso_read_buffer(struct sr_dev_inst *sdi);
SR_PRIV int mso_arm(const struct sr_dev_inst *sdi);
SR_PRIV int mso_force_capture(struct sr_dev_inst *sdi);
SR_PRIV int mso_dac_out(const struct sr_dev_inst *sdi, uint16_t val);
SR_PRIV int mso_reset_fsm(const struct sr_dev_inst *sdi);

SR_PRIV int mso_configure_channels(const struct sr_dev_inst *sdi);
SR_PRIV void stop_acquisition(const struct sr_dev_inst *sdi);

/* bank agnostic registers */
#define REG_CTL2		15

/* bank 0 registers */
#define REG_BUFFER		1
#define REG_STATUS		2
#define REG_TRIG_THRESH		3
#define REG_TRIG		4
#define REG_TRIG_LA_VAL		5
#define REG_TRIG_LA_MASK	6
#define REG_TRIG_POS_LSB	7
#define REG_TRIG_POS_MSB	8
#define REG_CLKRATE1		9
#define REG_CLKRATE2		10
#define REG_TRIG_WIDTH		11
#define REG_DAC_MSB		12
#define REG_DAC_LSB		13
/* possibly bank agnostic: */
#define REG_CTL1		14

/* bank 2 registers (SPI/I2C protocol trigger) */
#define REG_PT_WORD(x)		(x)
#define REG_PT_MASK(x)		(x + 4)
#define REG_PT_SPIMODE		8

/* bits - REG_STATUS */
#define BITS_STATUS_ACTION(x)		(x & STATUS_MASK)
enum {
	STATUS_MASK =			0x7,
	STATUS_NOT_RUNNING =		1,
	STATUS_TRIGGER_PRE_FILL =	3,
	STATUS_TRIGGER_WAIT =		4,
	STATUS_TRIGGER_POST_FILL =	5,
	STATUS_DATA_READY =		6,
	BIT_STATUS_ARMED =		1 << 4,
	BIT_STATUS_OK =			1 << 5,
};

/* bits - REG_TRIG */
enum {
	TRIG_THRESH_MSB_MASK =	3 << 0,

	TRIG_EDGE_RISING =	0 << 2,
	TRIG_EDGE_FALLING =	1 << 2,
	TRIG_EDGE_T_F =		0 << 2,
	TRIG_EDGE_F_T =		1 << 2,

	TRIG_OUT_TRIGGER =	0 << 3,
	TRIG_OUT_PG =		1 << 3,
	TRIG_OUT_NOISE =	3 << 3,

	TRIG_SRC_DSO =		0 << 5,
	TRIG_SRC_DSO_PULSE_GE =	1 << 5,
	TRIG_SRC_DSO_PULSE_LT =	2 << 5,
	TRIG_SRC_SPI =		4 << 5,
	TRIG_SRC_I2C =		5 << 5,
	TRIG_SRC_LA =		7 << 5,
};

/* bits - REG_TRIG_POS_MSB */
enum {
	TRIG_POS_VALUE_MASK =	0x7fff,
	TRIG_POS_IS_POSITIVE =	0 << 15,
	TRIG_POS_IS_NEGATIVE =	1 << 15,
};

/* bits - REG_DAC */
enum {
	DAC_DSO_VALUE_MASK =	0xfff,
	DAC_SELECT_DSO =	0 << 15,
	DAC_SELECT_LA =		1 << 15,
};

/* bits - REG_CTL1 */
#define BIT_CTL1_RESETFSM		(1 << 0)
#define BIT_CTL1_ARM		    	(1 << 1)
#define BIT_CTL1_FORCE_TRIGGER	    	(1 << 3)
#define BIT_CTL1_ADC_ENABLE	    	(1 << 4)
#define BIT_CTL1_LOAD_DAC		(1 << 5)
#define BIT_CTL1_RESETADC		(1 << 6)
#define BIT_CTL1_DC_COUPLING		(1 << 7)

/* bits - REG_CTL2 */
#define BITS_CTL2_BANK(x)	(x & 0x3)
#define BIT_CTL2_SLOWMODE	(1 << 5)

struct rate_map {
	uint32_t rate;
	uint16_t val;
	uint8_t slowmode;
};

static const struct rate_map rate_map[] = {
	{ SR_MHZ(200), 0x0205, 0 },
	{ SR_MHZ(100), 0x0105, 0 },
	{ SR_MHZ(50),  0x0005, 0 },
	{ SR_MHZ(20),  0x0303, 0 },
	{ SR_MHZ(10),  0x0308, 0 },
	{ SR_MHZ(5),   0x0312, 0 },
	{ SR_MHZ(2),   0x0330, 0 },
	{ SR_MHZ(1),   0x0362, 0 },
	{ SR_KHZ(500), 0x03c6, 0 },
	{ SR_KHZ(200), 0x07f2, 0 },
	{ SR_KHZ(100), 0x0fe6, 0 },
	{ SR_KHZ(50),  0x1fce, 0 },
	{ SR_KHZ(20),  0x4f86, 0 },
	{ SR_KHZ(10),  0x9f0e, 0 },
	{ SR_KHZ(5),   0x03c7, 0x20 },
	{ SR_KHZ(2),   0x07f3, 0x20 },
	{ SR_KHZ(1),   0x0fe7, 0x20 },
	{ SR_HZ(500),  0x1fcf, 0x20 },
	{ SR_HZ(200),  0x4f87, 0x20 },
	{ SR_HZ(100),  0x9f0f, 0x20 },
};

#endif
