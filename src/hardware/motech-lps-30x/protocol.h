/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com> (code from atten-pps3xxx)
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

/** @file
 *  <em>Motech LPS-30x series</em> power supply driver
 *  @internal
 */

#ifndef LIBSIGROK_HARDWARE_MOTECH_LPS_30X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MOTECH_LPS_30X_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

SR_PRIV int lps_process_status(struct sr_dev_inst *sdi, int stat);
SR_PRIV int lps_send_req(struct sr_serial_dev_inst *serial, const char *fmt, ...);

#define LOG_PREFIX "motech-lps-30x"

#define LINELEN_MAX 50	/**< Max. line length for requests */

#define REQ_TIMEOUT_MS 250 /**< Timeout [ms] for single request. */

#define MAX_CHANNELS 3

typedef enum {
	LPS_UNKNOWN = 0,/**< Unknown model (used during detection process) */
	LPS_301,	/**< Motech/Amrel LPS-301, 1 output */
	LPS_302,	/**< Motech/Amrel LPS-302, 1 output */
	LPS_303,	/**< Motech/Amrel LPS-303, 1 output */
	LPS_304,	/**< Motech/Amrel LPS-304, 3 outputs */
	LPS_305,	/**< Motech/Amrel LPS-305, 3 outputs */
} lps_modelid;

/** Channel specification */
struct channel_spec {
	/* Min, max, step. */
	gdouble voltage[3];
	gdouble current[3];
};

/** Model properties specification */
struct lps_modelspec {
	lps_modelid modelid;
	const char *modelstr;
	uint8_t num_channels;
	struct channel_spec channels[3];
};

/** Used to implement a little state machine to query all required values in a row. */
typedef enum {
	AQ_NONE,
	AQ_U1,
	AQ_I1,
	AQ_I2,
	AQ_U2,
	AQ_STATUS,
} acquisition_req;

/** Status of a single channel. */
struct channel_status {
	/* Channel information (struct channel_info*). data (struct) owned by sdi, just a reference to address a single channel. */
	GSList *info;
	/* Received from device. */
	gdouble output_voltage_last;
	gdouble output_current_last;
	gboolean output_enabled;	/**< Also used when set. */
	gboolean cc_mode;		/**< Constant current mode. If false, constant voltage mode. */
	/* Set by frontend. */
	gdouble output_voltage_max;
	gdouble output_current_max;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct lps_modelspec *model;

	/* Acquisition status */
	gboolean acq_running;		/**< Acquisition is running. */
	uint64_t limit_samples;		/**< Target number of samples */
	uint64_t limit_msec;		/**< Target sampling time */
	acquisition_req acq_req;	/**< Current request. */
	uint8_t	acq_req_pending;	/**< Request pending. 0=none, 1=reply, 2=OK */

	/* Operational state */
	struct channel_status channel_status[MAX_CHANNELS];
	guint8 tracking_mode;		/**< 0=off, 1=Tracking from CH1, 2=Tracking from CH2. */

	/* Temporary state across callbacks */
	int64_t req_sent_at;    /**< Request sent. */
	uint64_t num_samples;	/**< Current #samples for limit_samples */
	GTimer *elapsed_msec;	/**< Used for sampling with limit_msec  */
	gchar buf[LINELEN_MAX];	/**< Buffer for read callback */
	int buflen;		/**< Data len in buf */
};

SR_PRIV int motech_lps_30x_receive_data(int fd, int revents, void *cb_data);

#endif
