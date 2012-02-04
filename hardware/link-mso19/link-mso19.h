/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
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

#ifndef LIBSIGROK_HARDWARE_LINK_MSO19_LINK_MSO19_H
#define LIBSIGROK_HARDWARE_LINK_MSO19_LINK_MSO19_H

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
	uint8_t buffer[1024];
	/* Input/output configuration for the samples buffer (?)*/
	uint8_t io[1024];
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

/* our private per-instance data */
struct mso {
	/* info */
	uint8_t hwmodel;
	uint8_t hwrev;
	uint32_t serial;
//	uint8_t num_sample_rates;
	/* calibration */
	double vbit;
	uint16_t dac_offset;
	uint16_t offset_range;
	/* register cache */
	uint8_t ctlbase1;
	uint8_t ctlbase2;
	/* state */
	uint8_t la_threshold;
	uint64_t cur_rate;
	uint8_t dso_probe_attn;
	uint8_t trigger_chan;
	uint8_t trigger_slope;
	uint8_t trigger_outsrc;
	uint8_t trigger_state;
	uint8_t la_trigger;
	uint8_t la_trigger_mask;
	double dso_trigger_voltage;
	uint16_t dso_trigger_width;
	struct mso_prototrig protocol_trigger;
	gpointer session_id;
	uint16_t buffer_n;
	char buffer[4096];
};

/* serial protocol */
#define mso_trans(a, v) \
	(((v) & 0x3f) | (((v) & 0xc0) << 6) | (((a) & 0xf) << 8) | \
	((~(v) & 0x20) << 1) | ((~(v) & 0x80) << 7))

const char mso_head[] = { 0x40, 0x4c, 0x44, 0x53, 0x7e };
const char mso_foot[] = { 0x7e };

/* bank agnostic registers */
#define REG_CTL2		15

/* bank 0 registers */
#define REG_BUFFER		1
#define REG_TRIGGER		2
#define REG_CLKRATE1		9
#define REG_CLKRATE2		10
#define REG_DAC1		12
#define REG_DAC2		13
/* possibly bank agnostic: */
#define REG_CTL1		14

/* bank 2 registers (SPI/I2C protocol trigger) */
#define REG_PT_WORD(x)	       (x)
#define REG_PT_MASK(x)	       (x+4)
#define REG_PT_SPIMODE          8

/* bits - REG_CTL1 */
#define BIT_CTL1_RESETFSM      (1 << 0)
#define BIT_CTL1_ARM	       (1 << 1)
#define BIT_CTL1_ADC_UNKNOWN4  (1 << 4) /* adc enable? */
#define BIT_CTL1_RESETADC      (1 << 6)
#define BIT_CTL1_LED	       (1 << 7)

/* bits - REG_CTL2 */
#define BITS_CTL2_BANK(x)      (x & 0x3)
#define BIT_CTL2_SLOWMODE      (1 << 5)

struct rate_map {
	uint32_t rate;
	uint16_t val;
	uint8_t slowmode;
};

static struct rate_map rate_map[] = {
	{ SR_MHZ(200),	0x0205, 0	},
	{ SR_MHZ(100),	0x0105, 0	},
	{ SR_MHZ(50),	0x0005, 0	},
	{ SR_MHZ(20),	0x0303, 0	},
	{ SR_MHZ(10),	0x0308, 0	},
	{ SR_MHZ(5),	0x030c, 0	},
	{ SR_MHZ(2),	0x0330, 0	},
	{ SR_MHZ(1),	0x0362, 0	},
	{ SR_KHZ(500),	0x03c6, 0	},
	{ SR_KHZ(200),	0x07f2, 0	},
	{ SR_KHZ(100),	0x0fe6, 0	},
	{ SR_KHZ(50),	0x1fce, 0	},
	{ SR_KHZ(20),	0x4f86, 0	},
	{ SR_KHZ(10),	0x9f0e, 0	},
	{ SR_KHZ(5),	0x03c7, 0x20	},
	{ SR_KHZ(2),	0x07f3, 0x20	},
	{ SR_KHZ(1),	0x0fe7, 0x20	},
	{ 500,		0x1fcf, 0x20	},
	{ 200,		0x4f87, 0x20	},
	{ 100,		0x9f0f, 0x20	},
};

/* FIXME: Determine corresponding voltages */
static uint16_t la_threshold_map[] = {
	0x8600,
	0x8770,
	0x88ff,
	0x8c70,
	0x8eff,
	0x8fff,
};

#endif
