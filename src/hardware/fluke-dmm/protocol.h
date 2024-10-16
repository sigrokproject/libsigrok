/*
 * This file is part of the libsigrok project.
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

#ifndef LIBSIGROK_HARDWARE_FLUKE_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_FLUKE_DMM_PROTOCOL_H

#define LOG_PREFIX "fluke-dmm"

#define FLUKEDMM_BUFSIZE 512

/* Always USB-serial, 1ms is plenty. */
#define SERIAL_WRITE_TIMEOUT_MS 1

/* Supported models */
enum {
	FLUKE_87 = 1,
	FLUKE_89,
	FLUKE_187,
	FLUKE_189,
	FLUKE_190,
	FLUKE_287,
	FLUKE_289,
};

/* Supported device profiles */
struct flukedmm_profile {
	int model;
	const char *modelname;
	/* Initialize the device channel list */
	void (*init_channels)(struct sr_dev_inst *sdi);
	/* Which poll command to use */
	const char *poll_cmd;
	/* Response handler */
	void (*handler)(const struct sr_dev_inst *sdi, char **tokens);
	/* How often to poll, in ms. */
	int poll_period;
	/* If no response received, how long to wait before retrying. */
	int timeout;
};

struct dev_context {
	const struct flukedmm_profile *profile;
	struct sr_sw_limits limits;

	char buf[FLUKEDMM_BUFSIZE];
	int buflen;
	int64_t cmd_sent_at;
	int expect_response;
	int meas_type;
	int is_relative;
	enum sr_mq mq;
	enum sr_unit unit;
	enum sr_mqflag mqflags;
};


SR_PRIV void fluke_init_channels_28x(struct sr_dev_inst *sdi);

SR_PRIV void fluke_handle_qm_18x(const struct sr_dev_inst *sdi, char **tokens);
SR_PRIV void fluke_handle_qm_190(const struct sr_dev_inst *sdi, char **tokens);
SR_PRIV void fluke_handle_qdda_28x(const struct sr_dev_inst *sdi, char **tokens);

SR_PRIV int fluke_receive_data(int fd, int revents, void *cb_data);

#endif
