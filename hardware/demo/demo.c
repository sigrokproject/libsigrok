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

//#define DEMO_ANALOG

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

#ifdef DEMO_ANALOG
#define NUM_PROBES             9
#else
#define NUM_PROBES             8
#endif
#define DEMONAME               "Demo device"
/* size of chunks to send through the session bus */
#ifdef DEMO_ANALOG
#define BUFSIZE		       32768
#else
#define BUFSIZE                4096
#endif

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

#ifndef DEMO_ANALOG
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
#endif

/* List of struct sigrok_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;
static uint64_t cur_samplerate = KHZ(200);
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
#ifdef DEMO_ANALOG
	/*
	 * We will simulate a device with 8 logic probes and 1 analog probe.
	 * This fictional device sends the data packed: 8 bits for 8 logic
	 * probes and 16 bits for the analog probe, in this order.
	 * Total of 24 bits.
	 * I could just generate a properly formatted DF_ANALOG packet here,
	 * but I will leave the formatting to receive_data() to make its code
	 * more like a real hardware driver.
	 */
	memset(buf, 0, size * 3);

	switch (mydata->sample_generator) {
	default:
	case GENMODE_DEFAULT:
	case GENMODE_RANDOM:
		for (i = 0; i < size * 3; i += 3) {
			*(buf + i) = (uint8_t)(rand() & 0xff);
			*(uint16_t *) (buf + i + 1) = (uint16_t)(rand() & 0xffff);
		}
		break;
	case GENMODE_INC:
		for (i = 0; i < size * 3; i += 3) {
			*(buf + i) = i / 3;
			*(uint16_t *)(buf + i + 1) = i / 3 * 256 * 10;
		}
		break;
	}
#else

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
#endif
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

		if (limit_samples)
			nb_to_send = MIN(nb_to_send,
					limit_samples - mydata->samples_counter);

		/* Make sure we don't overflow. */
#ifdef DEMO_ANALOG
		nb_to_send = MIN(nb_to_send, BUFSIZE / 3);
#else
		nb_to_send = MIN(nb_to_send, BUFSIZE);
#endif

		if (nb_to_send) {
			samples_generator(buf, nb_to_send, data);
			mydata->samples_counter += nb_to_send;
#ifdef DEMO_ANALOG
			g_io_channel_write_chars(channels[1], (gchar *) &buf,
				nb_to_send * 3, (gsize *) &bytes_written, NULL);
#else
			g_io_channel_write_chars(channels[1], (gchar *) &buf,
				nb_to_send, (gsize *) &bytes_written, NULL);
#endif
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
	struct datafeed_packet packet;
	char c[BUFSIZE];
	uint64_t z;
#ifdef DEMO_ANALOG
	struct analog_sample *sample;
	unsigned int i, x;
	int sample_size = sizeof(struct analog_sample) +
		(NUM_PROBES * sizeof(struct analog_probe));
	char *buf;
#endif

	/* Avoid compiler warnings. */
	fd = fd;
	revents = revents;

	do {
		g_io_channel_read_chars(channels[0],
				        (gchar *) &c, BUFSIZE, (gsize *) &z, NULL);

		if (z > 0) {
#ifdef DEMO_ANALOG
			packet.type = DF_ANALOG;

			packet.length = (z / 3) * sample_size;
			packet.unitsize = sample_size;

			buf = malloc(sample_size * packet.length);
			if (!buf)
				return FALSE;

			/* Craft our packet. */
			for (i = 0; i < z / 3; i++) {
				sample = (struct analog_sample *) (buf + (i * sample_size));
				sample->num_probes = NUM_PROBES;

				/* 8 Logic probes */
				for (x = 0; x < NUM_PROBES - 1; x++) {
					sample->probes[x].val =
						(c[i * 3] >> x) & 1;
					sample->probes[x].res = 1;
				}

				/* 1 Analog probe, 16 bit adc */
				for (; x < NUM_PROBES; x++) {
					sample->probes[x].val =
						*(uint16_t *) (c + i * 3 + 1);
					sample->probes[x].val &= ((1 << 16) - 1);
					sample->probes[x].res = 16;
				}

			}

			packet.payload = buf;
			session_bus(user_data, &packet);
			free(buf);
#else
			packet.type = DF_LOGIC;
			packet.length = z;
			packet.unitsize = 1;
			packet.payload = c;
			session_bus(user_data, &packet);
#endif
		}
	} while (z > 0);

	if (!thread_running && z <= 0)
	{
		/* Make sure we don't receive more packets */
		g_io_channel_close(channels[0]);

		/* Send last packet. */
		packet.type = DF_END;
		session_bus(user_data, &packet);

		return FALSE;
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
#ifdef DEMO_ANALOG
	packet->unitsize = sizeof(struct analog_sample) +
		(NUM_PROBES * sizeof(struct analog_probe));
#endif
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
	/* Avoid compiler warnings. */
	device_index = device_index;
	session_device_id = session_device_id;

	/* Stop generate thread. */
	thread_running = 0;
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
