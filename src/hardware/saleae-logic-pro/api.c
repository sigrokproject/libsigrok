/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Jan Luebbe <jluebbe@lasnet.de>
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
#include <glib.h>
#include <string.h>
#include "protocol.h"

#define BUF_COUNT 512
#define BUF_SIZE (16 * 1024)
#define BUF_TIMEOUT 1000

SR_PRIV struct sr_dev_driver saleae_logic_pro_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"0", "1", "2", "3", "4", "5", "6", "7",
	"8", "9", "10", "11", "12", "13", "14", "15",
};

static const uint64_t samplerates[] = {
	SR_MHZ(1),
	SR_MHZ(2),
	SR_KHZ(2500),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
};

#define FW_HEADER_SIZE 7
#define FW_MAX_PART_SIZE (4 * 1024)

static int upload_firmware(struct sr_context *ctx, libusb_device *dev, const char *name)
{
	struct libusb_device_handle *hdl = NULL;
	unsigned char *firmware = NULL;
	int ret = SR_ERR;
	size_t fw_size, fw_offset = 0;
	uint32_t part_address = 0;
	uint16_t part_size = 0;
	uint8_t part_final = 0;

	firmware = sr_resource_load(ctx, SR_RESOURCE_FIRMWARE,
				    name, &fw_size, 256 * 1024);
	if (!firmware)
		goto out;

	sr_info("Uploading firmware '%s'.", name);

	if (libusb_open(dev, &hdl) != 0)
		goto out;

	while ((fw_offset + FW_HEADER_SIZE) <= fw_size) {
		part_size = GUINT16_FROM_LE(*(uint16_t*)(firmware + fw_offset));
		part_address = GUINT32_FROM_LE(*(uint32_t*)(firmware + fw_offset + 2));
		part_final = *(uint8_t*)(firmware + fw_offset + 6);
		if (part_size > FW_MAX_PART_SIZE) {
			sr_err("Part too large (%d).", part_size);
			goto out;
		}
		fw_offset += FW_HEADER_SIZE;
		if ((fw_offset + part_size) > fw_size) {
			sr_err("Truncated firmware file.");
			goto out;
		}
		ret = libusb_control_transfer(hdl, LIBUSB_REQUEST_TYPE_VENDOR |
					      LIBUSB_ENDPOINT_OUT, 0xa0,
					      part_address & 0xffff, part_address >> 16,
					      firmware + fw_offset, part_size,
					      100);
		if (ret < 0) {
			sr_err("Unable to send firmware to device: %s.",
			       libusb_error_name(ret));
			ret = SR_ERR;
			goto out;
		}
		if (part_size)
			sr_spew("Uploaded %d bytes.", part_size);
		else
			sr_info("Started firmware at 0x%x.", part_address);
		fw_offset += part_size;
	}

	if ((!part_final) || (part_size != 0)) {
		sr_err("Missing final part.");
		goto out;
	}

	ret = SR_OK;

	sr_info("Firmware upload done.");

 out:
	if (hdl)
		libusb_close(hdl);

	g_free(firmware);

	return ret;
}

static gboolean scan_firmware(libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	gboolean ret;
	unsigned char strdesc[64];

	hdl = NULL;
	ret = FALSE;

	libusb_get_device_descriptor(dev, &des);

	if (libusb_open(dev, &hdl) != 0)
		goto out;

	if (libusb_get_string_descriptor_ascii(hdl,
		des.iManufacturer, strdesc, sizeof(strdesc)) < 0)
		goto out;
	if (strcmp((const char *)strdesc, "Saleae"))
		goto out;

	if (libusb_get_string_descriptor_ascii(hdl,
		des.iProduct, strdesc, sizeof(strdesc)) < 0)
		goto out;
	if (strcmp((const char *)strdesc, "Logic Pro"))
		goto out;

	ret = TRUE;

out:
	if (hdl)
		libusb_close(hdl);

	return ret;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	GSList *devices, *conn_devices;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	const char *conn;
	char connection_id[64];
	gboolean fw_loaded = FALSE;

	devices = NULL;
	conn_devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	conn = NULL;
	for (GSList *l = options; l; l = l->next) {
		struct sr_config *src = l->data;

		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (unsigned int i = 0; devlist[i]; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != 0x21a9 || des.idProduct != 0x1006)
			continue;

		if (!scan_firmware(devlist[i])) {
			sr_info("Found a Logic Pro 16 device (no firmware loaded).");
			if (upload_firmware(drvc->sr_ctx, devlist[i],
					    "saleae-logicpro16-fx3.fw") != SR_OK) {
				sr_err("Firmware upload failed.");
				continue;
			};
			fw_loaded = TRUE;
		}

	}
	if (fw_loaded) {
		/* Give the device some time to come back and scan again */
		libusb_free_device_list(devlist, 1);
		g_usleep(500 * 1000);
		libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	for (unsigned int i = 0; devlist[i]; i++) {
		if (conn_devices) {
			struct sr_usb_dev_inst *usb = NULL;
			GSList *l;

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

		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != 0x21a9 || des.idProduct != 0x1006)
			continue;

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup("Saleae");
		sdi->model = g_strdup("Logic Pro 16");
		sdi->connection_id = g_strdup(connection_id);

		for (unsigned int j = 0; j < ARRAY_SIZE(channel_names); j++)
			sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE,
				       channel_names[j]);

		sr_dbg("Found a Logic Pro 16 device.");
		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
						libusb_get_device_address(devlist[i]), NULL);

		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		devices = g_slist_append(devices, sdi);

	}
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_claim_interface(usb->devhdl, 0))) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	/* Configure default samplerate. */
	if (devc->dig_samplerate == 0)
		devc->dig_samplerate = samplerates[3];

	return saleae_logic_pro_init(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	sr_usb_close(sdi->conn);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;

	(void)cg;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR;
		devc = sdi->priv;
		*data = g_variant_new_uint64(devc->dig_samplerate);
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

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->dig_samplerate = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void dev_acquisition_abort(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	unsigned int i;

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

static int dev_acquisition_handle(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct drv_context *drvc = sdi->driver->context;
	struct timeval tv = ALL_ZERO;

	(void)fd;

	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	/* Handle timeout */
	if (!revents)
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;
	struct libusb_transfer *transfer;
	struct sr_usb_dev_inst *usb;
	uint8_t *buf;
	unsigned int i, ret;

	ret = saleae_logic_pro_prepare(sdi);
	if (ret != SR_OK)
		return ret;

	usb = sdi->conn;

	devc->conv_buffer = g_malloc(CONV_BUFFER_SIZE);

	devc->num_transfers = BUF_COUNT;
	devc->transfers = g_malloc0(sizeof(*devc->transfers) * BUF_COUNT);
	for (i = 0; i < devc->num_transfers; i++) {
		buf = g_malloc(BUF_SIZE);
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
			2 | LIBUSB_ENDPOINT_IN, buf, BUF_SIZE,
			saleae_logic_pro_receive_data, (void *)sdi, 0);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			dev_acquisition_abort(sdi);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	usb_source_add(sdi->session, drvc->sr_ctx, BUF_TIMEOUT, dev_acquisition_handle, (void *)sdi);

	std_session_send_df_header(sdi);

	saleae_logic_pro_start(sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;

	saleae_logic_pro_stop(sdi);

	std_session_send_df_end(sdi);

	usb_source_remove(sdi->session, drvc->sr_ctx);

	g_free(devc->conv_buffer);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver saleae_logic_pro_driver_info = {
	.name = "saleae-logic-pro",
	.longname = "Saleae Logic Pro",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(saleae_logic_pro_driver_info);
