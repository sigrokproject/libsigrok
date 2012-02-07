/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <sys/time.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>
#include "config.h"
#include "sigrok.h"
#include "sigrok-internal.h"
#include "saleae-logic.h"

static struct fx2_profile supported_fx2[] = {
	/* Saleae Logic */
	{ 0x0925, 0x3881, 0x0925, 0x3881, "Saleae", "Logic", NULL, 8 },
	/* default Cypress FX2 without EEPROM */
	{ 0x04b4, 0x8613, 0x0925, 0x3881, "Cypress", "FX2", NULL, 16 },
	{ 0, 0, 0, 0, 0, 0, 0, 0 }
};

static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,

	/* These are really implemented in the driver, not the hardware. */
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
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
	NULL,
};

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

/* List of struct sr_device_instance, maintained by opendev()/closedev(). */
static GSList *device_instances = NULL;
static libusb_context *usb_context = NULL;

static int new_saleae_logic_firmware = 0;

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
		if (intf_dsc->bNumEndpoints == 4) {
			/* The new Saleae Logic firmware has 4 endpoints. */
			new_saleae_logic_firmware = 1;
		} else if (intf_dsc->bNumEndpoints == 2) {
			/* The old Saleae Logic firmware has 2 endpoints. */
			new_saleae_logic_firmware = 0;
		} else {
			/* Other number of endpoints -> not a Saleae Logic. */
			break;
		}

		if ((intf_dsc->endpoint[0].bEndpointAddress & 0x8f) !=
		    (1 | LIBUSB_ENDPOINT_OUT))
			/* First endpoint should be 1 (outbound). */
			break;

		if ((intf_dsc->endpoint[1].bEndpointAddress & 0x8f) !=
		    (2 | LIBUSB_ENDPOINT_IN))
			/* First endpoint should be 2 (inbound). */
			break;

		/* TODO: The new firmware has 4 endpoints... */

		/* If we made it here, it must be a Saleae Logic. */
		ret = 1;
	}

	if (conf_dsc)
		libusb_free_config_descriptor(conf_dsc);

	return ret;
}

static int sl_open_device(int device_index)
{
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	struct sr_device_instance *sdi;
	struct fx2_device *fx2;
	int err, skip, i;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;
	fx2 = sdi->priv;

	if (sdi->status == SR_ST_ACTIVE)
		/* already in use */
		return SR_ERR;

	skip = 0;
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((err = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_warn("failed to get device descriptor: %d", err);
			continue;
		}

		if (des.idVendor != fx2->profile->fw_vid || des.idProduct != fx2->profile->fw_pid)
			continue;

		if (sdi->status == SR_ST_INITIALIZING) {
			if (skip != device_index) {
				/* Skip devices of this type that aren't the one we want. */
				skip += 1;
				continue;
			}
		} else if (sdi->status == SR_ST_INACTIVE) {
			/*
			 * This device is fully enumerated, so we need to find this
			 * device by vendor, product, bus and address.
			 */
			if (libusb_get_bus_number(devlist[i]) != fx2->usb->bus
				|| libusb_get_device_address(devlist[i]) != fx2->usb->address)
				/* this is not the one */
				continue;
		}

		if (!(err = libusb_open(devlist[i], &fx2->usb->devhdl))) {
			if (fx2->usb->address == 0xff)
				/*
				 * first time we touch this device after firmware upload,
				 * so we don't know the address yet.
				 */
				fx2->usb->address = libusb_get_device_address(devlist[i]);

			sdi->status = SR_ST_ACTIVE;
			sr_info("saleae: opened device %d on %d.%d interface %d",
				  sdi->index, fx2->usb->bus,
				  fx2->usb->address, USB_INTERFACE);
		} else {
			sr_warn("failed to open device: %d", err);
		}

		/* if we made it here, we handled the device one way or another */
		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	return SR_OK;
}

static void close_device(struct sr_device_instance *sdi)
{
	struct fx2_device *fx2;

	fx2 = sdi->priv;

	if (fx2->usb->devhdl == NULL)
		return;

	sr_info("saleae: closing device %d on %d.%d interface %d", sdi->index,
		fx2->usb->bus, fx2->usb->address, USB_INTERFACE);
	libusb_release_interface(fx2->usb->devhdl, USB_INTERFACE);
	libusb_close(fx2->usb->devhdl);
	fx2->usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;
}

static int configure_probes(struct fx2_device *fx2, GSList *probes)
{
	struct sr_probe *probe;
	GSList *l;
	int probe_bit, stage, i;
	char *tc;

	fx2->probe_mask = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		fx2->trigger_mask[i] = 0;
		fx2->trigger_value[i] = 0;
	}

	stage = -1;
	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;
		probe_bit = 1 << (probe->index - 1);
		fx2->probe_mask |= probe_bit;
		if (!(probe->trigger))
			continue;

		stage = 0;
		for (tc = probe->trigger; *tc; tc++) {
			fx2->trigger_mask[stage] |= probe_bit;
			if (*tc == '1')
				fx2->trigger_value[stage] |= probe_bit;
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
		fx2->trigger_stage = TRIGGER_FIRED;
	else
		fx2->trigger_stage = 0;

	return SR_OK;
}

static struct fx2_device *fx2_device_new(void)
{
	struct fx2_device *fx2;

	if (!(fx2 = g_try_malloc0(sizeof(struct fx2_device)))) {
		sr_err("saleae: %s: fx2 malloc failed", __func__);
		return NULL;
	}
	fx2->trigger_stage = TRIGGER_FIRED;
	fx2->usb = NULL;

	return fx2;
}


/*
 * API callbacks
 */

static int hw_init(const char *deviceinfo)
{
	struct sr_device_instance *sdi;
	struct libusb_device_descriptor des;
	struct fx2_profile *fx2_prof;
	struct fx2_device *fx2;
	libusb_device **devlist;
	int err, devcnt, i, j;

	/* Avoid compiler warnings. */
	(void)deviceinfo;

	if (libusb_init(&usb_context) != 0) {
		sr_warn("Failed to initialize USB.");
		return 0;
	}

	/* Find all Saleae Logic devices and upload firmware to all of them. */
	devcnt = 0;
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		fx2_prof = NULL;
		err = libusb_get_device_descriptor(devlist[i], &des);
		if (err != 0) {
			sr_warn("failed to get device descriptor: %d", err);
			continue;
		}

		for (j = 0; supported_fx2[j].orig_vid; j++) {
			if (des.idVendor == supported_fx2[j].orig_vid
				&& des.idProduct == supported_fx2[j].orig_pid) {
				fx2_prof = &supported_fx2[j];
				break;
			}
		}
		if (!fx2_prof)
			/* not a supported VID/PID */
			continue;

		sdi = sr_device_instance_new(devcnt, SR_ST_INITIALIZING,
			fx2_prof->vendor, fx2_prof->model, fx2_prof->model_version);
		if (!sdi)
			return 0;
		fx2 = fx2_device_new();
		fx2->profile = fx2_prof;
		sdi->priv = fx2;
		device_instances = g_slist_append(device_instances, sdi);

		if (check_conf_profile(devlist[i])) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("Found a Saleae Logic with %s firmware.",
			       new_saleae_logic_firmware ? "new" : "old");
			sdi->status = SR_ST_INACTIVE;
			fx2->usb = sr_usb_device_instance_new
			    (libusb_get_bus_number(devlist[i]),
			     libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION, FIRMWARE) == SR_OK)
				/* Remember when the firmware on this device was updated */
				g_get_current_time(&fx2->fw_updated);
			else
				sr_warn("firmware upload failed for device %d", devcnt);
			fx2->usb = sr_usb_device_instance_new
				(libusb_get_bus_number(devlist[i]), 0xff, NULL);
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
	struct fx2_device *fx2;
	int timediff, err;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;
	fx2 = sdi->priv;

	/*
	 * if the firmware was recently uploaded, wait up to MAX_RENUM_DELAY ms
	 * for the FX2 to renumerate
	 */
	err = 0;
	if (GTV_TO_MSEC(fx2->fw_updated) > 0) {
		sr_info("saleae: waiting for device to reset");
		/* takes at least 300ms for the FX2 to be gone from the USB bus */
		g_usleep(300*1000);
		timediff = 0;
		while (timediff < MAX_RENUM_DELAY) {
			if ((err = sl_open_device(device_index)) == SR_OK)
				break;
			g_usleep(100*1000);
			g_get_current_time(&cur_time);
			timediff = GTV_TO_MSEC(cur_time) - GTV_TO_MSEC(fx2->fw_updated);
		}
		sr_info("saleae: device came back after %d ms", timediff);
	} else {
		err = sl_open_device(device_index);
	}

	if (err != SR_OK) {
		sr_warn("unable to open device");
		return SR_ERR;
	}
	fx2 = sdi->priv;

	err = libusb_claim_interface(fx2->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_warn("Unable to claim interface: %d", err);
		return SR_ERR;
	}

	if (fx2->cur_samplerate == 0) {
		/* Samplerate hasn't been set; default to the slowest one. */
		if (hw_set_configuration(device_index, SR_HWCAP_SAMPLERATE,
		    &supported_samplerates[0]) == SR_ERR)
			return SR_ERR;
	}

	return SR_OK;
}

static int hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index))) {
		sr_err("logic: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	/* TODO */
	close_device(sdi);

	return SR_OK;
}

static void hw_cleanup(void)
{
	GSList *l;
	struct sr_device_instance *sdi;
	struct fx2_device *fx2;

	/* Properly close and free all devices. */
	for (l = device_instances; l; l = l->next) {
		sdi = l->data;
		fx2 = sdi->priv;
		close_device(sdi);
		sr_usb_device_instance_free(fx2->usb);
		sr_device_instance_free(sdi);
	}

	g_slist_free(device_instances);
	device_instances = NULL;

	if (usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sr_device_instance *sdi;
	struct fx2_device *fx2;
	void *info = NULL;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return NULL;
	fx2 = sdi->priv;

	switch (device_info_id) {
	case SR_DI_INSTANCE:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(fx2->profile->num_probes);
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
		info = &fx2->cur_samplerate;
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

static uint8_t new_firmware_divider_value(uint64_t samplerate)
{
	switch (samplerate) {
	case SR_MHZ(24):
		return 0xe0;
		break;
	case SR_MHZ(16):
		return 0xd5;
		break;
	case SR_MHZ(12):
		return 0xe2;
		break;
	case SR_MHZ(8):
		return 0xd4;
		break;
	case SR_MHZ(4):
		return 0xda;
		break;
	case SR_MHZ(2):
		return 0xe6;
		break;
	case SR_MHZ(1):
		return 0x8e;
		break;
	case SR_KHZ(500):
		return 0xfe;
		break;
	case SR_KHZ(250):
		return 0x9e;
		break;
	case SR_KHZ(200):
		return 0x4e;
		break;
	}

	/* Shouldn't happen. */
	sr_err("saleae: %s: Invalid samplerate %" PRIu64 "",
	       __func__, samplerate);
	return 0;
}

static int set_configuration_samplerate(struct sr_device_instance *sdi,
					uint64_t samplerate)
{
	struct fx2_device *fx2;
	uint8_t divider;
	int ret, result, i;
	unsigned char buf[2];

	fx2 = sdi->priv;
	for (i = 0; supported_samplerates[i]; i++) {
		if (supported_samplerates[i] == samplerate)
			break;
	}
	if (supported_samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	if (new_saleae_logic_firmware)
		divider = new_firmware_divider_value(samplerate);
	else
		divider = (uint8_t) (48 / (samplerate / 1000000.0)) - 1;

	sr_info("saleae: setting samplerate to %" PRIu64 " Hz (divider %d)",
		samplerate, divider);

	buf[0] = (new_saleae_logic_firmware) ? 0xd5 : 0x01;
	buf[1] = divider;
	ret = libusb_bulk_transfer(fx2->usb->devhdl, 1 | LIBUSB_ENDPOINT_OUT,
				   buf, 2, &result, 500);
	if (ret != 0) {
		sr_warn("failed to set samplerate: %d", ret);
		return SR_ERR;
	}
	fx2->cur_samplerate = samplerate;

	return SR_OK;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	struct sr_device_instance *sdi;
	struct fx2_device *fx2;
	int ret;
	uint64_t *tmp_u64;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;
	fx2 = sdi->priv;

	if (capability == SR_HWCAP_SAMPLERATE) {
		tmp_u64 = value;
		ret = set_configuration_samplerate(sdi, *tmp_u64);
	} else if (capability == SR_HWCAP_PROBECONFIG) {
		ret = configure_probes(fx2, (GSList *) value);
	} else if (capability == SR_HWCAP_LIMIT_SAMPLES) {
		tmp_u64 = value;
		fx2->limit_samples = *tmp_u64;
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
	/* TODO: these statics have to move to fx2_device struct */
	static int num_samples = 0;
	static int empty_transfer_count = 0;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct fx2_device *fx2;
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
	fx2 = transfer->user_data;

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
			hw_stop_acquisition(-1, fx2->session_data);
		}
		return;
	} else {
		empty_transfer_count = 0;
	}

	trigger_offset = 0;
	if (fx2->trigger_stage >= 0) {
		for (i = 0; i < cur_buflen; i++) {

			if ((cur_buf[i] & fx2->trigger_mask[fx2->trigger_stage]) == fx2->trigger_value[fx2->trigger_stage]) {
				/* Match on this trigger stage. */
				fx2->trigger_buffer[fx2->trigger_stage] = cur_buf[i];
				fx2->trigger_stage++;

				if (fx2->trigger_stage == NUM_TRIGGER_STAGES || fx2->trigger_mask[fx2->trigger_stage] == 0) {
					/* Match on all trigger stages, we're done. */
					trigger_offset = i + 1;

					/*
					 * TODO: Send pre-trigger buffer to session bus.
					 * Tell the frontend we hit the trigger here.
					 */
					packet.type = SR_DF_TRIGGER;
					packet.payload = NULL;
					sr_session_bus(fx2->session_data, &packet);

					/*
					 * Send the samples that triggered it, since we're
					 * skipping past them.
					 */
					packet.type = SR_DF_LOGIC;
					packet.payload = &logic;
					logic.length = fx2->trigger_stage;
					logic.unitsize = 1;
					logic.data = fx2->trigger_buffer;
					sr_session_bus(fx2->session_data, &packet);

					fx2->trigger_stage = TRIGGER_FIRED;
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
			if (fx2->trigger_stage > 0) {
				i -= fx2->trigger_stage;
				if (i < -1)
					i = -1; /* Oops, went back past this buffer. */
				/* Reset trigger stage. */
				fx2->trigger_stage = 0;
			}
		}
	}

	if (fx2->trigger_stage == TRIGGER_FIRED) {
		/* Send the incoming transfer to the session bus. */
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = cur_buflen - trigger_offset;
		logic.unitsize = 1;
		logic.data = cur_buf + trigger_offset;
		sr_session_bus(fx2->session_data, &packet);
		g_free(cur_buf);

		num_samples += cur_buflen;
		if (fx2->limit_samples && (unsigned int) num_samples > fx2->limit_samples) {
			hw_stop_acquisition(-1, fx2->session_data);
		}
	} else {
		/*
		 * TODO: Buffer pre-trigger data in capture
		 * ratio-sized buffer.
		 */
	}
}

static int hw_start_acquisition(int device_index, gpointer session_data)
{
	struct sr_device_instance *sdi;
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct fx2_device *fx2;
	struct libusb_transfer *transfer;
	const struct libusb_pollfd **lupfd;
	int size, i;
	unsigned char *buf;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;
	fx2 = sdi->priv;
	fx2->session_data = session_data;

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
		libusb_fill_bulk_transfer(transfer, fx2->usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, fx2, 40);
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
	packet->payload = header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	header->samplerate = fx2->cur_samplerate;
	header->num_logic_probes = fx2->profile->num_probes;
	sr_session_bus(session_data, packet);
	g_free(header);
	g_free(packet);

	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring device_index. */
static void hw_stop_acquisition(int device_index, gpointer session_data)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	(void)device_index;

	packet.type = SR_DF_END;
	sr_session_bus(session_data, &packet);

	receive_transfer(NULL);

	/* TODO: Need to cancel and free any queued up transfers. */
}

SR_PRIV struct sr_device_plugin saleae_logic_plugin_info = {
	.name = "saleae-logic",
	.longname = "Saleae Logic",
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
