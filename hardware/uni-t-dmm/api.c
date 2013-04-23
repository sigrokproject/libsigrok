/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define UNI_T_UT_D04_NEW "1a86.e008"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

SR_PRIV struct sr_dev_driver uni_t_ut61d_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc820_driver_info;

static struct sr_dev_driver *di_ut61d = &uni_t_ut61d_driver_info;
static struct sr_dev_driver *di_vc820 = &voltcraft_vc820_driver_info;

/* After hw_init() this will point to a device-specific entry (see above). */
static struct sr_dev_driver *di = NULL;

static int clear_instances(void)
{
	/* TODO: Use common code later. */

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx, int dmm)
{
	if (dmm == UNI_T_UT61D)
		di = di_ut61d;
	else if (dmm == VOLTCRAFT_VC820)
		di = di_vc820;
	sr_dbg("Selected '%s' subdriver.", di->name);

	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static int hw_init_ut61d(struct sr_context *sr_ctx)
{
	return hw_init(sr_ctx, UNI_T_UT61D);
}

static int hw_init_vc820(struct sr_context *sr_ctx)
{
	return hw_init(sr_ctx, VOLTCRAFT_VC820);
}

static GSList *hw_scan(GSList *options)
{
	GSList *usb_devices, *devices, *l;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct sr_probe *probe;
	const char *conn;

	(void)options;

	drvc = di->priv;

	/* USB scan is always authoritative. */
	clear_instances();

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
		conn = UNI_T_UT_D04_NEW;

	devices = NULL;
	if (!(usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn))) {
		g_slist_free_full(usb_devices, g_free);
		return NULL;
	}

	for (l = usb_devices; l; l = l->next) {
		usb = l->data;

		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}

		if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
				di->longname, NULL, NULL))) {
			sr_err("sr_dev_inst_new returned NULL.");
			return NULL;
		}
		sdi->priv = devc;
		sdi->driver = di;
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);

		devc->usb = usb;

		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct dev_context *devc;
    int ret;

	drvc = di->priv;
	devc = sdi->priv;

    if ((ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, devc->usb)) == SR_OK)
        sdi->status = SR_ST_ACTIVE;

    return ret;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO */

    sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	switch (id) {
	case SR_CONF_LIMIT_MSEC:
		/* TODO: Not yet implemented. */
		if (g_variant_get_uint64(data) == 0) {
			sr_err("Time limit cannot be 0.");
			return SR_ERR;
		}
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (g_variant_get_uint64(data) == 0) {
			sr_err("Sample limit cannot be 0.");
			return SR_ERR;
		}
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
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

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;

	devc = sdi->priv;

	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	if (!strcmp(di->name, "uni-t-ut61d")) {
		sr_source_add(0, 0, 10 /* poll_timeout */,
			      uni_t_ut61d_receive_data, (void *)sdi);
	} else if (!strcmp(di->name, "voltcraft-vc820")) {
		sr_source_add(0, 0, 10 /* poll_timeout */,
			      voltcraft_vc820_receive_data, (void *)sdi);
	}

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)sdi;

	sr_dbg("Stopping acquisition.");

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	/* TODO? */
	sr_source_remove(0);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver uni_t_ut61d_driver_info = {
	.name = "uni-t-ut61d",
	.longname = "UNI-T UT61D",
	.api_version = 1,
	.init = hw_init_ut61d,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver voltcraft_vc820_driver_info = {
	.name = "voltcraft-vc820",
	.longname = "Voltcraft VC-820",
	.api_version = 1,
	.init = hw_init_vc820,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
