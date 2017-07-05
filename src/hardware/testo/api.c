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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET | SR_CONF_GET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET | SR_CONF_GET,
};

static const uint8_t TESTO_x35_REQUEST[] = { 0x12, 0, 0, 0, 1, 1, 0x55, 0xd1, 0xb7 };

static const struct testo_model models[] = {
	{ "435", 9, TESTO_x35_REQUEST },
};

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
		sr_sw_limits_init(&devc->sw_limits);
		sdi->priv = devc;
		if (testo_probe_channels(sdi) != SR_OK)
			continue;
		devices = g_slist_append(devices, sdi);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct sr_usb_dev_inst *usb;
	int ret;

	usb = sdi->conn;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (ret != SR_OK)
		return ret;

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
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;
	if (!usb->devhdl)
		/* Nothing to do. */
		return SR_OK;

	libusb_release_interface(usb->devhdl, 0);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
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
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_get(&devc->sw_limits, key, data);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	return sr_sw_limits_config_set(&devc->sw_limits, key, data);
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
		sr_sw_limits_update_samples_read(&devc->sw_limits, 1);
	} else {
		sr_dbg("Packet has invalid CRC.");
	}

	devc->reply_size = 0;
	if (sr_sw_limits_check(&devc->sw_limits))
		sr_dev_acquisition_stop(sdi);
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
		sr_dev_acquisition_stop(sdi);
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
			sr_dev_acquisition_stop(sdi);
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
	struct sr_dev_inst *sdi;
	struct timeval tv;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	if (sr_sw_limits_check(&devc->sw_limits))
		sr_dev_acquisition_stop(sdi);

	if (sdi->status == SR_ST_STOPPING) {
		usb_source_remove(sdi->session, drvc->sr_ctx);
		dev_close(sdi);
		std_session_send_df_end(sdi);
	}

	memset(&tv, 0, sizeof(struct timeval));
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
			NULL);

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
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


	devc = sdi->priv;
	usb = sdi->conn;
	devc->reply_size = 0;

	std_session_send_df_header(sdi);

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

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_STOPPING;

	return SR_OK;
}

static struct sr_dev_driver testo_driver_info = {
	.name = "testo",
	.longname = "Testo",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(testo_driver_info);
