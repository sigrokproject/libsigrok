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
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "norma-dmm: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)
#define sr_err(s, args...) sr_err(LOG_PREFIX s, ## args)

#define NMADMM_BUFSIZE  256

/** Norma DMM request types (used ones only, the multimeters support about 50).
 */
enum nmadmm_req_t {
	NMADMM_REQ_IDN = 0,	/**< Request identity */
	NMADMM_REQ_STATUS,	/**< Request device status (value + ...) */
};

/** Defines requests used to communicate with device. */
struct nmadmm_req {
	enum nmadmm_req_t req_t;	/** Request type. */
	const char* reqstr;		/** Request string */
};

/** Strings for requests. */
extern const struct nmadmm_req nmadmm_requests[];

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	char*	version;	/**< Version string */
	int	type;		/**< DM9x0, e.g. 5 = DM950 */
	/* Acquisition settings */
	uint64_t limit_samples;	/**< Target number of samples */
	uint64_t limit_msec;	/**< Target sampling time */

	/* Opaque pointer passed in by frontend. */
	void *cb_data;

	/* Operational state */
	enum nmadmm_req_t last_req;	/**< Last request. */
	gboolean last_req_pending;	/**< Last request not answered yet.*/
	int	lowbatt;		/**< Low battery. 1=low, 2=critical.*/
	/* Temporary state across callbacks */
	uint64_t num_samples;		/**< Current #samples. */
	GTimer* elapsed_msec;		/**< Used for sampling with limit_msec*/
	unsigned char buf[NMADMM_BUFSIZE];	/**< Buffer for read callback */
	int buflen;			/**< Data len in buf */
};

SR_PRIV int norma_dmm_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int xgittoint(char xgit);

#endif
