/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#include <string.h>
#include "protocol.h"

static const uint32_t devopts[] = {
	SR_CONF_THERMOMETER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"T1", "T2", "T1-T2",
};

static const char *data_sources[] = {
	"Live",
	"Memory",
};

SR_PRIV struct sr_dev_driver uni_t_ut32x_driver_info;

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	GSList *usb_devices, *devices, *l;
	unsigned int i;
	const char *conn;

	drvc = di->priv;
	drvc->instances = NULL;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	devices = NULL;
	if ((usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn))) {
		/* We have a list of sr_usb_dev_inst matching the connection
		 * string. Wrap them in sr_dev_inst and we're done. */
		for (l = usb_devices; l; l = l->next) {
			sdi = g_malloc0(sizeof(struct sr_dev_inst));
			sdi->status = SR_ST_INACTIVE;
			sdi->vendor = g_strdup(VENDOR);
			sdi->model = g_strdup(MODEL);
			sdi->driver = di;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = l->data;
			for (i = 0; i < ARRAY_SIZE(channel_names); i++)
				sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
						channel_names[i]);
			devc = g_malloc0(sizeof(struct dev_context));
			sdi->priv = devc;
			devc->limit_samples = 0;
			devc->data_source = DEFAULT_DATA_SOURCE;
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
		}
		g_slist_free(usb_devices);
	} else
		g_slist_free_full(usb_devices, g_free);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	if (!(drvc = di->priv)) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

/*
 * The libusb 1.0.9 Darwin backend is broken: it can report a kernel
 * driver being active, but detaching it always returns an error.
 */
#if !defined(__APPLE__)
	if (libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
		if ((ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE)) < 0) {
			sr_err("failed to detach kernel driver: %s",
					libusb_error_name(ret));
			return SR_ERR;
		}
	}
#endif

	if ((ret = libusb_set_configuration(usb->devhdl, USB_CONFIGURATION))) {
		sr_err("Failed to set configuration: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if ((ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE))) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sdi->status = SR_ST_ACTIVE;

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct sr_usb_dev_inst *usb;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;
	if (!usb->devhdl)
		/*  Nothing to do. */
		return SR_OK;

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	ret = std_dev_clear(di, NULL);
	g_free(drvc);

	return ret;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else
			*data = g_variant_new_string("Memory");
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
	const char *tmp_str;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "Live"))
			devc->data_source = DATA_SOURCE_LIVE;
		else if (!strcmp(tmp_str, "Memory"))
			devc->data_source = DATA_SOURCE_MEMORY;
		else
			return SR_ERR;
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
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int len, ret;
	unsigned char cmd[2];

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	devc->cb_data = cb_data;
	devc->num_samples = 0;
	devc->packet_len = 0;

	/* Configure serial port parameters on USB-UART interface
	 * chip inside the device (just baudrate 2400 actually). */
	cmd[0] = 0x09;
	cmd[1] = 0x60;
	ret = libusb_control_transfer(usb->devhdl, 0x21, 0x09, 0x0300, 0x00,
			cmd, 2, 5);
	if (ret != 2) {
		sr_dbg("Failed to configure CH9325: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	if (!(devc->xfer = libusb_alloc_transfer(0)))
		return SR_ERR;

	/* Length of payload to follow. */
	cmd[0] = 0x01;
	if (devc->data_source == DATA_SOURCE_LIVE)
		cmd[1] = CMD_GET_LIVE;
	else
		cmd[1] = CMD_GET_STORED;

	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, cmd, 2, &len, 5);
	if (ret != 0 || len != 2) {
		sr_dbg("Failed to start acquisition: %s", libusb_error_name(ret));
		libusb_free_transfer(devc->xfer);
		return SR_ERR;
	}

	libusb_fill_bulk_transfer(devc->xfer, usb->devhdl, EP_IN, devc->buf,
			8, uni_t_ut32x_receive_transfer, (void *)sdi, 15);
	if (libusb_submit_transfer(devc->xfer) != 0) {
		libusb_free_transfer(devc->xfer);
		return SR_ERR;
	}

	usb_source_add(sdi->session, drvc->sr_ctx, 10,
			uni_t_ut32x_handle_events, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Signal USB transfer handler to clean up and stop. */
	sdi->status = SR_ST_STOPPING;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver uni_t_ut32x_driver_info = {
	.name = "uni-t-ut32x",
	.longname = "UNI-T UT32x",
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
