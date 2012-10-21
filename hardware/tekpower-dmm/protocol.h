/*
 * This file is part of the sigrok project.
 *
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

#ifndef LIBSIGROK_HARDWARE_TEKPOWER_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_TEKPOWER_DMM_PROTOCOL_H

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "tekpower-dmm: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define DMM_BUFSIZE  256

/* Flags present in the packet */
#define LCD14_AC	(1<<23)
#define LCD14_DC	(1<<22)
#define LCD14_AUTO	(1<<21)
#define LCD14_RS232	(1<<20)
#define LCD14_MICRO	(1<<19)
#define LCD14_NANO	(1<<18)
#define LCD14_KILO	(1<<17)
#define LCD14_DIODE	(1<<16)
#define LCD14_MILLI	(1<<15)
#define LCD14_DUTY	(1<<14)
#define LCD14_MEGA	(1<<13)
#define LCD14_BEEP	(1<<12)
#define LCD14_FARAD	(1<<11)
#define LCD14_OHM	(1<<10)
#define LCD14_REL	(1<< 9)
#define LCD14_HOLD	(1<< 8)
#define LCD14_AMP	(1<< 7)
#define LCD14_VOLT	(1<< 6)
#define LCD14_HZ	(1<< 5)
#define LCD14_LOW_BATT	(1<< 4)
#define LCD14_HFE	(1<< 3)
#define LCD14_CELSIUS	(1<< 2)
#define LCD14_RSVD1	(1<< 1)
#define LCD14_RSVD0	(0<< 0)

/* mask to remove the decimal point from a digit */
#define LCD14_DP_MASK	(0x80)
#define LCD14_D0_NEG	LCD14_DP_MASK
/* mask to remove the syncronization nibble */
#define LCD14_SYNC_MASK	(0xF0)

/* What the LCD values represent */
#define LCD14_LCD_0 0x7d
#define LCD14_LCD_1 0x05
#define LCD14_LCD_2 0x5b
#define LCD14_LCD_3 0x1f
#define LCD14_LCD_4 0x27
#define LCD14_LCD_5 0x3e
#define LCD14_LCD_6 0x7e
#define LCD14_LCD_7 0x15
#define LCD14_LCD_8 0x7f
#define LCD14_LCD_9 0x3f


#define LCD14_LCD_INVALID	0xff

typedef struct {
	uint8_t raw[14];
} lcd14_packet;

typedef struct {
	uint8_t digit[4];
	uint32_t flags;
} lcd14_data;

#define LCD14_PACKET_SIZE (sizeof(lcd14_packet))

SR_PRIV gboolean lcd14_is_packet_valid(const lcd14_packet *packet,
				       lcd14_data *data);

/* Private, per-device-instance driver context. */
struct dev_context {
	uint64_t limit_samples;
	struct sr_serial_dev_inst *serial;
	char *serialcomm;

	/* Opaque pointer passed in by the frontend. */
	void *cb_data;

	/* Runtime. */
	uint64_t num_samples;
	uint8_t buf[DMM_BUFSIZE];
	size_t bufoffset;
	size_t buflen;
};


SR_PRIV int lcd14_receive_data(int fd, int revents, void *cb_data);

#endif /* LIBSIGROK_HARDWARE_TEKPOWER_DMM_PROTOCOL_H */
