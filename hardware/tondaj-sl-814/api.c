/*
 * This file is part of the sigrok project.
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

#include <fcntl.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define SERIALCOMM "9600/8e1"

static const int hwopts[] = {
	SR_HWOPT_CONN,
	SR_HWOPT_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_SOUNDLEVELMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0,
};

static const char *probe_names[] = {
	"P1",
	NULL,
};

SR_PRIV struct sr_dev_driver tondaj_sl_814_driver_info;
static struct sr_dev_driver *di = &tondaj_sl_814_driver_info;

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
		sr_serial_dev_inst_free(devc->serial);
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
	struct sr_hwopt *opt;
	struct sr_probe *probe;
	GSList *devices, *l;
	const char *conn, *serialcomm;

	devices = NULL;
	drvc = di->priv;
	drvc->instances = NULL;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		if (!(opt = l->data)) {
			sr_err("Invalid option data, skipping.");
			continue;
		}
		switch (opt->hwopt) {
		case SR_HWOPT_CONN:
			conn = opt->value;
			break;
		case SR_HWOPT_SERIALCOMM:
			serialcomm = opt->value;
			break;
		default:
			sr_err("Unknown option %d, skipping.", opt->hwopt);
			break;
		}
	}
	if (!conn) {
		sr_dbg("Couldn't determine connection options.");
		return NULL;
	}
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "Tondaj",
				    "SL-814", NULL))) {
		sr_err("Failed to create device instance.");
		return NULL;
	}

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		return NULL;
	}

	if (!(devc->serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(devc->serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	sdi->priv = devc;
	sdi->driver = di;
	probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, probe_names[0]);
	if (!probe) {
		sr_err("Failed to create probe.");
		return NULL;
	}
	sdi->probes = g_slist_append(sdi->probes, probe);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

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
	struct dev_context *devc;

	devc = sdi->priv;

	if (serial_open(devc->serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->serial && devc->serial->fd != -1) {
		serial_close(devc->serial);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
		       const struct sr_dev_inst *sdi)
{
	(void)sdi;

	switch (info_id) {
	case SR_DI_HWOPTS:
		*data = hwopts;
		break;
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(1);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	default:
		sr_err("Unknown info_id: %d.", info_id);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
			     const void *value)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't set config options.");
		return SR_ERR;
	}

	devc = sdi->priv;

	switch (hwcap) {
	case SR_HWCAP_LIMIT_SAMPLES:
		devc->limit_samples = *(const uint64_t *)value;
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		sr_err("Unknown hardware capability: %d.", hwcap);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct dev_context *devc;

	devc = sdi->priv;
	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	sr_dbg("Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(devc->cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	sr_dbg("Sending SR_DF_META_ANALOG.");
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = 1;
	sr_session_send(devc->cb_data, &packet);

	/* Poll every 500ms, or whenever some data comes in. */
	sr_source_add(devc->serial->fd, G_IO_IN, 500,
		      tondaj_sl_814_receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	int ret, final_ret;
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	final_ret = SR_OK;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	devc = sdi->priv;

	if ((ret = sr_source_remove(devc->serial->fd)) < 0) {
		sr_err("Error removing source: %d.", ret);
		final_ret = SR_ERR;
	}
	
	if ((ret = hw_dev_close(sdi)) < 0) {
		sr_err("Error closing device: %d.", ret);
		final_ret = SR_ERR;
	}

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return final_ret;
}

SR_PRIV struct sr_dev_driver tondaj_sl_814_driver_info = {
	.name = "tondaj-sl-814",
	.longname = "Tondaj SL-814",
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
