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
#include "protocol.h"

#define BRYMEN_BC86X "0820.0001"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *usb_devices, *devices, *l;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	const char *conn;

	drvc = di->context;

	conn = BRYMEN_BC86X;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	devices = NULL;
	if (!(usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn)))
		return NULL;

	for (l = usb_devices; l; l = l->next) {
		usb = l->data;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("Brymen");
		sdi->model = g_strdup("BM869");
		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
		sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "P2");

		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;

		sr_sw_limits_init(&devc->sw_limits);

		devices = g_slist_append(devices, sdi);
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;

	if ((ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb)) < 0)
		return SR_ERR;

	if (libusb_kernel_driver_active(usb->devhdl, 0) == 1) {
		ret = libusb_detach_kernel_driver(usb->devhdl, 0);
		if (ret < 0) {
			sr_err("Failed to detach kernel driver: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
		devc->detached_kernel_driver = 1;
	}

	if ((ret = libusb_claim_interface(usb->devhdl, 0)) < 0) {
		sr_err("Failed to claim interface 0: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;

	if (!usb->devhdl)
		return SR_OK;

	if ((ret = libusb_release_interface(usb->devhdl, 0)))
		sr_err("Failed to release interface 0: %s.\n", libusb_error_name(ret));

	if (!ret && devc->detached_kernel_driver) {
		if ((ret = libusb_attach_kernel_driver(usb->devhdl, 0)))
			sr_err("Failed to attach kernel driver: %s.\n",
			       libusb_error_name(ret));
		else
			devc->detached_kernel_driver = 0;
	}

	libusb_close(usb->devhdl);

	return (ret == 0) ? SR_OK : SR_ERR;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	return sr_sw_limits_config_get(&devc->sw_limits, key, data);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->sw_limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	std_session_send_df_header(sdi);

	sr_session_source_add(sdi->session, -1, 0, 10,
			brymen_bm86x_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	std_session_send_df_end(sdi);

	sr_session_source_remove(sdi->session, -1);

	return SR_OK;
}

static struct sr_dev_driver brymen_bm86x_driver_info = {
	.name = "brymen-bm86x",
	.longname = "Brymen BM86X",
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
SR_REGISTER_DEV_DRIVER(brymen_bm86x_driver_info);
