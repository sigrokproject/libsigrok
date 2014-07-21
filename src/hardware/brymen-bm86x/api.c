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

#include "protocol.h"

#define BRYMEN_BC86X "0820.0001"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

SR_PRIV struct sr_dev_driver brymen_bm86x_driver_info;
static struct sr_dev_driver *di = &brymen_bm86x_driver_info;

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	GSList *usb_devices, *devices, *l;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct sr_channel *ch;
	const char *conn;

	drvc = di->priv;
	drvc->instances = NULL;

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
	if (!(usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn))) {
		g_slist_free_full(usb_devices, g_free);
		return NULL;
	}

	for (l = usb_devices; l; l = l->next) {
		usb = l->data;

		if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
		                            "Brymen", "BM869", NULL))) {
			sr_err("sr_dev_inst_new returned NULL.");
			return NULL;
		}

		if (!(devc = g_try_malloc0(sizeof(*devc)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}

		sdi->priv = devc;
		sdi->driver = di;
		if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->channels = g_slist_append(sdi->channels, ch);
		if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, "P2")))
			return NULL;
		sdi->channels = g_slist_append(sdi->channels, ch);

		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;

		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc = di->priv;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;

	if ((ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb)) == SR_OK)
		sdi->status = SR_ST_ACTIVE;

	/* Detach kernel drivers which grabbed this device (if any). */
	if (libusb_kernel_driver_active(usb->devhdl, 0) == 1) {
		ret = libusb_detach_kernel_driver(usb->devhdl, 0);
		if (ret < 0) {
			sr_err("Failed to detach kernel driver: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
		devc->detached_kernel_driver = 1;
		sr_dbg("Successfully detached kernel driver.");
	} else {
		sr_dbg("No need to detach a kernel driver.");
	}

	/* Claim interface 0. */
	if ((ret = libusb_claim_interface(usb->devhdl, 0)) < 0) {
		sr_err("Failed to claim interface 0: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Successfully claimed interface 0.");

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;

	if ((ret = libusb_release_interface(usb->devhdl, 0)))
		sr_err("Failed to release interface 0: %s.\n", libusb_error_name(ret));
	else
		sr_dbg("Successfully released interface 0.\n");

	if (!ret && devc->detached_kernel_driver) {
		if ((ret = libusb_attach_kernel_driver(usb->devhdl, 0))) {
			sr_err("Failed to attach kernel driver: %s.\n",
			       libusb_error_name(ret));
		} else {
			devc->detached_kernel_driver = 0;
			sr_dbg("Successfully attached kernel driver.\n");
		}
	}

	libusb_close(usb->devhdl);

	sdi->status = SR_ST_INACTIVE;

	return ret;
}

static int cleanup(void)
{
	return std_dev_clear(di, NULL);
}

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".", devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.", devc->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

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

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->start_time = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);

	sr_session_source_add(sdi->session, 0, 0, 10,
			brymen_bm86x_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Send end packet to the session bus. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	sr_session_source_remove(sdi->session, 0);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver brymen_bm86x_driver_info = {
	.name = "brymen-bm86x",
	.longname = "Brymen BM86X",
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
