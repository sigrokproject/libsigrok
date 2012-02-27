/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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
#include <stdbool.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>

#include "config.h"
#include "sigrok.h"
#include "sigrok-internal.h"
#include "fx2lafw.h"

static struct fx2lafw_profile supported_fx2[] = {
	/* USBee AX */
	{ 0x08a9, 0x0014, "CWAV", "USBee AX", NULL, 8 },
	{ 0, 0, 0, 0, 0, 0 }
};

static int fx2lafw_capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,

	/* These are really implemented in the driver, not the hardware. */
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0
};

static const char *fx2lafw_probe_names[] = {
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	NULL
};

static uint64_t fx2lafw_supported_samplerates[] = {
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(3),
	SR_MHZ(4),
	SR_MHZ(6),
	SR_MHZ(8),
	SR_MHZ(12),
	SR_MHZ(16),
	SR_MHZ(24)
};

static struct sr_samplerates fx2lafw_samplerates = {
	SR_MHZ(1),
	SR_MHZ(24),
	SR_HZ(0),
	fx2lafw_supported_samplerates
};

static GSList *dev_insts = NULL;
static libusb_context *usb_context = NULL;

static int hw_dev_acquisition_stop(int dev_index, gpointer session_dev_id);

/**
 * Check the USB configuration to determine if this is an fx2lafw device.
 *
 * @return true if the device's configuration profile match fx2lafw
 *         configuration, flase otherwise.
 */
static bool check_conf_profile(libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *conf_dsc = NULL;
	const struct libusb_interface_descriptor *intf_dsc;
	bool ret = false;

	while (!ret) {
		/* Assume it's not a Saleae Logic unless proven wrong. */
		ret = 0;

		if (libusb_get_device_descriptor(dev, &des) != 0)
			break;

		if (des.bNumConfigurations != 1)
			/* Need exactly 1 configuration. */
			break;

		if (libusb_get_config_descriptor(dev, 0, &conf_dsc) != 0)
			break;

		if (conf_dsc->bNumInterfaces != 1)
			/* Need exactly 1 interface. */
			break;

		if (conf_dsc->interface[0].num_altsetting != 1)
			/* Need just one alternate setting. */
			break;

		intf_dsc = &(conf_dsc->interface[0].altsetting[0]);
		if (intf_dsc->bNumEndpoints != 3)
			/* Need exactly 3 end points. */
			break;

		if ((intf_dsc->endpoint[0].bEndpointAddress & 0x8f) !=
		    (1 | LIBUSB_ENDPOINT_OUT))
			/* The first endpoint should be 1 (outbound). */
			break;

		if ((intf_dsc->endpoint[1].bEndpointAddress & 0x8f) !=
		    (2 | LIBUSB_ENDPOINT_IN))
			/* The second endpoint should be 2 (inbound). */
			break;

		/* TODO: Check the debug channel... */

		/* If we made it here, it must be an fx2lafw. */
		ret = true;
	}

	if (conf_dsc)
		libusb_free_config_descriptor(conf_dsc);

	return ret;
}

static int fx2lafw_open_dev(int dev_index)
{
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	struct sr_dev_inst *sdi;
	struct fx2lafw_device *ctx;
	int err, skip, i;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	ctx = sdi->priv;

	if (sdi->status == SR_ST_ACTIVE)
		/* already in use */
		return SR_ERR;

	skip = 0;
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((err = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("fx2lafw: failed to get device descriptor: %d", err);
			continue;
		}

		if (des.idVendor != FIRMWARE_VID
		    || des.idProduct != FIRMWARE_PID)
			continue;

		if (sdi->status == SR_ST_INITIALIZING) {
			if (skip != dev_index) {
				/* Skip devices of this type that aren't the one we want. */
				skip += 1;
				continue;
			}
		} else if (sdi->status == SR_ST_INACTIVE) {
			/*
			 * This device is fully enumerated, so we need to find
			 * this device by vendor, product, bus and address.
			 */
			if (libusb_get_bus_number(devlist[i]) != ctx->usb->bus
				|| libusb_get_device_address(devlist[i]) != ctx->usb->address)
				/* this is not the one */
				continue;
		}

		if (!(err = libusb_open(devlist[i], &ctx->usb->devhdl))) {
			if (ctx->usb->address == 0xff)
				/*
				 * first time we touch this device after firmware upload,
				 * so we don't know the address yet.
				 */
				ctx->usb->address = libusb_get_device_address(devlist[i]);

			sdi->status = SR_ST_ACTIVE;
			sr_info("fx2lafw: opened device %d on %d.%d interface %d",
				sdi->index, ctx->usb->bus,
				ctx->usb->address, USB_INTERFACE);
		} else {
			sr_err("fx2lafw: failed to open device: %d", err);
		}

		/* if we made it here, we handled the device one way or another */
		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	return SR_OK;
}

static void close_dev(struct sr_dev_inst *sdi)
{
	struct fx2lafw_device *ctx;

	ctx = sdi->priv;

	if (ctx->usb->devhdl == NULL)
		return;

	sr_info("fx2lafw: closing device %d on %d.%d interface %d", sdi->index,
		ctx->usb->bus, ctx->usb->address, USB_INTERFACE);
	libusb_release_interface(ctx->usb->devhdl, USB_INTERFACE);
	libusb_close(ctx->usb->devhdl);
	ctx->usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;
}

static struct fx2lafw_device* fx2lafw_device_new(void)
{
	struct fx2lafw_device *fx2lafw;

	if (!(fx2lafw = g_try_malloc0(sizeof(struct fx2lafw_device)))) {
		sr_err("fx2lafw: %s: fx2lafw_device malloc failed", __func__);
		return NULL;
	}

	return fx2lafw;
}

/*
 * API callbacks
 */

static int hw_init(const char *deviceinfo)
{
	struct sr_dev_inst *sdi;
	struct libusb_device_descriptor des;
	struct fx2lafw_profile *fx2lafw_prof;
	struct fx2lafw_device *ctx;
	libusb_device **devlist;
	int err;
	int devcnt = 0;
	int i, j;

	/* Avoid compiler warnings. */
	(void)deviceinfo;

	if (libusb_init(&usb_context) != 0) {
		sr_warn("Failed to initialize USB.");
		return 0;
	}

	/* Find all fx2lafw compatible devices and upload firware to all of them. */
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {

		if ((err = libusb_get_device_descriptor(
			devlist[i], &des)) != 0) {
			sr_warn("failed to get device descriptor: %d", err);
			continue;
		}

		fx2lafw_prof = NULL;
		for (j = 0; supported_fx2[j].vid; j++) {
			if (des.idVendor == supported_fx2[j].vid &&
				des.idProduct == supported_fx2[j].pid) {
				fx2lafw_prof = &supported_fx2[j];
			}
		}

		/* Skip if the device was not found */
		if(!fx2lafw_prof)
			continue;

		sdi = sr_dev_inst_new(devcnt, SR_ST_INITIALIZING,
			fx2lafw_prof->vendor, fx2lafw_prof->model,
			fx2lafw_prof->model_version);
		if(!sdi)
			return 0;

		ctx = fx2lafw_device_new();
		ctx->profile = fx2lafw_prof;
		sdi->priv = ctx;
		dev_insts = g_slist_append(dev_insts, sdi);

		if (check_conf_profile(devlist[i])) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("fx2lafw: Found a fx2lafw device.");
			sdi->status = SR_ST_INACTIVE;
			ctx->usb = sr_usb_dev_inst_new
			    (libusb_get_bus_number(devlist[i]),
			     libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION, FIRMWARE) == SR_OK)
				/* Remember when the firmware on this device was updated */
				g_get_current_time(&ctx->fw_updated);
			else
				sr_err("fx2lafw: firmware upload failed for "
				       "device %d", devcnt);
			ctx->usb = sr_usb_dev_inst_new
				(libusb_get_bus_number(devlist[i]), 0xff, NULL);
		}

		devcnt++;
	}
	libusb_free_device_list(devlist, 1);

	return devcnt;
}

static int hw_dev_open(int device_index)
{
	GTimeVal cur_time;
	struct sr_dev_inst *sdi;
	struct fx2lafw_device *ctx;
	int timediff, err;

	if (!(sdi = sr_dev_inst_get(dev_insts, device_index)))
		return SR_ERR;
	ctx = sdi->priv;

	/*
	 * if the firmware was recently uploaded, wait up to MAX_RENUM_DELAY ms
	 * for the FX2 to renumerate
	 */
	err = 0;
	if (GTV_TO_MSEC(ctx->fw_updated) > 0) {
		sr_info("fx2lafw: waiting for device to reset");
		/* takes at least 300ms for the FX2 to be gone from the USB bus */
		g_usleep(300 * 1000);
		timediff = 0;
		while (timediff < MAX_RENUM_DELAY) {
			if ((err = fx2lafw_open_dev(device_index)) == SR_OK)
				break;
			g_usleep(100 * 1000);
			g_get_current_time(&cur_time);
			timediff = GTV_TO_MSEC(cur_time) - GTV_TO_MSEC(ctx->fw_updated);
		}
		sr_info("fx2lafw: device came back after %d ms", timediff);
	} else {
		err = fx2lafw_open_dev(device_index);
	}

	if (err != SR_OK) {
		sr_err("fx2lafw: unable to open device");
		return SR_ERR;
	}
	ctx = sdi->priv;

	err = libusb_claim_interface(ctx->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_err("fx2lafw: Unable to claim interface: %d", err);
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("fx2lafw: %s: sdi was NULL", __func__);
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
	struct fx2lafw_device *ctx;
	int ret = SR_OK;

	for(l = dev_insts; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("fx2lafw: %s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		if (!(ctx = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("fx2lafw: %s: sdi->priv was NULL, continuing",
			       __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		close_dev(sdi);
		sdi = l->data;
		sr_dev_inst_free(sdi);
	}

	g_slist_free(dev_insts);
	dev_insts = NULL;

	if(usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;

	return ret;
}

static void *hw_dev_info_get(int device_index, int device_info_id)
{
	struct sr_dev_inst *sdi;
	struct fx2lafw_device *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, device_index)))
		return NULL;
	ctx = sdi->priv;

	switch (device_info_id) {
	case SR_DI_INST:
		return sdi;
	case SR_DI_NUM_PROBES:
		return GINT_TO_POINTER(ctx->profile->num_probes);
	case SR_DI_PROBE_NAMES:
		return fx2lafw_probe_names;
	case SR_DI_SAMPLERATES:
		return &fx2lafw_samplerates;
	case SR_DI_TRIGGER_TYPES:
		return TRIGGER_TYPES;
	}

	return NULL;
}

static int hw_dev_status_get(int device_index)
{
	const struct sr_dev_inst *const sdi =
		sr_dev_inst_get(dev_insts, device_index);

	if (!sdi)
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static int *hw_hwcap_get_all(void)
{
	return fx2lafw_capabilities;
}

static int hw_dev_config_set(int dev_index, int hwcap, void *value)
{
	struct sr_dev_inst *sdi;
	struct fx2lafw_device *ctx;
	int ret;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	ctx = sdi->priv;

	if (hwcap == SR_HWCAP_LIMIT_SAMPLES) {
		ctx->limit_samples = *(uint64_t *)value;
		ret = SR_OK;
	} else {
		ret = SR_ERR;
	}

	return ret;
}

static int receive_data(int fd, int revents, void *user_data)
{
	struct timeval tv;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;
	(void)user_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(usb_context, &tv);

	return TRUE;
}

static void receive_transfer(struct libusb_transfer *transfer)
{
	/* TODO: These statics have to move to the ctx struct. */
	static int num_samples = 0;
	static int empty_transfer_count = 0;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct fx2lafw_device *ctx;
	int cur_buflen;
	unsigned char *cur_buf, *new_buf;

	/* hw_dev_acquisition_stop() is telling us to stop. */
	if (transfer == NULL)
		num_samples = -1;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (num_samples == -1) {
		if (transfer)
			libusb_free_transfer(transfer);
		return;
	}

	sr_info("fx2lafw: receive_transfer(): status %d received %d bytes",
		transfer->status, transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */
	cur_buf = transfer->buffer;
	cur_buflen = transfer->actual_length;
	ctx = transfer->user_data;

	/* Fire off a new request. */
	if (!(new_buf = g_try_malloc(4096))) {
		sr_err("fx2lafw: %s: new_buf malloc failed", __func__);
		return; /* TODO: SR_ERR_MALLOC */
	}

	transfer->buffer = new_buf;
	transfer->length = 4096;
	if (libusb_submit_transfer(transfer) != 0) {
		/* TODO: Stop session? */
		/* TODO: Better error message. */
		sr_err("fx2lafw: %s: libusb_submit_transfer error", __func__);
	}

	if (cur_buflen == 0) {
		empty_transfer_count++;
		if (empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			hw_dev_acquisition_stop(-1, ctx->session_data);
		}
		return;
	} else {
		empty_transfer_count = 0;
	}

	/* Send the incoming transfer to the session bus. */
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = cur_buflen;
	logic.unitsize = 1;
	logic.data = cur_buf;
	sr_session_bus(ctx->session_data, &packet);
	g_free(cur_buf);

	num_samples += cur_buflen;
	if (ctx->limit_samples &&
		(unsigned int) num_samples > ctx->limit_samples) {
		hw_dev_acquisition_stop(-1, ctx->session_data);
	}
}

static int hw_dev_acquisition_start(int dev_index, gpointer session_data)
{
	struct sr_dev_inst *sdi;
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct fx2lafw_device *ctx;
	struct libusb_transfer *transfer;
	const struct libusb_pollfd **lupfd;
	int size, i;
	unsigned char *buf;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;
	ctx = sdi->priv;
	ctx->session_data = session_data;

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("fx2lafw: %s: packet malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("fx2lafw: %s: header malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	/* Start with 2K transfer, subsequently increased to 4K. */
	size = 2048;
	for (i = 0; i < NUM_SIMUL_TRANSFERS; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("fx2lafw: %s: buf malloc failed", __func__);
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, ctx->usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, ctx, 40);
		if (libusb_submit_transfer(transfer) != 0) {
			/* TODO: Free them all. */
			libusb_free_transfer(transfer);
			g_free(buf);
			return SR_ERR;
		}
		size = 4096;
	}

	lupfd = libusb_get_pollfds(usb_context);
	for (i = 0; lupfd[i]; i++)
		sr_source_add(lupfd[i]->fd, lupfd[i]->events,
			      40, receive_data, NULL);
	free(lupfd); /* NOT g_free()! */

	packet->type = SR_DF_HEADER;
	packet->payload = header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = 24000000UL;
	header->num_logic_probes = ctx->profile->num_probes;
	sr_session_bus(session_data, packet);
	g_free(header);
	g_free(packet);

	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring device_index. */
static int hw_dev_acquisition_stop(int dev_index, gpointer session_data)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	(void)dev_index;

	packet.type = SR_DF_END;
	sr_session_bus(session_data, &packet);

	receive_transfer(NULL);

	/* TODO: Need to cancel and free any queued up transfers. */

	return SR_OK;
}

SR_PRIV struct sr_dev_plugin fx2lafw_plugin_info = {
	.name = "fx2lafw",
	.longname = "fx2lafw",
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
