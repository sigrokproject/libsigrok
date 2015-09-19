/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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
#include "protocol.h"

#define SERIALCOMM "115200/8n1"

SR_PRIV struct sr_dev_driver testo_driver_info;
static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static const uint8_t TESTO_x35_REQUEST[] = { 0x12, 0, 0, 0, 1, 1, 0x55, 0xd1, 0xb7 };

static const struct testo_model models[] = {
	{ "435", 9, TESTO_x35_REQUEST },
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	GSList *conn_devices, *devices, *l;
	int ret, i;
	const char *str;
	char manufacturer[64], product[64], connection_id[64];

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	conn_devices = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key != SR_CONF_CONN)
			continue;
		str = g_variant_get_string(src->data, NULL);
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, str);
	}

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn_devices) {
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

		if ((ret = libusb_open(devlist[i], &hdl)) < 0)
			continue;

		manufacturer[0] = product[0] = '\0';
		if (des.iManufacturer && (ret = libusb_get_string_descriptor_ascii(
				hdl, des.iManufacturer, (unsigned char *) manufacturer,
				sizeof(manufacturer))) < 0) {
			sr_warn("Failed to get manufacturer string descriptor: %s.",
				libusb_error_name(ret));
		}
		if (des.iProduct && (ret = libusb_get_string_descriptor_ascii(
				hdl, des.iProduct, (unsigned char *) product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
		}
		libusb_close(hdl);

		if (strncmp(manufacturer, "testo", 5))
			continue;

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		/* Hardcode the 435 for now.*/
		if (strcmp(product, "testo 435/635/735"))
			continue;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("Testo");
		sdi->model = g_strdup("435/635/735");
		sdi->driver = di;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
		sdi->connection_id = g_strdup(connection_id);
		devc = g_malloc(sizeof(struct dev_context));
		devc->model = &models[0];
		devc->limit_msec = 0;
		devc->limit_samples = 0;
		sdi->priv = devc;
		if (testo_probe_channels(sdi) != SR_OK)
			continue;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, NULL);
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

	if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
		if (libusb_kernel_driver_active(usb->devhdl, 0) == 1) {
			if ((ret = libusb_detach_kernel_driver(usb->devhdl, 0)) < 0) {
				sr_err("Failed to detach kernel driver: %s.",
					   libusb_error_name(ret));
				return SR_ERR;
			}
		}
	}

	if ((ret = libusb_claim_interface(usb->devhdl, 0))) {
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

	libusb_release_interface(usb->devhdl, 0);
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
		return SR_OK;

	ret = dev_clear(di);
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
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	ret = SR_OK;
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
		ret = SR_ERR_NA;
	}

	return ret;
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
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void receive_data(struct sr_dev_inst *sdi, unsigned char *data, int len)
{
	struct dev_context *devc;
	int packet_size;
	uint16_t crc;

	devc = sdi->priv;

	if (devc->reply_size + len > MAX_REPLY_SIZE) {
		/* Something went very wrong. */
		sr_dbg("Receive buffer overrun.");
		devc->reply_size = 0;
		return;
	}

	memcpy(devc->reply + devc->reply_size, data, len);
	devc->reply_size += len;
	/* Sixth byte contains the length of the packet. */
	if (devc->reply_size < 7)
		return;

	packet_size = 7 + devc->reply[6] * 7 + 2;
	if (devc->reply_size < packet_size)
		return;

	if (!testo_check_packet_prefix(devc->reply, devc->reply_size))
		return;

	crc = crc16_mcrf4xx(0xffff, devc->reply, devc->reply_size - 2);
	if (crc == RL16(&devc->reply[devc->reply_size - 2])) {
		testo_receive_packet(sdi);
		devc->num_samples++;
	} else {
		sr_dbg("Packet has invalid CRC.");
	}

	devc->reply_size = 0;
	if (devc->limit_samples && devc->num_samples >= devc->limit_samples)
		dev_acquisition_stop(sdi, devc->cb_data);
	else
		testo_request_packet(sdi);

}

SR_PRIV void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	int ret;

	sdi = transfer->user_data;
	devc = sdi->priv;
	if (transfer == devc->out_transfer)
		/* Just the command acknowledgement. */
		return;

	if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
		/* USB device was unplugged. */
		dev_acquisition_stop(sdi, devc->cb_data);
	} else if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		/* First two bytes in any transfer are FTDI status bytes. */
		if (transfer->actual_length > 2)
			receive_data(sdi, transfer->buffer + 2, transfer->actual_length - 2);
	}
	/* Anything else is either an error or a timeout, which is fine:
	 * we were just going to send another transfer request anyway. */

	if (sdi->status == SR_ST_ACTIVE) {
		if ((ret = libusb_submit_transfer(transfer) != 0)) {
			sr_err("Unable to resubmit transfer: %s.",
			       libusb_error_name(ret));
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			dev_acquisition_stop(sdi, devc->cb_data);
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
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_datafeed_packet packet;
	struct sr_dev_inst *sdi;
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
			dev_acquisition_stop(sdi, devc->cb_data);
	}

	if (sdi->status == SR_ST_STOPPING) {
		usb_source_remove(sdi->session, drvc->sr_ctx);

		dev_close(sdi);

		packet.type = SR_DF_END;
		sr_session_send(sdi, &packet);
	}

	memset(&tv, 0, sizeof(struct timeval));
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
			NULL);

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	int ret;
	unsigned char *buf;

	drvc = di->context;
	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	usb = sdi->conn;
	devc->cb_data = cb_data;
	devc->end_time = 0;
	devc->num_samples = 0;
	devc->reply_size = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	usb_source_add(sdi->session, drvc->sr_ctx, 100,
			handle_events, (void *)sdi);

	if (testo_set_serial_params(usb) != SR_OK)
		return SR_ERR;

	devc->out_transfer = libusb_alloc_transfer(0);
	if (testo_request_packet(sdi) != SR_OK)
		return SR_ERR;

	buf = g_malloc(MAX_REPLY_SIZE);
	transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(transfer, usb->devhdl, EP_IN, buf,
			MAX_REPLY_SIZE, receive_transfer, (void *)sdi, 100);
	if ((ret = libusb_submit_transfer(transfer) != 0)) {
		sr_err("Unable to submit transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(transfer);
		g_free(buf);
		return SR_ERR;
	}
	devc->reply_size = 0;

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

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	sdi->status = SR_ST_STOPPING;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver testo_driver_info = {
	.name = "testo",
	.longname = "Testo",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
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
