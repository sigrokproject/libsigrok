/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

#ifndef LIBSIGROK_HARDWARE_NORMA_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_NORMA_DMM_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/**
 * @file
 *
 * Norma DM9x0/Siemens B102x DMMs driver.
 *
 * @internal
 */

#define LOG_PREFIX "norma-dmm"

#define NMADMM_BUFSIZE 256

#define NMADMM_TIMEOUT_MS 2000 /**< Request timeout. */

/** Norma DMM request types (used ones only, the DMMs support about 50). */
enum {
	NMADMM_REQ_IDN = 0,	/**< Request identity */
	NMADMM_REQ_STATUS,	/**< Request device status (value + ...) */
};

/** Defines requests used to communicate with device. */
struct nmadmm_req {
	int req_type;		/**< Request type. */
	const char *req_str;	/**< Request string. */
};

/** Strings for requests. */
extern const struct nmadmm_req nmadmm_requests[];

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	int type;		/**< DM9x0, e.g. 5 = DM950 */

	/* Acquisition settings */
	struct sr_sw_limits limits;

	/* Operational state */
	int last_req;			/**< Last request. */
	int64_t req_sent_at;		/**< Request sent. */
	gboolean last_req_pending;	/**< Last request not answered yet. */
	int lowbatt;			/**< Low battery. 1=low, 2=critical. */

	/* Temporary state across callbacks */
	uint8_t buf[NMADMM_BUFSIZE];	/**< Buffer for read callback */
	int buflen;			/**< Data len in buf */
};

SR_PRIV int norma_dmm_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int xgittoint(char xgit);

#endif
