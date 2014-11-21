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

#include "protocol.h"

static const struct fx2lafw_profile supported_fx2[] = {
	/*
	 * CWAV USBee AX
	 * EE Electronics ESLA201A
	 * ARMFLY AX-Pro
	 */
	{ 0x08a9, 0x0014, "CWAV", "USBee AX", NULL,
		FIRMWARE_DIR "/fx2lafw-cwav-usbeeax.fw",
		0, NULL, NULL},
	/*
	 * CWAV USBee DX
	 * XZL-Studio DX
	 */
	{ 0x08a9, 0x0015, "CWAV", "USBee DX", NULL,
		FIRMWARE_DIR "/fx2lafw-cwav-usbeedx.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	/*
	 * CWAV USBee SX
	 */
	{ 0x08a9, 0x0009, "CWAV", "USBee SX", NULL,
		FIRMWARE_DIR "/fx2lafw-cwav-usbeesx.fw",
		0, NULL, NULL},

	/*
	 * Saleae Logic
	 * EE Electronics ESLA100
	 * Robomotic MiniLogic
	 * Robomotic BugLogic 3
	 */
	{ 0x0925, 0x3881, "Saleae", "Logic", NULL,
		FIRMWARE_DIR "/fx2lafw-saleae-logic.fw",
		0, NULL, NULL},

	/*
	 * Default Cypress FX2 without EEPROM, e.g.:
	 * Lcsoft Mini Board
	 * Braintechnology USB Interface V2.x
	 */
	{ 0x04B4, 0x8613, "Cypress", "FX2", NULL,
		FIRMWARE_DIR "/fx2lafw-cypress-fx2.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	/*
	 * Braintechnology USB-LPS
	 */
	{ 0x16d0, 0x0498, "Braintechnology", "USB-LPS", NULL,
		FIRMWARE_DIR "/fx2lafw-braintechnology-usb-lps.fw",
		DEV_CAPS_16BIT, NULL, NULL },

	{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",
	"8",  "9", "10", "11", "12", "13", "14", "15",
	NULL,
};

static const int32_t soft_trigger_matches[] = {
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
	SR_MHZ(24),
};

SR_PRIV struct sr_dev_driver fx2lafw_driver_info;
static struct sr_dev_driver *di = &fx2lafw_driver_info;

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_channel *ch;
	struct sr_config *src;
	const struct fx2lafw_profile *prof;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	int num_logic_channels, ret, i, j;
	const char *conn;
	char manufacturer[64], product[64], serial_num[64], connection_id[64];

	drvc = di->priv;

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
		for (j = 0; j < num_logic_channels; j++) {
			if (!(ch = sr_channel_new(j, SR_CHANNEL_LOGIC, TRUE,
					channel_names[j])))
				return NULL;
			sdi->channels = g_slist_append(sdi->channels, ch);
		}

		devc = fx2lafw_dev_new();
		devc->profile = prof;
		devc->sample_wide = (prof->dev_caps & DEV_CAPS_16BIT) != 0;
		sdi->priv = devc;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);

		if (fx2lafw_check_conf_profile(devlist[i])) {
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

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
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
		devc->cur_samplerate = samplerates[0];
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;
	if (usb->devhdl == NULL)
		return SR_ERR;

	sr_info("fx2lafw: Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		return SR_OK;

	ret = std_dev_clear(di, NULL);

	g_free(drvc);
	di->priv = NULL;

	return ret;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
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
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
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
	unsigned int i;
	int ret;

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
			for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
				if (samplerates[i] == arg) {
					devc->cur_samplerate = arg;
					break;
				}
			}
			if (i == ARRAY_SIZE(samplerates))
				ret = SR_ERR_ARG;
			break;
		case SR_CONF_LIMIT_SAMPLES:
			devc->limit_samples = g_variant_get_uint64(data);
			break;
		default:
			ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
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
		else
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
				ARRAY_SIZE(samplerates), sizeof(uint64_t));
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
	(void)cb_data;

	drvc = di->priv;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_trigger *trigger;
	struct libusb_transfer *transfer;
	unsigned int i, timeout, num_transfers;
	int ret;
	unsigned char *buf;
	size_t size;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	devc->cb_data = cb_data;
	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->empty_transfer_count = 0;

	if ((trigger = sr_session_trigger_get(sdi->session))) {
		devc->stl = soft_trigger_logic_new(sdi, trigger);
		devc->trigger_fired = FALSE;
	} else
		devc->trigger_fired = TRUE;

	timeout = fx2lafw_get_timeout(devc);
	num_transfers = fx2lafw_get_number_of_transfers(devc);
	size = fx2lafw_get_buffer_size(devc);
	devc->submitted_transfers = 0;

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

	devc->ctx = drvc->sr_ctx;

	usb_source_add(sdi->session, devc->ctx, timeout, receive_data, NULL);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	if ((ret = fx2lafw_command_start_acquisition(sdi)) != SR_OK) {
		fx2lafw_abort_acquisition(devc);
		return ret;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	fx2lafw_abort_acquisition(sdi->priv);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver fx2lafw_driver_info = {
	.name = "fx2lafw",
	.longname = "fx2lafw (generic driver for FX2 based LAs)",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
