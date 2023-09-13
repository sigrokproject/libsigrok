/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Lubomir Rintel <lkundrak@v3.sk>
 *
 * Heavily based on USBTMC code:
 *
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
 *
 * The protocol description has been obtained from dso3000 project
 * by Ben Johnson <circuitben@gmail.com>:
 * https://github.com/cktben/dso3000
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
#include <inttypes.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi_ds5000usb"

#define READ_RESPONSE 0
#define  RESPONSE_LENGTH 0
#define  RESPONSE_DATA 1
#define WRITE_CHAR 1

#define MAX_TRANSFER_LENGTH 256
#define TRANSFER_TIMEOUT 1000

struct scpi_ds5000usb_libusb {
	struct sr_context *ctx;
	struct sr_usb_dev_inst *usb;
	int detached_kernel_driver;
	uint8_t buffer[MAX_TRANSFER_LENGTH];
	uint8_t response_length;
	uint8_t response_bytes_read;
};

/*
 * This is a version of libusb_control_transfer() that tries a little
 * harder. The reason behind it is that USB firmware on the oscilloscope
 * sometimes just decides not to respond. This is no surprise given the
 * overall shittiness of the firmware.
 *
 * The documentation actually suggest that USB should not actually be used
 * by anything other than their official software. My guess is that the
 * OEM was aware, couldn't make things work reliably, wanted to avoid the
 * embarassment and just worked around the issues in their tools.
 */

static int scpi_ds5000usb_libusb_control_transfer (libusb_device_handle *dev_handle,
	uint8_t request_type, uint8_t bRequest, uint16_t wValue,
	uint16_t wIndex, unsigned char *data, uint16_t wLength,
	unsigned int timeout)
{
	int retries = 10;
	int ret;

	while (retries--) {
		ret = libusb_control_transfer(dev_handle, request_type,
			bRequest, wValue, wIndex, data, wLength, timeout);
		if (ret != LIBUSB_ERROR_TIMEOUT)
			break;
		sr_dbg("Timed out. %d more tries...\n", retries);
	}

	return ret;
}

static int scpi_ds5000usb_libusb_device_valid(struct libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *confdes;
	const struct libusb_interface_descriptor *intfdes;
	int ret = SR_ERR;

	libusb_get_device_descriptor(dev, &des);

	if (des.idVendor != 0x0400 || des.idProduct != 0xc55d) {
		sr_dbg("Vendor Id or Product Id mismatch.");
		return SR_ERR;
	}

	if (des.bNumConfigurations < 1) {
		sr_dbg("Device descriptor contains no configurations.");
		return SR_ERR;
	}

	ret = libusb_get_config_descriptor(dev, 0, &confdes);
	if (ret < 0) {
		sr_err("Failed to read configuration descriptor: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	if (confdes->bNumInterfaces < 1) {
		sr_dbg("Configuration descriptor contains no interfaces.");
		goto free_confdes;
	}

	intfdes = confdes->interface[0].altsetting;

	if (intfdes->bInterfaceClass    != LIBUSB_CLASS_VENDOR_SPEC ||
	    intfdes->bInterfaceSubClass != 0 ||
	    intfdes->bInterfaceProtocol != 0xff) {
		sr_dbg("Interface 0 doesn't look like a DS5000USB interface.");
		goto free_confdes;
	}

	ret = SR_OK;
free_confdes:
	libusb_free_config_descriptor(confdes);
	return ret;
}

static GSList *scpi_ds5000usb_libusb_scan(struct drv_context *drvc)
{
	struct libusb_device **devlist;
	GSList *resources = NULL;
	int ret, i;
	char *res;

	ret = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (ret < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(ret));
		return NULL;
	}
	for (i = 0; devlist[i]; i++) {
		if (scpi_ds5000usb_libusb_device_valid(devlist[i]) != SR_OK)
			continue;

		sr_dbg("Found DS5000USB device (bus.address = %d.%d).",
		       libusb_get_bus_number(devlist[i]),
		       libusb_get_device_address(devlist[i]));
		res = g_strdup_printf("ds5000usb/%d.%d",
				      libusb_get_bus_number(devlist[i]),
				      libusb_get_device_address(devlist[i]));
		resources = g_slist_append(resources, res);
	}
	libusb_free_device_list(devlist, 1);

	return resources;
}

static int scpi_ds5000usb_libusb_dev_inst_new(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	GSList *devices;

	(void)resource;
	(void)serialcomm;

	if (!params || !params[1]) {
		sr_err("Invalid parameters.");
		return SR_ERR;
	}

	uscpi->ctx = drvc->sr_ctx;
	devices = sr_usb_find(uscpi->ctx->libusb_ctx, params[1]);
	if (g_slist_length(devices) != 1) {
		sr_err("Failed to find USB device '%s'.", params[1]);
		g_slist_free_full(devices, (GDestroyNotify)sr_usb_dev_inst_free);
		return SR_ERR;
	}
	uscpi->usb = devices->data;
	g_slist_free(devices);

	return SR_OK;
}

static int scpi_ds5000usb_libusb_fill_buffer(struct scpi_ds5000usb_libusb *uscpi)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int ret;

	uscpi->response_length = 0;
	uscpi->response_bytes_read = 0;

	ret = scpi_ds5000usb_libusb_control_transfer(usb->devhdl, LIBUSB_ENDPOINT_IN |
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		READ_RESPONSE, RESPONSE_LENGTH, 0,
		&uscpi->response_length, 1, TRANSFER_TIMEOUT);
	if (ret < 0) {
		sr_err("Error reading remaining length: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	if (uscpi->response_length == 0)
		return SR_OK;

	ret = scpi_ds5000usb_libusb_control_transfer(usb->devhdl, LIBUSB_ENDPOINT_IN |
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		READ_RESPONSE, RESPONSE_DATA, 0,
		uscpi->buffer, uscpi->response_length, TRANSFER_TIMEOUT);
	if (ret < 0) {
		sr_err("Error reading data: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	if (ret != uscpi->response_length) {
		sr_err("Short read of data.");
		return SR_ERR;
	}

	return SR_OK;
}

static int scpi_ds5000usb_libusb_open(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_ds5000usb_libusb *uscpi = scpi->priv;
	struct sr_usb_dev_inst *usb = uscpi->usb;
	struct libusb_device *dev;
	int ret;

	if (usb->devhdl)
		return SR_OK;

	if (sr_usb_open(uscpi->ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	dev = libusb_get_device(usb->devhdl);

	ret = scpi_ds5000usb_libusb_device_valid(dev);
	if (ret != SR_OK) {
		sr_err("The device doesn't look like a DS5000USB.");
		return SR_ERR;
	}

	if (libusb_kernel_driver_active(usb->devhdl, 0) == 1) {
		ret = libusb_detach_kernel_driver(usb->devhdl, 0);
		if (ret < 0) {
			sr_err("Failed to detach kernel driver: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
		uscpi->detached_kernel_driver = 1;
	}

	ret = libusb_set_configuration(usb->devhdl, 1);
	if (ret < 0) {
		sr_err("Failed to set configuration: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, 0);
	if (ret < 0) {
		sr_err("Failed to claim interface 0: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Flush whatever's buffered down the drain. */
	do {
		scpi_ds5000usb_libusb_fill_buffer (uscpi);
	} while (uscpi->response_length);

	return SR_OK;
}

static int scpi_ds5000usb_libusb_connection_id(struct sr_scpi_dev_inst *scpi,
		char **connection_id)
{
	struct scpi_ds5000usb_libusb *uscpi = scpi->priv;
	struct sr_usb_dev_inst *usb = uscpi->usb;

	*connection_id = g_strdup_printf("%s/%" PRIu8 ".%" PRIu8 "",
		scpi->prefix, usb->bus, usb->address);

	return SR_OK;
}

static int scpi_ds5000usb_libusb_source_add(struct sr_session *session,
		void *priv, int events, int timeout, sr_receive_data_callback cb,
		void *cb_data)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	(void)events;
	return usb_source_add(session, uscpi->ctx, timeout, cb, cb_data);
}

static int scpi_ds5000usb_libusb_source_remove(struct sr_session *session,
		void *priv)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	return usb_source_remove(session, uscpi->ctx);
}

static int scpi_ds5000usb_libusb_putchar (struct scpi_ds5000usb_libusb *uscpi, char c)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int ret;

	ret = scpi_ds5000usb_libusb_control_transfer(usb->devhdl, LIBUSB_ENDPOINT_IN |
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		WRITE_CHAR, c == '\x0a' ? '\x0d' : c, 0, 0, 0, TRANSFER_TIMEOUT);
	if (ret < 0) {
		sr_err("Error writing a character: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int scpi_ds5000usb_libusb_send(void *priv, const char *command)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	const char *c = command;
	int ret;

	while (*c) {
		ret = scpi_ds5000usb_libusb_putchar (uscpi, *c++);
		if (ret != SR_OK)
			return ret;
	}

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

static int scpi_ds5000usb_libusb_read_begin(void *priv)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;

	return scpi_ds5000usb_libusb_fill_buffer(uscpi);
}

static int scpi_ds5000usb_libusb_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	int read_length;

	read_length = MIN(uscpi->response_length - uscpi->response_bytes_read, maxlen);

	memcpy(buf, uscpi->buffer + uscpi->response_bytes_read, read_length);

	uscpi->response_bytes_read += read_length;

	if (uscpi->response_bytes_read == uscpi->response_length)
		scpi_ds5000usb_libusb_fill_buffer(uscpi);

	return read_length;
}

static int scpi_ds5000usb_libusb_read_complete(void *priv)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	return uscpi->response_length == 0;
}

static int scpi_ds5000usb_libusb_close(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_ds5000usb_libusb *uscpi = scpi->priv;
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int ret;

	if (!usb->devhdl)
		return SR_ERR;

	ret = libusb_release_interface(usb->devhdl, 0);
	if (ret < 0) {
		sr_err("Failed to release interface: %s.",
		       libusb_error_name(ret));
	}

	if (uscpi->detached_kernel_driver) {
		ret = libusb_attach_kernel_driver(usb->devhdl, 0);
		if (ret < 0) {
			sr_err("Failed to re-attach kernel driver: %s.",
			       libusb_error_name(ret));
		}

		uscpi->detached_kernel_driver = 0;
	}
	sr_usb_close(usb);

	return SR_OK;
}

static void scpi_ds5000usb_libusb_free(void *priv)
{
	struct scpi_ds5000usb_libusb *uscpi = priv;
	sr_usb_dev_inst_free(uscpi->usb);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_ds5000usb_libusb_dev = {
	.name          = "DS5000USB",
	.prefix        = "ds5000usb",
	.transport     = SCPI_TRANSPORT_DS5000USB,
	.priv_size     = sizeof(struct scpi_ds5000usb_libusb),
	.scan          = scpi_ds5000usb_libusb_scan,
	.dev_inst_new  = scpi_ds5000usb_libusb_dev_inst_new,
	.open          = scpi_ds5000usb_libusb_open,
	.connection_id = scpi_ds5000usb_libusb_connection_id,
	.source_add    = scpi_ds5000usb_libusb_source_add,
	.source_remove = scpi_ds5000usb_libusb_source_remove,
	.send          = scpi_ds5000usb_libusb_send,
	.read_begin    = scpi_ds5000usb_libusb_read_begin,
	.read_data     = scpi_ds5000usb_libusb_read_data,
	.read_complete = scpi_ds5000usb_libusb_read_complete,
	.close         = scpi_ds5000usb_libusb_close,
	.free          = scpi_ds5000usb_libusb_free,
};
