/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 libsigrok contributors
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

/*
 * Driver for the Hantek DSO2xxx series oscilloscopes using the
 * "quick-fetch" firmware patch by phmarek
 * (https://github.com/phmarek/hantek-dso2000-quick-fetch).
 *
 * The patched firmware exposes a TCP server (default port 5025) on the
 * scope's network interface (requires DavidAlfa's USB-networking kernel).
 * When the user presses SAVE TO USB the scope sends one binary frame:
 *
 *   [frame_header]  (struct hantek_frame_header, 32 bytes, little-endian)
 *   [ch1_samples]   (num_samples × int8_t, only if ch1_enabled != 0)
 *   [ch2_samples]   (num_samples × int8_t, only if ch2_enabled != 0)
 */

#ifndef LIBSIGROK_HARDWARE_HANTEK_DSO2XXX_PROTOCOL_H
#define LIBSIGROK_HARDWARE_HANTEK_DSO2XXX_PROTOCOL_H

#define _GNU_SOURCE
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <glib.h>

#include <locale.h>

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "hantek-dso2xxx"

#define HANTEK_CHANNELS 4

/**
 * Hantek DSO2xxx packet header.
 * All fields are ASCII-encoded decimal strings (NOT null-terminated).
 * Total size: 128 bytes.
 */
struct __attribute__((packed)) hantek_frame_header {
    char   magic[2];            /* data[0-1]:      Packet identifier, always "#9" */
    char   packet_length[9];    /* data[2-10]:     Byte length of current packet */
    char   total_length[9];     /* data[11-19]:    Total byte length of all data */
    char   uploaded_length[9];  /* data[20-28]:    Byte length of uploaded data */
    char   run_status;          /* data[29]:       Current running status */
    char   trigger_status;      /* data[30]:       Trigger status */
    char   unknown[8];          /* data[31-38]:       Unknown */
    int8_t ch_offset[HANTEK_CHANNELS][2];     /* data[39-40] etc.: 16bit LE Channel offset */
    char   ch_voltage[HANTEK_CHANNELS][7];    /* data[47-53] etc.: Channel voltage scale as float */
    char   ch_enable[HANTEK_CHANNELS];        /* data[75-78]:    Channel enable flags (CH1-CH4, one char each) */
    char   sample_rate[9];      /* data[79-87]:    Sampling rate */
    char   sample_multiple[6];  /* data[88-93]:    Sampling multiple */
    char   trigger_time[9];     /* data[94-102]:   Display trigger time of current frame */
    char   acq_start_time[9];   /* data[103-111]:  Acquisition start time point of current frame */
    char   reserved[16];        /* data[112-127]:  Reserved */
};

/*
#9
000004128
000008000
000000000
0
0
\0\302\353\v \0\0\0\000
2\0 \316\377 \0\0 \0\000
1.0e+00 1.0e+00 1.0e+00 1.0e+00
1100
1.250e+06
000001
\0\0\0\0\0\0\0\0\0
+0.00e+00
000808\0\300\200\200\200\200\200\200\200\0
*/

#define HANTEK_BLOCK_SIZE 4000

/** Default TCP port exposed by the patched firmware. */
#define HANTEK_DEFAULT_PORT    "8001"

/** Default TCP address (scope must be on the network via USB-networking). */
#define HANTEK_DEFAULT_ADDR    "172.31.254.254"

/** I/O timeout for TCP operations, in milliseconds. */
#define HANTEK_TCP_TIMEOUT_MS  60000

/** Number of ADC counts per oscilloscope division. */
#define HANTEK_COUNTS_PER_DIV  25


/** Per-device instance state. */
struct dev_context {
	/* Network connection */
	char    *tcp_address;     /**< IP / hostname of the scope. */
	char    *tcp_port;        /**< TCP port string. */
	int      sockfd;          /**< Connected socket fd, or -1. */

	/* Acquisition state */
	gboolean acq_running;
	uint64_t frames_received;

	/* Channel metadata filled in from the most recent header */
	uint32_t num_samples;

	double   sample_period;   /**< 1 / samplerate */
	uint32_t sample_rate;

	uint8_t  ch_enabled[HANTEK_CHANNELS];
	int16_t  ch_offset[HANTEK_CHANNELS];
	float    ch_scale[HANTEK_CHANNELS];
};

/* Forward declarations for api.c ↔ protocol.c interface */
SR_PRIV int  hantek_dso2xxx_tcp_connect(const struct sr_dev_inst *sdi);
SR_PRIV void hantek_dso2xxx_tcp_close(const struct sr_dev_inst *sdi);
SR_PRIV int  hantek_dso2xxx_receive_frame(const struct sr_dev_inst *sdi);
SR_PRIV int hantek_dso2xxx_timesync(struct dev_context *devc);
SR_PRIV int hantek_dso2xxx_acq_now(struct dev_context *devc);
SR_PRIV int drv_init(struct sr_dev_driver *di, struct sr_context *sr_ctx);
SR_PRIV int hantek_dso2xxx_timeout(struct dev_context *devc);
SR_PRIV double atof_for_C_locale(const char *input);

extern locale_t hantek_dso2xxx_c_locale;

#endif /* LIBSIGROK_HARDWARE_HANTEK_DSO2XXX_PROTOCOL_H */
