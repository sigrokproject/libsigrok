/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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


#ifndef LIBSIGROK_FLUKE_DMM_H
#define LIBSIGROK_FLUKE_DMM_H

#define FLUKEDMM_BUFSIZE  256

/* Supported models */
enum {
	FLUKE_187 = 1,
	FLUKE_287,
};

/* Supported device profiles */
struct flukedmm_profile {
	int model;
	const char *modelname;
	/* How often to poll, in ms. */
	int poll_period;
};

/* Private, per-device-instance driver context. */
struct dev_context {
	const struct flukedmm_profile *profile;
	uint64_t limit_samples;
	uint64_t limit_msec;
	struct sr_serial_dev_inst *serial;
	char *serialcomm;

	/* Opaque pointer passed in by the frontend. */
	void *cb_data;

	/* Runtime. */
	uint64_t num_samples;
	char buf[FLUKEDMM_BUFSIZE];
	int buflen;
	int64_t cmd_sent_at;
	int expect_response;
};

SR_PRIV int fluke_receive_data(int fd, int revents, void *cb_data);

#endif /* LIBSIGROK_FLUKE_DMM_H */
