/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>

#include "config.h"
#include "sigrok.h"
#include "sigrok-internal.h"
#include "fx2lafw.h"

static struct fx2lafw_profile supported_fx2[] = {
	/* USBee AX */
	{ 0x08a9, 0x0014, "CWAV", "USBee AX", NULL, 8 },
	{ 0, 0, 0, 0, 0, 0 }
};

static int fx2lafw_capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,

	/* These are really implemented in the driver, not the hardware. */
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0
};

static const char *fx2lafw_probe_names[] = {
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	NULL
};

static uint64_t fx2lafw_supported_samplerates[] = {
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(3),
	SR_MHZ(4),
	SR_MHZ(6),
	SR_MHZ(8),
	SR_MHZ(12),
	SR_MHZ(16),
	SR_MHZ(24)
};

static struct sr_samplerates fx2lafw_samplerates = {
	SR_MHZ(1),
	SR_MHZ(24),
	SR_HZ(0),
	fx2lafw_supported_samplerates
};

static GSList *dev_insts = NULL;
static libusb_context *usb_context = NULL;

static struct fx2lafw_device* fx2lafw_device_new(void)
{
	struct fx2lafw_device *fx2lafw;

	if (!(fx2lafw = g_try_malloc0(sizeof(struct fx2lafw_device)))) {
		sr_err("fx2lafw: %s: fx2lafw_device malloc failed", __func__);
		return NULL;
	}

	return fx2lafw;
}

/*
 * API callbacks
 */

static int hw_init(const char *deviceinfo)
{
	struct sr_dev_inst *sdi;
	struct libusb_device_descriptor des;
	struct fx2lafw_profile *fx2lafw_prof;
	struct fx2lafw_device *fx2lafw_dev;
	libusb_device **devlist;
	int err;
	int devcnt = 0;
	int i, j;

	/* Avoid compiler warnings. */
	(void)deviceinfo;

	if (libusb_init(&usb_context) != 0) {
		sr_warn("Failed to initialize USB.");
		return 0;
	}

	/* Find all fx2lafw compatible devices and upload firware to all of them. */
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {

		if ((err = libusb_get_device_descriptor(
			devlist[i], &des)) != 0) {
			sr_warn("failed to get device descriptor: %d", err);
			continue;
		}

		fx2lafw_prof = NULL;
		for (j = 0; supported_fx2[j].vid; j++) {
			if (des.idVendor == supported_fx2[j].vid &&
				des.idProduct == supported_fx2[j].pid) {
				fx2lafw_prof = &supported_fx2[j];
			}
		}

		/* Skip if the device was not found */
		if(!fx2lafw_prof)
			continue;

		sdi = sr_dev_inst_new(devcnt, SR_ST_INITIALIZING,
			fx2lafw_prof->vendor, fx2lafw_prof->model,
			fx2lafw_prof->model_version);
		if(!sdi)
			return 0;

		fx2lafw_dev = fx2lafw_device_new();
		fx2lafw_dev->profile = fx2lafw_prof;
		sdi->priv = fx2lafw_dev;
		device_instances = g_slist_append(dev_insts, sdi);

		devcnt++;
	}
	libusb_free_device_list(devlist, 1);

	return devcnt;
}

static int hw_dev_open(int device_index)
{
	(void)device_index;
	return SR_OK;
}

static int hw_dev_close(int device_index)
{
	(void)device_index;
	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;

	for(l = device_instances; l; l = l->next) {
		sdi = l->data;
		sr_dev_inst_free(sdi);
	}

	g_slist_free(device_instances);
	device_instances = NULL;

	if(usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;

	return SR_OK;
}

static void *hw_dev_info_get(int device_index, int device_info_id)
{
	struct sr_dev_inst *sdi;
	struct fx2lafw_device *fx2lafw_dev;

	if (!(sdi = sr_dev_inst_get(dev_insts, device_index)))
		return NULL;
	fx2lafw_dev = sdi->priv;

	switch (device_info_id) {
	case SR_DI_INST:
		return sdi;
	case SR_DI_NUM_PROBES:
		return GINT_TO_POINTER(fx2lafw_dev->profile->num_probes);
	case SR_DI_PROBE_NAMES:
		return fx2lafw_probe_names;
	case SR_DI_SAMPLERATES:
		return &fx2lafw_samplerates;
	case SR_DI_TRIGGER_TYPES:
		return TRIGGER_TYPES;
	}

	return NULL;
}

static int hw_dev_status_get(int device_index)
{
	const struct sr_dev_inst *const sdi =
		sr_dev_inst_get(dev_insts, device_index);

	if (!sdi)
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static int *hw_hwcap_get_all(void)
{
	return fx2lafw_capabilities;
}

static int hw_dev_config_set(int dev_index, int capability, void *value)
{
	(void)dev_index;
	(void)capability;
	(void)value;
	return SR_OK;
}

static int hw_dev_acquisition_start(int dev_index, gpointer session_data)
{
	(void)dev_index;
	(void)session_data;
	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring device_index. */
static int hw_dev_acquisition_stop(int dev_index, gpointer session_data)
{
	(void)dev_index;
	(void)session_data;
	return SR_OK;
}

SR_PRIV struct sr_dev_plugin fx2lafw_plugin_info = {
	.name = "fx2lafw",
	.longname = "fx2lafw",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
