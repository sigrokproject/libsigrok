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

#include <stdlib.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define UNI_T_UT_D04_NEW "1a86.e008"

static const int hwopts[] = {
	SR_HWOPT_CONN,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_MULTIMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_LIMIT_MSEC,
	SR_HWCAP_CONTINUOUS,
	0,
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
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	if (dmm == UNI_T_UT61D)
		di = di_ut61d;
	else if (dmm == VOLTCRAFT_VC820)
		di = di_vc820;
	sr_dbg("Selected '%s' subdriver.", di->name);

	drvc->sr_ctx = sr_ctx;
	di->priv = drvc;

	return SR_OK;
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
	struct sr_hwopt *opt;
	struct sr_probe *probe;
	const char *conn;

	(void)options;

	drvc = di->priv;

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
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct dev_context *devc;

	drvc = di->priv;
	devc = sdi->priv;

	return sr_usb_open(drvc->sr_ctx->libusb_ctx, devc->usb);
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO */

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

	sr_spew("Backend requested info_id %d.", info_id);

	switch (info_id) {
	case SR_DI_HWOPTS:
		*data = hwopts;
		break;
	case SR_DI_HWCAPS:
		*data = hwcaps;
		sr_spew("%s: Returning hwcaps.", __func__);
		break;
	case SR_DI_SAMPLERATES:
		/* TODO: Get rid of this. */
		*data = NULL;
		sr_spew("%s: Returning samplerates.", __func__);
		return SR_ERR_ARG;
		break;
	case SR_DI_CUR_SAMPLERATE:
		/* TODO: Get rid of this. */
		*data = NULL;
		sr_spew("%s: Returning current samplerate.", __func__);
		return SR_ERR_ARG;
		break;
	default:
		return SR_ERR_ARG;
		break;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
			     const void *value)
{
	struct dev_context *devc;

	devc = sdi->priv;

	switch (hwcap) {
	case SR_HWCAP_LIMIT_MSEC:
		/* TODO: Not yet implemented. */
		if (*(const uint64_t *)value == 0) {
			sr_err("Time limit cannot be 0.");
			return SR_ERR;
		}
		devc->limit_msec = *(const uint64_t *)value;
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		if (*(const uint64_t *)value == 0) {
			sr_err("Sample limit cannot be 0.");
			return SR_ERR;
		}
		devc->limit_samples = *(const uint64_t *)value;
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		sr_err("Unknown capability: %d.", hwcap);
		return SR_ERR;
		break;
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

	sr_dbg("Starting acquisition.");

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
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
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
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
