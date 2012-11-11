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
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "demo: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

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

/* Private, per-device-instance driver context. */
struct dev_context {
	int pipe_fds[2];
	GIOChannel *channels[2];
	uint8_t sample_generator;
	uint8_t thread_running;
	uint64_t samples_counter;
	void *session_dev_id;
	GTimer *timer;
};

static const int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_DEMO_DEV,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_PATTERN_MODE,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_LIMIT_MSEC,
	SR_HWCAP_CONTINUOUS,
};

static const struct sr_samplerates samplerates = {
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
SR_PRIV struct sr_dev_driver demo_driver_info;
static struct sr_dev_driver *ddi = &demo_driver_info;
static uint64_t cur_samplerate = SR_KHZ(200);
static uint64_t limit_samples = 0;
static uint64_t limit_msec = 0;
static int default_pattern = PATTERN_SIGROK;
static GThread *my_thread;
static int thread_running;

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static int hw_init(void)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}
	ddi->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct drv_context *drvc;
	GSList *devices;
	int i;

	(void)options;

	drvc = ddi->priv;
	devices = NULL;

	sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, DEMONAME, NULL, NULL);
	if (!sdi) {
		sr_err("%s: sr_dev_inst_new failed", __func__);
		return 0;
	}
	sdi->driver = ddi;

	for (i = 0; probe_names[i]; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE,
				probe_names[i])))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = ddi->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* Nothing needed so far. */

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* Nothing needed so far. */

	return SR_OK;
}

static int hw_cleanup(void)
{
	/* Nothing needed so far. */
	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{
	(void)sdi;

	switch (info_id) {
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	case SR_DI_SAMPLERATES:
		*data = &samplerates;
		break;
	case SR_DI_CUR_SAMPLERATE:
		*data = &cur_samplerate;
		break;
	case SR_DI_PATTERNS:
		*data = &pattern_strings;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	int ret;
	const char *stropt;

	(void)sdi;

	if (hwcap == SR_HWCAP_SAMPLERATE) {
		cur_samplerate = *(const uint64_t *)value;
		sr_dbg("%s: setting samplerate to %" PRIu64, __func__,
		       cur_samplerate);
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_LIMIT_SAMPLES) {
		limit_msec = 0;
		limit_samples = *(const uint64_t *)value;
		sr_dbg("%s: setting limit_samples to %" PRIu64, __func__,
		       limit_samples);
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_LIMIT_MSEC) {
		limit_msec = *(const uint64_t *)value;
		limit_samples = 0;
		sr_dbg("%s: setting limit_msec to %" PRIu64, __func__,
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
		sr_dbg("%s: setting pattern to %d", __func__, default_pattern);
	} else {
		ret = SR_ERR;
	}

	return ret;
}

static void samples_generator(uint8_t *buf, uint64_t size, void *data)
{
	static uint64_t p = 0;
	struct dev_context *devc = data;
	uint64_t i;

	/* TODO: Needed? */
	memset(buf, 0, size);

	switch (devc->sample_generator) {
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
		sr_err("Unknown pattern: %d.", devc->sample_generator);
		break;
	}
}

/* Thread function */
static void thread_func(void *data)
{
	struct dev_context *devc = data;
	uint8_t buf[BUFSIZE];
	uint64_t nb_to_send = 0;
	int bytes_written;
	double time_cur, time_last, time_diff;

	time_last = g_timer_elapsed(devc->timer, NULL);

	while (thread_running) {
		/* Rate control */
		time_cur = g_timer_elapsed(devc->timer, NULL);

		time_diff = time_cur - time_last;
		time_last = time_cur;

		nb_to_send = cur_samplerate * time_diff;

		if (limit_samples) {
			nb_to_send = MIN(nb_to_send,
				      limit_samples - devc->samples_counter);
		}

		/* Make sure we don't overflow. */
		nb_to_send = MIN(nb_to_send, BUFSIZE);

		if (nb_to_send) {
			samples_generator(buf, nb_to_send, data);
			devc->samples_counter += nb_to_send;

			g_io_channel_write_chars(devc->channels[1], (gchar *)&buf,
				nb_to_send, (gsize *)&bytes_written, NULL);
		}

		/* Check if we're done. */
		if ((limit_msec && time_cur * 1000 > limit_msec) ||
		    (limit_samples && devc->samples_counter >= limit_samples))
		{
			close(devc->pipe_fds[1]);
			thread_running = 0;
		}

		g_usleep(10);
	}
}

/* Callback handling data */
static int receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc = cb_data;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	static uint64_t samples_received = 0;
	unsigned char c[BUFSIZE];
	gsize z;

	(void)fd;
	(void)revents;

	do {
		g_io_channel_read_chars(devc->channels[0],
				        (gchar *)&c, BUFSIZE, &z, NULL);

		if (z > 0) {
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = z;
			logic.unitsize = 1;
			logic.data = c;
			sr_session_send(devc->session_dev_id, &packet);
			samples_received += z;
		}
	} while (z > 0);

	if (!thread_running && z <= 0) {
		/* Make sure we don't receive more packets. */
		g_io_channel_shutdown(devc->channels[0], FALSE, NULL);

		/* Send last packet. */
		packet.type = SR_DF_END;
		sr_session_send(devc->session_dev_id, &packet);

		return FALSE;
	}

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct sr_datafeed_meta_logic meta;
	struct dev_context *devc;

	(void)sdi;

	/* TODO: 'devc' is never g_free()'d? */
	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("%s: devc malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	devc->sample_generator = default_pattern;
	devc->session_dev_id = cb_data;
	devc->samples_counter = 0;

	if (pipe(devc->pipe_fds)) {
		/* TODO: Better error message. */
		sr_err("%s: pipe() failed", __func__);
		return SR_ERR;
	}

	devc->channels[0] = g_io_channel_unix_new(devc->pipe_fds[0]);
	devc->channels[1] = g_io_channel_unix_new(devc->pipe_fds[1]);

	g_io_channel_set_flags(devc->channels[0], G_IO_FLAG_NONBLOCK, NULL);

	/* Set channel encoding to binary (default is UTF-8). */
	g_io_channel_set_encoding(devc->channels[0], NULL, NULL);
	g_io_channel_set_encoding(devc->channels[1], NULL, NULL);

	/* Make channels to unbuffered. */
	g_io_channel_set_buffered(devc->channels[0], FALSE);
	g_io_channel_set_buffered(devc->channels[1], FALSE);

	sr_session_source_add_channel(devc->channels[0], G_IO_IN | G_IO_ERR,
		    40, receive_data, devc);

	/* Run the demo thread. */
	devc->timer = g_timer_new();
	thread_running = 1;
	my_thread = g_thread_try_new("sigrok demo generator",
			(GThreadFunc)thread_func, devc, NULL);
	if (!my_thread) {
		sr_err("%s: g_thread_try_new failed", __func__);
		return SR_ERR; /* TODO */
	}

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("%s: packet malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("%s: header malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	packet->type = SR_DF_HEADER;
	packet->payload = header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	sr_session_send(devc->session_dev_id, packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet->type = SR_DF_META_LOGIC;
	packet->payload = &meta;
	meta.samplerate = cur_samplerate;
	meta.num_probes = NUM_PROBES;
	sr_session_send(devc->session_dev_id, packet);

	g_free(header);
	g_free(packet);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	devc = sdi->priv;

	/* Stop generate thread. */
	thread_running = 0;

	sr_session_source_remove_channel(devc->channels[0]);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver demo_driver_info = {
	.name = "demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
