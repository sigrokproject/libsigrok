/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
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

#ifndef SIGROK_LINK_MSO19_H
#define SIGROK_LINK_MSO19_H

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
	uint8_t ctlbase;
	uint8_t slowmode;
	/* state */
	uint8_t la_threshold;
	uint64_t cur_rate;
	uint8_t dso_probe_attn;
	uint8_t trigger_chan;
	uint8_t trigger_slope;
	uint8_t trigger_spimode;
	uint8_t trigger_outsrc;
	uint8_t trigger_state;
	uint8_t la_trigger;
	uint8_t la_trigger_mask;
	double dso_trigger_voltage;
	uint16_t dso_trigger_width;
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

/* registers */
#define REG_BUFFER		1
#define REG_TRIGGER		2
#define REG_CLKRATE1		9
#define REG_CLKRATE2		10
#define REG_DAC1		12
#define REG_DAC2		13
#define REG_CTL			14

/* bits */
#define BIT_CTL_RESETFSM	(1 << 0)
#define BIT_CTL_ARM		(1 << 1)
#define BIT_CTL_ADC_UNKNOWN4	(1 << 4) /* adc enable? */
#define BIT_CTL_RESETADC	(1 << 6)
#define BIT_CTL_LED		(1 << 7)

struct rate_map {
	uint32_t rate;
	uint16_t val;
	uint8_t slowmode;
};

static struct rate_map rate_map[] = {
	{ MHZ(200),	0x0205, 0	},
	{ MHZ(100),	0x0105, 0	},
	{ MHZ(50),	0x0005, 0	},
	{ MHZ(20),	0x0303, 0	},
	{ MHZ(10),	0x0308, 0	},
	{ MHZ(5),	0x030c, 0	},
	{ MHZ(2),	0x0330, 0	},
	{ MHZ(1),	0x0362, 0	},
	{ KHZ(500),	0x03c6, 0	},
	{ KHZ(200),	0x07f2, 0	},
	{ KHZ(100),	0x0fe6, 0	},
	{ KHZ(50),	0x1fce, 0	},
	{ KHZ(20),	0x4f86, 0	},
	{ KHZ(10),	0x9f0e, 0	},
	{ KHZ(5),	0x03c7, 0x20	},
	{ KHZ(2),	0x07f3, 0x20	},
	{ KHZ(1),	0x0fe7, 0x20	},
	{ 500,		0x1fcf, 0x20	},
	{ 200,		0x4f87, 0x20	},
	{ 100,		0x9f0f, 0x20	},
};

/* FIXME: Determine corresponding voltages */
uint16_t la_threshold_map[] = {
	0x8600,
	0x8770,
	0x88ff,
	0x8c70,
	0x8eff,
	0x8fff,
};

#endif
