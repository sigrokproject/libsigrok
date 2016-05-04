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
#include "dslogic.h"
#include <math.h>

static const struct fx2lafw_profile supported_fx2[] = {
	/*
	 * CWAV USBee AX
	 * EE Electronics ESLA201A
	 * ARMFLY AX-Pro
	 */
	{ 0x08a9, 0x0014, "CWAV", "USBee AX", NULL,
		"fx2lafw-cwav-usbeeax.fw",
		DEV_CAPS_AX_ANALOG, NULL, NULL},
	/*
	 * CWAV USBee DX
	 * XZL-Studio DX
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

	/* DreamSourceLab DSLogic (before FW upload) */
	{ 0x2a0e, 0x0001, "DreamSourceLab", "DSLogic", NULL,
		"dreamsourcelab-dslogic-fx2.fw",
		DEV_CAPS_16BIT, NULL, NULL},
	/* DreamSourceLab DSLogic (after FW upload) */
	{ 0x2a0e, 0x0001, "DreamSourceLab", "DSLogic", NULL,
		"dreamsourcelab-dslogic-fx2.fw",
		DEV_CAPS_16BIT, "DreamSourceLab", "DSLogic"},

	/* DreamSourceLab DSCope (before FW upload) */
	{ 0x2a0e, 0x0002, "DreamSourceLab", "DSCope", NULL,
		"dreamsourcelab-dscope-fx2.fw",
		DEV_CAPS_16BIT, NULL, NULL},
	/* DreamSourceLab DSCope (after FW upload) */
	{ 0x2a0e, 0x0002, "DreamSourceLab", "DSCope", NULL,
		"dreamsourcelab-dscope-fx2.fw",
		DEV_CAPS_16BIT, "DreamSourceLab", "DSCope"},

	/* DreamSourceLab DSLogic Pro (before FW upload) */
	{ 0x2a0e, 0x0003, "DreamSourceLab", "DSLogic Pro", NULL,
		"dreamsourcelab-dslogic-pro-fx2.fw",
		DEV_CAPS_16BIT, NULL, NULL},
	/* DreamSourceLab DSLogic Pro (after FW upload) */
	{ 0x2a0e, 0x0003, "DreamSourceLab", "DSLogic Pro", NULL,
		"dreamsourcelab-dslogic-pro-fx2.fw",
		DEV_CAPS_16BIT, "DreamSourceLab", "DSLogic"},

	/*
	 * Saleae Logic
	 * EE Electronics ESLA100
	 * Robomotic MiniLogic
	 * Robomotic BugLogic 3
	 */
	{ 0x0925, 0x3881, "Saleae", "Logic", NULL,
		"fx2lafw-saleae-logic.fw",
		0, NULL, NULL},

	/*
	 * Default Cypress FX2 without EEPROM, e.g.:
	 * Lcsoft Mini Board
	 * Braintechnology USB Interface V2.x
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

	ALL_ZERO
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t dslogic_devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t soft_trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const struct {
	int range;
	gdouble low;
	gdouble high;
} volt_thresholds[] = {
	{ DS_VOLTAGE_RANGE_18_33_V, 0.7,     1.4 },
	{ DS_VOLTAGE_RANGE_5_V,	    1.4,     3.6 },
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

static const uint64_t dslogic_samplerates[] = {
	SR_KHZ(10),
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
	SR_MHZ(400),
};

SR_PRIV struct sr_dev_driver fx2lafw_driver_info;

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

		if ((ret = libusb_open(devlist[i], &hdl)) < 0)
			continue;

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

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		libusb_close(hdl);

		prof = NULL;
		for (j = 0; supported_fx2[j].vid; j++) {
			if (des.idVendor == supported_fx2[j].vid &&
					des.idProduct == supported_fx2[j].pid &&
					(!supported_fx2[j].usb_manufacturer ||
					 !strcmp(manufacturer, supported_fx2[j].usb_manufacturer)) &&
					(!supported_fx2[j].usb_manufacturer ||
					 !strcmp(product, supported_fx2[j].usb_product))) {
				prof = &supported_fx2[j];
				break;
			}
		}

		/* Skip if the device was not found. */
		if (!prof)
			continue;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup(prof->vendor);
		sdi->model = g_strdup(prof->model);
		sdi->version = g_strdup(prof->model_version);
		sdi->driver = di;
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);

		/* Fill in channellist according to this device's profile. */
		num_logic_channels = prof->dev_caps & DEV_CAPS_16BIT ? 16 : 8;
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
		if ((prof->dev_caps & DEV_CAPS_16BIT) || (prof->dev_caps & DEV_CAPS_AX_ANALOG))
			devc->sample_wide = TRUE;
		sdi->priv = devc;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);

		if (!strcmp(prof->model, "DSLogic")
				|| !strcmp(prof->model, "DSLogic Pro")
				|| !strcmp(prof->model, "DSCope")) {
			devc->dslogic = TRUE;
			devc->samplerates = dslogic_samplerates;
			devc->num_samplerates = ARRAY_SIZE(dslogic_samplerates);
			has_firmware = match_manuf_prod(devlist[i], "DreamSourceLab", "DSLogic")
					|| match_manuf_prod(devlist[i], "DreamSourceLab", "DSCope");
		} else {
			devc->dslogic = FALSE;
			devc->samplerates = samplerates;
			devc->num_samplerates = ARRAY_SIZE(samplerates);
			has_firmware = match_manuf_prod(devlist[i],
					"sigrok", "fx2lafw");
		}

		if (has_firmware) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("Found an fx2lafw device.");
			sdi->status = SR_ST_INACTIVE;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(drvc->sr_ctx, devlist[i],
					USB_CONFIGURATION, prof->firmware) == SR_OK)
				/* Store when this device's FW was updated. */
				devc->fw_updated = g_get_monotonic_time();
			else
				sr_err("Firmware upload failed for "
				       "device %d.%d (logical).",
				       libusb_get_bus_number(devlist[i]),
				       libusb_get_device_address(devlist[i]));
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
					0xff, NULL);
		}
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return devices;
}

static void clear_dev_context(void *priv)
{
	struct dev_context *devc;

	devc = priv;
	g_slist_free(devc->enabled_analog_channels);
	g_free(devc);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, clear_dev_context);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	const char *fpga_firmware = NULL;
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

	if (devc->dslogic) {
		if (!strcmp(devc->profile->model, "DSLogic")) {
			if (devc->dslogic_voltage_threshold == DS_VOLTAGE_RANGE_18_33_V)
				fpga_firmware = DSLOGIC_FPGA_FIRMWARE_3V3;
			else
				fpga_firmware = DSLOGIC_FPGA_FIRMWARE_5V;
		} else if (!strcmp(devc->profile->model, "DSLogic Pro")){
			fpga_firmware = DSLOGIC_PRO_FPGA_FIRMWARE;
		} else if (!strcmp(devc->profile->model, "DSCope")) {
			fpga_firmware = DSCOPE_FPGA_FIRMWARE;
		}

		if ((ret = dslogic_fpga_firmware_upload(sdi, fpga_firmware)) != SR_OK)
			return ret;
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
		return SR_ERR;

	sr_info("fx2lafw: Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	GVariant *range[2];
	unsigned int i;
	char str[128];

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
		snprintf(str, 128, "%d.%d", usb->bus, usb->address);
		*data = g_variant_new_string(str);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		for (i = 0; i < ARRAY_SIZE(volt_thresholds); i++) {
			if (volt_thresholds[i].range != devc->dslogic_voltage_threshold)
				continue;
			range[0] = g_variant_new_double(volt_thresholds[i].low);
			range[1] = g_variant_new_double(volt_thresholds[i].high);
			*data = g_variant_new_tuple(range, 2);
			break;
		}
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

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t arg;
	int i, ret;
	gdouble low, high;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	devc = sdi->priv;

	ret = SR_OK;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		arg = g_variant_get_uint64(data);
		for (i = 0; i < devc->num_samplerates; i++) {
			if (devc->samplerates[i] == arg) {
				devc->cur_samplerate = arg;
				break;
			}
		}
		if (i == devc->num_samplerates)
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		ret = (devc->capture_ratio > 100) ? SR_ERR : SR_OK;
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &low, &high);
		ret = SR_ERR_ARG;
		for (i = 0; (unsigned int)i < ARRAY_SIZE(volt_thresholds); i++) {
			if (fabs(volt_thresholds[i].low - low) < 0.1 &&
			    fabs(volt_thresholds[i].high - high) < 0.1) {
				devc->dslogic_voltage_threshold = volt_thresholds[i].range;
				break;
			}
		}
		if (!strcmp(devc->profile->model, "DSLogic")) {
			if (devc->dslogic_voltage_threshold == DS_VOLTAGE_RANGE_5_V)
				ret = dslogic_fpga_firmware_upload(sdi, DSLOGIC_FPGA_FIRMWARE_5V);
			else
				ret = dslogic_fpga_firmware_upload(sdi, DSLOGIC_FPGA_FIRMWARE_3V3);
		}else if (!strcmp(devc->profile->model, "DSLogic Pro")){
			ret = dslogic_fpga_firmware_upload(sdi, DSLOGIC_PRO_FPGA_FIRMWARE);
		}
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *gvar, *range[2];
	GVariantBuilder gvb;
	unsigned int i;

	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi)
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
							  drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		else{
			devc = sdi->priv;
			if (!devc->dslogic)
				*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
								  devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			else
				*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
								  dslogic_devopts, ARRAY_SIZE(dslogic_devopts), sizeof(uint32_t));
		}
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		if (!sdi->priv) return SR_ERR_ARG;
		devc = sdi->priv;
		if (!devc->dslogic) return SR_ERR_NA;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(volt_thresholds); i++) {
			range[0] = g_variant_new_double(volt_thresholds[i].low);
			range[1] = g_variant_new_double(volt_thresholds[i].high);
			gvar = g_variant_new_tuple(range, 2);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi->priv)
			return SR_ERR_ARG;
		devc = sdi->priv;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), devc->samplerates,
				devc->num_samplerates, sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				soft_trigger_matches, ARRAY_SIZE(soft_trigger_matches),
				sizeof(int32_t));
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

	drvc = (struct drv_context *)cb_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

static int start_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_trigger *trigger;
	struct libusb_transfer *transfer;
	unsigned int i, num_transfers;
	int endpoint, timeout, ret;
	unsigned char *buf;
	size_t size;

	devc = sdi->priv;
	usb = sdi->conn;

	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->empty_transfer_count = 0;

	if ((trigger = sr_session_trigger_get(sdi->session)) && !devc->dslogic) {
		int pre_trigger_samples = 0;
		if (devc->limit_samples > 0)
			pre_trigger_samples = devc->capture_ratio * devc->limit_samples/100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	} else
		devc->trigger_fired = TRUE;

	num_transfers = fx2lafw_get_number_of_transfers(devc);
	size = fx2lafw_get_buffer_size(devc);
	devc->submitted_transfers = 0;

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * num_transfers);
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		return SR_ERR_MALLOC;
	}

	timeout = fx2lafw_get_timeout(devc);
	endpoint = devc->dslogic ? 6 : 2;
	devc->num_transfers = num_transfers;
	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				endpoint | LIBUSB_ENDPOINT_IN, buf, size,
				fx2lafw_receive_transfer, (void *)sdi, timeout);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			fx2lafw_abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	if (devc->profile->dev_caps & DEV_CAPS_AX_ANALOG)
		devc->send_data_proc = mso_send_data_proc;
	else
		devc->send_data_proc = la_send_data_proc;

	std_session_send_df_header(sdi, LOG_PREFIX);

	return SR_OK;
}

static void LIBUSB_CALL dslogic_trigger_receive(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi;
	struct dslogic_trigger_pos *tpos;
	struct dev_context *devc;

	sdi = transfer->user_data;
	devc = sdi->priv;
	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		sr_dbg("Trigger transfer canceled.");
		/* Terminate session. */
		std_session_send_df_end(sdi, LOG_PREFIX);
		usb_source_remove(sdi->session, devc->ctx);
		devc->num_transfers = 0;
		g_free(devc->transfers);
		if (devc->stl) {
			soft_trigger_logic_free(devc->stl);
			devc->stl = NULL;
		}
	} else if (transfer->status == LIBUSB_TRANSFER_COMPLETED
			&& transfer->actual_length == sizeof(struct dslogic_trigger_pos)) {
		tpos = (struct dslogic_trigger_pos *)transfer->buffer;
		sr_info("tpos real_pos %d ram_saddr %d cnt %d", tpos->real_pos, tpos->ram_saddr, tpos->remain_cnt);
		devc->trigger_pos  = tpos->real_pos;
		g_free(tpos);
		start_transfers(sdi);
	}
	libusb_free_transfer(transfer);
}

static int dslogic_trigger_request(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	struct dslogic_trigger_pos *tpos;
	struct dev_context *devc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;

	if ((ret = dslogic_stop_acquisition(sdi)) != SR_OK)
		return ret;

	if ((ret = dslogic_fpga_configure(sdi)) != SR_OK)
		return ret;

	/* if this is a dslogic pro, set the voltage threshold */
	if (!strcmp(devc->profile->model, "DSLogic Pro")){
		if(devc->dslogic_voltage_threshold == DS_VOLTAGE_RANGE_18_33_V){
			dslogic_set_vth(sdi, 1.4);
		}else{
			dslogic_set_vth(sdi, 3.3);
		}
	}

	if ((ret = dslogic_start_acquisition(sdi)) != SR_OK)
		return ret;

	sr_dbg("Getting trigger.");
	tpos = g_malloc(sizeof(struct dslogic_trigger_pos));
	transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(transfer, usb->devhdl, 6 | LIBUSB_ENDPOINT_IN,
			(unsigned char *)tpos, sizeof(struct dslogic_trigger_pos),
			dslogic_trigger_receive, (void *)sdi, 0);
	if ((ret = libusb_submit_transfer(transfer)) < 0) {
		sr_err("Failed to request trigger: %s.", libusb_error_name(ret));
		libusb_free_transfer(transfer);
		g_free(tpos);
		return SR_ERR;
	}

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers));
	if (!devc->transfers) {
		sr_err("USB trigger_pos transfer malloc failed.");
		return SR_ERR_MALLOC;
	}
	devc->num_transfers = 1;
	devc->submitted_transfers++;
	devc->transfers[0] = transfer;

	return ret;
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const GSList *l;
	int p;
	struct sr_channel *ch;

	devc = sdi->priv;

	g_slist_free(devc->enabled_analog_channels);
	devc->enabled_analog_channels = NULL;
	memset(devc->ch_enabled, 0, sizeof(devc->ch_enabled));

	for (l = sdi->channels, p = 0; l; l = l->next, p++) {
		ch = l->data;
		if ((p <= NUM_CHANNELS) && (ch->type == SR_CHANNEL_ANALOG)) {
			devc->ch_enabled[p] = ch->enabled;
			devc->enabled_analog_channels =
			    g_slist_append(devc->enabled_analog_channels, ch);
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct dev_context *devc;
	int timeout, ret;
	size_t size;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;

	devc->ctx = drvc->sr_ctx;
	devc->sent_samples = 0;
	devc->empty_transfer_count = 0;
	devc->acq_aborted = FALSE;

	if (configure_channels(sdi) != SR_OK) {
		sr_err("Failed to configure channels.");
		return SR_ERR;
	}

	timeout = fx2lafw_get_timeout(devc);
	usb_source_add(sdi->session, devc->ctx, timeout, receive_data, drvc);

	if (devc->dslogic) {
		dslogic_trigger_request(sdi);
	} else {
		size = fx2lafw_get_buffer_size(devc);
		/* Prepare for analog sampling. */
		if (devc->profile->dev_caps & DEV_CAPS_AX_ANALOG) {
			/* We need a buffer half the size of a transfer. */
			devc->logic_buffer = g_try_malloc(size / 2);
			devc->analog_buffer = g_try_malloc(
				sizeof(float) * size / 2);
		}
		start_transfers(sdi);
		if ((ret = fx2lafw_command_start_acquisition(sdi)) != SR_OK) {
			fx2lafw_abort_acquisition(devc);
			return ret;
		}
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->dslogic)
		dslogic_stop_acquisition(sdi);

	fx2lafw_abort_acquisition(sdi->priv);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver fx2lafw_driver_info = {
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
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
