/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2011 Olivier Fauchon <olivier@aixmarseille.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#define STR_PATTERN_SIGROK   "sigrok"
#define STR_PATTERN_RANDOM   "random"
#define STR_PATTERN_INC      "incremental"
#define STR_PATTERN_ALL_LOW  "all-low"
#define STR_PATTERN_ALL_HIGH "all-high"

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
	uint64_t samples_counter;
	void *cb_data;
	int64_t starttime;
};

static const int hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_DEMO_DEV,
	SR_CONF_SAMPLERATE,
	SR_CONF_PATTERN_MODE,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

static const struct sr_samplerates samplerates = {
	.low  = SR_HZ(1),
	.high = SR_GHZ(1),
	.step = SR_HZ(1),
	.list = NULL,
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
	"0", "1", "2", "3", "4", "5", "6", "7",
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
static struct sr_dev_driver *di = &demo_driver_info;
static uint64_t cur_samplerate = SR_KHZ(200);
static uint64_t limit_samples = 0;
static uint64_t limit_msec = 0;
static int default_pattern = PATTERN_SIGROK;

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static int clear_instances(void)
{
	/* Nothing needed so far. */

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct drv_context *drvc;
	GSList *devices;
	int i;

	(void)options;

	drvc = di->priv;

	devices = NULL;

	sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, DEMONAME, NULL, NULL);
	if (!sdi) {
		sr_err("%s: sr_dev_inst_new failed", __func__);
		return 0;
	}
	sdi->driver = di;

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
	return ((struct drv_context *)(di->priv))->instances;
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

static int config_get(int id, const void **data, const struct sr_dev_inst *sdi)
{
	(void)sdi;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		*data = &cur_samplerate;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = &limit_samples;
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = &limit_msec;
		break;
	case SR_CONF_PATTERN_MODE:
		switch (default_pattern) {
		case PATTERN_SIGROK:
			*data = STR_PATTERN_SIGROK;
			break;
		case PATTERN_RANDOM:
			*data = STR_PATTERN_RANDOM;
			break;
		case PATTERN_INC:
			*data = STR_PATTERN_INC;
			break;
		case PATTERN_ALL_LOW:
			*data = STR_PATTERN_ALL_LOW;
			break;
		case PATTERN_ALL_HIGH:
			*data = STR_PATTERN_ALL_HIGH;
			break;
		}
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int config_set(int id, const void *value, const struct sr_dev_inst *sdi)
{
	int ret;
	const char *stropt;

	(void)sdi;

	if (id == SR_CONF_SAMPLERATE) {
		cur_samplerate = *(const uint64_t *)value;
		sr_dbg("%s: setting samplerate to %" PRIu64, __func__,
		       cur_samplerate);
		ret = SR_OK;
	} else if (id == SR_CONF_LIMIT_SAMPLES) {
		limit_msec = 0;
		limit_samples = *(const uint64_t *)value;
		sr_dbg("%s: setting limit_samples to %" PRIu64, __func__,
		       limit_samples);
		ret = SR_OK;
	} else if (id == SR_CONF_LIMIT_MSEC) {
		limit_msec = *(const uint64_t *)value;
		limit_samples = 0;
		sr_dbg("%s: setting limit_msec to %" PRIu64, __func__,
		       limit_msec);
		ret = SR_OK;
	} else if (id == SR_CONF_PATTERN_MODE) {
		stropt = value;
		ret = SR_OK;
		if (!strcmp(stropt, STR_PATTERN_SIGROK)) {
			default_pattern = PATTERN_SIGROK;
		} else if (!strcmp(stropt, STR_PATTERN_RANDOM)) {
			default_pattern = PATTERN_RANDOM;
		} else if (!strcmp(stropt, STR_PATTERN_INC)) {
			default_pattern = PATTERN_INC;
		} else if (!strcmp(stropt, STR_PATTERN_ALL_LOW)) {
			default_pattern = PATTERN_ALL_LOW;
		} else if (!strcmp(stropt, STR_PATTERN_ALL_HIGH)) {
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

static int config_list(int key, const void **data, const struct sr_dev_inst *sdi)
{

	(void)sdi;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = hwcaps;
		break;
	case SR_CONF_SAMPLERATE:
		*data = &samplerates;
		break;
	case SR_CONF_PATTERN_MODE:
		*data = &pattern_strings;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static void samples_generator(uint8_t *buf, uint64_t size,
			      struct dev_context *devc)
{
	static uint64_t p = 0;
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

/* Callback handling data */
static int receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc = cb_data;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint8_t buf[BUFSIZE];
	static uint64_t samples_to_send, expected_samplenum, sending_now;
	int64_t time, elapsed;

	(void)fd;
	(void)revents;

	/* How many "virtual" samples should we have collected by now? */
	time = g_get_monotonic_time();
	elapsed = time - devc->starttime;
	expected_samplenum = elapsed * cur_samplerate / 1000000;
	/* Of those, how many do we still have to send? */
	samples_to_send = expected_samplenum - devc->samples_counter;

	if (limit_samples) {
		samples_to_send = MIN(samples_to_send,
				 limit_samples - devc->samples_counter);
	}

	while (samples_to_send > 0) {
		sending_now = MIN(samples_to_send, sizeof(buf));
		samples_to_send -= sending_now;
		samples_generator(buf, sending_now, devc);

		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = sending_now;
		logic.unitsize = 1;
		logic.data = buf;
		sr_session_send(devc->cb_data, &packet);
		devc->samples_counter += sending_now;
	}

	if (limit_samples && devc->samples_counter >= limit_samples) {
		sr_info("Requested number of samples reached.");
		hw_dev_acquisition_stop(NULL, cb_data);
		return TRUE;
	}

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;

	(void)sdi;

	/* TODO: 'devc' is never g_free()'d? */
	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("%s: devc malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	devc->sample_generator = default_pattern;
	devc->cb_data = cb_data;
	devc->samples_counter = 0;

	/*
	 * Setting two channels connected by a pipe is a remnant from when the
	 * demo driver generated data in a thread, and collected and sent the
	 * data in the main program loop.
	 * They are kept here because it provides a convenient way of setting
	 * up a timeout-based polling mechanism.
	 */
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

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	/* We use this timestamp to decide how many more samples to send. */
	devc->starttime = g_get_monotonic_time();

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;

	(void)sdi;

	devc = cb_data;

	sr_dbg("Stopping aquisition.");

	sr_session_source_remove_channel(devc->channels[0]);
	g_io_channel_shutdown(devc->channels[0], FALSE, NULL);

	/* Send last packet. */
	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

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
	.dev_clear = clear_instances,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
