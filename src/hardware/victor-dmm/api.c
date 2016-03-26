/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define VICTOR_VID 0x1244
#define VICTOR_PID 0xd237
#define VICTOR_VENDOR "Victor"
#define VICTOR_INTERFACE 0
#define VICTOR_ENDPOINT (LIBUSB_ENDPOINT_IN | 1)

SR_PRIV struct sr_dev_driver victor_dmm_driver_info;
static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	GSList *devices;
	int i;
	char connection_id[64];

	(void)options;

	drvc = di->context;

	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != VICTOR_VID || des.idProduct != VICTOR_PID)
			continue;

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(VICTOR_VENDOR);
		sdi->driver = di;
		sdi->connection_id = g_strdup(connection_id);
		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;

		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
		sdi->inst_type = SR_INST_USB;

		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct sr_usb_dev_inst *usb;
	libusb_device **devlist;
	int ret, i;
	char connection_id[64];

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));
		if (strcmp(sdi->connection_id, connection_id))
			continue;
		if ((ret = libusb_open(devlist[i], &usb->devhdl))) {
			sr_err("Failed to open device: %s.", libusb_error_name(ret));
			return SR_ERR;
		}
		break;
	}
	libusb_free_device_list(devlist, 1);
	if (!devlist[i]) {
		sr_err("Device not found.");
		return SR_ERR;
	}

	/* The device reports as HID class, so the kernel would have
	 * claimed it. */
	if (libusb_kernel_driver_active(usb->devhdl, 0) == 1) {
		if ((ret = libusb_detach_kernel_driver(usb->devhdl, 0)) < 0) {
			sr_err("Failed to detach kernel driver: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
	}

	if ((ret = libusb_claim_interface(usb->devhdl,
			VICTOR_INTERFACE))) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct sr_usb_dev_inst *usb;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (!usb->devhdl)
		/*  Nothing to do. */
		return SR_OK;

	libusb_release_interface(usb->devhdl, VICTOR_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->context))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	ret = std_dev_clear(di, NULL);
	g_free(drvc);

	return ret;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_usb_dev_inst *usb;
	char str[128];

	(void)cg;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		snprintf(str, 128, "%d.%d", usb->bus, usb->address);
		*data = g_variant_new_string(str);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_dev_driver *di = sdi->driver;
	struct dev_context *devc;
	gint64 now;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		now = g_get_monotonic_time() / 1000;
		devc->end_time = now + devc->limit_msec;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
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
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	int ret;

	sdi = transfer->user_data;
	devc = sdi->priv;
	if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		/* USB device was unplugged. */
		dev_acquisition_stop(sdi, sdi);
	} else if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		sr_dbg("Got %d-byte packet.", transfer->actual_length);
		if (transfer->actual_length == DMM_DATA_SIZE) {
			victor_dmm_receive_data(sdi, transfer->buffer);
			if (devc->limit_samples) {
				if (devc->num_samples >= devc->limit_samples)
					dev_acquisition_stop(sdi, sdi);
			}
		}
	}
	/* Anything else is either an error or a timeout, which is fine:
	 * we were just going to send another transfer request anyway. */

	if (sdi->status == SR_ST_ACTIVE) {
		/* Send the same request again. */
		if ((ret = libusb_submit_transfer(transfer) != 0)) {
			sr_err("Unable to resubmit transfer: %s.",
			       libusb_error_name(ret));
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			dev_acquisition_stop(sdi, sdi);
		}
	} else {
		/* This was the last transfer we're going to receive, so
		 * clean up now. */
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
	}
}

static int handle_events(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct sr_dev_driver *di;
	struct timeval tv;
	gint64 now;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	if (devc->limit_msec) {
		now = g_get_monotonic_time() / 1000;
		if (now > devc->end_time)
			dev_acquisition_stop(sdi, sdi);
	}

	if (sdi->status == SR_ST_STOPPING) {
		usb_source_remove(sdi->session, drvc->sr_ctx);
		dev_close(sdi);
		std_session_send_df_end(sdi, LOG_PREFIX);
	}

	memset(&tv, 0, sizeof(struct timeval));
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
					       NULL);

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_dev_driver *di = sdi->driver;
	struct dev_context *devc;
	struct drv_context *drvc = di->context;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	int ret;
	unsigned char *buf;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	usb = sdi->conn;
	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	usb_source_add(sdi->session, drvc->sr_ctx, 100,
			handle_events, (void *)sdi);

	buf = g_malloc(DMM_DATA_SIZE);
	transfer = libusb_alloc_transfer(0);
	/* Each transfer request gets 100ms to arrive before it's restarted.
	 * The device only sends 1 transfer/second no matter how many
	 * times you ask, but we want to keep step with the USB events
	 * handling above. */
	libusb_fill_interrupt_transfer(transfer, usb->devhdl,
			VICTOR_ENDPOINT, buf, DMM_DATA_SIZE, receive_transfer,
			cb_data, 100);
	if ((ret = libusb_submit_transfer(transfer) != 0)) {
		sr_err("Unable to submit transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(transfer);
		g_free(buf);
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_dev_driver *di = sdi->driver;
	(void)cb_data;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device not active, can't stop acquisition.");
		return SR_ERR;
	}

	sdi->status = SR_ST_STOPPING;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver victor_dmm_driver_info = {
	.name = "victor-dmm",
	.longname = "Victor DMMs",
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
	.context = NULL,
};
