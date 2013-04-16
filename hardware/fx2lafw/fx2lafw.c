/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_TRIGGER_TYPE,
	SR_CONF_SAMPLERATE,

	/* These are really implemented in the driver, not the hardware. */
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
};

static const char *probe_names[] = {
	"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",
	"8",  "9", "10", "11", "12", "13", "14", "15",
	NULL,
};

static const uint64_t samplerates[] = {
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
};

SR_PRIV struct sr_dev_driver fx2lafw_driver_info;
static struct sr_dev_driver *di = &fx2lafw_driver_info;
static int hw_dev_close(struct sr_dev_inst *sdi);
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

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
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct version_info vi;
	int ret, skip, i, device_count;
	uint8_t revid;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	if (sdi->status == SR_ST_ACTIVE)
		/* Device is already in use. */
		return SR_ERR;

	skip = 0;
	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != devc->profile->vid
		    || des.idProduct != devc->profile->pid)
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
			if (libusb_get_bus_number(devlist[i]) != usb->bus
				|| libusb_get_device_address(devlist[i]) != usb->address)
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			break;
		}

		ret = command_get_fw_version(usb->devhdl, &vi);
		if (ret != SR_OK) {
			sr_err("Failed to get firmware version.");
			break;
		}

		ret = command_get_revid_version(usb->devhdl, &revid);
		if (ret != SR_OK) {
			sr_err("Failed to get REVID.");
			break;
		}

		/*
		 * Changes in major version mean incompatible/API changes, so
		 * bail out if we encounter an incompatible version.
		 * Different minor versions are OK, they should be compatible.
		 */
		if (vi.major != FX2LAFW_REQUIRED_VERSION_MAJOR) {
			sr_err("Expected firmware version %d.x, "
			       "got %d.%d.", FX2LAFW_REQUIRED_VERSION_MAJOR,
			       vi.major, vi.minor);
			break;
		}

		sdi->status = SR_ST_ACTIVE;
		sr_info("Opened device %d on %d.%d, "
			"interface %d, firmware %d.%d.",
			sdi->index, usb->bus, usb->address,
			USB_INTERFACE, vi.major, vi.minor);

		sr_info("Detected REVID=%d, it's a Cypress CY7C68013%s.",
			revid, (revid != 1) ? " (FX2)" : "A (FX2LP)");

		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	return SR_OK;
}

static int configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_probe *probe;
	GSList *l;
	int probe_bit, stage, i;
	char *tc;

	devc = sdi->priv;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		devc->trigger_mask[i] = 0;
		devc->trigger_value[i] = 0;
	}

	stage = -1;
	for (l = sdi->probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;

		if (probe->index > 7)
			devc->sample_wide = TRUE;

		probe_bit = 1 << (probe->index);
		if (!(probe->trigger))
			continue;

		stage = 0;
		for (tc = probe->trigger; *tc; tc++) {
			devc->trigger_mask[stage] |= probe_bit;
			if (*tc == '1')
				devc->trigger_value[stage] |= probe_bit;
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
		devc->trigger_stage = TRIGGER_FIRED;
	else
		devc->trigger_stage = 0;

	return SR_OK;
}

static struct dev_context *fx2lafw_dev_new(void)
{
	struct dev_context *devc;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		return NULL;
	}

	devc->trigger_stage = TRIGGER_FIRED;

	return devc;
}

static int clear_instances(void)
{
	return std_dev_clear(di, NULL);
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_probe *probe;
	struct sr_config *src;
	const struct fx2lafw_profile *prof;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int devcnt, num_logic_probes, ret, i, j;
	const char *conn;

	(void)options;

	drvc = di->priv;

	/* This scan always invalidates any previous scans. */
	clear_instances();

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all fx2lafw compatible devices and upload firmware to them. */
	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
					&& usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		if ((ret = libusb_get_device_descriptor( devlist[i], &des)) != 0) {
			sr_warn("Failed to get device descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		prof = NULL;
		for (j = 0; supported_fx2[j].vid; j++) {
			if (des.idVendor == supported_fx2[j].vid &&
				des.idProduct == supported_fx2[j].pid) {
				prof = &supported_fx2[j];
			}
		}

		/* Skip if the device was not found. */
		if (!prof)
			continue;

		devcnt = g_slist_length(drvc->instances);
		sdi = sr_dev_inst_new(devcnt, SR_ST_INITIALIZING,
			prof->vendor, prof->model, prof->model_version);
		if (!sdi)
			return NULL;
		sdi->driver = di;

		/* Fill in probelist according to this device's profile. */
		num_logic_probes = prof->dev_caps & DEV_CAPS_16BIT ? 16 : 8;
		for (j = 0; j < num_logic_probes; j++) {
			if (!(probe = sr_probe_new(j, SR_PROBE_LOGIC, TRUE,
					probe_names[j])))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

		devc = fx2lafw_dev_new();
		devc->profile = prof;
		sdi->priv = devc;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);

		if (check_conf_profile(devlist[i])) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("Found an fx2lafw device.");
			sdi->status = SR_ST_INACTIVE;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION,
				prof->firmware) == SR_OK)
				/* Store when this device's FW was updated. */
				devc->fw_updated = g_get_monotonic_time();
			else
				sr_err("Firmware upload failed for "
				       "device %d.", devcnt);
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new (libusb_get_bus_number(devlist[i]),
					0xff, NULL);
		}
	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	int ret;
	int64_t timediff_us, timediff_ms;

	devc = sdi->priv;
	usb = sdi->conn;

	/*
	 * If the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * milliseconds for the FX2 to renumerate.
	 */
	ret = SR_ERR;
	if (devc->fw_updated > 0) {
		sr_info("Waiting for device to reset.");
		/* Takes >= 300ms for the FX2 to be gone from the USB bus. */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((ret = fx2lafw_dev_open(sdi)) == SR_OK)
				break;
			g_usleep(100 * 1000);

			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("Waited %" PRIi64 "ms.", timediff_ms);
		}
		if (ret != SR_OK) {
			sr_err("Device failed to renumerate.");
			return SR_ERR;
		}
		sr_info("Device came back after %" PRIi64 "ms.", timediff_ms);
	} else {
		sr_info("Firmware upload was not needed.");
		ret = fx2lafw_dev_open(sdi);
	}

	if (ret != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret != 0) {
		switch(ret) {
		case LIBUSB_ERROR_BUSY:
			sr_err("Unable to claim USB interface. Another "
			       "program or driver has already claimed it.");
			break;
		case LIBUSB_ERROR_NO_DEVICE:
			sr_err("Device has been disconnected.");
			break;
		default:
			sr_err("Unable to claim interface: %s.",
			       libusb_error_name(ret));
			break;
		}

		return SR_ERR;
	}

	if (devc->cur_samplerate == 0) {
		/* Samplerate hasn't been set; default to the slowest one. */
		devc->cur_samplerate = samplerates[0];
	}

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;
	if (usb->devhdl == NULL)
		return SR_ERR;

	sr_info("fx2lafw: Closing device %d on %d.%d interface %d.",
		sdi->index, usb->bus, usb->address, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int hw_cleanup(void)
{
	struct drv_context *drvc;
	int ret;

	if (!(drvc = di->priv))
		return SR_OK;

	ret = clear_instances();

	g_free(drvc);
	di->priv = NULL;

	return ret;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = g_variant_new_uint64(devc->cur_samplerate);
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	if (id == SR_CONF_SAMPLERATE) {
		devc->cur_samplerate = g_variant_get_uint64(data);
		ret = SR_OK;
	} else if (id == SR_CONF_LIMIT_SAMPLES) {
		devc->limit_samples = g_variant_get_uint64(data);
		ret = SR_OK;
	} else {
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
				ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_TYPE:
		*data = g_variant_new_string(TRIGGER_TYPE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	struct drv_context *drvc;

	(void)fd;
	(void)revents;
	(void)cb_data;

	drvc = di->priv;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

static void abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->num_samples = -1;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

static void finish_acquisition(struct dev_context *devc)
{
	struct sr_datafeed_packet packet;
	struct drv_context *drvc = di->priv;
	const struct libusb_pollfd **lupfd;
	int i;

	/* Terminate session. */
	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

	/* Remove fds from polling. */
	lupfd = libusb_get_pollfds(drvc->sr_ctx->libusb_ctx);
	for (i = 0; lupfd[i]; i++)
		sr_source_remove(lupfd[i]->fd);
	free(lupfd); /* NOT g_free()! */

	devc->num_transfers = 0;
	g_free(devc->transfers);
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct dev_context *devc;
	unsigned int i;

	devc = transfer->user_data;

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	devc->submitted_transfers--;
	if (devc->submitted_transfers == 0)
		finish_acquisition(devc);
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
	int ret;

	if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS)
		return;

	free_transfer(transfer);
	/* TODO: Stop session? */

	sr_err("%s: %s", __func__, libusb_error_name(ret));
}

static void receive_transfer(struct libusb_transfer *transfer)
{
	gboolean packet_has_error = FALSE;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct dev_context *devc;
	int trigger_offset, i, sample_width, cur_sample_count;
	int trigger_offset_bytes;
	uint8_t *cur_buf;

	devc = transfer->user_data;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->num_samples == -1) {
		free_transfer(transfer);
		return;
	}

	sr_info("receive_transfer(): status %d received %d bytes.",
		transfer->status, transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */
	cur_buf = transfer->buffer;
	sample_width = devc->sample_wide ? 2 : 1;
	cur_sample_count = transfer->actual_length / sample_width;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		abort_acquisition(devc);
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (transfer->actual_length == 0 || packet_has_error) {
		devc->empty_transfer_count++;
		if (devc->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			abort_acquisition(devc);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	trigger_offset = 0;
	if (devc->trigger_stage >= 0) {
		for (i = 0; i < cur_sample_count; i++) {

			const uint16_t cur_sample = devc->sample_wide ?
				*((const uint16_t*)cur_buf + i) :
				*((const uint8_t*)cur_buf + i);

			if ((cur_sample & devc->trigger_mask[devc->trigger_stage]) ==
				devc->trigger_value[devc->trigger_stage]) {
				/* Match on this trigger stage. */
				devc->trigger_buffer[devc->trigger_stage] = cur_sample;
				devc->trigger_stage++;

				if (devc->trigger_stage == NUM_TRIGGER_STAGES ||
					devc->trigger_mask[devc->trigger_stage] == 0) {
					/* Match on all trigger stages, we're done. */
					trigger_offset = i + 1;

					/*
					 * TODO: Send pre-trigger buffer to session bus.
					 * Tell the frontend we hit the trigger here.
					 */
					packet.type = SR_DF_TRIGGER;
					packet.payload = NULL;
					sr_session_send(devc->cb_data, &packet);

					/*
					 * Send the samples that triggered it,
					 * since we're skipping past them.
					 */
					packet.type = SR_DF_LOGIC;
					packet.payload = &logic;
					logic.unitsize = sizeof(*devc->trigger_buffer);
					logic.length = devc->trigger_stage * logic.unitsize;
					logic.data = devc->trigger_buffer;
					sr_session_send(devc->cb_data, &packet);

					devc->trigger_stage = TRIGGER_FIRED;
					break;
				}
			} else if (devc->trigger_stage > 0) {
				/*
				 * We had a match before, but not in the next sample. However, we may
				 * have a match on this stage in the next bit -- trigger on 0001 will
				 * fail on seeing 00001, so we need to go back to stage 0 -- but at
				 * the next sample from the one that matched originally, which the
				 * counter increment at the end of the loop takes care of.
				 */
				i -= devc->trigger_stage;
				if (i < -1)
					i = -1; /* Oops, went back past this buffer. */
				/* Reset trigger stage. */
				devc->trigger_stage = 0;
			}
		}
	}

	if (devc->trigger_stage == TRIGGER_FIRED) {
		/* Send the incoming transfer to the session bus. */
		trigger_offset_bytes = trigger_offset * sample_width;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = transfer->actual_length - trigger_offset_bytes;
		logic.unitsize = sample_width;
		logic.data = cur_buf + trigger_offset_bytes;
		sr_session_send(devc->cb_data, &packet);

		devc->num_samples += cur_sample_count;
		if (devc->limit_samples &&
			(unsigned int)devc->num_samples > devc->limit_samples) {
			abort_acquisition(devc);
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

static size_t get_buffer_size(struct dev_context *devc)
{
	size_t s;

	/*
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of 512.
	 */
	s = 10 * to_bytes_per_ms(devc->cur_samplerate);
	return (s + 511) & ~511;
}

static unsigned int get_number_of_transfers(struct dev_context *devc)
{
	unsigned int n;

	/* Total buffer size should be able to hold about 500ms of data. */
	n = 500 * to_bytes_per_ms(devc->cur_samplerate) / get_buffer_size(devc);

	if (n > NUM_SIMUL_TRANSFERS)
		return NUM_SIMUL_TRANSFERS;

	return n;
}

static unsigned int get_timeout(struct dev_context *devc)
{
	size_t total_size;
	unsigned int timeout;

	total_size = get_buffer_size(devc) * get_number_of_transfers(devc);
	timeout = total_size / to_bytes_per_ms(devc->cur_samplerate);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	const struct libusb_pollfd **lupfd;
	unsigned int i, timeout, num_transfers;
	int ret;
	unsigned char *buf;
	size_t size;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	if (devc->submitted_transfers != 0)
		return SR_ERR;

	if (configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
		return SR_ERR;
	}

	devc->cb_data = cb_data;
	devc->num_samples = 0;
	devc->empty_transfer_count = 0;

	timeout = get_timeout(devc);
	num_transfers = get_number_of_transfers(devc);
	size = get_buffer_size(devc);

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * num_transfers);
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		return SR_ERR_MALLOC;
	}

	devc->num_transfers = num_transfers;

	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, devc, timeout);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	lupfd = libusb_get_pollfds(drvc->sr_ctx->libusb_ctx);
	for (i = 0; lupfd[i]; i++)
		sr_source_add(lupfd[i]->fd, lupfd[i]->events,
			      timeout, receive_data, NULL);
	free(lupfd); /* NOT g_free()! */

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	if ((ret = command_start_acquisition (usb->devhdl,
		devc->cur_samplerate, devc->sample_wide)) != SR_OK) {
		abort_acquisition(devc);
		return ret;
	}

	return SR_OK;
}

/* TODO: This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
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
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
