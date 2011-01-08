/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
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
#include "config.h"

#define BUFSIZE 			4096

#define NUM_PROBES			8
#define NUM_TRIGGER_STAGES		4
#define TRIGGER_TYPES			"01"

/* Software trigger implementation: positive values indicate trigger stage. */
#define TRIGGER_FIRED			-1

#define USB_MODEL_NAME			"Demo Driver"
#define USB_VENDOR_NAME			"Sigrok project"
#define USB_MODEL_VERSION		"v1.0"

#define GENMODE_RANDOM			1
#define GENMODE_INC			2

static GThread *my_thread;
static int thread_running;

static int capabilities[] = {
	HWCAP_LOGIC_ANALYZER,
	HWCAP_SAMPLERATE,
	HWCAP_LIMIT_SAMPLES,
};

/* Random selection of samplerates this "device" shall support. */
static uint64_t supported_samplerates[] = {
	KHZ(100),
	KHZ(500),
	MHZ(1),
	MHZ(2),
	MHZ(12),
	MHZ(24),
	0,
};

static struct samplerates samplerates = {
	KHZ(100),
	MHZ(24),
	0,
	supported_samplerates,
};

struct databag {
	int pipe_fds[2];
	uint8_t sample_generator;
	uint8_t thread_running;
	uint64_t samples_counter;
	int device_index;
	int loop_sleep;
	gpointer session_device_id;
};

/* List of struct sigrok_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;

/* TODO: All of these should go in a device-specific struct. */
static uint64_t cur_samplerate = 0;
static uint64_t limit_samples = 0;
// static uint8_t probe_mask = 0;
// static uint8_t trigger_mask[NUM_TRIGGER_STAGES] = { 0 };
// static uint8_t trigger_value[NUM_TRIGGER_STAGES] = { 0 };
// static uint8_t trigger_buffer[NUM_TRIGGER_STAGES] = { 0 };

// static int trigger_stage = TRIGGER_FIRED;

static int hw_set_configuration(int device_index, int capability, void *value);
static void hw_stop_acquisition(int device_index, gpointer session_device_id);

static int hw_init(char *deviceinfo)
{
	/* Avoid compiler warning. */
	deviceinfo = deviceinfo;

	struct sigrok_device_instance *sdi;

	sdi = sigrok_device_instance_new(0, ST_ACTIVE,
		 USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);

	if (!sdi)
		return 0;
	device_instances = g_slist_append(device_instances, sdi);

	return 1;
}

static int hw_opendev(int device_index)
{
	/* Avoid compiler warning. */
	device_index = device_index;

	/* Nothing needed so far. */
	return SIGROK_OK;
}

static void hw_closedev(int device_index)
{
	/* Avoid compiler warning. */
	device_index = device_index;

	/* Nothing needed so far. */
}

static void hw_cleanup(void)
{
	/* Nothing needed so far. */
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	/* Avoid compiler warning. */
	device_index = device_index;

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
	case DI_SAMPLERATES:
		info = &samplerates;
		break;
	case DI_TRIGGER_TYPES:
		info = TRIGGER_TYPES;
		break;
	case DI_CUR_SAMPLERATE:
		info = &cur_samplerate;
		break;
	}

	return info;
}

static int hw_get_status(int device_index)
{
	/* Avoid compiler warning. */
	device_index = device_index;
	return 0; /* FIXME */
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	int ret;
	uint64_t *tmp_u64;

	/* Avoid compiler warning. */
	device_index = device_index;

	if (capability == HWCAP_SAMPLERATE) {
		cur_samplerate = *(uint64_t *) value;
		ret = SIGROK_OK;
	} else if (capability == HWCAP_PROBECONFIG) {
		// ret = configure_probes((GSList *) value); FIXME
		ret = SIGROK_OK;
	} else if (capability == HWCAP_LIMIT_SAMPLES) {
		tmp_u64 = value;
		limit_samples = *tmp_u64;
		ret = SIGROK_OK;
	} else {
		ret = SIGROK_ERR;
	}

	return ret;
}

static void samples_generator(uint8_t *buf, uint64_t sz, void *data)
{
	struct databag *mydata = data;
	uint64_t i;
	uint8_t val;

	memset(buf, 0, sz);

	switch (mydata->sample_generator) {
	case GENMODE_RANDOM: /* Random */
		for (i = 0; i < sz; i++) {
			val = rand() & 0xff;
			*(buf + i) = val;
		}
		break;
	case GENMODE_INC: /* Simple increment */
		for (i = 0; i < sz; i++)
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

	while (thread_running) {
		nb_to_send = limit_samples - mydata->samples_counter;

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

		write(mydata->pipe_fds[1], &buf, nb_to_send);
		g_usleep(mydata->loop_sleep);
	}
}

/* Callback handling data */
static int receive_data(int fd, int revents, void *user_data)
{
	struct datafeed_packet packet;
	/* uint16_t samples[1000]; */
	char c[BUFSIZE];
	uint64_t z;

	/* Avoid compiler warnings. */
	revents = revents;

	z = read(fd, &c, BUFSIZE);
	if (z > 0) {
		packet.type = DF_LOGIC8;
		packet.length = z;
		packet.payload = c;
		session_bus(user_data, &packet);
	}
	return TRUE;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct datafeed_packet *packet;
	struct datafeed_header *header;
	unsigned char *buf;
	struct databag *mydata;

	mydata = malloc(sizeof(struct databag));
	/* TODO: Error handling. */

	mydata->sample_generator = GENMODE_RANDOM;
	mydata->session_device_id = session_device_id;
	mydata->device_index = device_index;
	mydata->samples_counter = 0;
	mydata->loop_sleep = 100000;

	if (pipe(mydata->pipe_fds)) {
		fprintf(stderr, "Pipe failed.\n");
		return SIGROK_ERR_MALLOC; /* FIXME */

	}
	source_add(mydata->pipe_fds[0], G_IO_IN | G_IO_ERR, 40, receive_data,
		   session_device_id);

	/* Run the demo thread. */
	g_thread_init(NULL);
	thread_running = 1;
	my_thread =
	    g_thread_create((GThreadFunc)thread_func, mydata, TRUE, NULL);
	if (!my_thread) {
		fprintf(stderr, "demo: Thread creation failed.\n");
		return SIGROK_ERR_MALLOC; /* FIXME */
	}

	packet = malloc(sizeof(struct datafeed_packet));
	header = malloc(sizeof(struct datafeed_header));
	buf = malloc(2048);
	if (!packet || !header || !buf)
		return SIGROK_ERR_MALLOC;

	/* FIXME */
	memset(buf, 0x55, 2048);

	packet->type = DF_HEADER;
	packet->length = sizeof(struct datafeed_header);
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = cur_samplerate;
	header->protocol_id = PROTO_RAW;
	header->num_probes = NUM_PROBES;
	session_bus(session_device_id, packet);
	free(header);
	free(packet);

	return SIGROK_OK;
}

/* This stops acquisition on ALL devices, ignoring device_index. */
static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct datafeed_packet packet;

	/* QUICK HACK */
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
