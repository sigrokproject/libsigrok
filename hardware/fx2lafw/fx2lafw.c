/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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
#include <string.h>
#include <inttypes.h>
#include <libusb.h>
#include "config.h"
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "fx2lafw.h"
#include "command.h"

static const struct fx2lafw_profile supported_fx2[] = {
	/*
	 * CWAV USBee AX
	 * EE Electronics ESLA201A
	 * ARMFLY AX-Pro
	 */
	{ 0x08a9, 0x0014, "CWAV", "USBee AX", NULL,
		FIRMWARE_DIR "/fx2lafw-cwav-usbeeax.fw",
		0 },
	/*
	 * CWAV USBee DX
	 * XZL-Studio DX
	 */
	{ 0x08a9, 0x0015, "CWAV", "USBee DX", NULL,
		FIRMWARE_DIR "/fx2lafw-cwav-usbeedx.fw",
		DEV_CAPS_16BIT },

	/*
	 * CWAV USBee SX
	 */
	{ 0x08a9, 0x0009, "CWAV", "USBee SX", NULL,
		FIRMWARE_DIR "/fx2lafw-cwav-usbeesx.fw",
		0 },

	/*
	 * Saleae Logic
	 * EE Electronics ESLA100
	 * Robomotic MiniLogic
	 * Robomotic BugLogic 3
	 */
	{ 0x0925, 0x3881, "Saleae", "Logic", NULL,
		FIRMWARE_DIR "/fx2lafw-saleae-logic.fw",
		0 },

	/*
	 * Default Cypress FX2 without EEPROM, e.g.:
	 * Lcsoft Mini Board
	 * Braintechnology USB Interface V2.x
	 */
	{ 0x04B4, 0x8613, "Cypress", "FX2", NULL,
		FIRMWARE_DIR "/fx2lafw-cypress-fx2.fw",
		DEV_CAPS_16BIT },

	/*
	 * Braintechnology USB-LPS
	 */
	{ 0x16d0, 0x0498, "Braintechnology", "USB-LPS", NULL,
		FIRMWARE_DIR "/fx2lafw-braintechnology-usb-lps.fw",
		DEV_CAPS_16BIT },

	{ 0, 0, 0, 0, 0, 0, 0 }
};

static const int hwcaps[] = {
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

static const uint64_t supported_samplerates[] = {
	SR_KHZ(20),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(3),
	SR_MHZ(4),
	SR_MHZ(6),
	SR_MHZ(8),
	SR_MHZ(12),
	SR_MHZ(16),
	SR_MHZ(24),
	0,
};

static const struct sr_samplerates samplerates = {
	0,
	0,
	0,
	supported_samplerates,
};

static libusb_context *usb_context = NULL;

SR_PRIV struct sr_dev_driver fx2lafw_driver_info;
static struct sr_dev_driver *fdi = &fx2lafw_driver_info;
static int hw_dev_close(struct sr_dev_inst *sdi);
static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value);
static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data);

/**
 * Check the USB configuration to determine if this is an fx2lafw device.
 *
 * @return TRUE if the device's configuration profile match fx2lafw
 *         configuration, FALSE otherwise.
 */
static gboolean check_conf_profile(libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	gboolean ret;
	unsigned char strdesc[64];

	hdl = NULL;
	ret = FALSE;
	while (!ret) {
		/* Assume the FW has not been loaded, unless proven wrong. */
		if (libusb_get_device_descriptor(dev, &des) != 0)
			break;

		if (libusb_open(dev, &hdl) != 0)
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strncmp((const char *)strdesc, "sigrok", 6))
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strncmp((const char *)strdesc, "fx2lafw", 7))
			break;

		/* If we made it here, it must be an fx2lafw. */
		ret = TRUE;
	}
	if (hdl)
		libusb_close(hdl);

	return ret;
}

static int fx2lafw_dev_open(struct sr_dev_inst *sdi)
{
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	struct context *ctx;
	struct version_info vi;
	int ret, skip, i;
	uint8_t revid;

	ctx = sdi->priv;

	if (sdi->status == SR_ST_ACTIVE)
		/* already in use */
		return SR_ERR;

	skip = 0;
	const int device_count = libusb_get_device_list(usb_context, &devlist);
	if (device_count < 0) {
		sr_err("fx2lafw: Failed to retrieve device list (%d)",
			device_count);
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("fx2lafw: Failed to get device descriptor: %d.",
			       ret);
			continue;
		}

		if (des.idVendor != ctx->profile->vid
		    || des.idProduct != ctx->profile->pid)
			continue;

		if (sdi->status == SR_ST_INITIALIZING) {
			if (skip != sdi->index) {
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

		if (!(ret = libusb_open(devlist[i], &ctx->usb->devhdl))) {
			if (ctx->usb->address == 0xff)
				/*
				 * first time we touch this device after firmware upload,
				 * so we don't know the address yet.
				 */
				ctx->usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("fx2lafw: Failed to open device: %d.", ret);
			break;
		}

		ret = command_get_fw_version(ctx->usb->devhdl, &vi);
		if (ret != SR_OK) {
			sr_err("fx2lafw: Failed to retrieve "
			       "firmware version information.");
			break;
		}

		ret = command_get_revid_version(ctx->usb->devhdl, &revid);
		if (ret != SR_OK) {
			sr_err("fx2lafw: Failed to retrieve REVID.");
			break;
		}

		/*
		 * Changes in major version mean incompatible/API changes, so
		 * bail out if we encounter an incompatible version.
		 * Different minor versions are OK, they should be compatible.
		 */
		if (vi.major != FX2LAFW_REQUIRED_VERSION_MAJOR) {
			sr_err("fx2lafw: Expected firmware version %d.x, "
			       "got %d.%d.", FX2LAFW_REQUIRED_VERSION_MAJOR,
			       vi.major, vi.minor);
			break;
		}

		sdi->status = SR_ST_ACTIVE;
		sr_info("fx2lafw: Opened device %d on %d.%d "
			"interface %d, firmware %d.%d, REVID %d.",
			sdi->index, ctx->usb->bus, ctx->usb->address,
			USB_INTERFACE, vi.major, vi.minor, revid);

		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	return SR_OK;
}

static int configure_probes(struct context *ctx, GSList *probes)
{
	struct sr_probe *probe;
	GSList *l;
	int probe_bit, stage, i;
	char *tc;

	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		ctx->trigger_mask[i] = 0;
		ctx->trigger_value[i] = 0;
	}

	stage = -1;
	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;

		if (probe->index > 8)
			ctx->sample_wide = TRUE;

		probe_bit = 1 << (probe->index - 1);
		if (!(probe->trigger))
			continue;

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

	if (stage == -1)
		/*
		 * We didn't configure any triggers, make sure acquisition
		 * doesn't wait for any.
		 */
		ctx->trigger_stage = TRIGGER_FIRED;
	else
		ctx->trigger_stage = 0;

	return SR_OK;
}

static struct context *fx2lafw_dev_new(void)
{
	struct context *ctx;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("fx2lafw: %s: ctx malloc failed.", __func__);
		return NULL;
	}

	ctx->trigger_stage = TRIGGER_FIRED;

	return ctx;
}

static int clear_instances(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int ret;

	ret = SR_OK;
	for (l = fdi->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("fx2lafw: %s: sdi was NULL, continuing.",
				   __func__);
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
		hw_dev_close(sdi);
		sdi = l->data;
		sr_dev_inst_free(sdi);
	}

	g_slist_free(fdi->instances);
	fdi->instances = NULL;

	return ret;
}


/*
 * API callbacks
 */

static int hw_init(void)
{

	if (libusb_init(&usb_context) != 0) {
		sr_warn("fx2lafw: Failed to initialize libusb.");
		return SR_ERR;
	}

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	GSList *devices;
	struct libusb_device_descriptor des;
	struct sr_dev_inst *sdi;
	const struct fx2lafw_profile *prof;
	struct context *ctx;
	struct sr_probe *probe;
	libusb_device **devlist;
	int devcnt, num_logic_probes, ret, i, j;

	/* Avoid compiler warnings. */
	(void)options;

	/* This scan always invalidates any previous scans. */
	clear_instances();

	/* Find all fx2lafw compatible devices and upload firmware to them. */
	devices = NULL;
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {

		if ((ret = libusb_get_device_descriptor(
		     devlist[i], &des)) != 0) {
			sr_warn("fx2lafw: Failed to get device descriptor: %d.", ret);
			continue;
		}

		prof = NULL;
		for (j = 0; supported_fx2[j].vid; j++) {
			if (des.idVendor == supported_fx2[j].vid &&
				des.idProduct == supported_fx2[j].pid) {
				prof = &supported_fx2[j];
			}
		}

		/* Skip if the device was not found */
		if (!prof)
			continue;

		devcnt = g_slist_length(fdi->instances);
		sdi = sr_dev_inst_new(devcnt, SR_ST_INITIALIZING,
			prof->vendor, prof->model, prof->model_version);
		if (!sdi)
			return NULL;
		sdi->driver = fdi;

		/* Fill in probelist according to this device's profile. */
		num_logic_probes = prof->dev_caps & DEV_CAPS_16BIT ? 16 : 8;
		for (j = 0; j < num_logic_probes; j++) {
			if (!(probe = sr_probe_new(j, SR_PROBE_LOGIC, TRUE,
					probe_names[j])))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

		ctx = fx2lafw_dev_new();
		ctx->profile = prof;
		sdi->priv = ctx;
		fdi->instances = g_slist_append(fdi->instances, sdi);
		devices = g_slist_append(devices, sdi);

		if (check_conf_profile(devlist[i])) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("fx2lafw: Found an fx2lafw device.");
			sdi->status = SR_ST_INACTIVE;
			ctx->usb = sr_usb_dev_inst_new
			    (libusb_get_bus_number(devlist[i]),
			     libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION,
				prof->firmware) == SR_OK)
				/* Remember when the firmware on this device was updated */
				ctx->fw_updated = g_get_monotonic_time();
			else
				sr_err("fx2lafw: Firmware upload failed for "
				       "device %d.", devcnt);
			ctx->usb = sr_usb_dev_inst_new
				(libusb_get_bus_number(devlist[i]), 0xff, NULL);
		}
	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct context *ctx;
	int ret;
	int64_t timediff_us, timediff_ms;

	ctx = sdi->priv;

	/*
	 * If the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * milliseconds for the FX2 to renumerate.
	 */
	ret = SR_ERR;
	if (ctx->fw_updated > 0) {
		sr_info("fx2lafw: Waiting for device to reset.");
		/* takes at least 300ms for the FX2 to be gone from the USB bus */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((ret = fx2lafw_dev_open(sdi)) == SR_OK)
				break;
			g_usleep(100 * 1000);

			timediff_us = g_get_monotonic_time() - ctx->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("fx2lafw: waited %" PRIi64 " ms", timediff_ms);
		}
		sr_info("fx2lafw: Device came back after %d ms.", timediff_ms);
	} else {
		ret = fx2lafw_dev_open(sdi);
	}

	if (ret != SR_OK) {
		sr_err("fx2lafw: Unable to open device.");
		return SR_ERR;
	}
	ctx = sdi->priv;

	ret = libusb_claim_interface(ctx->usb->devhdl, USB_INTERFACE);
	if (ret != 0) {
		switch(ret) {
		case LIBUSB_ERROR_BUSY:
			sr_err("fx2lafw: Unable to claim USB interface. Another "
				"program or driver has already claimed it.");
			break;

		case LIBUSB_ERROR_NO_DEVICE:
			sr_err("fx2lafw: Device has been disconnected.");
			break;

		default:
			sr_err("fx2lafw: Unable to claim interface: %d.", ret);
			break;
		}

		return SR_ERR;
	}

	if (ctx->cur_samplerate == 0) {
		/* Samplerate hasn't been set; default to the slowest one. */
		if (hw_dev_config_set(sdi, SR_HWCAP_SAMPLERATE,
		    &supported_samplerates[0]) == SR_ERR)
			return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct context *ctx;

	ctx = sdi->priv;
	if (ctx->usb->devhdl == NULL)
		return SR_ERR;

	sr_info("fx2lafw: Closing device %d on %d.%d interface %d.",
		sdi->index, ctx->usb->bus, ctx->usb->address, USB_INTERFACE);
	libusb_release_interface(ctx->usb->devhdl, USB_INTERFACE);
	libusb_close(ctx->usb->devhdl);
	ctx->usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int hw_cleanup(void)
{
	int ret;

	ret = clear_instances();

	if (usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;

	return ret;
}

static int hw_info_get(int info_id, const void **data,
		const struct sr_dev_inst *sdi)
{
	struct context *ctx;

	switch (info_id) {
	case SR_DI_INST:
		*data = sdi;
		break;
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		if (sdi) {
			ctx = sdi->priv;
			*data = GINT_TO_POINTER(
				(ctx->profile->dev_caps & DEV_CAPS_16BIT) ?
				16 : 8);
		} else
			return SR_ERR;
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	case SR_DI_SAMPLERATES:
		*data = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		*data = TRIGGER_TYPES;
		break;
	case SR_DI_CUR_SAMPLERATE:
		if (sdi) {
			ctx = sdi->priv;
			*data = &ctx->cur_samplerate;
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_status_get(int dev_index)
{
	const struct sr_dev_inst *const sdi =
		sr_dev_inst_get(fdi->instances, dev_index);

	if (!sdi)
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	struct context *ctx;
	int ret;

	ctx = sdi->priv;

	if (hwcap == SR_HWCAP_SAMPLERATE) {
		ctx->cur_samplerate = *(const uint64_t *)value;
		ret = SR_OK;
	} else if (hwcap == SR_HWCAP_PROBECONFIG) {
		ret = configure_probes(ctx, (GSList *) value);
	} else if (hwcap == SR_HWCAP_LIMIT_SAMPLES) {
		ctx->limit_samples = *(const uint64_t *)value;
		ret = SR_OK;
	} else {
		ret = SR_ERR;
	}

	return ret;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;
	(void)cb_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(usb_context, &tv);

	return TRUE;
}

static void abort_acquisition(struct context *ctx)
{
	int i;

	ctx->num_samples = -1;

	for (i = ctx->num_transfers - 1; i >= 0; i--) {
		if (ctx->transfers[i])
			libusb_cancel_transfer(ctx->transfers[i]);
	}
}

static void finish_acquisition(struct context *ctx)
{
	struct sr_datafeed_packet packet;
	int i;

	/* Terminate session */
	packet.type = SR_DF_END;
	sr_session_send(ctx->session_dev_id, &packet);

	/* Remove fds from polling */
	const struct libusb_pollfd **const lupfd =
		libusb_get_pollfds(usb_context);
	for (i = 0; lupfd[i]; i++)
		sr_source_remove(lupfd[i]->fd);
	free(lupfd); /* NOT g_free()! */

	ctx->num_transfers = 0;
	g_free(ctx->transfers);
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct context *ctx = transfer->user_data;
	unsigned int i;

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < ctx->num_transfers; i++) {
		if (ctx->transfers[i] == transfer) {
			ctx->transfers[i] = NULL;
			break;
		}
	}

	ctx->submitted_transfers--;
	if (ctx->submitted_transfers == 0)
		finish_acquisition(ctx);

}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
	if (libusb_submit_transfer(transfer) != 0) {
		free_transfer(transfer);
		/* TODO: Stop session? */
		/* TODO: Better error message. */
		sr_err("fx2lafw: %s: libusb_submit_transfer error.", __func__);
	}
}

static void receive_transfer(struct libusb_transfer *transfer)
{
	gboolean packet_has_error = FALSE;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct context *ctx = transfer->user_data;
	int trigger_offset, i;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (ctx->num_samples == -1) {
		free_transfer(transfer);
		return;
	}

	sr_info("fx2lafw: receive_transfer(): status %d received %d bytes.",
		transfer->status, transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */
	uint8_t *const cur_buf = transfer->buffer;
	const int sample_width = ctx->sample_wide ? 2 : 1;
	const int cur_sample_count = transfer->actual_length / sample_width;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		abort_acquisition(ctx);
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (transfer->actual_length == 0 || packet_has_error) {
		ctx->empty_transfer_count++;
		if (ctx->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			abort_acquisition(ctx);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		ctx->empty_transfer_count = 0;
	}

	trigger_offset = 0;
	if (ctx->trigger_stage >= 0) {
		for (i = 0; i < cur_sample_count; i++) {

			const uint16_t cur_sample = ctx->sample_wide ?
				*((const uint16_t*)cur_buf + i) :
				*((const uint8_t*)cur_buf + i);

			if ((cur_sample & ctx->trigger_mask[ctx->trigger_stage]) ==
				ctx->trigger_value[ctx->trigger_stage]) {
				/* Match on this trigger stage. */
				ctx->trigger_buffer[ctx->trigger_stage] = cur_sample;
				ctx->trigger_stage++;

				if (ctx->trigger_stage == NUM_TRIGGER_STAGES ||
					ctx->trigger_mask[ctx->trigger_stage] == 0) {
					/* Match on all trigger stages, we're done. */
					trigger_offset = i + 1;

					/*
					 * TODO: Send pre-trigger buffer to session bus.
					 * Tell the frontend we hit the trigger here.
					 */
					packet.type = SR_DF_TRIGGER;
					packet.payload = NULL;
					sr_session_send(ctx->session_dev_id, &packet);

					/*
					 * Send the samples that triggered it, since we're
					 * skipping past them.
					 */
					packet.type = SR_DF_LOGIC;
					packet.payload = &logic;
					logic.unitsize = sizeof(*ctx->trigger_buffer);
					logic.length = ctx->trigger_stage * logic.unitsize;
					logic.data = ctx->trigger_buffer;
					sr_session_send(ctx->session_dev_id, &packet);

					ctx->trigger_stage = TRIGGER_FIRED;
					break;
				}
			} else if (ctx->trigger_stage > 0) {
				/*
				 * We had a match before, but not in the next sample. However, we may
				 * have a match on this stage in the next bit -- trigger on 0001 will
				 * fail on seeing 00001, so we need to go back to stage 0 -- but at
				 * the next sample from the one that matched originally, which the
				 * counter increment at the end of the loop takes care of.
				 */
				i -= ctx->trigger_stage;
				if (i < -1)
					i = -1; /* Oops, went back past this buffer. */
				/* Reset trigger stage. */
				ctx->trigger_stage = 0;
			}
		}
	}

	if (ctx->trigger_stage == TRIGGER_FIRED) {
		/* Send the incoming transfer to the session bus. */
		const int trigger_offset_bytes = trigger_offset * sample_width;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = transfer->actual_length - trigger_offset_bytes;
		logic.unitsize = sample_width;
		logic.data = cur_buf + trigger_offset_bytes;
		sr_session_send(ctx->session_dev_id, &packet);

		ctx->num_samples += cur_sample_count;
		if (ctx->limit_samples &&
			(unsigned int)ctx->num_samples > ctx->limit_samples) {
			abort_acquisition(ctx);
			free_transfer(transfer);
			return;
		}
	} else {
		/*
		 * TODO: Buffer pre-trigger data in capture
		 * ratio-sized buffer.
		 */
	}

	resubmit_transfer(transfer);
}

static unsigned int to_bytes_per_ms(unsigned int samplerate)
{
	return samplerate / 1000;
}

static size_t get_buffer_size(struct context *ctx)
{
	size_t s;

	/* The buffer should be large enough to hold 10ms of data and a multiple
	 * of 512. */
	s = 10 * to_bytes_per_ms(ctx->cur_samplerate);
	return (s + 511) & ~511;
}

static unsigned int get_number_of_transfers(struct context *ctx)
{
	unsigned int n;

	/* Total buffer size should be able to hold about 500ms of data */
	n = 500 * to_bytes_per_ms(ctx->cur_samplerate) / get_buffer_size(ctx);

	if (n > NUM_SIMUL_TRANSFERS)
		return NUM_SIMUL_TRANSFERS;

	return n;
}

static unsigned int get_timeout(struct context *ctx)
{
	size_t total_size;
	unsigned int timeout;

	total_size = get_buffer_size(ctx) * get_number_of_transfers(ctx);
	timeout = total_size / to_bytes_per_ms(ctx->cur_samplerate);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent */
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_logic meta;
	struct context *ctx;
	struct libusb_transfer *transfer;
	const struct libusb_pollfd **lupfd;
	unsigned int i;
	int ret;
	unsigned char *buf;

	ctx = sdi->priv;
	if (ctx->submitted_transfers != 0)
		return SR_ERR;

	ctx->session_dev_id = cb_data;
	ctx->num_samples = 0;
	ctx->empty_transfer_count = 0;

	const unsigned int timeout = get_timeout(ctx);
	const unsigned int num_transfers = get_number_of_transfers(ctx);
	const size_t size = get_buffer_size(ctx);

	ctx->transfers = g_try_malloc0(sizeof(*ctx->transfers) * num_transfers);
	if (!ctx->transfers)
		return SR_ERR;

	ctx->num_transfers = num_transfers;

	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("fx2lafw: %s: buf malloc failed.", __func__);
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, ctx->usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, ctx, timeout);
		if (libusb_submit_transfer(transfer) != 0) {
			libusb_free_transfer(transfer);
			g_free(buf);
			abort_acquisition(ctx);
			return SR_ERR;
		}
		ctx->transfers[i] = transfer;
		ctx->submitted_transfers++;
	}

	lupfd = libusb_get_pollfds(usb_context);
	for (i = 0; lupfd[i]; i++)
		sr_source_add(lupfd[i]->fd, lupfd[i]->events,
			      timeout, receive_data, NULL);
	free(lupfd); /* NOT g_free()! */

	packet.type = SR_DF_HEADER;
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(cb_data, &packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet.type = SR_DF_META_LOGIC;
	packet.payload = &meta;
	meta.samplerate = ctx->cur_samplerate;
	meta.num_probes = ctx->sample_wide ? 16 : 8;
	sr_session_send(cb_data, &packet);

	if ((ret = command_start_acquisition (ctx->usb->devhdl,
		ctx->cur_samplerate, ctx->sample_wide)) != SR_OK) {
		abort_acquisition(ctx);
		return ret;
	}

	return SR_OK;
}

/* TODO: This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{

	/* Avoid compiler warnings. */
	(void)cb_data;

	abort_acquisition(sdi->priv);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver fx2lafw_driver_info = {
	.name = "fx2lafw",
	.longname = "fx2lafw (generic driver for FX2 based LAs)",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_status_get = hw_dev_status_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.instances = NULL,
};
