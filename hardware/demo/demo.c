/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Olivier Fauchon <olivier@aixmarseille.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif
#include "sigrok.h"
#include "sigrok-internal.h"

/* TODO: Number of probes should be configurable. */
#define NUM_PROBES             8

#define DEMONAME               "Demo device"

/* The size of chunks to send through the session bus. */
/* TODO: Should be configurable. */
#define BUFSIZE                4096

/* Supported patterns which we can generate */
enum {
	/**
	 * Pattern which spells "sigrok" using '0's (with '1's as "background")
	 * when displayed using the 'bits' output format.
	 */
	PATTERN_SIGROK,

	/** Pattern which consists of (pseudo-)random values on all probes. */
	PATTERN_RANDOM,

	/**
	 * Pattern which consists of incrementing numbers.
	 * TODO: Better description.
	 */
	PATTERN_INC,

	/** Pattern where all probes have a low logic state. */
	PATTERN_ALL_LOW,

	/** Pattern where all probes have a high logic state. */
	PATTERN_ALL_HIGH,
};

/* FIXME: Should not be global. */
SR_PRIV GIOChannel *channels[2];

struct context {
	int pipe_fds[2];
	uint8_t sample_generator;
	uint8_t thread_running;
	uint64_t samples_counter;
	int dev_index;
	void *session_dev_id;
	GTimer *timer;
};

static int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_DEMO_DEV,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_PATTERN_MODE,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_LIMIT_MSEC,
	SR_HWCAP_CONTINUOUS,
};

static struct sr_samplerates samplerates = {
	SR_HZ(1),
	SR_GHZ(1),
	SR_HZ(1),
	NULL,
};

static const char *pattern_strings[] = {
	"sigrok",
	"random",
	"incremental",
	"all-low",
	"all-high",
	NULL,
};

/* We name the probes 0-7 on our demo driver. */
static const char *probe_names[NUM_PROBES + 1] = {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	NULL,
};

static uint8_t pattern_sigrok[] = {
	0x4c, 0x92, 0x92, 0x92, 0x64, 0x00, 0x00, 0x00,
	0x82, 0xfe, 0xfe, 0x82, 0x00, 0x00, 0x00, 0x00,
	0x7c, 0x82, 0x82, 0x92, 0x74, 0x00, 0x00, 0x00,
	0xfe, 0x12, 0x12, 0x32, 0xcc, 0x00, 0x00, 0x00,
	0x7c, 0x82, 0x82, 0x82, 0x7c, 0x00, 0x00, 0x00,
	0xfe, 0x10, 0x28, 0x44, 0x82, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xbe, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Private, per-device-instance driver context. */
/* TODO: struct context as with the other drivers. */

/* List of struct sr_dev_inst, maintained by dev_open()/dev_close(). */
static GSList *dev_insts = NULL;
static uint64_t cur_samplerate = SR_KHZ(200);
static uint64_t limit_samples = 0;
static uint64_t limit_msec = 0;
static int default_pattern = PATTERN_SIGROK;
static GThread *my_thread;
static int thread_running;

static int hw_dev_acquisition_stop(int dev_index, void *cb_data);

static int hw_init(const char *devinfo)
{
	struct sr_dev_inst *sdi;

	/* Avoid compiler warnings. */
	(void)devinfo;

	sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, DEMONAME, NULL, NULL);
	if (!sdi) {
		sr_err("demo: %s: sr_dev_inst_new failed", __func__);
		return 0;
	}

	dev_insts = g_slist_append(dev_insts, sdi);

	return 1;
}

static int hw_dev_open(int dev_index)
{
	/* Avoid compiler warnings. */
	(void)dev_index;

	/* Nothing needed so far. */

	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	/* Avoid compiler warnings. */
	(void)dev_index;

	/* Nothing needed so far. */

	return SR_OK;
}

static int hw_cleanup(void)
{
	/* Nothing needed so far. */
	return SR_OK;
}

static void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	void *info = NULL;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("demo: %s: sdi was NULL", __func__);
		return NULL;
	}

	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		break;
	case SR_DI_SAMPLERATES:
		info = &samplerates;
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &cur_samplerate;
		break;
	case SR_DI_PATTERNMODES:
		info = &pattern_strings;
		break;
	}

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	/* Avoid compiler warnings. */
	(void)dev_index;

	return SR_ST_ACTIVE;
}

static int *hw_hwcap_get_all(void)
{
	return hwcaps;
}

static int hw_dev_config_set(int dev_index, int hwcap, void *value)
{
	int ret;
	char *stropt;

	/* Avoid compiler warnings. */
	(void)dev_index;

	if (hwcap == SR_HWCAP_PROBECONFIG) {
		/* Nothing to do, but must be supported */
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_SAMPLERATE) {
		cur_samplerate = *(uint64_t *)value;
		sr_dbg("demo: %s: setting samplerate to %" PRIu64, __func__,
		       cur_samplerate);
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_LIMIT_SAMPLES) {
		limit_samples = *(uint64_t *)value;
		sr_dbg("demo: %s: setting limit_samples to %" PRIu64, __func__,
		       limit_samples);
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_LIMIT_MSEC) {
		limit_msec = *(uint64_t *)value;
		sr_dbg("demo: %s: setting limit_msec to %" PRIu64, __func__,
		       limit_msec);
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_PATTERN_MODE) {
		stropt = value;
		ret = SR_OK;
		if (!strcmp(stropt, "sigrok")) {
			default_pattern = PATTERN_SIGROK;
		} else if (!strcmp(stropt, "random")) {
			default_pattern = PATTERN_RANDOM;
		} else if (!strcmp(stropt, "incremental")) {
			default_pattern = PATTERN_INC;
		} else if (!strcmp(stropt, "all-low")) {
			default_pattern = PATTERN_ALL_LOW;
		} else if (!strcmp(stropt, "all-high")) {
			default_pattern = PATTERN_ALL_HIGH;
		} else {
			ret = SR_ERR;
		}
		sr_dbg("demo: %s: setting pattern to %d", __func__,
		       default_pattern);
	} else {
		ret = SR_ERR;
	}

	return ret;
}

static void samples_generator(uint8_t *buf, uint64_t size, void *data)
{
	static uint64_t p = 0;
	struct context *ctx = data;
	uint64_t i;

	/* TODO: Needed? */
	memset(buf, 0, size);

	switch (ctx->sample_generator) {
	case PATTERN_SIGROK: /* sigrok pattern */
		for (i = 0; i < size; i++) {
			*(buf + i) = ~(pattern_sigrok[p] >> 1);
			if (++p == 64)
				p = 0;
		}
		break;
	case PATTERN_RANDOM: /* Random */
		for (i = 0; i < size; i++)
			*(buf + i) = (uint8_t)(rand() & 0xff);
		break;
	case PATTERN_INC: /* Simple increment */
		for (i = 0; i < size; i++)
			*(buf + i) = i;
		break;
	case PATTERN_ALL_LOW: /* All probes are low */
		memset(buf, 0x00, size);
		break;
	case PATTERN_ALL_HIGH: /* All probes are high */
		memset(buf, 0xff, size);
		break;
	default:
		sr_err("demo: %s: unknown pattern %d", __func__,
		       ctx->sample_generator);
		break;
	}
}

/* Thread function */
static void thread_func(void *data)
{
	struct context *ctx = data;
	uint8_t buf[BUFSIZE];
	uint64_t nb_to_send = 0;
	int bytes_written;
	double time_cur, time_last, time_diff;

	time_last = g_timer_elapsed(ctx->timer, NULL);

	while (thread_running) {
		/* Rate control */
		time_cur = g_timer_elapsed(ctx->timer, NULL);

		time_diff = time_cur - time_last;
		time_last = time_cur;

		nb_to_send = cur_samplerate * time_diff;

		if (limit_samples) {
			nb_to_send = MIN(nb_to_send,
				      limit_samples - ctx->samples_counter);
		}

		/* Make sure we don't overflow. */
		nb_to_send = MIN(nb_to_send, BUFSIZE);

		if (nb_to_send) {
			samples_generator(buf, nb_to_send, data);
			ctx->samples_counter += nb_to_send;

			g_io_channel_write_chars(channels[1], (gchar *)&buf,
				nb_to_send, (gsize *)&bytes_written, NULL);
		}

		/* Check if we're done. */
		if ((limit_msec && time_cur * 1000 > limit_msec) ||
		    (limit_samples && ctx->samples_counter >= limit_samples))
		{
			close(ctx->pipe_fds[1]);
			thread_running = 0;
		}

		g_usleep(10);
	}
}

/* Callback handling data */
static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	static uint64_t samples_received = 0;
	unsigned char c[BUFSIZE];
	gsize z;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;

	do {
		g_io_channel_read_chars(channels[0],
				        (gchar *)&c, BUFSIZE, &z, NULL);

		if (z > 0) {
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = z;
			logic.unitsize = 1;
			logic.data = c;
			sr_session_send(cb_data, &packet);
			samples_received += z;
		}
	} while (z > 0);

	if (!thread_running && z <= 0) {
		/* Make sure we don't receive more packets. */
		g_io_channel_close(channels[0]);

		/* Send last packet. */
		packet.type = SR_DF_END;
		sr_session_send(cb_data, &packet);

		return FALSE;
	}

	return TRUE;
}

static int hw_dev_acquisition_start(int dev_index, void *cb_data)
{
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct context *ctx;

	/* TODO: 'ctx' is never g_free()'d? */
	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("demo: %s: ctx malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	ctx->sample_generator = default_pattern;
	ctx->session_dev_id = cb_data;
	ctx->dev_index = dev_index;
	ctx->samples_counter = 0;

	if (pipe(ctx->pipe_fds)) {
		/* TODO: Better error message. */
		sr_err("demo: %s: pipe() failed", __func__);
		return SR_ERR;
	}

	channels[0] = g_io_channel_unix_new(ctx->pipe_fds[0]);
	channels[1] = g_io_channel_unix_new(ctx->pipe_fds[1]);

	/* Set channel encoding to binary (default is UTF-8). */
	g_io_channel_set_encoding(channels[0], NULL, NULL);
	g_io_channel_set_encoding(channels[1], NULL, NULL);

	/* Make channels to unbuffered. */
	g_io_channel_set_buffered(channels[0], FALSE);
	g_io_channel_set_buffered(channels[1], FALSE);

	sr_source_add(ctx->pipe_fds[0], G_IO_IN | G_IO_ERR, 40,
		      receive_data, ctx->session_dev_id);

	/* Run the demo thread. */
	g_thread_init(NULL);
	/* This must to be done between g_thread_init() & g_thread_create(). */
	ctx->timer = g_timer_new();
	thread_running = 1;
	my_thread =
	    g_thread_create((GThreadFunc)thread_func, ctx, TRUE, NULL);
	if (!my_thread) {
		sr_err("demo: %s: g_thread_create failed", __func__);
		return SR_ERR; /* TODO */
	}

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("demo: %s: packet malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("demo: %s: header malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	packet->type = SR_DF_HEADER;
	packet->payload = header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = cur_samplerate;
	header->num_logic_probes = NUM_PROBES;
	sr_session_send(ctx->session_dev_id, packet);
	g_free(header);
	g_free(packet);

	return SR_OK;
}

/* TODO: This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(int dev_index, void *cb_data)
{
	/* Avoid compiler warnings. */
	(void)dev_index;
	(void)cb_data;

	/* Stop generate thread. */
	thread_running = 0;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver demo_driver_info = {
	.name = "demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
