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

#define USB_TIMEOUT (3 * 1000)

//static struct sr_dev_driver tiny_logic_friend_la_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	GSList *l, *devices, *conn_devices;
	struct sr_config *src;
	struct sr_usb_dev_inst *usb;
	const char *conn;
	struct libusb_device_handle *hdl;
	int ret, i;
	char manufacturer[64], product[64], serial_num[64], connection_id[64];
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

	// This main loop looks through all USB connected devices and
	// looks at their vendor ID (VID) and product ID (PID) to see
	// if they are compatible with this driver
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

		/////////////////
		// if it got this far in the loop, this device may be a TinyLogicFriend
		// Close this device handle and try to connect to the device
		libusb_close(hdl);

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		if (strstr(product, "Tiny") && strstr(product, "Logic") && strstr(product, "Friend")) {
			sr_log(log_level-1, "I found a friend named: %s.  ******", product);
		} else {
			continue; // was not a friend
		}

		// You found a TinyLogicFriend!  Now start a conversation to confirm it is friendly
		// Send some "LV".  If it's a friend, it should respond "LVU2".
		// First create a sigrok device instance (sr_dev_inst *sdi)

		sdi = g_malloc0(sizeof(struct sr_dev_inst)); // allocate the device instance

		sr_log(log_level-1, "7");

		sdi->status = SR_ST_INITIALIZING; // set it to the "initializing" status
		sdi->inst_type = SR_INST_USB; // define the instrument as connected via USB
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]), // store USB connection
				libusb_get_device_address(devlist[i]), NULL);

		// *** To do: Talk to the unit and get this information
		// get sdi->vendor
		// get sdi->model
		// get sdi->version
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);
		sr_log(log_level-1, "8");

		// Need to configure these, see "dslogic_acquisition_start"

		// Do we need to add a USB source to talk to the device?
		//usb_source_add(sdi->session, devc->ctx, timeout, receive_data, drvc);

		////// send info from command_stop_acquisition from dslogic
		struct sr_usb_dev_inst *usb;
		int ret;
		char message[10];
		sr_log(log_level-1, "9");

		// mode.flags = DS_START_FLAGS_STOP;
		// mode.sample_delay_h = mode.sample_delay_l = 0;
		usb = sdi->conn; // find the connection to this device instance
     	sr_log(log_level-1, "10");
		/// This looks like the main way that information is passed to a device **
		/// See: https://www.freebsd.org/cgi/man.cgi?query=libusb_control_transfer
		/// See also: http://libusb.sourceforge.net/api-1.0/group__libusb__syncio.html
		///
		/// int libusb_control_transfer(libusb_device_handle *devh, uint8_t
     	/// bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t	wIndex,
     	/// unsigned char *data, uint16_t wLength, unsigned int timeout)

		//////////
     	// need to define the different bmRequest type and bRequests
     	// Ignore for now.
     	//

     	// send some "LV" when looking for a friend
     	strcpy(message, "LV");

     	struct libusb_config_descriptor *config;
     	const struct libusb_interface_descriptor *lid;
     	const struct libusb_endpoint_descriptor  *led;
     	int j,k;

     	if ( !(ret = libusb_open(devlist[i], &usb->devhdl)) ) {

     		if ( !(ret = libusb_get_active_config_descriptor(devlist[i], &config)) ) {

     			sr_log(log_level-1, "bLength: %d", config->bLength);
     			sr_log(log_level-1, "bDescriptorType: %x", config->bDescriptorType);
     			sr_log(log_level-1, "bNumInterfaces: %d", config->bNumInterfaces);
     			sr_log(log_level-1, "bConfigurationValue: %d", config->bConfigurationValue);
     			sr_log(log_level-1, "bmAttributes: %x", config->bmAttributes);
     			for(j=0; j < config->bNumInterfaces; j++){
     				lid = config->interface[j].altsetting;
     				sr_log(log_level-1, "Interface: %d **", j);
     				sr_log(log_level-1, "lid:bLength: %d", lid->bLength);
     				sr_log(log_level-1, "lid:bDescriptorType: %x", lid->bDescriptorType);
     				sr_log(log_level-1, "lid:bNumEndpoints: %d", lid->bNumEndpoints);
     				sr_log(log_level-1, "lid:bInterfaceClass: %x", lid->bInterfaceClass);
     				sr_log(log_level-1, "lid:bInterfaceSubClass: %x", lid->bInterfaceSubClass);
     				sr_log(log_level-1, "lid:bInterfaceProtocol: %x", lid->bInterfaceProtocol);
     				for(k=0; k<lid->bNumEndpoints; k++) {
     					led = &lid->endpoint[k];
     					sr_log(log_level-1, "Interface: %d, Endpoint: %d ********", j, k);
     					sr_log(log_level-1, "led:bLength: %d", led->bLength);
     					sr_log(log_level-1, "led:bDescriptorType: %x", led->bDescriptorType);
     					sr_log(log_level-1, "led:bEndpointAddress: %d", led->bEndpointAddress);
     					sr_log(log_level-1, "led:bmAttributes: %x", led->bmAttributes);
     					sr_log(log_level-1, "led:wMaxPacketSize: %d", led->wMaxPacketSize);
     					sr_log(log_level-1, "led:bInterval: %d", led->bInterval);
     				}

     			}
     		}





			sr_log(log_level-1, "Sending some LV  ******");
			ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR,
					0x00, 0x0000, 0x0000,
					(unsigned char *)&message, sizeof(message), USB_TIMEOUT);
			sr_log(log_level-1, "Sent some LV, await response");
		} else {
			sr_log(log_level-1, "Did not open USB.  No LV :(");
		}


		/////////////////
		//



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
