/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
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
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi_usbtmc"

#define MAX_TRANSFER_LENGTH 2048
#define TRANSFER_TIMEOUT 1000

struct scpi_usbtmc_libusb {
	struct sr_context *ctx;
	struct sr_usb_dev_inst *usb;
	int detached_kernel_driver;
	uint8_t interface;
	uint8_t bulk_in_ep;
	uint8_t bulk_out_ep;
	uint8_t interrupt_ep;
	uint8_t usbtmc_int_cap;
	uint8_t usbtmc_dev_cap;
	uint8_t usb488_dev_cap;
	uint8_t bTag;
	uint8_t bulkin_attributes;
	uint8_t buffer[MAX_TRANSFER_LENGTH];
	int response_length;
	int response_bytes_read;
	int remaining_length;
};

/* Some USBTMC-specific enums, as defined in the USBTMC standard. */
#define SUBCLASS_USBTMC  0x03
#define USBTMC_USB488    0x01

enum {
	/* USBTMC control requests */
	INITIATE_ABORT_BULK_OUT     =   1,
	CHECK_ABORT_BULK_OUT_STATUS =   2,
	INITIATE_ABORT_BULK_IN      =   3,
	CHECK_ABORT_BULK_IN_STATUS  =   4,
	INITIATE_CLEAR              =   5,
	CHECK_CLEAR_STATUS          =   6,
	GET_CAPABILITIES            =   7,
	INDICATOR_PULSE             =  64,

	/* USB488 control requests */
	READ_STATUS_BYTE            = 128,
	REN_CONTROL                 = 160,
	GO_TO_LOCAL                 = 161,
	LOCAL_LOCKOUT               = 162,
};

/* USBTMC status codes */
#define USBTMC_STATUS_SUCCESS      0x01

/* USBTMC capabilities */
#define USBTMC_INT_CAP_LISTEN_ONLY 0x01
#define USBTMC_INT_CAP_TALK_ONLY   0x02
#define USBTMC_INT_CAP_INDICATOR   0x04

#define USBTMC_DEV_CAP_TERMCHAR    0x01

#define USB488_DEV_CAP_DT1         0x01
#define USB488_DEV_CAP_RL1         0x02
#define USB488_DEV_CAP_SR1         0x04
#define USB488_DEV_CAP_SCPI        0x08

/* Bulk messages constants */
#define USBTMC_BULK_HEADER_SIZE  12

/* Bulk MsgID values */
#define DEV_DEP_MSG_OUT         1
#define REQUEST_DEV_DEP_MSG_IN  2
#define DEV_DEP_MSG_IN          2

/* bmTransferAttributes */
#define EOM                0x01
#define TERM_CHAR_ENABLED  0x02

struct usbtmc_blacklist {
	uint16_t vid;
	uint16_t pid;
};

/* Devices that publish RL1 support, but don't support it. */
static struct usbtmc_blacklist blacklist_remote[] = {
	{ 0x1ab1, 0x0588 },  /* Rigol DS1000 series */
	{ 0x1ab1, 0x04b0 },  /* Rigol DS2000 series */
	{ 0x0957, 0x0588 },  /* Agilent DSO1000 series (rebadged Rigol DS1000) */
	{ 0x0b21, 0xffff },  /* All Yokogawa devices */
	ALL_ZERO
};

static GSList *scpi_usbtmc_libusb_scan(struct drv_context *drvc)
{
	struct libusb_device **devlist;
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *confdes;
	const struct libusb_interface_descriptor *intfdes;
	GSList *resources = NULL;
	int confidx, intfidx, ret, i;
	char *res;

	ret = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (ret < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(ret));
		return NULL;
	}
	for (i = 0; devlist[i]; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		for (confidx = 0; confidx < des.bNumConfigurations; confidx++) {
			if ((ret = libusb_get_config_descriptor(devlist[i], confidx, &confdes)) < 0) {
				sr_dbg("Failed to get configuration descriptor: %s, "
				       "ignoring device.", libusb_error_name(ret));
				break;
			}
			for (intfidx = 0; intfidx < confdes->bNumInterfaces; intfidx++) {
				intfdes = confdes->interface[intfidx].altsetting;
				if (intfdes->bInterfaceClass    != LIBUSB_CLASS_APPLICATION ||
				    intfdes->bInterfaceSubClass != SUBCLASS_USBTMC          ||
				    intfdes->bInterfaceProtocol != USBTMC_USB488)
					continue;
				sr_dbg("Found USBTMC device (VID:PID = %04x:%04x, "
				       "bus.address = %d.%d).", des.idVendor, des.idProduct,
				       libusb_get_bus_number(devlist[i]),
				       libusb_get_device_address(devlist[i]));
				res = g_strdup_printf("usbtmc/%d.%d",
				                      libusb_get_bus_number(devlist[i]),
				                      libusb_get_device_address(devlist[i]));
				resources = g_slist_append(resources, res);
			}
			libusb_free_config_descriptor(confdes);
		}
	}
	libusb_free_device_list(devlist, 1);

	sr_dbg("Found %d device(s).", g_slist_length(resources));

	return resources;
}

static int scpi_usbtmc_libusb_dev_inst_new(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm)
{
	struct scpi_usbtmc_libusb *uscpi = priv;
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

static int check_usbtmc_blacklist(struct usbtmc_blacklist *blacklist,
		uint16_t vid, uint16_t pid)
{
	int i;

	for (i = 0; blacklist[i].vid; i++) {
		if ((blacklist[i].vid == vid && blacklist[i].pid == 0xFFFF) ||
			(blacklist[i].vid == vid && blacklist[i].pid == pid))
			return TRUE;
	}

	return FALSE;
}

static int scpi_usbtmc_remote(struct scpi_usbtmc_libusb *uscpi)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	struct libusb_device *dev;
	struct libusb_device_descriptor des;
	int ret;
	uint8_t status;

	if (!(uscpi->usb488_dev_cap & USB488_DEV_CAP_RL1))
		return SR_OK;

	dev = libusb_get_device(usb->devhdl);
	libusb_get_device_descriptor(dev, &des);
	if (check_usbtmc_blacklist(blacklist_remote, des.idVendor, des.idProduct))
		return SR_OK;

	sr_dbg("Locking out local control.");
	ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_ENDPOINT_IN         |
			LIBUSB_REQUEST_TYPE_CLASS  |
			LIBUSB_RECIPIENT_INTERFACE,
			REN_CONTROL, 1,
			uscpi->interface,
			&status, 1,
			TRANSFER_TIMEOUT);
	if (ret < 0 || status != USBTMC_STATUS_SUCCESS) {
		if (ret < 0)
			sr_dbg("Failed to enter REN state: %s.", libusb_error_name(ret));
		else
			sr_dbg("Failed to enter REN state: USBTMC status %d.", status);
		return SR_ERR;
	}

	ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_ENDPOINT_IN         |
			LIBUSB_REQUEST_TYPE_CLASS  |
			LIBUSB_RECIPIENT_INTERFACE,
			LOCAL_LOCKOUT, 0,
			uscpi->interface,
			&status, 1,
			TRANSFER_TIMEOUT);
	if (ret < 0 || status != USBTMC_STATUS_SUCCESS) {
		if (ret < 0)
			sr_dbg("Failed to enter local lockout state: %s.",
					libusb_error_name(ret));
		else
			sr_dbg("Failed to enter local lockout state: USBTMC "
					"status %d.", status);
		return SR_ERR;
	}

	return SR_OK;
}

static void scpi_usbtmc_local(struct scpi_usbtmc_libusb *uscpi)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	struct libusb_device *dev;
	struct libusb_device_descriptor des;
	int ret;
	uint8_t status;

	if (!(uscpi->usb488_dev_cap & USB488_DEV_CAP_RL1))
		return;

	dev = libusb_get_device(usb->devhdl);
	libusb_get_device_descriptor(dev, &des);
	if (check_usbtmc_blacklist(blacklist_remote, des.idVendor, des.idProduct))
		return;

	sr_dbg("Returning local control.");
	ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_ENDPOINT_IN         |
			LIBUSB_REQUEST_TYPE_CLASS  |
			LIBUSB_RECIPIENT_INTERFACE,
			GO_TO_LOCAL, 0,
			uscpi->interface,
			&status, 1,
			TRANSFER_TIMEOUT);
	if (ret < 0 || status != USBTMC_STATUS_SUCCESS) {
		if (ret < 0)
			sr_dbg("Failed to clear local lockout state: %s.",
					libusb_error_name(ret));
		else
			sr_dbg("Failed to clear local lockout state: USBTMC "
					"status %d.", status);
	}

	return;
}

static int scpi_usbtmc_libusb_open(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_usbtmc_libusb *uscpi = scpi->priv;
	struct sr_usb_dev_inst *usb = uscpi->usb;
	struct libusb_device *dev;
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *confdes;
	const struct libusb_interface_descriptor *intfdes;
	const struct libusb_endpoint_descriptor *ep;
	int confidx, intfidx, epidx, config = 0, current_config;
	uint8_t capabilities[24];
	int ret, found = 0;

	if (usb->devhdl)
		return SR_OK;

	if (sr_usb_open(uscpi->ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	dev = libusb_get_device(usb->devhdl);
	libusb_get_device_descriptor(dev, &des);

	for (confidx = 0; confidx < des.bNumConfigurations; confidx++) {
		if ((ret = libusb_get_config_descriptor(dev, confidx, &confdes)) < 0) {
			sr_dbg("Failed to get configuration descriptor: %s, "
			       "ignoring device.", libusb_error_name(ret));
			continue;
		}
		for (intfidx = 0; intfidx < confdes->bNumInterfaces; intfidx++) {
			intfdes = confdes->interface[intfidx].altsetting;
			if (intfdes->bInterfaceClass    != LIBUSB_CLASS_APPLICATION ||
			    intfdes->bInterfaceSubClass != SUBCLASS_USBTMC          ||
			    intfdes->bInterfaceProtocol != USBTMC_USB488)
				continue;
			uscpi->interface = intfdes->bInterfaceNumber;
			config = confdes->bConfigurationValue;
			sr_dbg("Interface %d configuration %d.", uscpi->interface, config);
			for (epidx = 0; epidx < intfdes->bNumEndpoints; epidx++) {
				ep = &intfdes->endpoint[epidx];
				if (ep->bmAttributes == LIBUSB_TRANSFER_TYPE_BULK &&
				    !(ep->bEndpointAddress & (LIBUSB_ENDPOINT_DIR_MASK))) {
					uscpi->bulk_out_ep = ep->bEndpointAddress;
					sr_dbg("Bulk OUT EP %d", uscpi->bulk_out_ep);
				}
				if (ep->bmAttributes == LIBUSB_TRANSFER_TYPE_BULK &&
				    ep->bEndpointAddress & (LIBUSB_ENDPOINT_DIR_MASK)) {
					uscpi->bulk_in_ep = ep->bEndpointAddress;
					sr_dbg("Bulk IN EP %d", uscpi->bulk_in_ep & 0x7f);
				}
				if (ep->bmAttributes == LIBUSB_TRANSFER_TYPE_INTERRUPT &&
				    ep->bEndpointAddress & (LIBUSB_ENDPOINT_DIR_MASK)) {
					uscpi->interrupt_ep = ep->bEndpointAddress;
					sr_dbg("Interrupt EP %d", uscpi->interrupt_ep & 0x7f);
				}
			}
			found = 1;
		}
		libusb_free_config_descriptor(confdes);
		if (found)
			break;
	}

	if (!found) {
		sr_err("Failed to find USBTMC interface.");
		return SR_ERR;
	}

	if (libusb_kernel_driver_active(usb->devhdl, uscpi->interface) == 1) {
		if ((ret = libusb_detach_kernel_driver(usb->devhdl,
		                                       uscpi->interface)) < 0) {
			sr_err("Failed to detach kernel driver: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
		uscpi->detached_kernel_driver = 1;
	}

	if (libusb_get_configuration(usb->devhdl, &current_config) == 0
	    && current_config != config) {
		if ((ret = libusb_set_configuration(usb->devhdl, config)) < 0) {
			sr_err("Failed to set configuration: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
	}

	if ((ret = libusb_claim_interface(usb->devhdl, uscpi->interface)) < 0) {
		sr_err("Failed to claim interface: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Get capabilities. */
	ret = libusb_control_transfer(usb->devhdl,
	                              LIBUSB_ENDPOINT_IN         |
	                              LIBUSB_REQUEST_TYPE_CLASS  |
	                              LIBUSB_RECIPIENT_INTERFACE,
	                              GET_CAPABILITIES, 0,
	                              uscpi->interface,
	                              capabilities, sizeof(capabilities),
	                              TRANSFER_TIMEOUT);
	if (ret == sizeof(capabilities)) {
		uscpi->usbtmc_int_cap = capabilities[ 4];
		uscpi->usbtmc_dev_cap = capabilities[ 5];
		uscpi->usb488_dev_cap = capabilities[15];
	}
	sr_dbg("Device capabilities: %s%s%s%s%s, %s, %s",
	       uscpi->usb488_dev_cap & USB488_DEV_CAP_SCPI       ? "SCPI, "    : "",
	       uscpi->usbtmc_dev_cap & USBTMC_DEV_CAP_TERMCHAR   ? "TermChar, ": "",
	       uscpi->usbtmc_int_cap & USBTMC_INT_CAP_LISTEN_ONLY? "L3, " :
	       uscpi->usbtmc_int_cap & USBTMC_INT_CAP_TALK_ONLY  ? ""     : "L4, ",
	       uscpi->usbtmc_int_cap & USBTMC_INT_CAP_TALK_ONLY  ? "T5, " :
	       uscpi->usbtmc_int_cap & USBTMC_INT_CAP_LISTEN_ONLY? ""     : "T6, ",
	       uscpi->usb488_dev_cap & USB488_DEV_CAP_SR1        ? "SR1"  : "SR0",
	       uscpi->usb488_dev_cap & USB488_DEV_CAP_RL1        ? "RL1"  : "RL0",
	       uscpi->usb488_dev_cap & USB488_DEV_CAP_DT1        ? "DT1"  : "DT0");

	scpi_usbtmc_remote(uscpi);

	return SR_OK;
}

static int scpi_usbtmc_libusb_source_add(struct sr_session *session,
		void *priv, int events, int timeout, sr_receive_data_callback cb,
		void *cb_data)
{
	struct scpi_usbtmc_libusb *uscpi = priv;
	(void)events;
	return usb_source_add(session, uscpi->ctx, timeout, cb, cb_data);
}

static int scpi_usbtmc_libusb_source_remove(struct sr_session *session,
		void *priv)
{
	struct scpi_usbtmc_libusb *uscpi = priv;
	return usb_source_remove(session, uscpi->ctx);
}

static void usbtmc_bulk_out_header_write(void *header, uint8_t MsgID,
                                         uint8_t bTag,
                                         uint32_t TransferSize,
                                         uint8_t bmTransferAttributes,
                                         char TermChar)
{
	  W8(header+ 0, MsgID);
	  W8(header+ 1, bTag);
	  W8(header+ 2, ~bTag);
	  W8(header+ 3, 0);
	WL32(header+ 4, TransferSize);
	  W8(header+ 8, bmTransferAttributes);
	  W8(header+ 9, TermChar);
	WL16(header+10, 0);
}

static int usbtmc_bulk_in_header_read(void *header, uint8_t MsgID,
                                      unsigned char bTag,
                                      int32_t *TransferSize,
                                      uint8_t *bmTransferAttributes)
{
	if (R8(header+0) != MsgID ||
	    R8(header+1) != bTag  ||
	    R8(header+2) != (unsigned char)~bTag)
		return SR_ERR;
	if (TransferSize)
		*TransferSize = RL32(header+4);
	if (bmTransferAttributes)
		*bmTransferAttributes = R8(header+8);
	return SR_OK;
}

static int scpi_usbtmc_bulkout(struct scpi_usbtmc_libusb *uscpi,
                               uint8_t msg_id, const void *data, int32_t size,
                               uint8_t transfer_attributes)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int padded_size, ret, transferred;

	if (data && size+USBTMC_BULK_HEADER_SIZE+3 > (int)sizeof(uscpi->buffer)) {
		sr_err("USBTMC bulk out transfer is too big.");
		return SR_ERR;
	}

	uscpi->bTag++;
	uscpi->bTag += !uscpi->bTag;  /* bTag == 0 is invalid so avoid it. */

	usbtmc_bulk_out_header_write(uscpi->buffer, msg_id, uscpi->bTag,
	                             size, transfer_attributes, 0);
	if (data)
		memcpy(uscpi->buffer+USBTMC_BULK_HEADER_SIZE, data, size);
	else
		size = 0;
	size += USBTMC_BULK_HEADER_SIZE;
	padded_size = (size + 3) & ~0x3;
	memset(uscpi->buffer+size, 0, padded_size - size);

	ret = libusb_bulk_transfer(usb->devhdl, uscpi->bulk_out_ep,
	                           uscpi->buffer, padded_size, &transferred,
	                           TRANSFER_TIMEOUT);
	if (ret < 0) {
		sr_err("USBTMC bulk out transfer error: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	if (transferred < padded_size) {
		sr_dbg("USBTMC bulk out partial transfer (%d/%d bytes).",
		       transferred, padded_size);
		return SR_ERR;
	}

	return transferred - USBTMC_BULK_HEADER_SIZE;
}

static int scpi_usbtmc_bulkin_start(struct scpi_usbtmc_libusb *uscpi,
                                    uint8_t msg_id, void *data, int32_t size,
                                    uint8_t *transfer_attributes)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int ret, transferred, message_size;

	ret = libusb_bulk_transfer(usb->devhdl, uscpi->bulk_in_ep, data, size,
	                           &transferred, TRANSFER_TIMEOUT);
	if (ret < 0) {
		sr_err("USBTMC bulk in transfer error: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	if (usbtmc_bulk_in_header_read(data, msg_id, uscpi->bTag, &message_size,
	                               transfer_attributes) != SR_OK) {
		sr_err("USBTMC invalid bulk in header.");
		return SR_ERR;
	}

	message_size += USBTMC_BULK_HEADER_SIZE;
	uscpi->response_length = MIN(transferred, message_size);
	uscpi->response_bytes_read = USBTMC_BULK_HEADER_SIZE;
	uscpi->remaining_length = message_size - uscpi->response_length;

	return transferred - USBTMC_BULK_HEADER_SIZE;
}

static int scpi_usbtmc_bulkin_continue(struct scpi_usbtmc_libusb *uscpi,
                                       void *data, int size)
{
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int ret, transferred;

	ret = libusb_bulk_transfer(usb->devhdl, uscpi->bulk_in_ep, data, size,
	                           &transferred, TRANSFER_TIMEOUT);
	if (ret < 0) {
		sr_err("USBTMC bulk in transfer error: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	uscpi->response_length = MIN(transferred, uscpi->remaining_length);
	uscpi->response_bytes_read = 0;
	uscpi->remaining_length -= uscpi->response_length;

	return transferred;
}

static int scpi_usbtmc_libusb_send(void *priv, const char *command)
{
	struct scpi_usbtmc_libusb *uscpi = priv;

	if (scpi_usbtmc_bulkout(uscpi, DEV_DEP_MSG_OUT,
	                        command, strlen(command), EOM) <= 0)
		return SR_ERR;

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

static int scpi_usbtmc_libusb_read_begin(void *priv)
{
	struct scpi_usbtmc_libusb *uscpi = priv;

	uscpi->remaining_length = 0;

	if (scpi_usbtmc_bulkout(uscpi, REQUEST_DEV_DEP_MSG_IN,
	    NULL, INT32_MAX, 0) < 0)
		return SR_ERR;
	if (scpi_usbtmc_bulkin_start(uscpi, DEV_DEP_MSG_IN,
	                             uscpi->buffer, sizeof(uscpi->buffer),
	                             &uscpi->bulkin_attributes) < 0)
		return SR_ERR;

	return SR_OK;
}

static int scpi_usbtmc_libusb_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_usbtmc_libusb *uscpi = priv;
	int read_length;

	if (uscpi->response_bytes_read >= uscpi->response_length) {
		if (uscpi->remaining_length > 0) {
			if (scpi_usbtmc_bulkin_continue(uscpi, uscpi->buffer,
			                                sizeof(uscpi->buffer)) <= 0)
				return SR_ERR;
		} else {
			if (uscpi->bulkin_attributes & EOM)
				return SR_ERR;
			if (scpi_usbtmc_libusb_read_begin(uscpi) < 0)
				return SR_ERR;
		}
	}

	read_length = MIN(uscpi->response_length - uscpi->response_bytes_read, maxlen);

	memcpy(buf, uscpi->buffer + uscpi->response_bytes_read, read_length);

	uscpi->response_bytes_read += read_length;

	return read_length;
}

static int scpi_usbtmc_libusb_read_complete(void *priv)
{
	struct scpi_usbtmc_libusb *uscpi = priv;
	return uscpi->response_bytes_read >= uscpi->response_length &&
	       uscpi->remaining_length <= 0 &&
	       uscpi->bulkin_attributes & EOM;
}

static int scpi_usbtmc_libusb_close(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_usbtmc_libusb *uscpi = scpi->priv;
	struct sr_usb_dev_inst *usb = uscpi->usb;
	int ret;

	if (!usb->devhdl)
		return SR_ERR;

	scpi_usbtmc_local(uscpi);

	if ((ret = libusb_release_interface(usb->devhdl, uscpi->interface)) < 0)
		sr_err("Failed to release interface: %s.",
		       libusb_error_name(ret));
	
	if (uscpi->detached_kernel_driver) {
		if ((ret = libusb_attach_kernel_driver(usb->devhdl,
						uscpi->interface)) < 0)
			sr_err("Failed to re-attach kernel driver: %s.",
			       libusb_error_name(ret));

		uscpi->detached_kernel_driver = 0;
	}
	sr_usb_close(usb);

	return SR_OK;
}

static void scpi_usbtmc_libusb_free(void *priv)
{
	struct scpi_usbtmc_libusb *uscpi = priv;
	sr_usb_dev_inst_free(uscpi->usb);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_usbtmc_libusb_dev = {
	.name          = "USBTMC",
	.prefix        = "usbtmc",
	.priv_size     = sizeof(struct scpi_usbtmc_libusb),
	.scan          = scpi_usbtmc_libusb_scan,
	.dev_inst_new  = scpi_usbtmc_libusb_dev_inst_new,
	.open          = scpi_usbtmc_libusb_open,
	.source_add    = scpi_usbtmc_libusb_source_add,
	.source_remove = scpi_usbtmc_libusb_source_remove,
	.send          = scpi_usbtmc_libusb_send,
	.read_begin    = scpi_usbtmc_libusb_read_begin,
	.read_data     = scpi_usbtmc_libusb_read_data,
	.read_complete = scpi_usbtmc_libusb_read_complete,
	.close         = scpi_usbtmc_libusb_close,
	.free          = scpi_usbtmc_libusb_free,
};
