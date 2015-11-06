/*
 * This file is part of the libsigrok project.
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

#define LOG_PREFIX "serial-dmm"

struct dmm_info {
	/** libsigrok driver info struct. */
	struct sr_dev_driver di;
	/** Manufacturer/brand. */
	const char *vendor;
	/** Model. */
	const char *device;
	/** serialconn string. */
	const char *conn;
	/** Baud rate. */
	uint32_t baudrate;
	/** Packet size in bytes. */
	int packet_size;
	/** Request timeout [ms] before request is considered lost and a new
	 *  one is sent. Used only if device needs polling. */
	int64_t req_timeout_ms;
	/** Delay between reception of packet and next request. Some DMMs
	 *  need this. Used only if device needs polling. */
	int64_t req_delay_ms;
	/** Packet request function. */
	int (*packet_request)(struct sr_serial_dev_inst *);
	/** Packet validation function. */
	gboolean (*packet_valid)(const uint8_t *);
	/** Packet parsing function. */
	int (*packet_parse)(const uint8_t *, float *,
			    struct sr_datafeed_analog_old *, void *);
	/** */
	void (*dmm_details)(struct sr_datafeed_analog_old *, void *);
	/** Size of chipset info struct. */
	gsize info_size;
};

#define DMM_BUFSIZE 256

/** Private, per-device-instance driver context. */
struct dev_context {
	/** The current sampling limit (in number of samples). */
	uint64_t limit_samples;

	/** The time limit (in milliseconds). */
	uint64_t limit_msec;

	/** Opaque pointer passed in by the frontend. */
	void *cb_data;

	/** The current number of already received samples. */
	uint64_t num_samples;

	/** The starting time of current sampling run. */
	int64_t starttime;

	uint8_t buf[DMM_BUFSIZE];
	int bufoffset;
	int buflen;

	/** The timestamp [µs] to send the next request.
	 *  Used only if device needs polling. */
	int64_t req_next_at;
};

SR_PRIV int req_packet(struct sr_dev_inst *sdi);
SR_PRIV int receive_data(int fd, int revents, void *cb_data);

#endif
