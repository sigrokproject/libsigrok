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
#include <sigrok.h>
#include <sigrok-internal.h>

#define USB_VENDOR	            0x0925
#define USB_PRODUCT			0x3881
#define USB_VENDOR_NAME        "Saleae"
#define USB_MODEL_NAME         "Logic"
#define USB_MODEL_VERSION      ""

#define USB_INTERFACE          0
#define USB_CONFIGURATION      1
#define NUM_PROBES             8
#define NUM_TRIGGER_STAGES     4
#define TRIGGER_TYPES          "01"
#define FIRMWARE               FIRMWARE_DIR "/saleae-logic.fw"

/* delay in ms */
#define FIRMWARE_RENUM_DELAY   2000
#define NUM_SIMUL_TRANSFERS    10
#define MAX_EMPTY_TRANSFERS    (NUM_SIMUL_TRANSFERS * 2)

/* Software trigger implementation: positive values indicate trigger stage. */
#define TRIGGER_FIRED          -1

/* There is only one model Saleae Logic, and this is what it supports: */
static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,

	/* These are really implemented in the driver, not the hardware. */
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0,
};

/* List of struct sr_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;

/*
 * Since we can't keep track of a Saleae Logic device after upgrading the
 * firmware -- it re-enumerates into a different device address after the
 * upgrade -- this is like a global lock. No device will open until a proper
 * delay after the last device was upgraded.
 */
static GTimeVal firmware_updated = { 0, 0 };

static libusb_context *usb_context = NULL;

static uint64_t supported_samplerates[] = {
	SR_KHZ(200),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(8),
	SR_MHZ(12),
	SR_MHZ(16),
	SR_MHZ(24),
	0,
};

static struct sr_samplerates samplerates = {
	SR_KHZ(200),
	SR_MHZ(24),
	SR_HZ(0),
	supported_samplerates,
};

/* TODO: All of these should go in a device-specific struct. */
static uint64_t cur_samplerate = 0;
static uint64_t limit_samples = 0;
static uint8_t probe_mask = 0;
static uint8_t trigger_mask[NUM_TRIGGER_STAGES] = { 0 };
static uint8_t trigger_value[NUM_TRIGGER_STAGES] = { 0 };
static uint8_t trigger_buffer[NUM_TRIGGER_STAGES] = { 0 };

static int trigger_stage = TRIGGER_FIRED;

static int hw_set_configuration(int device_index, int capability, void *value);
static void hw_stop_acquisition(int device_index, gpointer session_device_id);

/**
 * Check the USB configuration to determine if this is a Saleae Logic.
 *
 * @return 1 if the device's configuration profile match the Logic firmware's
 *         configuration, 0 otherwise.
 */
static int check_conf_profile(libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *conf_dsc = NULL;
	const struct libusb_interface_descriptor *intf_dsc;
	int ret = -1;

	while (ret == -1) {
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
		if (intf_dsc->bNumEndpoints != 2)
			/* Need 2 endpoints. */
			break;

		if ((intf_dsc->endpoint[0].bEndpointAddress & 0x8f) !=
		    (1 | LIBUSB_ENDPOINT_OUT))
			/* First endpoint should be 1 (outbound). */
			break;

		if ((intf_dsc->endpoint[1].bEndpointAddress & 0x8f) !=
		    (2 | LIBUSB_ENDPOINT_IN))
			/* First endpoint should be 2 (inbound). */
			break;

		/* If we made it here, it must be a Saleae Logic. */
		ret = 1;
	}

	if (conf_dsc)
		libusb_free_config_descriptor(conf_dsc);

	return ret;
}

static struct sr_device_instance *sl_open_device(int device_index)
{
	struct sr_device_instance *sdi;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	int err, skip, i;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return NULL;

	libusb_get_device_list(usb_context, &devlist);
	if (sdi->status == SR_ST_INITIALIZING) {
		/*
		 * This device was renumerating last time we touched it.
		 * opendev() guarantees we've waited long enough for it to
		 * have booted properly, so now we need to find it on
		 * the bus and record its new address.
		 */
		skip = 0;
		for (i = 0; devlist[i]; i++) {
			/* TODO: Error handling. */
			err = opendev2(device_index, &sdi, devlist[i], &des,
				       &skip, USB_VENDOR, USB_PRODUCT,
				       USB_INTERFACE);
		}
	} else if (sdi->status == SR_ST_INACTIVE) {
		/*
		 * This device is fully enumerated, so we need to find this
		 * device by vendor, product, bus and address.
		 */
		libusb_get_device_list(usb_context, &devlist);
		for (i = 0; devlist[i]; i++) {
			/* TODO: Error handling. */
			err = opendev3(&sdi, devlist[i], &des, USB_VENDOR,
				       USB_PRODUCT, USB_INTERFACE);
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

static int upload_firmware(libusb_device *dev)
{
	int ret;

	ret = ezusb_upload_firmware(dev, USB_CONFIGURATION, FIRMWARE);
	if (ret != 0)
		return 1;

	/* Remember when the last firmware update was done. */
	g_get_current_time(&firmware_updated);

	return 0;
}

static void close_device(struct sr_device_instance *sdi)
{
	if (sdi->usb->devhdl == NULL)
		return;

	sr_info("saleae: closing device %d on %d.%d interface %d", sdi->index,
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
		if (!(probe->trigger))
			continue;

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

	if (stage == -1)
		/*
		 * We didn't configure any triggers, make sure acquisition
		 * doesn't wait for any.
		 */
		trigger_stage = TRIGGER_FIRED;
	else
		trigger_stage = 0;

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
	deviceinfo = deviceinfo;

	if (libusb_init(&usb_context) != 0) {
		sr_warn("Failed to initialize USB.");
		return 0;
	}

	/* Find all Saleae Logic devices and upload firmware to all of them. */
	devcnt = 0;
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		err = libusb_get_device_descriptor(devlist[i], &des);
		if (err != 0) {
			sr_warn("failed to get device descriptor: %d", err);
			continue;
		}

		if (des.idVendor != USB_VENDOR || des.idProduct != USB_PRODUCT)
			continue; /* Not a Saleae Logic... */

		sdi = sr_device_instance_new(devcnt, SR_ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);
		if (!sdi)
			return 0;
		device_instances = g_slist_append(device_instances, sdi);

		if (check_conf_profile(devlist[i]) == 0) {
			/*
			 * Continue on the off chance that the device is in a
			 * working state. TODO: Could maybe try a USB reset,
			 * or uploading the firmware again.
			 */
			if (upload_firmware(devlist[i]) > 0)
				sr_warn("firmware upload failed for device %d",
					devcnt);

			sdi->usb = sr_usb_device_instance_new
				(libusb_get_bus_number(devlist[i]), 0, NULL);
		} else {
			/* Already has the firmware, so fix the new address. */
			sdi->usb = sr_usb_device_instance_new
			    (libusb_get_bus_number(devlist[i]),
			     libusb_get_device_address(devlist[i]), NULL);
		}
		devcnt++;
	}
	libusb_free_device_list(devlist, 1);

	return devcnt;
}

static int hw_opendev(int device_index)
{
	GTimeVal cur_time;
	struct sr_device_instance *sdi;
	int timediff, err;
	unsigned int cur, upd;

	if (firmware_updated.tv_sec > 0) {
		/* Firmware was recently uploaded. */
		g_get_current_time(&cur_time);
		cur = cur_time.tv_sec * 1000 + cur_time.tv_usec / 1000;
		upd = firmware_updated.tv_sec * 1000 +
		      firmware_updated.tv_usec / 1000;
		timediff = cur - upd;
		if (timediff < FIRMWARE_RENUM_DELAY) {
			timediff = FIRMWARE_RENUM_DELAY - timediff;
			sr_info("saleae: waiting %d ms for device to reset",
				timediff);
			g_usleep(timediff * 1000);
			firmware_updated.tv_sec = 0;
		}
	}

	if (!(sdi = sl_open_device(device_index))) {
		sr_warn("unable to open device");
		return SR_ERR;
	}

	err = libusb_claim_interface(sdi->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_warn("Unable to claim interface: %d", err);
		return SR_ERR;
	}

	if (cur_samplerate == 0) {
		/* Samplerate hasn't been set; default to the slowest one. */
		if (hw_set_configuration(device_index, SR_HWCAP_SAMPLERATE,
		    &supported_samplerates[0]) == SR_ERR)
			return SR_ERR;
	}

	return SR_OK;
}

static void hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;

	if ((sdi = sr_get_device_instance(device_instances, device_index)))
		close_device(sdi);
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
		info = GINT_TO_POINTER(NUM_PROBES);
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

static int set_configuration_samplerate(struct sr_device_instance *sdi,
					uint64_t samplerate)
{
	uint8_t divider;
	int ret, result, i;
	unsigned char buf[2];

	for (i = 0; supported_samplerates[i]; i++) {
		if (supported_samplerates[i] == samplerate)
			break;
	}
	if (supported_samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	divider = (uint8_t) (48 / (samplerate / 1000000.0)) - 1;

	sr_info("saleae: setting samplerate to %" PRIu64 " Hz (divider %d)",
		samplerate, divider);
	buf[0] = 0x01;
	buf[1] = divider;
	ret = libusb_bulk_transfer(sdi->usb->devhdl, 1 | LIBUSB_ENDPOINT_OUT,
				   buf, 2, &result, 500);
	if (ret != 0) {
		sr_warn("failed to set samplerate: %d", ret);
		return SR_ERR;
	}
	cur_samplerate = samplerate;

	return SR_OK;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	struct sr_device_instance *sdi;
	int ret;
	uint64_t *tmp_u64;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	if (capability == SR_HWCAP_SAMPLERATE) {
		tmp_u64 = value;
		ret = set_configuration_samplerate(sdi, *tmp_u64);
	} else if (capability == SR_HWCAP_PROBECONFIG) {
		ret = configure_probes((GSList *) value);
	} else if (capability == SR_HWCAP_LIMIT_SAMPLES) {
		tmp_u64 = value;
		limit_samples = *tmp_u64;
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
	fd = fd;
	revents = revents;
	user_data = user_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(usb_context, &tv);

	return TRUE;
}

void receive_transfer(struct libusb_transfer *transfer)
{
	static int num_samples = 0;
	static int empty_transfer_count = 0;
	struct sr_datafeed_packet packet;
	void *user_data;
	int cur_buflen, trigger_offset, i;
	unsigned char *cur_buf, *new_buf;

	/* hw_stop_acquisition() is telling us to stop. */
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

	sr_info("saleae: receive_transfer(): status %d received %d bytes",
		transfer->status, transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */
	cur_buf = transfer->buffer;
	cur_buflen = transfer->actual_length;
	user_data = transfer->user_data;

	/* Fire off a new request. */
	if (!(new_buf = g_try_malloc(4096))) {
		sr_err("saleae: %s: new_buf malloc failed", __func__);
		// return SR_ERR_MALLOC;
		return; /* FIXME */
	}

	transfer->buffer = new_buf;
	transfer->length = 4096;
	if (libusb_submit_transfer(transfer) != 0) {
		/* TODO: Stop session? */
		sr_warn("eek");
	}

	if (cur_buflen == 0) {
		empty_transfer_count++;
		if (empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			hw_stop_acquisition(-1, user_data);
		}
		return;
	} else {
		empty_transfer_count = 0;
	}

	trigger_offset = 0;
	if (trigger_stage >= 0) {
		for (i = 0; i < cur_buflen; i++) {

			if ((cur_buf[i] & trigger_mask[trigger_stage]) == trigger_value[trigger_stage]) {
				/* Match on this trigger stage. */
				trigger_buffer[trigger_stage] = cur_buf[i];
				trigger_stage++;
				if (trigger_stage == NUM_TRIGGER_STAGES || trigger_mask[trigger_stage] == 0) {
					/* Match on all trigger stages, we're done. */
					trigger_offset = i + 1;

					/*
					 * TODO: Send pre-trigger buffer to session bus.
					 * Tell the frontend we hit the trigger here.
					 */
					packet.type = SR_DF_TRIGGER;
					packet.length = 0;
					sr_session_bus(user_data, &packet);

					/*
					 * Send the samples that triggered it, since we're
					 * skipping past them.
					 */
					packet.type = SR_DF_LOGIC;
					packet.length = trigger_stage;
					packet.unitsize = 1;
					packet.payload = trigger_buffer;
					sr_session_bus(user_data, &packet);

					trigger_stage = TRIGGER_FIRED;
					break;
				}
				return;
			}

			/*
			 * We had a match before, but not in the next sample. However, we may
			 * have a match on this stage in the next bit -- trigger on 0001 will
			 * fail on seeing 00001, so we need to go back to stage 0 -- but at
			 * the next sample from the one that matched originally, which the
			 * counter increment at the end of the loop takes care of.
			 */
			if (trigger_stage > 0) {
				i -= trigger_stage;
				if (i < -1)
					i = -1; /* Oops, went back past this buffer. */
				/* Reset trigger stage. */
				trigger_stage = 0;
			}


		}
	}

	if (trigger_stage == TRIGGER_FIRED) {
		/* Send the incoming transfer to the session bus. */
		packet.type = SR_DF_LOGIC;
		packet.length = cur_buflen - trigger_offset;
		packet.unitsize = 1;
		packet.payload = cur_buf + trigger_offset;
		sr_session_bus(user_data, &packet);
		g_free(cur_buf);

		num_samples += cur_buflen;
		if (limit_samples && (unsigned int) num_samples > limit_samples) {
			hw_stop_acquisition(-1, user_data);
		}
	} else {
		/*
		 * TODO: Buffer pre-trigger data in capture
		 * ratio-sized buffer.
		 */
	}
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_device_instance *sdi;
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct libusb_transfer *transfer;
	const struct libusb_pollfd **lupfd;
	int size, i;
	unsigned char *buf;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("saleae: %s: packet malloc failed", __func__);
		return SR_ERR_MALLOC;
	}
	
	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("saleae: %s: header malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	/* Start with 2K transfer, subsequently increased to 4K. */
	size = 2048;
	for (i = 0; i < NUM_SIMUL_TRANSFERS; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("saleae: %s: buf malloc failed", __func__);
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, sdi->usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, session_device_id, 40);
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
		sr_source_add(lupfd[i]->fd, lupfd[i]->events, 40, receive_data,
			      NULL);
	free(lupfd);

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

/* This stops acquisition on ALL devices, ignoring device_index. */
static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	device_index = device_index;

	packet.type = SR_DF_END;
	sr_session_bus(session_device_id, &packet);

	receive_transfer(NULL);

	/* TODO: Need to cancel and free any queued up transfers. */
}

struct sr_device_plugin saleae_logic_plugin_info = {
	.name = "saleae-logic",
	.longname = "Saleae Logic",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.open = hw_opendev,
	.close = hw_closedev,
	.get_device_info = hw_get_device_info,
	.get_status = hw_get_status,
	.get_capabilities = hw_get_capabilities,
	.set_configuration = hw_set_configuration,
	.start_acquisition = hw_start_acquisition,
	.stop_acquisition = hw_stop_acquisition,
};
