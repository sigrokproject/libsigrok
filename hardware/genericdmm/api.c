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


extern SR_PRIV struct dmmchip dmmchip_fs9922;

static struct sr_hwopt victor_70c_vidpid[] = {
	{ SR_HWOPT_CONN, "1244.d237" },
	{ 0, NULL }
};
static struct dev_profile dev_profiles[] = {
	{ "victor-70c", "Victor", "70C", &dmmchip_fs9922,
		DMM_TRANSPORT_USBHID, victor_70c_vidpid
	},
	{ "mastech-va18b", "Mastech", "VA18B", NULL, DMM_TRANSPORT_SERIAL, NULL},
	{ NULL, NULL, NULL, NULL, 0, NULL }
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
SR_PRIV libusb_context *genericdmm_usb_context = NULL;


static GSList *connect_usb(const char *conn)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	GSList *devices;
	GRegex *reg;
	GMatchInfo *match;
	int vid, pid, bus, addr, devcnt, err, i;
	char *mstr;

	vid = pid = bus = addr = 0;
	reg = g_regex_new(DMM_CONN_USB_VIDPID, 0, 0, NULL);
	if (g_regex_match(reg, conn, 0, &match)) {
		/* Extract VID. */
		if ((mstr = g_match_info_fetch(match, 1)))
			vid = strtoul(mstr, NULL, 16);
		g_free(mstr);

		/* Extract PID. */
		if ((mstr = g_match_info_fetch(match, 2)))
			pid = strtoul(mstr, NULL, 16);
		g_free(mstr);
	} else {
		g_match_info_unref(match);
		g_regex_unref(reg);
		reg = g_regex_new(DMM_CONN_USB_BUSADDR, 0, 0, NULL);
		if (g_regex_match(reg, conn, 0, &match)) {
			/* Extract bus. */
			if ((mstr = g_match_info_fetch(match, 0)))
				bus = strtoul(mstr, NULL, 16);
			g_free(mstr);

			/* Extract address. */
			if ((mstr = g_match_info_fetch(match, 0)))
				addr = strtoul(mstr, NULL, 16);
			g_free(mstr);
		}
	}
	g_match_info_unref(match);
	g_regex_unref(reg);

	if (vid + pid + bus + addr == 0)
		return NULL;

	if (bus > 64) {
		sr_err("invalid bus");
		return NULL;
	}

	if (addr > 127) {
		sr_err("invalid address");
		return NULL;
	}

	/* Looks like a valid USB device specification, but is it connected? */
	devices = NULL;
	libusb_get_device_list(genericdmm_usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((err = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("genericdmm: failed to get device descriptor: %d", err);
			continue;
		}

		if (vid + pid && (des.idVendor != vid || des.idProduct != pid))
			/* VID/PID specified, but no match. */
			continue;

		if (bus + addr && (
				libusb_get_bus_number(devlist[i]) != bus
				|| libusb_get_device_address(devlist[i]) != addr))
			/* Bus/address specified, but no match. */
			continue;

		/* Found one. */
		if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
			sr_err("genericdmm: ctx malloc failed.");
			return 0;
		}

		devcnt = g_slist_length(gdi->instances);
		if (!(sdi = sr_dev_inst_new(devcnt, SR_ST_ACTIVE,
				NULL, NULL, NULL))) {
			sr_err("genericdmm: sr_dev_inst_new returned NULL.");
			return NULL;
		}
		sdi->priv = ctx;
		ctx->usb = sr_usb_dev_inst_new(
				libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
		devices = g_slist_append(devices, sdi);
	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static GSList *connect_serial(const char *conn, const char *serialcomm)
{
	GSList *devices;

	devices = NULL;

	/* TODO */
	sr_dbg("not yet implemented");

	return devices;
}

GSList *genericdmm_connect(const char *conn, const char *serialcomm)
{
	GSList *devices;

	if (serialcomm)
		/* Must be a serial port. */
		return connect_serial(conn, serialcomm);

	if ((devices = connect_usb(conn)))
		return devices;

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

static int hw_init(void)
{

	if (libusb_init(&genericdmm_usb_context) != 0) {
		sr_err("genericdmm: Failed to initialize USB.");
		return SR_ERR;
	}


	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	GSList *l, *ldef, *defopts, *newopts, *devices;
	struct sr_hwopt *opt, *defopt;
	struct dev_profile *pr, *profile;
	struct sr_dev_inst *sdi;
	const char *model;

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
			sdi->driver = gdi;
			if (!sdi->vendor)
				sdi->vendor = g_strdup(profile->vendor);
			if (!sdi->model)
				sdi->model = g_strdup(profile->model);
			/* Add a copy of these new devices to the driver instances. */
			gdi->instances = g_slist_append(gdi->instances, l->data);
		}
	}

	return devices;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct context *ctx;

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("genericdmm: Opening serial port '%s'.", ctx->serial->port);

	switch (ctx->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		/* TODO */
		break;
	case DMM_TRANSPORT_SERIAL:
		/* TODO: O_NONBLOCK? */
		ctx->serial->fd = serial_open(ctx->serial->port, O_RDWR | O_NONBLOCK);
		if (ctx->serial->fd == -1) {
			sr_err("genericdmm: Couldn't open serial port '%s'.",
			       ctx->serial->port);
			return SR_ERR;
		}
		//	serial_set_params(ctx->serial->fd, 2400, 8, 0, 1, 2);
		break;
	default:
		sr_err("No transport set.");
	}

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct context *ctx;

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: %s: sdi->priv was NULL.", __func__);
		return SR_ERR_BUG;
	}

	switch (ctx->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		/* TODO */
		break;
	case DMM_TRANSPORT_SERIAL:
		if (ctx->serial && ctx->serial->fd != -1) {
			serial_close(ctx->serial->fd);
			ctx->serial->fd = -1;
			sdi->status = SR_ST_INACTIVE;
		}
		break;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Properly close and free all devices. */
	for (l = gdi->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("genericdmm: sdi was NULL, continuing.");
			continue;
		}
		if (!(ctx = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("genericdmm: sdi->priv was NULL, continuing.");
			continue;
		}

		if (ctx->profile) {
			switch (ctx->profile->transport) {
			case DMM_TRANSPORT_USBHID:
				/* TODO */
				break;
			case DMM_TRANSPORT_SERIAL:
				if (ctx->serial && ctx->serial->fd != -1)
					serial_close(ctx->serial->fd);
				sr_serial_dev_inst_free(ctx->serial);
				break;
			}
		}

		sr_dev_inst_free(sdi);
	}

	g_slist_free(gdi->instances);
	gdi->instances = NULL;

	if (genericdmm_usb_context)
		libusb_exit(genericdmm_usb_context);

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
		const struct sr_dev_inst *sdi)
{
	struct context *ctx;

	switch (info_id) {
	case SR_DI_INST:
		*data = sdi;
		sr_spew("genericdmm: Returning sdi.");
		break;
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
	struct context *ctx;

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (hwcap) {
	case SR_HWCAP_LIMIT_MSEC:
		if (*(const uint64_t *)value == 0) {
			sr_err("genericdmm: LIMIT_MSEC can't be 0.");
			return SR_ERR;
		}
		ctx->limit_msec = *(const uint64_t *)value;
		sr_dbg("genericdmm: Setting LIMIT_MSEC to %" PRIu64 ".",
		       ctx->limit_msec);
		break;
	case SR_HWCAP_LIMIT_SAMPLES:
		ctx->limit_samples = *(const uint64_t *)value;
		sr_dbg("genericdmm: Setting LIMIT_SAMPLES to %" PRIu64 ".",
		       ctx->limit_samples);
		break;
	default:
		sr_err("genericdmm: Unknown capability: %d.", hwcap);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = cb_data))
		return FALSE;

	if (!(ctx = sdi->priv))
		return FALSE;

	if (revents != G_IO_IN) {
		sr_err("genericdmm: No data?");
		return FALSE;
	}

	switch (ctx->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		/* TODO */
		break;
	case DMM_TRANSPORT_SERIAL:
		/* TODO */
		break;
	}

	return TRUE;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct context *ctx;

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("genericdmm: Starting acquisition.");

	ctx->cb_data = cb_data;

	/* Send header packet to the session bus. */
	sr_dbg("genericdmm: Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(ctx->cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	sr_dbg("genericdmm: Sending SR_DF_META_ANALOG.");
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = 1;
	sr_session_send(ctx->cb_data, &packet);

	/* Hook up a proxy handler to receive data from the device. */
	switch (ctx->profile->transport) {
	case DMM_TRANSPORT_USBHID:
		/* TODO libusb FD setup */
		break;
	case DMM_TRANSPORT_SERIAL:
		/* TODO serial FD setup */
		// sr_source_add(ctx->serial->fd, G_IO_IN, -1, receive_data, sdi);
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

	sr_dbg("genericdmm: Stopping acquisition.");

	/* Send end packet to the session bus. */
	sr_dbg("genericdmm: Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver genericdmm_driver_info = {
	.name = "genericdmm",
	.longname = "Generic DMM",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.instances = NULL,
};
