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
#include <string.h>
#include <sigrok.h>
#include "config.h"

#define NUM_PROBES			8
#define NUM_TRIGGER_STAGES		4
#define TRIGGER_TYPES			"01"

/* Software trigger implementation: positive values indicate trigger stage. */
#define TRIGGER_FIRED			-1

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

#if 0
static int configure_probes(GSList *probes)
{
	struct probe *probe;
	GSList *l;
	int probe_bit, stage, i;
	char *tc;

	probe_mask = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		trigger_mask[i] = 0;
		trigger_value[i] = 0;
	}

	stage = -1;
	for (l = probes; l; l = l->next) {
		probe = (struct probe *)l->data;
		if (!(probe->enabled))
			continue;
		probe_bit = 1 << (probe->index - 1);
		probe_mask |= probe_bit;
		if (!(probe->trigger))
			continue;

		stage = 0;
		for (tc = probe->trigger; *tc; tc++) {
			trigger_mask[stage] |= probe_bit;
			if (*tc == '1')
				trigger_value[stage] |= probe_bit;
			stage++;
			if (stage > NUM_TRIGGER_STAGES)
				return SIGROK_ERR;
		}
	}

	if (stage == -1)
		/*
		 * We didn't configure any triggers, make sure acquisition
		 * doesn't wait for any.
		 */
		trigger_stage = TRIGGER_FIRED;
	else
		trigger_stage = 0;

	return SIGROK_OK;
}
#endif

static int hw_init(char *deviceinfo)
{
	/* Avoid compiler warning. */
	deviceinfo = deviceinfo;

	/* Nothing needed so far. */
	return 1; /* FIXME? */
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
	void *info = NULL;

	/* Avoid compiler warning. */
	device_index = device_index;

	switch (device_info_id) {
	case DI_INSTANCE:
		/// info = sdi;
		/* TODO */
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

	/* Avoid compiler warning. */
	device_index = device_index;

	if (capability == HWCAP_SAMPLERATE) {
		cur_samplerate = *(uint64_t *)value;
		ret = SIGROK_OK;
	} else if (capability == HWCAP_PROBECONFIG) {
		// ret = configure_probes((GSList *) value);
		ret = SIGROK_ERR;
	} else if (capability == HWCAP_LIMIT_SAMPLES) {
		limit_samples = strtoull(value, NULL, 10);
		ret = SIGROK_OK;
	} else {
		ret = SIGROK_ERR;
	}

	return ret;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct datafeed_packet *packet;
	struct datafeed_header *header;
	unsigned char *buf;

	/* Avoid compiler warning. */
	device_index = device_index;

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

#if 0
void receive_transfer(struct libusb_transfer *transfer)
{
	static int num_samples = 0;
	static int empty_transfer_count = 0;
	struct datafeed_packet packet;
	void *user_data;
	int cur_buflen, trigger_offset, i;
	unsigned char *cur_buf, *new_buf;

	g_message("receive_transfer(): status %d received %d bytes",
		  transfer->status, transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */
	cur_buf = transfer->buffer;
	cur_buflen = transfer->actual_length;
	user_data = transfer->user_data;

	/* Fire off a new request. */
	new_buf = g_malloc(4096);
	transfer->buffer = new_buf;
	transfer->length = 4096;
	if (libusb_submit_transfer(transfer) != 0) {
		/* TODO: Stop session? */
		g_warning("eek");
	}

	trigger_offset = 0;
	if (trigger_stage >= 0) {
		for (i = 0; i < cur_buflen; i++) {
			trigger_helper(i, cur_buf, &packet, user_data,
				       &trigger_offset);
		}
	}

	if (trigger_stage == TRIGGER_FIRED) {
		/* Send the incoming transfer to the session bus. */
		packet.type = DF_LOGIC8;
		packet.length = cur_buflen - trigger_offset;
		packet.payload = cur_buf + trigger_offset;
		session_bus(user_data, &packet);
		free(cur_buf);

		num_samples += cur_buflen;
		if ((unsigned int)num_samples > limit_samples) {
			hw_stop_acquisition(-1, user_data);
		}
	} else {
		/*
		 * TODO: Buffer pre-trigger data in capture
		 * ratio-sized buffer.
		 */
	}
}
#endif

/* This stops acquisition on ALL devices, ignoring device_index. */
static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct datafeed_packet packet;

	/* QUICK HACK */
	device_index = device_index;

	packet.type = DF_END;
	session_bus(session_device_id, &packet);

	/// receive_transfer(NULL);

	/* TODO: Need to cancel and free any queued up transfers. */
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
