/*
 * This file is part of the libsigrok project.
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

#define LOG_PREFIX "demo"

/* TODO: Number of probes should be configurable. */
#define NUM_PROBES           8

/* The size of chunks to send through the session bus. */
/* TODO: Should be configurable. */
#define LOGIC_BUFSIZE        4096

/* Patterns we can generate */
enum {
	/**
	 * Spells "sigrok" across 8 probes using '0's (with '1's as
	 * "background") when displayed using the 'bits' output format.
	 */
	PATTERN_SIGROK,

	/** Pseudo-)random values on all probes. */
	PATTERN_RANDOM,

	/**
	 * Incrementing number across all probes.
	 */
	PATTERN_INC,

	/** All probes have a low logic state. */
	PATTERN_ALL_LOW,

	/** All probes have a high logic state. */
	PATTERN_ALL_HIGH,
};

static const char *pattern_strings[] = {
	"sigrok",
	"random",
	"incremental",
	"all-low",
	"all-high",
};

/* Private, per-device-instance driver context. */
struct dev_context {
	int pipe_fds[2];
	GIOChannel *channel;
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t limit_msec;
	uint8_t sample_generator;
	uint64_t samples_counter;
	void *cb_data;
	int64_t starttime;
	unsigned char logic_data[LOGIC_BUFSIZE];
	uint64_t step;
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

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_GHZ(1),
	SR_HZ(1),
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

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *devices;
	int i;
	char probe_name[16];

	(void)options;

	drvc = di->priv;

	devices = NULL;

	sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, "Demo device", NULL, NULL);
	if (!sdi) {
		sr_err("Device instance creation failed.");
		return NULL;
	}
	sdi->driver = di;

	for (i = 0; i < NUM_PROBES; i++) {
		sprintf(probe_name, "D%d", i);
		if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE, probe_name)))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);

	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		return NULL;
	}

	devc->cur_samplerate = SR_KHZ(200);
	devc->limit_samples = 0;
	devc->limit_msec = 0;
	devc->sample_generator = PATTERN_SIGROK;
	devc->step =0;

	sdi->priv = devc;

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	return dev_clear();
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *const devc = sdi->priv;

	(void)probe_group;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_string(pattern_strings[devc->sample_generator]);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	unsigned int i;
	int ret;
	const char *stropt;

	(void)probe_group;
	struct dev_context *const devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (id == SR_CONF_SAMPLERATE) {
		devc->cur_samplerate = g_variant_get_uint64(data);
		sr_dbg("Setting samplerate to %" PRIu64, devc->cur_samplerate);
		ret = SR_OK;
	} else if (id == SR_CONF_LIMIT_SAMPLES) {
		devc->limit_msec = 0;
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting limit_samples to %" PRIu64, devc->limit_samples);
		ret = SR_OK;
	} else if (id == SR_CONF_LIMIT_MSEC) {
		devc->limit_msec = g_variant_get_uint64(data);
		devc->limit_samples = 0;
		sr_dbg("Setting limit_msec to %" PRIu64, devc->limit_msec);
		ret = SR_OK;
	} else if (id == SR_CONF_PATTERN_MODE) {
		stropt = g_variant_get_string(data, NULL);
		ret = SR_ERR;
		for (i = 0; i < ARRAY_SIZE(pattern_strings); i++) {
			if (!strcmp(stropt, pattern_strings[i])) {
				devc->sample_generator = i;
				ret = SR_OK;
				break;
			}
		}
		/* Might as well do this now. */
		if (i == PATTERN_ALL_LOW)
			memset(devc->logic_data, 0x00, LOGIC_BUFSIZE);
		else if (i == PATTERN_ALL_HIGH)
			memset(devc->logic_data, 0xff, LOGIC_BUFSIZE);
		sr_dbg("Setting pattern to %s", pattern_strings[i]);
	} else {
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)probe_group;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
				ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerate-steps", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_strv(pattern_strings, ARRAY_SIZE(pattern_strings));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void sample_generator(struct sr_dev_inst *sdi, uint64_t size)
{
	struct dev_context *devc;
	uint64_t i;

	devc = sdi->priv;

	switch (devc->sample_generator) {
	case PATTERN_SIGROK:
		for (i = 0; i < size; i++) {
			devc->logic_data[i] = ~(pattern_sigrok[
				devc->step++ % sizeof(pattern_sigrok)] >> 1);
		}
		break;
	case PATTERN_RANDOM:
		for (i = 0; i < size; i++)
			devc->logic_data[i] = (uint8_t)(rand() & 0xff);
		break;
	case PATTERN_INC:
		for (i = 0; i < size; i++)
			devc->logic_data[i] = devc->step++;
		break;
	case PATTERN_ALL_LOW:
	case PATTERN_ALL_HIGH:
		/* These were set when the pattern mode was selected. */
		break;
	default:
		sr_err("Unknown pattern: %d.", devc->sample_generator);
		break;
	}
}

/* Callback handling data */
static int prepare_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	static uint64_t samples_to_send, expected_samplenum, sending_now;
	int64_t time, elapsed;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	/* How many "virtual" samples should we have collected by now? */
	time = g_get_monotonic_time();
	elapsed = time - devc->starttime;
	expected_samplenum = elapsed * devc->cur_samplerate / 1000000;
	/* Of those, how many do we still have to send? */
	samples_to_send = expected_samplenum - devc->samples_counter;

	if (devc->limit_samples) {
		samples_to_send = MIN(samples_to_send,
				 devc->limit_samples - devc->samples_counter);
	}

	while (samples_to_send > 0) {
		sending_now = MIN(samples_to_send, LOGIC_BUFSIZE);
		samples_to_send -= sending_now;
		sample_generator(sdi, sending_now);

		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = sending_now;
		logic.unitsize = 1;
		logic.data = devc->logic_data;
		sr_session_send(devc->cb_data, &packet);
		devc->samples_counter += sending_now;
	}

	if (devc->limit_samples &&
		devc->samples_counter >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
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
		sr_err("%s: pipe() failed", __func__);
		return SR_ERR;
	}

	devc->channel = g_io_channel_unix_new(devc->pipe_fds[0]);

	g_io_channel_set_flags(devc->channel, G_IO_FLAG_NONBLOCK, NULL);

	/* Set channel encoding to binary (default is UTF-8). */
	g_io_channel_set_encoding(devc->channel, NULL, NULL);

	/* Make channels to unbuffered. */
	g_io_channel_set_buffered(devc->channel, FALSE);

	sr_session_source_add_channel(devc->channel, G_IO_IN | G_IO_ERR,
			40, prepare_data, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* We use this timestamp to decide how many more samples to send. */
	devc->starttime = g_get_monotonic_time();

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *const devc = sdi->priv;
	struct sr_datafeed_packet packet;

	(void)cb_data;

	sr_dbg("Stopping aquisition.");

	sr_session_source_remove_channel(devc->channel);
	g_io_channel_shutdown(devc->channel, FALSE, NULL);
	g_io_channel_unref(devc->channel);
	devc->channel = NULL;

	/* Send last packet. */
	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver demo_driver_info = {
	.name = "demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
