/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Kevin Matocha <kmatocha@icloud.com>
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

//static struct sr_dev_driver tiny_logic_friend_la_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	GSList *l, *devices, *conn_devices;
	struct sr_config *src;
	struct sr_usb_dev_inst *usb;
	const char *conn;
	struct libusb_device_handle *hdl;
	int ret, i;
	char manufacturer[64], product[64], serial_num[64];
	struct libusb_device_descriptor des;
	libusb_device **devlist;

	// set the logging level of message
	int log_level;
	log_level=5;

	// (void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/* TODO: scan for devices, either based on a SR_CONF_CONN option
	 * or on a USB scan. */

	sr_log(log_level, "TinyLogicFriend: Starting scan! *****");

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

	// Read and print the VID/PID
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

		libusb_get_device_descriptor(devlist[i], &des);

		if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
			sr_warn("Failed to open potential device with "
				"VID:PID %04x:%04x: %s.", des.idVendor,
				des.idProduct, libusb_error_name(ret));
			continue;
		}

		// print the VID and PID of the found device
		sr_log(log_level, "Succesfully opened device with "
					"VID:PID %04x:%04x.", des.idVendor, des.idProduct);


		if (des.iManufacturer == 0) {
			manufacturer[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, (unsigned char *) manufacturer,
				sizeof(manufacturer))) < 0) {
			sr_warn("Failed to get manufacturer string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		// print the manufacturer string
		sr_log(log_level, "Found manufacturer string descriptor: %s.", manufacturer);

		if (des.iProduct == 0) {
			product[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, (unsigned char *) product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		// print the product string descriptor
		sr_log(log_level, "Found product string descriptor: %s.", product);


		if (des.iSerialNumber == 0) {
			serial_num[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iSerialNumber, (unsigned char *) serial_num,
				sizeof(serial_num))) < 0) {
			sr_warn("Failed to get serial number string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		// print the serial number descriptor
		sr_log(log_level, "Found serial number string descriptor: %s.", serial_num);

		// if got this far, close this device handle (opened by `libusb_open`)
		libusb_close(hdl);
	}

	// free items
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	sr_log(log_level, "TinyLogicFriend: Ending scan! *****");
	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and open it. */

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and close it. */

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;

	return SR_OK;
}

static struct sr_dev_driver tiny_logic_friend_la_driver_info = {
	.name = "tiny-logic-friend-la",
	.longname = "Tiny Logic Friend-la",
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
SR_REGISTER_DEV_DRIVER(tiny_logic_friend_la_driver_info);
