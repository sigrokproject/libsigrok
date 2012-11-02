/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <fcntl.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "genericdmm.h"

extern SR_PRIV struct dmmchip dmmchip_victor70c;

static struct sr_hwopt victor_70c_vidpid[] = {
	{ SR_HWOPT_CONN, "1244.d237" },
	{ 0, NULL }
};
static struct dev_profile dev_profiles[] = {
	{ "victor-70c", "Victor", "70C", &dmmchip_victor70c,
		DMM_TRANSPORT_USBHID, 1000, victor_70c_vidpid
	},
	{ NULL, NULL, NULL, NULL, 0, 0, NULL }
};

static const int hwopts[] = {
	SR_HWOPT_MODEL,
	SR_HWOPT_CONN,
	SR_HWOPT_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_MULTIMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_LIMIT_MSEC,
	SR_HWCAP_CONTINUOUS,
	0,
};

static const char *probe_names[] = {
	"Probe",
	NULL,
};

SR_PRIV struct sr_dev_driver genericdmm_driver_info;
static struct sr_dev_driver *gdi = &genericdmm_driver_info;
/* TODO need a way to keep this local to the static library */
static libusb_context *genericdmm_usb_context = NULL;
static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data);

static GSList *connect_serial(const char *conn, const char *serialcomm)
{
	GSList *devices;

	devices = NULL;

	/* TODO */
	sr_dbg("Not yet implemented.");

	return devices;
}

GSList *genericdmm_connect(const char *conn, const char *serialcomm)
{
	GSList *devices, *l;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_probe *probe;
	struct libusb_device *ldev;
	int i, devcnt;

	drvc = gdi->priv;

	devices = NULL;

	if (serialcomm)
		/* Must be a serial port. */
		return connect_serial(conn, serialcomm);

	if (!(l = sr_usb_connect(genericdmm_usb_context, conn))) {
		return NULL;
	}

	for (i = 0; i < (int)g_slist_length(l); i++) {
		ldev = (struct libusb_device *)l->data;

		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}

		devcnt = g_slist_length(drvc->instances);
		if (!(sdi = sr_dev_inst_new(devcnt, SR_ST_INACTIVE,
				NULL, NULL, NULL))) {
			sr_err("sr_dev_inst_new returned NULL.");
			return NULL;
		}
		sdi->priv = devc;
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
		devc->usb = sr_usb_dev_inst_new(
				libusb_get_bus_number(ldev),
				libusb_get_device_address(ldev), NULL);
		devices = g_slist_append(devices, sdi);

		return devices;
	}

	return NULL;
}

static GSList *default_scan(GSList *options)
{
	GSList *l, *devices;
	struct sr_hwopt *opt;
	const char *conn, *serialcomm;

	devices = NULL;
	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		opt = l->data;
		switch (opt->hwopt) {
		case SR_HWOPT_CONN:
			conn = opt->value;
			break;
		case SR_HWOPT_SERIALCOMM:
			serialcomm = opt->value;
			break;
		}
	}
	if (conn)
		devices = genericdmm_connect(conn, serialcomm);

	return devices;
}

static int clear_instances(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;

	if (!(drvc = gdi->priv))
		return SR_OK;

	/* Properly close and free all devices. */
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("sdi was NULL, continuing.");
			continue;
		}
		if (!(devc = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("sdi->priv was NULL, continuing.");
			continue;
		}

		if (devc->profile) {
			switch (devc->profile->transport) {
			case DMM_TRANSPORT_USBHID:
				sr_usb_dev_inst_free(devc->usb);
				break;
			case DMM_TRANSPORT_SERIAL:
				if (devc->serial && devc->serial->fd != -1)
					serial_close(devc->serial->fd);
				sr_serial_dev_inst_free(devc->serial);
				break;
			}
		}

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(void)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	if (libusb_init(&genericdmm_usb_context) != 0) {
		sr_err("Failed to initialize USB.");
		return SR_ERR;
	}

	gdi->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	GSList *l, *ldef, *defopts, *newopts, *devices;
	struct sr_hwopt *opt, *defopt;
	struct dev_profile *pr, *profile;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	const char *model;

	drvc = gdi->priv;

	/* Separate model from the options list. */
	model = NULL;
	newopts = NULL;
	for (l = options; l; l = l->next) {
		opt = l->data;
		if (opt->hwopt == SR_HWOPT_MODEL)
			model = opt->value;
		else
			/* New list with references to the original data. */
			newopts = g_slist_append(newopts, opt);
	}
	if (!model) {
		/* This driver only works when a model is specified. */
		return NULL;
	}

	/* Find a profile with this model name. */
	profile = NULL;
	for (pr = dev_profiles; pr->modelid; pr++) {
		if (!strcmp(pr->modelid, model)) {
			profile = pr;
			break;
		}
	}
	if (!profile) {
		sr_err("Unknown model %s.", model);
		return NULL;
	}

	/* Initialize the DMM chip driver. */
	if (profile->chip->init)
		profile->chip->init();

	/* Convert the profile's default options list to a GSList. */
	defopts = NULL;
	for (opt = profile->defaults_opts; opt->hwopt; opt++) {
		/* New list with references to const data in the profile. */
		defopts = g_slist_append(defopts, opt);
	}

	/* Options given as argument to this function override the
	 * profile's default options.
	 */
	for (ldef = defopts; ldef; ldef = ldef->next) {
		defopt = ldef->data;
		for (l = newopts; l; l = l->next) {
			opt = l->data;
			if (opt->hwopt == defopt->hwopt) {
				/* Override the default, and drop it from the
				 * options list.
				 */
				ldef->data = l->data;
				newopts = g_slist_remove(newopts, opt);
				break;
			}
		}
	}
	/* Whatever is left in newopts wasn't in the default options. */
	defopts = g_slist_concat(defopts, newopts);
	g_slist_free(newopts);

	if (profile->chip->scan)
		/* The DMM chip driver wants to do its own scanning. */
		devices = profile->chip->scan(defopts);
	else
		devices = default_scan(defopts);
	g_slist_free(defopts);

	if (devices) {
		/* TODO: need to fix up sdi->index fields */
		for (l = devices; l; l = l->next) {
			/* The default connection-based scanner doesn't really
			 * know about profiles, so it never filled in the vendor
			 * or model. Do that now.
			 */
			sdi = l->data;
			devc = sdi->priv;
			devc->profile = profile;
			sdi->driver = gdi;
			if (!sdi->vendor)
				sdi->vendor = g_strdup(profile->vendor);
			if (!sdi->model)
				sdi->model = g_strdup(profile->model);
			/* Add a copy of these new devices to the driver instances. */
			drvc->instances = g_slist_append(drvc->instances, l->data);
		}
	}

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = gdi->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	ret = SR_OK;
	switch (devc->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		if (sdi->status == SR_ST_ACTIVE) {
			sr_err("Device already in use.");
			ret = SR_ERR;
		} else {
			ret = sr_usb_open(genericdmm_usb_context, devc->usb);
		}
		break;
	case DMM_TRANSPORT_SERIAL:
		sr_dbg("Opening serial port '%s'.", devc->serial->port);
		devc->serial->fd = serial_open(devc->serial->port, O_RDWR | O_NONBLOCK);
		if (devc->serial->fd == -1) {
			sr_err("Couldn't open serial port '%s'.",
			       devc->serial->port);
			ret = SR_ERR;
		}
		//	serial_set_params(devc->serial->fd, 2400, 8, 0, 1, 2);
		break;
	default:
		sr_err("No transport set.");
		ret = SR_ERR;
	}

	return ret;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL.", __func__);
		return SR_ERR_BUG;
	}

	switch (devc->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		/* TODO */
		break;
	case DMM_TRANSPORT_SERIAL:
		if (devc->serial && devc->serial->fd != -1) {
			serial_close(devc->serial->fd);
			devc->serial->fd = -1;
			sdi->status = SR_ST_INACTIVE;
		}
		break;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{

	clear_instances();

	if (genericdmm_usb_context)
		libusb_exit(genericdmm_usb_context);

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
		const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	(void)sdi;
	(void)devc;

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
	case SR_DI_CUR_SAMPLERATE:
		/* TODO get rid of this */
		*data = NULL;
		return SR_ERR_ARG;
		break;
	default:
		/* Unknown device info ID. */
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (hwcap) {
	case SR_HWCAP_LIMIT_MSEC:
		/* TODO: not yet implemented */
		if (*(const uint64_t *)value == 0) {
			sr_err("LIMIT_MSEC can't be 0.");
			return SR_ERR;
		}
		devc->limit_msec = *(const uint64_t *)value;
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
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

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	switch (devc->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		if (devc->profile->chip->data)
			devc->profile->chip->data(sdi);
		break;
	case DMM_TRANSPORT_SERIAL:
		/* TODO */
		fd = fd;
		break;
	}

	if (devc->num_samples >= devc->limit_samples)
		hw_dev_acquisition_stop(sdi, cb_data);

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

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

	/* Hook up a proxy handler to receive data from the device. */
	switch (devc->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		/* Callously using stdin here. This works because no G_IO_* flags
		 * are set, but will certainly break when any other driver does
		 * this, and runs at the same time as genericdmm.
		 * We'll need a timeout-only source when revamping the whole
		 * driver source system.
		 */
		sr_source_add(0, 0, devc->profile->poll_timeout,
				receive_data, (void *)sdi);
		break;
	case DMM_TRANSPORT_SERIAL:
		/* TODO serial FD setup */
		// sr_source_add(devc->serial->fd, G_IO_IN, -1, receive_data, sdi);
		break;
	}

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	(void)sdi;

	sr_dbg("Stopping acquisition.");

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	sr_source_remove(0);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver genericdmm_driver_info = {
	.name = "genericdmm",
	.longname = "Generic DMM",
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
