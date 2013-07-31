/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
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

#define LOGIC16_VID 0x21a9
#define LOGIC16_PID 0x1001
#define NUM_PROBES  16

SR_PRIV struct sr_dev_driver saleae_logic16_driver_info;
static struct sr_dev_driver *di = &saleae_logic16_driver_info;

static const char *probe_names[NUM_PROBES + 1] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15",
	NULL,
};

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	GSList *devices;
	int ret, devcnt, i, j;

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

		if (des.idVendor != LOGIC16_VID || des.idProduct != LOGIC16_PID)
			continue;

		devcnt = g_slist_length(drvc->instances);
		if (!(sdi = sr_dev_inst_new(devcnt, SR_ST_INACTIVE,
					    "Saleae", "Logic16", NULL)))
			return NULL;
		sdi->driver = di;

		if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
			return NULL;
		sdi->priv = devc;

		for (j = 0; probe_names[j]; j++) {
			if (!(probe = sr_probe_new(j, SR_PROBE_LOGIC, TRUE,
						   probe_names[j])))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

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

static GSList *dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and open it. */

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and close it. */

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	ret = dev_clear();
	g_free(drvc);
	di->priv = NULL;

	return ret;
}

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	int ret;

	(void)sdi;
	(void)data;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi)
{
	int ret;

	(void)data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	int ret;

	(void)sdi;
	(void)data;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	(void)sdi;
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* TODO: stop acquisition. */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver saleae_logic16_driver_info = {
	.name = "saleae-logic16",
	.longname = "Saleae Logic16",
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
	.priv = NULL,
};
