/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>
#include "config.h"
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

static int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_PROBECONFIG,
	SR_HWCAP_CAPTURE_RATIO,

	/* These are really implemented in the driver, not the hardware. */
	SR_HWCAP_LIMIT_SAMPLES,
	0,
};

static const char *probe_names[] = {
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

/* List of struct sr_dev_inst, maintained by dev_open()/dev_close(). */
static GSList *dev_insts = NULL;

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

/* Private, per-device-instance driver context. */
struct context {
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	int num_channels; /* TODO: This isn't initialized before it's needed :( */
	uint64_t memory_size;
	uint8_t probe_mask;
	uint8_t trigger_mask[NUM_TRIGGER_STAGES];
	uint8_t trigger_value[NUM_TRIGGER_STAGES];
	// uint8_t trigger_buffer[NUM_TRIGGER_STAGES];

	struct sr_usb_dev_inst *usb;
};

static int hw_dev_config_set(int dev_index, int hwcap, void *value);

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

static int opendev4(struct sr_dev_inst **sdi, libusb_device *dev,
		    struct libusb_device_descriptor *des)
{
	struct context *ctx;
	unsigned int i;
	int err;

	/* Note: sdi is non-NULL, the caller already checked this. */

	if (!(ctx = (*sdi)->priv)) {
		sr_err("zp: %s: (*sdi)->priv was NULL", __func__);
		return -1;
	}

	if ((err = libusb_get_device_descriptor(dev, des))) {
		sr_err("zp: failed to get device descriptor: %d", err);
		return -1;
	}

	if (des->idVendor != USB_VENDOR)
		return 0;

	if (libusb_get_bus_number(dev) == ctx->usb->bus
	    && libusb_get_device_address(dev) == ctx->usb->address) {

		for (i = 0; i < ARRAY_SIZE(zeroplus_models); i++) {
			if (!(des->idProduct == zeroplus_models[i].pid))
				continue;

			sr_info("zp: Found ZeroPlus device 0x%04x (%s)",
				des->idProduct, zeroplus_models[i].model_name);
			ctx->num_channels = zeroplus_models[i].channels;
			ctx->memory_size = zeroplus_models[i].sample_depth * 1024;
			break;
		}

		if (ctx->num_channels == 0) {
			sr_err("zp: Unknown ZeroPlus device 0x%04x",
			       des->idProduct);
			return -2;
		}

		/* Found it. */
		if (!(err = libusb_open(dev, &(ctx->usb->devhdl)))) {
			(*sdi)->status = SR_ST_ACTIVE;
			sr_info("zp: opened device %d on %d.%d interface %d",
				(*sdi)->index, ctx->usb->bus,
				ctx->usb->address, USB_INTERFACE);
		} else {
			sr_err("zp: failed to open device: %d", err);
			*sdi = NULL;
		}
	}

	return 0;
}

static struct sr_dev_inst *zp_open_dev(int dev_index)
{
	struct sr_dev_inst *sdi;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	int err, i;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
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

static void close_dev(struct sr_dev_inst *sdi)
{
	struct context *ctx;

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return; /* FIXME */
	}

	if (!ctx->usb->devhdl)
		return;

	sr_info("zp: closing device %d on %d.%d interface %d", sdi->index,
		ctx->usb->bus, ctx->usb->address, USB_INTERFACE);
	libusb_release_interface(ctx->usb->devhdl, USB_INTERFACE);
	libusb_reset_device(ctx->usb->devhdl);
	libusb_close(ctx->usb->devhdl);
	ctx->usb->devhdl = NULL;
	/* TODO: Call libusb_exit() here or only in hw_cleanup()? */
	sdi->status = SR_ST_INACTIVE;
}

static int configure_probes(struct sr_dev_inst *sdi, GSList *probes)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	int probe_bit, stage, i;
	char *tc;

	/* Note: sdi and sdi->priv are non-NULL, the caller checked this. */
	ctx = sdi->priv;

	ctx->probe_mask = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		ctx->trigger_mask[i] = 0;
		ctx->trigger_value[i] = 0;
	}

	stage = -1;
	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;
		probe_bit = 1 << (probe->index - 1);
		ctx->probe_mask |= probe_bit;

		if (probe->trigger) {
			stage = 0;
			for (tc = probe->trigger; *tc; tc++) {
				ctx->trigger_mask[stage] |= probe_bit;
				if (*tc == '1')
					ctx->trigger_value[stage] |= probe_bit;
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

static int hw_init(const char *devinfo)
{
	struct sr_dev_inst *sdi;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int err, devcnt, i;
	struct context *ctx;

	/* Avoid compiler warnings. */
	(void)devinfo;

	/* Allocate memory for our private driver context. */
	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("zp: %s: ctx malloc failed", __func__);
		return 0;
	}

	/* Set some sane defaults. */
	ctx->cur_samplerate = 0;
	ctx->limit_samples = 0;
	ctx->num_channels = 32; /* TODO: This isn't initialized before it's needed :( */
	ctx->memory_size = 0;
	ctx->probe_mask = 0;
	memset(ctx->trigger_mask, 0, NUM_TRIGGER_STAGES);
	memset(ctx->trigger_value, 0, NUM_TRIGGER_STAGES);
	// memset(ctx->trigger_buffer, 0, NUM_TRIGGER_STAGES);

	if (libusb_init(&usb_context) != 0) {
		sr_err("zp: Failed to initialize USB.");
		return 0;
	}

	/* Find all ZeroPlus analyzers and add them to device list. */
	devcnt = 0;
	libusb_get_device_list(usb_context, &devlist); /* TODO: Errors. */

	for (i = 0; devlist[i]; i++) {
		err = libusb_get_device_descriptor(devlist[i], &des);
		if (err != 0) {
			sr_err("zp: failed to get device descriptor: %d", err);
			continue;
		}

		if (des.idVendor == USB_VENDOR) {
			/*
			 * Definitely a Zeroplus.
			 * TODO: Any way to detect specific model/version in
			 * the zeroplus range?
			 */
			/* Register the device with libsigrok. */
			if (!(sdi = sr_dev_inst_new(devcnt,
					SR_ST_INACTIVE, USB_VENDOR_NAME,
					USB_MODEL_NAME, USB_MODEL_VERSION))) {
				sr_err("zp: %s: sr_dev_inst_new failed",
				       __func__);
				return 0;
			}

			sdi->priv = ctx;

			dev_insts =
			    g_slist_append(dev_insts, sdi);
			ctx->usb = sr_usb_dev_inst_new(
				libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
			devcnt++;
		}
	}
	libusb_free_device_list(devlist, 1);

	return devcnt;
}

static int hw_dev_open(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int err;

	if (!(sdi = zp_open_dev(dev_index))) {
		sr_err("zp: unable to open device");
		return SR_ERR;
	}

	/* TODO: Note: sdi is retrieved in zp_open_dev(). */

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	err = libusb_set_configuration(ctx->usb->devhdl, USB_CONFIGURATION);
	if (err < 0) {
		sr_err("zp: Unable to set USB configuration %d: %d",
		       USB_CONFIGURATION, err);
		return SR_ERR;
	}

	err = libusb_claim_interface(ctx->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_err("zp: Unable to claim interface: %d", err);
		return SR_ERR;
	}

	analyzer_reset(ctx->usb->devhdl);
	analyzer_initialize(ctx->usb->devhdl);

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

	if (ctx->cur_samplerate == 0) {
		/* Samplerate hasn't been set. Default to the slowest one. */
		if (hw_dev_config_set(dev_index, SR_HWCAP_SAMPLERATE,
		     &samplerates.list[0]) == SR_ERR)
			return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("zp: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	/* TODO */
	close_dev(sdi);

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;

	for (l = dev_insts; l; l = l->next) {
		sdi = l->data;
		/* Properly close all devices... */
		close_dev(sdi);
		/* ...and free all their memory. */
		sr_dev_inst_free(sdi);
	}
	g_slist_free(dev_insts);
	dev_insts = NULL;

	if (usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;

	return SR_OK;
}

static void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	void *info;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("zp: %s: sdi was NULL", __func__);
		return NULL;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return NULL;
	}

	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(ctx->num_channels);
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
		info = &ctx->cur_samplerate;
		break;
	default:
		/* Unknown device info ID, return NULL. */
		sr_err("zp: %s: Unknown device info ID", __func__);
		info = NULL;
		break;
	}

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	sdi = sr_dev_inst_get(dev_insts, dev_index);
	if (sdi)
		return sdi->status;
	else
		return SR_ST_NOT_FOUND;
}

static int *hw_hwcap_get_all(void)
{
	return hwcaps;
}

static int set_samplerate(struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct context *ctx;

	if (!sdi) {
		sr_err("zp: %s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	sr_info("zp: Setting samplerate to %" PRIu64 "Hz.", samplerate);

	if (samplerate > SR_MHZ(1))
		analyzer_set_freq(samplerate / SR_MHZ(1), FREQ_SCALE_MHZ);
	else if (samplerate > SR_KHZ(1))
		analyzer_set_freq(samplerate / SR_KHZ(1), FREQ_SCALE_KHZ);
	else
		analyzer_set_freq(samplerate, FREQ_SCALE_HZ);

	ctx->cur_samplerate = samplerate;

	return SR_OK;
}

static int hw_dev_config_set(int dev_index, int hwcap, void *value)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("zp: %s: sdi was NULL", __func__);
		return SR_ERR;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		return set_samplerate(sdi, *(uint64_t *)value);
	case SR_HWCAP_PROBECONFIG:
		return configure_probes(sdi, (GSList *)value);
	case SR_HWCAP_LIMIT_SAMPLES:
		ctx->limit_samples = *(uint64_t *)value;
		return SR_OK;
	default:
		return SR_ERR;
	}
}

static int hw_dev_acquisition_start(int dev_index, gpointer session_data)
{
	struct sr_dev_inst *sdi;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_header header;
	uint64_t samples_read;
	int res;
	unsigned int packet_num;
	unsigned char *buf;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("zp: %s: sdi was NULL", __func__);
		return SR_ERR;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* push configured settings to device */
	analyzer_configure(ctx->usb->devhdl);

	analyzer_start(ctx->usb->devhdl);
	sr_info("zp: Waiting for data");
	analyzer_wait_data(ctx->usb->devhdl);

	sr_info("zp: Stop address    = 0x%x",
		analyzer_get_stop_address(ctx->usb->devhdl));
	sr_info("zp: Now address     = 0x%x",
		analyzer_get_now_address(ctx->usb->devhdl));
	sr_info("zp: Trigger address = 0x%x",
		analyzer_get_trigger_address(ctx->usb->devhdl));

	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = ctx->cur_samplerate;
	header.num_logic_probes = ctx->num_channels;
	sr_session_bus(session_data, &packet);

	if (!(buf = g_try_malloc(PACKET_SIZE))) {
		sr_err("zp: %s: buf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	samples_read = 0;
	analyzer_read_start(ctx->usb->devhdl);
	/* Send the incoming transfer to the session bus. */
	for (packet_num = 0; packet_num < (ctx->memory_size * 4 / PACKET_SIZE);
	     packet_num++) {
		res = analyzer_read_data(ctx->usb->devhdl, buf, PACKET_SIZE);
		sr_info("zp: Tried to read %llx bytes, actually read %x bytes",
			PACKET_SIZE, res);

		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = PACKET_SIZE;
		logic.unitsize = 4;
		logic.data = buf;
		sr_session_bus(session_data, &packet);
		samples_read += res / 4;
	}
	analyzer_read_stop(ctx->usb->devhdl);
	g_free(buf);

	packet.type = SR_DF_END;
	sr_session_bus(session_data, &packet);

	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(int dev_index, gpointer session_dev_id)
{
	struct sr_datafeed_packet packet;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	packet.type = SR_DF_END;
	sr_session_bus(session_dev_id, &packet);

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("zp: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("zp: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	analyzer_reset(ctx->usb->devhdl);
	/* TODO: Need to cancel and free any queued up transfers. */

	return SR_OK;
}

SR_PRIV struct sr_dev_plugin zeroplus_logic_cube_plugin_info = {
	.name = "zeroplus-logic-cube",
	.longname = "Zeroplus Logic Cube LAP-C series",
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
