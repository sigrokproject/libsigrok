/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>
#include "sigrok.h"
#include "sigrok-internal.h"
#include "analyzer.h"

#define USB_VENDOR			0x0c12
#define USB_VENDOR_NAME			"Zeroplus"
#define USB_MODEL_NAME			"Logic Cube"
#define USB_MODEL_VERSION		""

#define USB_INTERFACE			0
#define USB_CONFIGURATION		1
#define NUM_TRIGGER_STAGES		4
#define TRIGGER_TYPES			"01"

#define PACKET_SIZE			2048	/* ?? */

typedef struct {
	unsigned short pid;
	char model_name[64];
	unsigned int channels;
	unsigned int sample_depth;	/* In Ksamples/channel */
	unsigned int max_sampling_freq;
} model_t;

/*
 * Note -- 16032, 16064 and 16128 *usually* -- but not always -- have the
 * same 128K sample depth.
 */
static model_t zeroplus_models[] = {
	{0x7009, "LAP-C(16064)",  16, 64,   100},
	{0x700A, "LAP-C(16128)",  16, 128,  200},
	{0x700B, "LAP-C(32128)",  32, 128,  200},
	{0x700C, "LAP-C(321000)", 32, 1024, 200},
	{0x700D, "LAP-C(322000)", 32, 2048, 200},
	{0x700E, "LAP-C(16032)",  16, 32,   100},
	{0x7016, "LAP-C(162000)", 16, 2048, 200},
};

static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_PROBECONFIG,
	SR_HWCAP_CAPTURE_RATIO,

	/* These are really implemented in the driver, not the hardware. */
	SR_HWCAP_LIMIT_SAMPLES,
	0,
};

static const char* probe_names[] = {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"14",
	"15",
	"16",
	"17",
	"18",
	"19",
	"20",
	"21",
	"22",
	"23",
	"24",
	"25",
	"26",
	"27",
	"28",
	"29",
	"30",
	"31",
	NULL,
};

/* List of struct sr_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;

static libusb_context *usb_context = NULL;

/*
 * The hardware supports more samplerates than these, but these are the
 * options hardcoded into the vendor's Windows GUI.
 */

/*
 * TODO: We shouldn't support 150MHz and 200MHz on devices that don't go up
 * that high.
 */
static uint64_t supported_samplerates[] = {
	SR_HZ(100),
	SR_HZ(500),
	SR_KHZ(1),
	SR_KHZ(5),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(400),
	SR_KHZ(800),
	SR_MHZ(1),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(80),
	SR_MHZ(100),
	SR_MHZ(150),
	SR_MHZ(200),
	0,
};

static struct sr_samplerates samplerates = {
	SR_HZ(0),
	SR_HZ(0),
	SR_HZ(0),
	supported_samplerates,
};

/* TODO: All of these should go in a device-specific struct. */
static uint64_t cur_samplerate = 0;
static uint64_t period_ps = 0;
static uint64_t limit_samples = 0;
static int num_channels = 32; /* TODO: This isn't initialized before it's needed :( */
static uint64_t memory_size = 0;
static uint8_t probe_mask = 0;
static uint8_t trigger_mask[NUM_TRIGGER_STAGES] = { 0 };
static uint8_t trigger_value[NUM_TRIGGER_STAGES] = { 0 };

// static uint8_t trigger_buffer[NUM_TRIGGER_STAGES] = { 0 };

static int hw_set_configuration(int device_index, int capability, void *value);

static unsigned int get_memory_size(int type)
{
	if (type == MEMORY_SIZE_8K)
		return 8 * 1024;
	else if (type == MEMORY_SIZE_64K)
		return 64 * 1024;
	else if (type == MEMORY_SIZE_128K)
		return 128 * 1024;
	else if (type == MEMORY_SIZE_512K)
		return 512 * 1024;
	else
		return 0;
}

static int opendev4(struct sr_device_instance **sdi, libusb_device *dev,
		    struct libusb_device_descriptor *des)
{
	unsigned int i;
	int err;

	if ((err = libusb_get_device_descriptor(dev, des))) {
		sr_warn("failed to get device descriptor: %d", err);
		return -1;
	}

	if (des->idVendor != USB_VENDOR)
		return 0;

	if (libusb_get_bus_number(dev) == (*sdi)->usb->bus
	    && libusb_get_device_address(dev) == (*sdi)->usb->address) {

		for (i = 0; i < ARRAY_SIZE(zeroplus_models); i++) {
			if (!(des->idProduct == zeroplus_models[i].pid))
				continue;

			sr_info("Found PID=%04X (%s)", des->idProduct,
				zeroplus_models[i].model_name);
			num_channels = zeroplus_models[i].channels;
			memory_size = zeroplus_models[i].sample_depth * 1024;
			break;
		}

		if (num_channels == 0) {
			sr_warn("Unknown ZeroPlus device %04X", des->idProduct);
			return -2;
		}

		/* Found it. */
		if (!(err = libusb_open(dev, &((*sdi)->usb->devhdl)))) {
			(*sdi)->status = SR_ST_ACTIVE;
			sr_info("opened device %d on %d.%d interface %d",
				(*sdi)->index, (*sdi)->usb->bus,
				(*sdi)->usb->address, USB_INTERFACE);
		} else {
			sr_warn("failed to open device: %d", err);
			*sdi = NULL;
		}
	}

	return 0;
}

static struct sr_device_instance *zp_open_device(int device_index)
{
	struct sr_device_instance *sdi;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	int err, i;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return NULL;

	libusb_get_device_list(usb_context, &devlist);
	if (sdi->status == SR_ST_INACTIVE) {
		/* Find the device by vendor, product, bus and address. */
		libusb_get_device_list(usb_context, &devlist);
		for (i = 0; devlist[i]; i++) {
			/* TODO: Error handling. */
			err = opendev4(&sdi, devlist[i], &des);
		}
	} else {
		/* Status must be SR_ST_ACTIVE, i.e. already in use... */
		sdi = NULL;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi && sdi->status != SR_ST_ACTIVE)
		sdi = NULL;

	return sdi;
}

static void close_device(struct sr_device_instance *sdi)
{
	if (!sdi->usb->devhdl)
		return;

	sr_info("closing device %d on %d.%d interface %d", sdi->index,
		sdi->usb->bus, sdi->usb->address, USB_INTERFACE);
	libusb_release_interface(sdi->usb->devhdl, USB_INTERFACE);
	libusb_close(sdi->usb->devhdl);
	sdi->usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;
}

static int configure_probes(GSList *probes)
{
	struct sr_probe *probe;
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
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;
		probe_bit = 1 << (probe->index - 1);
		probe_mask |= probe_bit;

		if (probe->trigger) {
			stage = 0;
			for (tc = probe->trigger; *tc; tc++) {
				trigger_mask[stage] |= probe_bit;
				if (*tc == '1')
					trigger_value[stage] |= probe_bit;
				stage++;
				if (stage > NUM_TRIGGER_STAGES)
					return SR_ERR;
			}
		}
	}

	return SR_OK;
}

/*
 * API callbacks
 */

static int hw_init(const char *deviceinfo)
{
	struct sr_device_instance *sdi;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int err, devcnt, i;

	/* Avoid compiler warnings. */
	(void)deviceinfo;

	if (libusb_init(&usb_context) != 0) {
		sr_warn("Failed to initialize USB.");
		return 0;
	}

	/* Find all ZeroPlus analyzers and add them to device list. */
	devcnt = 0;
	libusb_get_device_list(usb_context, &devlist);

	for (i = 0; devlist[i]; i++) {
		err = libusb_get_device_descriptor(devlist[i], &des);
		if (err != 0) {
			sr_warn("failed to get device descriptor: %d", err);
			continue;
		}

		if (des.idVendor == USB_VENDOR) {
			/*
			 * Definitely a Zeroplus.
			 * TODO: Any way to detect specific model/version in
			 * the zeroplus range?
			 */
			sdi = sr_device_instance_new(devcnt,
					SR_ST_INACTIVE, USB_VENDOR_NAME,
					USB_MODEL_NAME, USB_MODEL_VERSION);
			if (!sdi)
				return 0;
			device_instances =
			    g_slist_append(device_instances, sdi);
			sdi->usb = sr_usb_device_instance_new(
				libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
			devcnt++;
		}
	}
	libusb_free_device_list(devlist, 1);

	return devcnt;
}

static int hw_opendev(int device_index)
{
	struct sr_device_instance *sdi;
	int err;

	if (!(sdi = zp_open_device(device_index))) {
		sr_warn("unable to open device");
		return SR_ERR;
	}

	err = libusb_claim_interface(sdi->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_warn("Unable to claim interface: %d", err);
		return SR_ERR;
	}
	analyzer_reset(sdi->usb->devhdl);
	analyzer_initialize(sdi->usb->devhdl);

	analyzer_set_memory_size(MEMORY_SIZE_512K);
	// analyzer_set_freq(g_freq, g_freq_scale);
	analyzer_set_trigger_count(1);
	// analyzer_set_ramsize_trigger_address((((100 - g_pre_trigger)
	// * get_memory_size(g_memory_size)) / 100) >> 2);
	analyzer_set_ramsize_trigger_address(
		(100 * get_memory_size(MEMORY_SIZE_512K) / 100) >> 2);

#if 0
	if (g_double_mode == 1)
		analyzer_set_compression(COMPRESSION_DOUBLE);
	else if (g_compression == 1)
		analyzer_set_compression(COMPRESSION_ENABLE);
	else
#endif
	analyzer_set_compression(COMPRESSION_NONE);

	if (cur_samplerate == 0) {
		/* Samplerate hasn't been set. Default to the slowest one. */
		if (hw_set_configuration(device_index, SR_HWCAP_SAMPLERATE,
		     &samplerates.low) == SR_ERR)
			return SR_ERR;
	}

	return SR_OK;
}

static int hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index))) {
		sr_err("lap-c: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	/* TODO */
	close_device(sdi);

	return SR_OK;
}

static void hw_cleanup(void)
{
	GSList *l;

	/* Properly close all devices... */
	for (l = device_instances; l; l = l->next)
		close_device((struct sr_device_instance *)l->data);

	/* ...and free all their memory. */
	for (l = device_instances; l; l = l->next)
		g_free(l->data);
	g_slist_free(device_instances);
	device_instances = NULL;

	if (usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sr_device_instance *sdi;
	void *info = NULL;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return NULL;

	switch (device_info_id) {
	case SR_DI_INSTANCE:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(num_channels);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		break;
	case SR_DI_SAMPLERATES:
		info = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		info = TRIGGER_TYPES;
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &cur_samplerate;
		break;
	}

	return info;
}

static int hw_get_status(int device_index)
{
	struct sr_device_instance *sdi;

	sdi = sr_get_device_instance(device_instances, device_index);
	if (sdi)
		return sdi->status;
	else
		return SR_ST_NOT_FOUND;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

/* TODO: This will set the same samplerate for all devices. */
static int set_configuration_samplerate(uint64_t samplerate)
{
	sr_info("%s(%" PRIu64 ")", __func__, samplerate);
	if (samplerate > SR_MHZ(1))
		analyzer_set_freq(samplerate / SR_MHZ(1), FREQ_SCALE_MHZ);
	else if (samplerate > SR_KHZ(1))
		analyzer_set_freq(samplerate / SR_KHZ(1), FREQ_SCALE_KHZ);
	else
		analyzer_set_freq(samplerate, FREQ_SCALE_HZ);

	cur_samplerate = samplerate;
	period_ps = 1000000000000 / samplerate;

	return SR_OK;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	struct sr_device_instance *sdi;
	uint64_t *tmp_u64;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	switch (capability) {
	case SR_HWCAP_SAMPLERATE:
		tmp_u64 = value;
		return set_configuration_samplerate(*tmp_u64);
	case SR_HWCAP_PROBECONFIG:
		return configure_probes((GSList *) value);
	case SR_HWCAP_LIMIT_SAMPLES:
		tmp_u64 = value;
		limit_samples = *tmp_u64;
		return SR_OK;
	default:
		return SR_ERR;
	}
}

static int hw_start_acquisition(int device_index, gpointer session_data)
{
	struct sr_device_instance *sdi;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_header header;
	uint64_t samples_read;
	int res;
	unsigned int packet_num;
	unsigned char *buf;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	/* push configured settings to device */
	analyzer_configure(sdi->usb->devhdl);

	analyzer_start(sdi->usb->devhdl);
	sr_info("Waiting for data");
	analyzer_wait_data(sdi->usb->devhdl);

	sr_info("Stop address    = 0x%x", analyzer_get_stop_address(sdi->usb->devhdl));
	sr_info("Now address     = 0x%x", analyzer_get_now_address(sdi->usb->devhdl));
	sr_info("Trigger address = 0x%x", analyzer_get_trigger_address(sdi->usb->devhdl));

	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = cur_samplerate;
	header.num_logic_probes = num_channels;
	header.num_analog_probes = 0;
	sr_session_bus(session_data, &packet);

	if (!(buf = g_try_malloc(PACKET_SIZE))) {
		sr_err("lap-c: %s: buf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	samples_read = 0;
	analyzer_read_start(sdi->usb->devhdl);
	/* Send the incoming transfer to the session bus. */
	for (packet_num = 0; packet_num < (memory_size * 4 / PACKET_SIZE);
	     packet_num++) {
		res = analyzer_read_data(sdi->usb->devhdl, buf, PACKET_SIZE);
		sr_info("Tried to read %llx bytes, actually read %x bytes",
			PACKET_SIZE, res);

		packet.type = SR_DF_LOGIC;
		packet.timeoffset = samples_read * period_ps;
		packet.duration = res / 4 * period_ps;
		packet.payload = &logic;
		logic.length = PACKET_SIZE;
		logic.unitsize = 4;
		logic.data = buf;
		sr_session_bus(session_data, &packet);
		samples_read += res / 4;
	}
	analyzer_read_stop(sdi->usb->devhdl);
	g_free(buf);

	packet.type = SR_DF_END;
	sr_session_bus(session_data, &packet);

	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring device_index. */
static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet packet;
	struct sr_device_instance *sdi;

	packet.type = SR_DF_END;
	sr_session_bus(session_device_id, &packet);

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return; /* TODO: Cry? */

	analyzer_reset(sdi->usb->devhdl);
	/* TODO: Need to cancel and free any queued up transfers. */
}

struct sr_device_plugin zeroplus_logic_cube_plugin_info = {
	.name = "zeroplus-logic-cube",
	.longname = "Zeroplus Logic Cube LAP-C series",
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
