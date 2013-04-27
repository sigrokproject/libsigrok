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

#include <glib.h>
#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define VICTOR_VID 0x1244
#define VICTOR_PID 0xd237
#define VICTOR_VENDOR "Victor"
#define VICTOR_INTERFACE 0
#define VICTOR_ENDPOINT LIBUSB_ENDPOINT_IN | 1

SR_PRIV struct sr_dev_driver victor_dmm_driver_info;
static struct sr_dev_driver *di = &victor_dmm_driver_info;
static int hw_dev_close(struct sr_dev_inst *sdi);
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
};

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data))
			continue;
		if (!(devc = sdi->priv))
			continue;
		hw_dev_close(sdi);
		sr_usb_dev_inst_free(sdi->conn);
		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
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
	struct sr_probe *probe;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	GSList *devices;
	int ret, devcnt, i;

	(void)options;

	drvc = di->priv;

	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des)) != 0) {
			sr_warn("Failed to get device descriptor: %s",
					libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != VICTOR_VID || des.idProduct != VICTOR_PID)
			continue;

		devcnt = g_slist_length(drvc->instances);
		if (!(sdi = sr_dev_inst_new(devcnt, SR_ST_INACTIVE,
				VICTOR_VENDOR, NULL, NULL)))
			return NULL;
		sdi->driver = di;

		if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
			return NULL;
		sdi->priv = devc;

		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->probes = g_slist_append(NULL, probe);

		if (!(sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL)))
			return NULL;
		sdi->inst_type = SR_INST_USB;

		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
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
	struct drv_context *drvc = di->priv;
	struct sr_usb_dev_inst *usb;
	libusb_device **devlist;
	int ret, i;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (libusb_get_bus_number(devlist[i]) != usb->bus
				|| libusb_get_device_address(devlist[i]) != usb->address)
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

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	if (!di->priv) {
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

static int hw_cleanup(void)
{
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	clear_instances();
	g_free(drvc);
	di->priv = NULL;

	return SR_OK;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	char str[128];

	switch (id) {
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

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gint64 now;
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	ret = SR_OK;
	switch (id) {
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		now = g_get_monotonic_time() / 1000;
		devc->end_time = now + devc->limit_msec;
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{

	(void)sdi;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void receive_transfer(struct libusb_transfer *transfer)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	int ret;

	sdi = transfer->user_data;
	devc = sdi->priv;
	if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		/* USB device was unplugged. */
		hw_dev_acquisition_stop(sdi, sdi);
	} else if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		sr_dbg("Got %d-byte packet.", transfer->actual_length);
		if (transfer->actual_length == DMM_DATA_SIZE) {
			victor_dmm_receive_data(sdi, transfer->buffer);
			if (devc->limit_samples) {
				if (devc->num_samples >= devc->limit_samples)
					hw_dev_acquisition_stop(sdi, sdi);
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
			hw_dev_acquisition_stop(sdi, sdi);
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
	struct drv_context *drvc = di->priv;
	struct sr_datafeed_packet packet;
	struct sr_dev_inst *sdi;
	struct timeval tv;
	gint64 now;
	int i;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	if (devc->limit_msec) {
		now = g_get_monotonic_time() / 1000;
		if (now > devc->end_time)
			hw_dev_acquisition_stop(sdi, sdi);
	}

	if (sdi->status == SR_ST_STOPPING) {
		for (i = 0; devc->usbfd[i] != -1; i++)
			sr_source_remove(devc->usbfd[i]);

		hw_dev_close(sdi);

		packet.type = SR_DF_END;
		sr_session_send(cb_data, &packet);
	}

	memset(&tv, 0, sizeof(struct timeval));
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
					       NULL);

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;
	struct drv_context *drvc = di->priv;
	const struct libusb_pollfd **pfd;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	int ret, i;
	unsigned char *buf;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	usb = sdi->conn;
	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	pfd = libusb_get_pollfds(drvc->sr_ctx->libusb_ctx);
	for (i = 0; pfd[i]; i++) {
		/* Handle USB events every 100ms, for decent latency. */
		sr_source_add(pfd[i]->fd, pfd[i]->events, 100,
				handle_events, (void *)sdi);
		/* We'll need to remove this fd later. */
		devc->usbfd[i] = pfd[i]->fd;
	}
	devc->usbfd[i] = -1;

	buf = g_try_malloc(DMM_DATA_SIZE);
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

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	if (!di->priv) {
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
