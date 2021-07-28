/*
 * This file is part of the libsigrok project.
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

#include <config.h>
#include "protocol.h"
#include <math.h>

static const struct fx2lafw_profile supported_fx2[] = {
	/*
	 * CWAV USBee AX
	 * ARMFLY AX-Pro (clone of the CWAV USBee AX)
	 * ARMFLY Mini-Logic (clone of the CWAV USBee AX)
	 * EE Electronics ESLA201A (clone of the CWAV USBee AX)
	 * HT USBee-AxPro (clone of the CWAV USBee AX)
	 * MCU123 USBee AX Pro clone (clone of the CWAV USBee AX)
	 * Noname LHT00SU1 (clone of the CWAV USBee AX)
	 * XZL_Studio AX (clone of the CWAV USBee AX)
	 */
	{ 0x08a9, 0x0014, "CWAV", "USBee AX", NULL,
		"fx2lafw-cwav-usbeeax.fw",
		DEV_CAPS_AX_ANALOG, NULL, NULL},

	/*
	 * CWAV USBee DX
	 * HT USBee-DxPro (clone of the CWAV USBee DX), not yet supported!
	 * XZL-Studio DX (clone of the CWAV USBee DX)
	 */
	{ 0x08a9, 0x0015, "CWAV", "USBee DX", NULL,
		"fx2lafw-cwav-usbeedx.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	/*
	 * CWAV USBee SX
	 */
	{ 0x08a9, 0x0009, "CWAV", "USBee SX", NULL,
		"fx2lafw-cwav-usbeesx.fw",
		0, NULL, NULL},

	/*
	 * CWAV USBee ZX
	 */
	{ 0x08a9, 0x0005, "CWAV", "USBee ZX", NULL,
		"fx2lafw-cwav-usbeezx.fw",
		0, NULL, NULL},

	/*
	 * Saleae Logic
	 * EE Electronics ESLA100 (clone of the Saleae Logic)
	 * Hantek 6022BL in LA mode (clone of the Saleae Logic)
	 * Instrustar ISDS205X in LA mode (clone of the Saleae Logic)
	 * Robomotic MiniLogic (clone of the Saleae Logic)
	 * Robomotic BugLogic 3 (clone of the Saleae Logic)
	 * MCU123 Saleae Logic clone (clone of the Saleae Logic)
	 */
	{ 0x0925, 0x3881, "Saleae", "Logic", NULL,
		"fx2lafw-saleae-logic.fw",
		0, NULL, NULL},

	/*
	 * Default Cypress FX2 without EEPROM, e.g.:
	 * Lcsoft Mini Board
	 * Braintechnology USB Interface V2.x
	 * fx2grok-tiny
	 */
	{ 0x04B4, 0x8613, "Cypress", "FX2", NULL,
		"fx2lafw-cypress-fx2.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	/*
	 * Braintechnology USB-LPS
	 */
	{ 0x16d0, 0x0498, "Braintechnology", "USB-LPS", NULL,
		"fx2lafw-braintechnology-usb-lps.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	/*
	 * sigrok FX2 based 8-channel logic analyzer
	 * fx2grok-flat (before and after renumeration)
	 */
	{ 0x1d50, 0x608c, "sigrok", "FX2 LA (8ch)", NULL,
		"fx2lafw-sigrok-fx2-8ch.fw",
		0, NULL, NULL},

	/*
	 * sigrok FX2 based 16-channel logic analyzer
	 */
	{ 0x1d50, 0x608d, "sigrok", "FX2 LA (16ch)", NULL,
		"fx2lafw-sigrok-fx2-16ch.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	/*
	 * usb-c-grok
	 */
	{ 0x1d50, 0x608f, "sigrok", "usb-c-grok", NULL,
		"fx2lafw-usb-c-grok.fw",
		0, NULL, NULL},

	/*
	 * Cypress SuperSpeed Explorer Kit (CYUSB3KIT-003)
	 */
	{ 0x04b4, 0x00f3, "Cypress", "SuperSpeed Explorer Kit", NULL,
		"fx3lafw-cypress-fx3.fw",
		DEV_CAPS_FX3 | DEV_CAPS_32BIT, NULL, NULL },

	ALL_ZERO
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
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
	SR_MHZ(32),
	SR_MHZ(24),
};

static gboolean is_plausible(const struct libusb_device_descriptor *des)
{
	int i;

	for (i = 0; supported_fx2[i].vid; i++) {
		if (des->idVendor != supported_fx2[i].vid)
			continue;
		if (des->idProduct == supported_fx2[i].pid)
			return TRUE;
	}

	return FALSE;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_config *src;
	const struct fx2lafw_profile *prof;
	GSList *l, *devices, *conn_devices;
	gboolean has_firmware;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	int ret, i, j;
	int num_logic_channels = 0, num_analog_channels = 0;
	const char *conn;
	char manufacturer[64], product[64], serial_num[64], connection_id[64];
	char channel_name[16];

	drvc = di->context;

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

		libusb_get_device_descriptor( devlist[i], &des);

		if (!is_plausible(&des))
			continue;

		if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
			sr_warn("Failed to open potential device with "
				"VID:PID %04x:%04x: %s.", des.idVendor,
				des.idProduct, libusb_error_name(ret));
			continue;
		}

		if (des.iManufacturer == 0) {
			manufacturer[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, (unsigned char *) manufacturer,
				sizeof(manufacturer))) < 0) {
			sr_warn("Failed to get manufacturer string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iProduct == 0) {
			product[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, (unsigned char *) product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iSerialNumber == 0) {
			serial_num[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iSerialNumber, (unsigned char *) serial_num,
				sizeof(serial_num))) < 0) {
			sr_warn("Failed to get serial number string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		libusb_close(hdl);

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		prof = NULL;
		for (j = 0; supported_fx2[j].vid; j++) {
			if (des.idVendor == supported_fx2[j].vid &&
					des.idProduct == supported_fx2[j].pid &&
					(!supported_fx2[j].usb_manufacturer ||
					 !strcmp(manufacturer, supported_fx2[j].usb_manufacturer)) &&
					(!supported_fx2[j].usb_product ||
					 !strcmp(product, supported_fx2[j].usb_product))) {
				prof = &supported_fx2[j];
				break;
			}
		}

		if (!prof)
			continue;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup(prof->vendor);
		sdi->model = g_strdup(prof->model);
		sdi->version = g_strdup(prof->model_version);
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);

		/* Fill in channellist according to this device's profile. */
		num_logic_channels =
			prof->dev_caps & DEV_CAPS_32BIT ? 32 :
			(prof->dev_caps & DEV_CAPS_24BIT ? 24 :
			 (prof->dev_caps & DEV_CAPS_16BIT ? 16 : 8));
		num_analog_channels = prof->dev_caps & DEV_CAPS_AX_ANALOG ? 1 : 0;

		/* Logic channels, all in one channel group. */
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");
		for (j = 0; j < num_logic_channels; j++) {
			sprintf(channel_name, "D%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC,
						TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);

		for (j = 0; j < num_analog_channels; j++) {
			snprintf(channel_name, 16, "A%d", j);
			ch = sr_channel_new(sdi, j + num_logic_channels,
					SR_CHANNEL_ANALOG, TRUE, channel_name);

			/* Every analog channel gets its own channel group. */
			cg = g_malloc0(sizeof(struct sr_channel_group));
			cg->name = g_strdup(channel_name);
			cg->channels = g_slist_append(NULL, ch);
			sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
		}

		devc = fx2lafw_dev_new();
		devc->profile = prof;
		sdi->priv = devc;
		devices = g_slist_append(devices, sdi);

		devc->samplerates = samplerates;
		devc->num_samplerates = ARRAY_SIZE(samplerates);
		has_firmware = usb_match_manuf_prod(devlist[i],
				"sigrok", "fx2lafw");

		if (has_firmware) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("Found an fx2lafw device.");
			sdi->status = SR_ST_INACTIVE;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(drvc->sr_ctx, devlist[i],
					USB_CONFIGURATION, prof->firmware) == SR_OK) {
				/* Store when this device's FW was updated. */
				devc->fw_updated = g_get_monotonic_time();
			} else {
				sr_err("Firmware upload failed for "
				       "device %d.%d (logical), name %s.",
				       libusb_get_bus_number(devlist[i]),
				       libusb_get_device_address(devlist[i]),
				       prof->firmware);
			}
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					0xff, NULL);
		}
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static void clear_helper(struct dev_context *devc)
{
	g_slist_free(devc->enabled_analog_channels);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
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
			if ((ret = fx2lafw_dev_open(sdi, di)) == SR_OK)
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
		ret = fx2lafw_dev_open(sdi, di);
	}

	if (ret != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret != 0) {
		switch (ret) {
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
		devc->cur_samplerate = devc->samplerates[0];
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	sr_info("Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 255)
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
			return SR_ERR;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limit_frames);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int idx;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if ((idx = std_u64_idx(data, devc->samplerates, devc->num_samplerates)) < 0)
			return SR_ERR_ARG;
		devc->cur_samplerate = devc->samplerates[idx];
		break;
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (cg)
			return SR_ERR_NA;
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_NA;
		*data = std_gvar_samplerates(devc->samplerates, devc->num_samplerates);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	fx2lafw_abort_acquisition(sdi->priv);

	return SR_OK;
}

static struct sr_dev_driver fx2lafw_driver_info = {
	.name = "fx2lafw",
	.longname = "fx2lafw (generic driver for FX2 based LAs)",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = fx2lafw_start_acquisition,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(fx2lafw_driver_info);
