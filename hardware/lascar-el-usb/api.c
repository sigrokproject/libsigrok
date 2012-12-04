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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

SR_PRIV struct sr_dev_inst *lascar_scan(int bus, int address);
SR_PRIV struct sr_dev_driver lascar_el_usb_driver_info;
static struct sr_dev_driver *di = &lascar_el_usb_driver_info;
static int hw_dev_close(struct sr_dev_inst *sdi);
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static const int hwopts[] = {
	SR_HWOPT_CONN,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_THERMOMETER,
	SR_HWCAP_HYGROMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	0
};

static const char *probe_names[] = {
	"P1",
};


/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	if (!(drvc = di->priv))
		return SR_OK;

	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data))
			continue;
		if (!(devc = sdi->priv))
			continue;

		hw_dev_close(sdi);
		sr_usb_dev_inst_free(devc->usb);
		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	drvc->sr_ctx = sr_ctx;
	di->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_probe *probe;
	struct sr_hwopt *opt;
	GSList *usb_devices, *devices, *l;
	const char *conn;

	(void)options;

	if (!(drvc = di->priv)) {
		sr_err("Driver was not initialized.");
		return NULL;
	}

	/* USB scan is always authoritative. */
	clear_instances();

	conn = NULL;
	for (l = options; l; l = l->next) {
		opt = l->data;
		switch (opt->hwopt) {
		case SR_HWOPT_CONN:
			conn = opt->value;
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
			usb = l->data;
			if (!(sdi = lascar_scan(usb->bus, usb->address))) {
				/* Not a Lascar EL-USB. */
				g_free(usb);
				continue;
			}
			if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
				sr_err("Device context malloc failed.");
				return NULL;
			}
			devc->usb = usb;
			sdi->priv = devc;
			if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
		}
		g_slist_free(usb_devices);
	} else
		g_slist_free_full(usb_devices, g_free);

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	/* TODO */

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	/* TODO */

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	/* TODO */

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
		const struct sr_dev_inst *sdi)
{
	switch (info_id) {
	/* TODO */
	default:
		sr_err("Unknown info_id: %d.", info_id);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	int ret;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't set config options.");
		return SR_ERR;
	}

	ret = SR_OK;
	switch (hwcap) {
	/* TODO */
	default:
		sr_err("Unknown hardware capability: %d.", hwcap);
		ret = SR_ERR_ARG;
	}

	return ret;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	/* TODO */

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	/* TODO */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver lascar_el_usb_driver_info = {
	.name = "lascar-el-usb",
	.longname = "Lascar EL-USB",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
