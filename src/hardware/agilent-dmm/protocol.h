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

#ifndef LIBSIGROK_HARDWARE_AGILENT_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_AGILENT_DMM_PROTOCOL_H

#define LOG_PREFIX "agilent-dmm"

#define MAX_CHANNELS 3
#define AGDMM_BUFSIZE 256

/* Always USB-serial, 1ms is plenty. */
#define SERIAL_WRITE_TIMEOUT_MS 1

/* Supported models */
enum {
	AGILENT_U1231 = 1,
	AGILENT_U1232,
	AGILENT_U1233,

	AGILENT_U1241,
	AGILENT_U1242,

	AGILENT_U1251,
	AGILENT_U1252,
	AGILENT_U1253,

	KEYSIGHT_U1281,
	KEYSIGHT_U1282,
};

/* Supported device profiles */
struct agdmm_profile {
	int model;
	const char *modelname;
	int nb_channels;
	const struct agdmm_job *jobs;
	const struct agdmm_recv *recvs;
};

/* Private, per-device-instance driver context. */
struct dev_context {
	const struct agdmm_profile *profile;
	struct sr_sw_limits limits;

	/* Runtime. */
	int64_t jobqueue[8];
	unsigned char buf[AGDMM_BUFSIZE];
	int buflen;
	struct sr_channel *cur_channel;
	struct sr_channel *cur_conf;
	int cur_mq[MAX_CHANNELS];
	int cur_unit[MAX_CHANNELS];
	int cur_mqflags[MAX_CHANNELS];
	int cur_digits[MAX_CHANNELS];
	int cur_encoding[MAX_CHANNELS];
	int cur_exponent[MAX_CHANNELS];
	int mode_tempaux;
	int mode_continuity;
	int mode_squarewave;
};

struct agdmm_job {
	int interval;
	int (*send) (const struct sr_dev_inst *sdi);
};

struct agdmm_recv {
	const char *recv_regex;
	int (*recv) (const struct sr_dev_inst *sdi, GMatchInfo *match);
};

SR_PRIV int agdmm_receive_data(int fd, int revents, void *cb_data);

#endif
