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

#ifndef LIBSIGROK_HARDWARE_SERIAL_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SERIAL_DMM_PROTOCOL_H

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "serial-dmm: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

enum {
	DIGITEK_DT4000ZC,
	TEKPOWER_TP4000ZC,
	METEX_ME31,
	PEAKTECH_3410,
	MASTECH_MAS345,
};

struct dmm_info {
	char *vendor;
	char *device;
	char *conn;
	uint32_t baudrate;
	int packet_size;
	int (*packet_request)(struct sr_serial_dev_inst *);
	gboolean (*packet_valid)(const uint8_t *);
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog *, void *);
	void (*dmm_details)(struct sr_datafeed_analog *, void *);
};

SR_PRIV struct dmm_info dmms[5];

#define DMM_BUFSIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received samples. */
	uint64_t num_samples;

	struct sr_serial_dev_inst *serial;

	uint8_t buf[DMM_BUFSIZE];
	int bufoffset;
	int buflen;
};

SR_PRIV int digitek_dt4000zc_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int tekpower_tp4000zc_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int metex_me31_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int peaktech_3410_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int mastech_mas345_receive_data(int fd, int revents, void *cb_data);

SR_PRIV void dmm_details_tp4000zc(struct sr_datafeed_analog *analog, void *info);
SR_PRIV void dmm_details_dt4000zc(struct sr_datafeed_analog *analog, void *info);

#endif
