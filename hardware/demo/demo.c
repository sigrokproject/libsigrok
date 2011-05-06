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
#include <sigrok.h>
#include <sigrok-internal.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif
#include "config.h"

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
GIOChannel *channels[2];

struct databag {
	int pipe_fds[2];
	uint8_t sample_generator;
	uint8_t thread_running;
	uint64_t samples_counter;
	int device_index;
	gpointer session_device_id;
	GTimer *timer;
};

static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
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

/* List of struct sr_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;
static uint64_t cur_samplerate = SR_KHZ(200);
static uint64_t limit_samples = 0;
static uint64_t limit_msec = 0;
static int default_pattern = PATTERN_SIGROK;
static GThread *my_thread;
static int thread_running;

static void hw_stop_acquisition(int device_index, gpointer session_device_id);

static int hw_init(const char *deviceinfo)
{
	struct sr_device_instance *sdi;

	/* Avoid compiler warnings. */
	deviceinfo = deviceinfo;

	sdi = sr_device_instance_new(0, SR_ST_ACTIVE, DEMONAME, NULL, NULL);
	if (!sdi) {
		sr_err("demo: %s: sr_device_instance_new failed", __func__);
		return 0;
	}

	device_instances = g_slist_append(device_instances, sdi);

	return 1;
}

static int hw_opendev(int device_index)
{
	/* Avoid compiler warnings. */
	device_index = device_index;

	/* Nothing needed so far. */

	return SR_OK;
}

static int hw_closedev(int device_index)
{
	/* Avoid compiler warnings. */
	device_index = device_index;

	/* Nothing needed so far. */

	return SR_OK;
}

static void hw_cleanup(void)
{
	/* Nothing needed so far. */
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sr_device_instance *sdi;
	void *info = NULL;

	if (!(sdi = sr_get_device_instance(device_instances, device_index))) {
		sr_err("demo: %s: sdi was NULL", __func__);
		return NULL;
	}

	switch (device_info_id) {
	case SR_DI_INSTANCE:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
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

static int hw_get_status(int device_index)
{
	/* Avoid compiler warnings. */
	device_index = device_index;

	return SR_ST_ACTIVE;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	int ret;
	char *stropt;

	/* Avoid compiler warnings. */
	device_index = device_index;

	if (capability == SR_HWCAP_PROBECONFIG) {
		/* Nothing to do, but must be supported */
		ret = SR_OK;
	} else if (capability == SR_HWCAP_SAMPLERATE) {
		cur_samplerate = *(uint64_t *)value;
		sr_dbg("demo: %s: setting samplerate to %" PRIu64, __func__,
		       cur_samplerate);
		ret = SR_OK;
	} else if (capability == SR_HWCAP_LIMIT_SAMPLES) {
		limit_samples = *(uint64_t *)value;
		sr_dbg("demo: %s: setting limit_samples to %" PRIu64, __func__,
		       limit_samples);
		ret = SR_OK;
	} else if (capability == SR_HWCAP_LIMIT_MSEC) {
		limit_msec = *(uint64_t *)value;
		sr_dbg("demo: %s: setting limit_msec to %" PRIu64, __func__,
		       limit_msec);
		ret = SR_OK;
	} else if (capability == SR_HWCAP_PATTERN_MODE) {
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
	struct databag *mydata = data;
	uint64_t i;

	/* TODO: Needed? */
	memset(buf, 0, size);

	switch (mydata->sample_generator) {
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
		for (i = 0; i < size; i++)
			*(buf + i) = 0x00;
		break;
	case PATTERN_ALL_HIGH: /* All probes are high */
		for (i = 0; i < size; i++)
			*(buf + i) = 0xff;
		break;
	default:
		/* TODO: Error handling. */
		break;
	}
}

/* Thread function */
static void thread_func(void *data)
{
	struct databag *mydata = data;
	uint8_t buf[BUFSIZE];
	uint64_t nb_to_send = 0;
	int bytes_written;
	double time_cur, time_last, time_diff;

	time_last = g_timer_elapsed(mydata->timer, NULL);

	while (thread_running) {
		/* Rate control */
		time_cur = g_timer_elapsed(mydata->timer, NULL);

		time_diff = time_cur - time_last;
		time_last = time_cur;

		nb_to_send = cur_samplerate * time_diff;

		if (limit_samples) {
			nb_to_send = MIN(nb_to_send,
				      limit_samples - mydata->samples_counter);
		}

		/* Make sure we don't overflow. */
		nb_to_send = MIN(nb_to_send, BUFSIZE);

		if (nb_to_send) {
			samples_generator(buf, nb_to_send, data);
			mydata->samples_counter += nb_to_send;

			g_io_channel_write_chars(channels[1], (gchar *)&buf,
				nb_to_send, (gsize *)&bytes_written, NULL);
		}

		/* Check if we're done. */
		if ((limit_msec && time_cur * 1000 > limit_msec) ||
		    (limit_samples && mydata->samples_counter >= limit_samples))
		{
			close(mydata->pipe_fds[1]);
			thread_running = 0;
		}

		g_usleep(10);
	}
}

/* Callback handling data */
static int receive_data(int fd, int revents, void *user_data)
{
	struct sr_datafeed_packet packet;
	char c[BUFSIZE];
	gsize z;

	/* Avoid compiler warnings. */
	fd = fd;
	revents = revents;

	do {
		g_io_channel_read_chars(channels[0],
				        (gchar *)&c, BUFSIZE, &z, NULL);

		if (z > 0) {
			packet.type = SR_DF_LOGIC;
			packet.length = z;
			packet.unitsize = 1;
			packet.payload = c;
			sr_session_bus(user_data, &packet);
		}
	} while (z > 0);

	if (!thread_running && z <= 0) {
		/* Make sure we don't receive more packets. */
		g_io_channel_close(channels[0]);

		/* Send last packet. */
		packet.type = SR_DF_END;
		sr_session_bus(user_data, &packet);

		return FALSE;
	}

	return TRUE;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct databag *mydata;

	/* TODO: 'mydata' is never g_free()'d? */
	if (!(mydata = g_try_malloc(sizeof(struct databag)))) {
		sr_err("demo: %s: mydata malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	mydata->sample_generator = default_pattern;
	mydata->session_device_id = session_device_id;
	mydata->device_index = device_index;
	mydata->samples_counter = 0;

	if (pipe(mydata->pipe_fds)) {
		/* TODO: Better error message. */
		sr_err("demo: %s: pipe() failed", __func__);
		return SR_ERR;
	}

	channels[0] = g_io_channel_unix_new(mydata->pipe_fds[0]);
	channels[1] = g_io_channel_unix_new(mydata->pipe_fds[1]);

	/* Set channel encoding to binary (default is UTF-8). */
	g_io_channel_set_encoding(channels[0], NULL, NULL);
	g_io_channel_set_encoding(channels[1], NULL, NULL);

	/* Make channels to unbuffered. */
	g_io_channel_set_buffered(channels[0], FALSE);
	g_io_channel_set_buffered(channels[1], FALSE);

	sr_source_add(mydata->pipe_fds[0], G_IO_IN | G_IO_ERR, 40,
		      receive_data, session_device_id);

	/* Run the demo thread. */
	g_thread_init(NULL);
	/* This must to be done between g_thread_init() & g_thread_create(). */
	mydata->timer = g_timer_new();
	thread_running = 1;
	my_thread =
	    g_thread_create((GThreadFunc)thread_func, mydata, TRUE, NULL);
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
	packet->length = sizeof(struct sr_datafeed_header);
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = cur_samplerate;
	header->protocol_id = SR_PROTO_RAW;
	header->num_logic_probes = NUM_PROBES;
	header->num_analog_probes = 0;
	sr_session_bus(session_device_id, packet);
	g_free(header);
	g_free(packet);

	return SR_OK;
}

static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	/* Avoid compiler warnings. */
	device_index = device_index;
	session_device_id = session_device_id;

	/* Stop generate thread. */
	thread_running = 0;
}

struct sr_device_plugin demo_plugin_info = {
	.name = "demo",
	.longname = "Demo driver and pattern generator",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.opendev = hw_opendev,
	.closedev = hw_closedev,
	.get_device_info = hw_get_device_info,
	.get_status = hw_get_status,
	.get_capabilities = hw_get_capabilities,
	.set_configuration = hw_set_configuration,
	.start_acquisition = hw_start_acquisition,
	.stop_acquisition = hw_stop_acquisition,
};
