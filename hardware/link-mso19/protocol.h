/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
 * Copyright (C) 2013 Lior Elazary <lelazary@yahoo.com>
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

#define USB_VENDOR "3195"
#define USB_PRODUCT "f190"

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "mso19: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define NUM_PROBES             8
#define NUM_TRIGGER_STAGES     4
#define TRIGGER_TYPES          "01" //the first r/f is used for the whole group
#define SERIALCOMM "460800/8n1/flow=2" 
#define SERIALCONN "/dev/ttyUSB0" 
#define CLOCK_RATE             SR_MHZ(100)
#define MIN_NUM_SAMPLES        4

#define MSO_TRIGGER_UNKNOWN	'!'
#define MSO_TRIGGER_UNKNOWN1	'1'
#define MSO_TRIGGER_UNKNOWN2	'2'
#define MSO_TRIGGER_UNKNOWN3	'3'
#define MSO_TRIGGER_WAIT	'4'
#define MSO_TRIGGER_FIRED	'5'
#define MSO_TRIGGER_DATAREADY	'6'

enum trigger_slopes {
  SLOPE_POSITIVE = 0, 
  SLOPE_NEGATIVE,
};

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

/* Private, per-device-instance driver context. */
struct dev_context {
	/* info */
	uint8_t hwmodel;
	uint8_t hwrev;
	struct sr_serial_dev_inst *serial;
//	uint8_t num_sample_rates;
	/* calibration */
	double vbit;
	uint16_t dac_offset;
	uint16_t offset_range;
  uint64_t limit_samples;
  uint64_t num_samples;
	/* register cache */
	uint8_t ctlbase1;
	uint8_t ctlbase2;
	/* state */
	uint8_t la_threshold;
	uint64_t cur_rate;
	uint8_t dso_probe_attn;
  int8_t  use_trigger;
	uint8_t trigger_chan;
	uint8_t trigger_slope;
	uint8_t trigger_outsrc;
	uint8_t trigger_state;
	uint8_t trigger_holdoff[2];
	uint8_t la_trigger;
	uint8_t la_trigger_mask;
	double dso_trigger_voltage;
	uint16_t dso_trigger_width;
	struct mso_prototrig protocol_trigger;
	void *session_dev_id;
	uint16_t buffer_n;
	char buffer[4096];
};

SR_PRIV int mso_parse_serial(const char *iSerial, const char *iProduct,
    struct dev_context *ctx);
SR_PRIV int mso_check_trigger(struct sr_serial_dev_inst *serial, uint8_t *info);
SR_PRIV int mso_reset_adc(struct sr_dev_inst *sdi);
SR_PRIV int mso_clkrate_out(struct sr_serial_dev_inst *serial, uint16_t val);
SR_PRIV int mso_configure_rate(struct sr_dev_inst *sdi, uint32_t rate);
SR_PRIV int mso_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int mso_configure_trigger(struct sr_dev_inst *sdi);
SR_PRIV int mso_configure_threshold_level(struct sr_dev_inst *sdi);
SR_PRIV int mso_read_buffer(struct sr_dev_inst *sdi);
SR_PRIV int mso_arm(struct sr_dev_inst *sdi);
SR_PRIV int mso_force_capture(struct sr_dev_inst *sdi);
SR_PRIV int mso_dac_out(struct sr_dev_inst *sdi, uint16_t val);
SR_PRIV inline uint16_t mso_calc_raw_from_mv(struct dev_context *devc);
SR_PRIV int mso_reset_fsm(struct sr_dev_inst *sdi);
SR_PRIV int mso_toggle_led(struct sr_dev_inst *sdi, int state);

SR_PRIV int mso_configure_probes(const struct sr_dev_inst *sdi);
SR_PRIV void stop_acquisition(const struct sr_dev_inst *sdi);

///////////////////////
//

/* serial protocol */
#define mso_trans(a, v) \
	(((v) & 0x3f) | (((v) & 0xc0) << 6) | (((a) & 0xf) << 8) | \
	((~(v) & 0x20) << 1) | ((~(v) & 0x80) << 7))

SR_PRIV static const char mso_head[] = { 0x40, 0x4c, 0x44, 0x53, 0x7e };
SR_PRIV static const char mso_foot[] = { 0x7e };

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
