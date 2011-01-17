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
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define pipe(fds) _pipe(fds, 4096, _O_BINARY)
#endif
#include "config.h"

#define NUM_PROBES             8
#define DEMONAME               "Demo device"
/* size of chunks to send through the session bus */
#define BUFSIZE                4096

enum {
	GENMODE_DEFAULT,
	GENMODE_RANDOM,
	GENMODE_INC,
};

GIOChannel *channels[2];

struct databag {
	int pipe_fds[2];
	uint8_t sample_generator;
	uint8_t thread_running;
	uint64_t samples_counter;
	int device_index;
	int loop_sleep;
	gpointer session_device_id;
	GTimer *timer;
};

static int capabilities[] = {
	HWCAP_LOGIC_ANALYZER,
	HWCAP_PATTERN_MODE,
	HWCAP_LIMIT_SAMPLES,
	HWCAP_LIMIT_MSEC,
	HWCAP_CONTINUOUS
};

static const char *patternmodes[] = {
	"random",
	"incremental",
	NULL,
};

static uint8_t genmode_default[] = {
	0x4c, 0x92, 0x92, 0x92, 0x64, 0x00, 0x00, 0x00,
	0x82, 0xfe, 0xfe, 0x82, 0x00, 0x00, 0x00, 0x00,
	0x7c, 0x82, 0x82, 0x92, 0x74, 0x00, 0x00, 0x00,
	0xfe, 0x12, 0x12, 0x32, 0xcc, 0x00, 0x00, 0x00,
	0x7c, 0x82, 0x82, 0x82, 0x7c, 0x00, 0x00, 0x00,
	0xfe, 0x10, 0x28, 0x44, 0x82, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xbe, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* List of struct sigrok_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;
static uint64_t cur_samplerate = 0;
static uint64_t limit_samples = 0;
static uint64_t limit_msec = 0;
static int default_genmode = GENMODE_DEFAULT;
static GThread *my_thread;
static int thread_running;

static void hw_stop_acquisition(int device_index, gpointer session_device_id);

static int hw_init(char *deviceinfo)
{
	struct sigrok_device_instance *sdi;

	/* Avoid compiler warnings. */
	deviceinfo = deviceinfo;

	sdi = sigrok_device_instance_new(0, ST_ACTIVE, DEMONAME, NULL, NULL);
	if (!sdi)
		return 0;

	device_instances = g_slist_append(device_instances, sdi);

	return 1;
}

static int hw_opendev(int device_index)
{
	/* Avoid compiler warnings. */
	device_index = device_index;

	/* Nothing needed so far. */
	return SIGROK_OK;
}

static void hw_closedev(int device_index)
{
	/* Avoid compiler warnings. */
	device_index = device_index;

	/* Nothing needed so far. */
}

static void hw_cleanup(void)
{
	/* Nothing needed so far. */
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sigrok_device_instance *sdi;
	void *info = NULL;

	if (!(sdi = get_sigrok_device_instance(device_instances, device_index)))
		return NULL;

	switch (device_info_id) {
	case DI_INSTANCE:
		info = sdi;
		break;
	case DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case DI_CUR_SAMPLERATE:
		info = &cur_samplerate;
		break;
	case DI_PATTERNMODES:
		info = &patternmodes;
		break;
	}

	return info;
}

static int hw_get_status(int device_index)
{
	/* Avoid compiler warnings. */
	device_index = device_index;

	return ST_ACTIVE;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	int ret;
	uint64_t *tmp_u64;
	char *stropt;

	/* Avoid compiler warnings. */
	device_index = device_index;

	if (capability == HWCAP_PROBECONFIG) {
		/* Nothing to do. */
		ret = SIGROK_OK;
	} else if (capability == HWCAP_LIMIT_SAMPLES) {
		tmp_u64 = value;
		limit_samples = *tmp_u64;
		ret = SIGROK_OK;
	} else if (capability == HWCAP_LIMIT_MSEC) {
		tmp_u64 = value;
		limit_msec = *tmp_u64;
		ret = SIGROK_OK;
	} else if (capability == HWCAP_PATTERN_MODE) {
		stropt = value;
		if (!strcmp(stropt, "random")) {
			default_genmode = GENMODE_RANDOM;
			ret = SIGROK_OK;
		} else if (!strcmp(stropt, "incremental")) {
			default_genmode = GENMODE_INC;
			ret = SIGROK_OK;
		} else {
			ret = SIGROK_ERR;
		}
	} else {
		ret = SIGROK_ERR;
	}

	return ret;
}

static void samples_generator(uint8_t *buf, uint64_t size, void *data)
{
	struct databag *mydata = data;
	uint64_t p, i;

	memset(buf, 0, size);

	switch (mydata->sample_generator) {
	case GENMODE_DEFAULT:
		p = 0;
		for (i = 0; i < size; i++) {
			*(buf + i) = ~(genmode_default[p] >> 1);
			if (++p == 64)
				p = 0;
		}
		break;
	case GENMODE_RANDOM: /* Random */
		for (i = 0; i < size; i++)
			*(buf + i) = (uint8_t)(rand() & 0xff);
		break;
	case GENMODE_INC: /* Simple increment */
		for (i = 0; i < size; i++)
			*(buf + i) = i;
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
	unsigned int msec_elapsed;

	while (thread_running) {
		if (limit_samples)
			nb_to_send = limit_samples - mydata->samples_counter;
		else
			nb_to_send = BUFSIZE;  /* Continuous mode */

		if (limit_msec) {
			msec_elapsed = g_timer_elapsed(mydata->timer, NULL) * 1000;
			if (msec_elapsed > limit_msec)
				nb_to_send = 0;
		}

		if (nb_to_send == 0) {
			close(mydata->pipe_fds[1]);
			thread_running = 0;
			hw_stop_acquisition(mydata->device_index,
					    mydata->session_device_id);
		} else if (nb_to_send > BUFSIZE) {
			nb_to_send = BUFSIZE;
		}

		samples_generator(buf, nb_to_send, data);
		mydata->samples_counter += nb_to_send;

		g_io_channel_write_chars(channels[1], (gchar *)&buf,
				nb_to_send, (gsize *)&bytes_written, NULL);

		g_usleep(mydata->loop_sleep);
	}
}

/* Callback handling data */
static int receive_data(int fd, int revents, void *user_data)
{
	struct datafeed_packet packet;
	char c[BUFSIZE];
	uint64_t z;

	/* Avoid compiler warnings. */
	fd = fd;
	revents = revents;

	g_io_channel_read_chars(channels[0], (gchar *)&c, BUFSIZE, &z, NULL);

	if (z > 0) {
		packet.type = DF_LOGIC;
		packet.length = z;
		packet.unitsize = 1;
		packet.payload = c;
		session_bus(user_data, &packet);
	}
	return TRUE;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct datafeed_packet *packet;
	struct datafeed_header *header;
	struct databag *mydata;

	mydata = malloc(sizeof(struct databag));
	if (!mydata)
		return SIGROK_ERR_MALLOC;

	mydata->sample_generator = default_genmode;
	mydata->session_device_id = session_device_id;
	mydata->device_index = device_index;
	mydata->samples_counter = 0;
	mydata->loop_sleep = 100000;

	if (pipe(mydata->pipe_fds))
		return SIGROK_ERR;

	channels[0] = g_io_channel_unix_new(mydata->pipe_fds[0]);
	channels[1] = g_io_channel_unix_new(mydata->pipe_fds[1]);

	/* Set channel encoding to binary (default is UTF-8). */
	g_io_channel_set_encoding(channels[0], NULL, NULL);
	g_io_channel_set_encoding(channels[1], NULL, NULL);

	/* Make channels to unbuffered. */
	g_io_channel_set_buffered(channels[0], FALSE);
	g_io_channel_set_buffered(channels[1], FALSE);

	source_add(mydata->pipe_fds[0], G_IO_IN | G_IO_ERR, 40, receive_data,
		   session_device_id);

	/* Run the demo thread. */
	g_thread_init(NULL);
	if (limit_msec)
		mydata->timer = g_timer_new();
	thread_running = 1;
	my_thread =
	    g_thread_create((GThreadFunc)thread_func, mydata, TRUE, NULL);
	if (!my_thread)
		return SIGROK_ERR;

	packet = malloc(sizeof(struct datafeed_packet));
	header = malloc(sizeof(struct datafeed_header));
	if (!packet || !header)
		return SIGROK_ERR_MALLOC;

	packet->type = DF_HEADER;
	packet->length = sizeof(struct datafeed_header);
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = cur_samplerate;
	header->protocol_id = PROTO_RAW;
	header->num_logic_probes = NUM_PROBES;
	header->num_analog_probes = 0;
	session_bus(session_device_id, packet);
	free(header);
	free(packet);

	return SIGROK_OK;
}

static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct datafeed_packet packet;

	/* Avoid compiler warnings. */
	device_index = device_index;

	/* Send last packet. */
	packet.type = DF_END;
	session_bus(session_device_id, &packet);
}

struct device_plugin demo_plugin_info = {
	"demo",
	1,
	hw_init,
	hw_cleanup,
	hw_opendev,
	hw_closedev,
	hw_get_device_info,
	hw_get_status,
	hw_get_capabilities,
	hw_set_configuration,
	hw_start_acquisition,
	hw_stop_acquisition,
};
