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

#ifndef LIBSIGROK_HARDWARE_RADIOSHACK_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RADIOSHACK_DMM_PROTOCOL_H

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "radioshack-dmm: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

#define RS_DMM_BUFSIZE		256

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
SR_PRIV gboolean rs_22_812_packet_valid(const struct rs_22_812_packet *rs_packet);

#endif
