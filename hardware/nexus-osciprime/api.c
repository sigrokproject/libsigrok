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

#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define OSCI_VENDOR "Nexus Computing"
#define OSCI_MODEL "OsciPrime"
#define OSCI_VERSION "1.0"
#define OSCI_FIRMWARE FIRMWARE_DIR "/nexus-osciprime.fw"
#define OSCI_VIDPID "04b4.1004"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t hwcaps[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
	SR_CONF_TIMEBASE,
	SR_CONF_VDIV,
};

static const uint64_t timebases[][2] = {
	/* 24 MHz */
	{ 42, 1e9 },
	/* 12 MHz */
	{ 83, 1e9 },
	/* 6 MHz */
	{ 167, 1e9 },
	/* 3 MHz */
	{ 333, 1e9 },
	/* 1.5 MHz */
	{ 667, 1e9 },
	/* 750 kHz */
	{ 1333, 1e9 },
	/* 375 kHz */
	{ 2667, 1e9 },
	/* 187.5 kHz */
	{ 5333, 1e9 },
	/* 93.25 kHz */
	{ 10724, 1e9 },
	/* 46.875 kHz */
	{ 21333, 1e9 },
	/* 23.4375 kHz */
	{ 42666, 1e9 },
	/* 11.718 kHz */
	{ 85339, 1e9 },
	/* 5.859 kHz */
	{ 170678, 1e9 },
	/* 2.929 kHz */
	{ 341413, 1e9 },
	/* 1.465 kHz */
	{ 682594, 1e9 },
	/* 732 Hz */
	{ 1366, 1e6 },
	/* 366 Hz */
	{ 2732, 1e6 },
	/* 183 Hz */
	{ 5464, 1e6 },
	/* 91 Hz */
	{ 10989, 1e6 },
	/* 46 Hz */
	{ 21739, 1e6 },
	/* 23 Hz */
	{ 43478, 1e6 },
	/* 12 Hz */
	{ 83333, 1e6 },
};

static const char *probe_names[] = {
	"CHA", "CHB",
	NULL,
};

static const uint64_t vdivs[][2] = {
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 2 },
	{ 5, 1 },
	{ 10, 1 },
};


SR_PRIV struct sr_dev_driver nexus_osciprime_driver_info;
static struct sr_dev_driver *di = &nexus_osciprime_driver_info;
static int hw_dev_close(struct sr_dev_inst *sdi);

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
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct sr_probe *probe;
	libusb_device *dev;
	GSList *usb_devices, *devices, *l;
	int i;
	const char *conn;

	(void)options;

	drvc = di->priv;

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
		conn = OSCI_VIDPID;

	devices = NULL;
	if ((usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn))) {
		for (l = usb_devices; l; l = l->next) {
			usb = l->data;
			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
					OSCI_VENDOR, OSCI_MODEL, OSCI_VERSION)))
				return NULL;
			sdi->driver = di;
			for (i = 0; probe_names[i]; i++) {
				if (!(probe = sr_probe_new(i, SR_PROBE_ANALOG, TRUE,
						probe_names[i])))
					return NULL;
				sdi->probes = g_slist_append(sdi->probes, probe);
			}

			if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
				return NULL;
			sdi->priv = devc;
			devc->usb = usb;

			if (strcmp(conn, OSCI_VIDPID)) {
				if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
					break;
				dev = libusb_get_device(usb->devhdl);
				if (ezusb_upload_firmware(dev, 0, OSCI_FIRMWARE) == SR_OK)
					/* Remember when the firmware on this device was updated */
					devc->fw_updated = g_get_monotonic_time();
				else
					sr_err("Firmware upload failed for device "
							"at bus %d address %d.", usb->bus, usb->address);
			}

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
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{

	/* TODO */
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{

	/* TODO */
    sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	/* TODO */

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	int ret;

	/* TODO */
	(void)data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	switch (id) {

	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	(void)sdi;
	(void)data;

	switch (key) {
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	/* TODO */
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

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

SR_PRIV struct sr_dev_driver nexus_osciprime_driver_info = {
	.name = "nexus-osciprime",
	.longname = "Nexus OsciPrime",
	.api_version = 1,
	.init = hw_init,
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
