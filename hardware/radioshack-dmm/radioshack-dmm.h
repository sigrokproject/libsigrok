/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#ifndef LIBSIGROK_HARDWARE_RADIOSHACK_DMM_RADIOSHACK_DMM_H
#define LIBSIGROK_HARDWARE_RADIOSHACK_DMM_RADIOSHACK_DMM_H

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "radioshack-dmm: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define RS_DMM_BUFSIZE		256

/* Byte 1 of the packet, and the modes it represents */
#define RS_22_812_IND1_HZ	0x80
#define RS_22_812_IND1_OHM	0x40
#define RS_22_812_IND1_KILO	0x20
#define RS_22_812_IND1_MEGA	0x10
#define RS_22_812_IND1_FARAD	0x08
#define RS_22_812_IND1_AMP	0x04
#define RS_22_812_IND1_VOLT	0x02
#define RS_22_812_IND1_MILI	0x01
/* Byte 2 of the packet, and the modes it represents */
#define RS_22_812_IND2_MICRO	0x80
#define RS_22_812_IND2_NANO	0x40
#define RS_22_812_IND2_DBM	0x20
#define RS_22_812_IND2_SEC	0x10
#define RS_22_812_IND2_DUTY	0x08
#define RS_22_812_IND2_HFE	0x04
#define RS_22_812_IND2_REL	0x02
#define RS_22_812_IND2_MIN	0x01
/* Byte 7 of the packet, and the modes it represents */
#define RS_22_812_INFO_BEEP	0x80
#define RS_22_812_INFO_DIODE	0x30
#define RS_22_812_INFO_BAT	0x20
#define RS_22_812_INFO_HOLD	0x10
#define RS_22_812_INFO_NEG	0x08
#define RS_22_812_INFO_AC	0x04
#define RS_22_812_INFO_RS232	0x02
#define RS_22_812_INFO_AUTO	0x01
/* Instead of a decimal point, digit 4 carries the MAX flag */
#define RS_22_812_DIG4_MAX	0x08
/* Mask to remove the decimal point from a digit */
#define RS_22_812_DP_MASK	0x08

/* What the LCD values represent */
#define RS_22_812_LCD_0		0xd7
#define RS_22_812_LCD_1		0x50
#define RS_22_812_LCD_2		0xb5
#define RS_22_812_LCD_3		0xf1
#define RS_22_812_LCD_4		0x72
#define RS_22_812_LCD_5		0xe3
#define RS_22_812_LCD_6		0xe7
#define RS_22_812_LCD_7		0x51
#define RS_22_812_LCD_8		0xf7
#define RS_22_812_LCD_9		0xf3

#define RS_22_812_LCD_C		0x87
#define RS_22_812_LCD_E
#define RS_22_812_LCD_F
#define RS_22_812_LCD_h		0x66
#define RS_22_812_LCD_H		0x76
#define RS_22_812_LCD_I
#define RS_22_812_LCD_n
#define RS_22_812_LCD_P		0x37
#define RS_22_812_LCD_r

#define RS_22_812_PACKET_SIZE	9

struct rs_22_812_packet {
	uint8_t mode;
	uint8_t indicatrix1;
	uint8_t indicatrix2;
	uint8_t digit4;
	uint8_t digit3;
	uint8_t digit2;
	uint8_t digit1;
	uint8_t info;
	uint8_t checksum;
};

enum {
	RS_22_812_MODE_DC_V		= 0,
	RS_22_812_MODE_AC_V		= 1,
	RS_22_812_MODE_DC_UA		= 2,
	RS_22_812_MODE_DC_MA		= 3,
	RS_22_812_MODE_DC_A 		= 4,
	RS_22_812_MODE_AC_UA		= 5,
	RS_22_812_MODE_AC_MA		= 6,
	RS_22_812_MODE_AC_A		= 7,
	RS_22_812_MODE_OHM		= 8,
	RS_22_812_MODE_FARAD		= 9,
	RS_22_812_MODE_HZ		= 10,
	RS_22_812_MODE_VOLT_HZ		= 11,
	RS_22_812_MODE_AMP_HZ		= 12,
	RS_22_812_MODE_DUTY		= 13,
	RS_22_812_MODE_VOLT_DUTY	= 14,
	RS_22_812_MODE_AMP_DUTY		= 15,
	RS_22_812_MODE_WIDTH		= 16,
	RS_22_812_MODE_VOLT_WIDTH	= 17,
	RS_22_812_MODE_AMP_WIDTH	= 18,
	RS_22_812_MODE_DIODE		= 19,
	RS_22_812_MODE_CONT		= 20,
	RS_22_812_MODE_HFE		= 21,
	RS_22_812_MODE_LOGIC		= 22,
	RS_22_812_MODE_DBM		= 23,
	// RS_22_812_MODE_EF		= 24,
	RS_22_812_MODE_TEMP		= 25,
	RS_22_812_MODE_INVALID		= 26,
};

SR_PRIV gboolean rs_22_812_packet_valid(const struct rs_22_812_packet *rs_packet);

/* Private, per-device-instance driver context. */
struct dev_context {
	uint64_t limit_samples;
	struct sr_serial_dev_inst *serial;
	char *serialcomm;

	/* Opaque pointer passed in by the frontend. */
	void *cb_data;

	/* Runtime. */
	uint64_t num_samples;
	uint8_t buf[RS_DMM_BUFSIZE];
	size_t bufoffset;
	size_t buflen;
};

SR_PRIV int radioshack_dmm_receive_data(int fd, int revents, void *cb_data);

#endif
