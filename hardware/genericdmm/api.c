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

static const int hwcaps[] = {
	SR_HWCAP_MULTIMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_LIMIT_MSEC,
	SR_HWCAP_CONTINUOUS,
	SR_HWCAP_MODEL,
	SR_HWCAP_CONN,
	SR_HWCAP_SERIALCOMM,
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


static int hw_init(void)
{

	if (libusb_init(&genericdmm_usb_context) != 0) {
		sr_err("genericdmm: Failed to initialize USB.");
		return SR_ERR;
	}


	return SR_OK;
}

static int hw_scan(void)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int devcnt = 0;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("genericdmm: ctx malloc failed.");
		return 0;
	}

	devcnt = g_slist_length(genericdmm_dev_insts);
	if (!(sdi = sr_dev_inst_new(devcnt, SR_ST_ACTIVE, "Generic DMM",
			NULL, NULL))) {
		sr_err("genericdmm: sr_dev_inst_new returned NULL.");
		return 0;
	}
	sdi->priv = ctx;
	genericdmm_dev_insts = g_slist_append(genericdmm_dev_insts, sdi);

	/* Always initialized just one device instance. */
	return 0;
}

static int hw_dev_open(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(gdi->instances, dev_index))) {
		sr_err("genericdmm: sdi was NULL.");
		return SR_ERR_BUG;
	}

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

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(gdi->instances, dev_index))) {
		sr_err("genericdmm: %s: sdi was NULL.", __func__);
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: %s: sdi->priv was NULL.", __func__);
		return SR_ERR_BUG;
	}

	/* TODO: Check for != NULL. */

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

static const void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	const void *info;

	if (!(sdi = sr_dev_inst_get(gdi->instances, dev_index))) {
		sr_err("genericdmm: sdi was NULL.");
		return NULL;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: sdi->priv was NULL.");
		return NULL;
	}

		sr_spew("genericdmm: dev_index %d, dev_info_id %d.",
				dev_index, dev_info_id);

	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		sr_spew("genericdmm: Returning sdi.");
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(1);
		sr_spew("genericdmm: Returning number of probes: 1.");
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		sr_spew("genericdmm: Returning probenames.");
		break;
	case SR_DI_CUR_SAMPLERATE:
		/* TODO get rid of this */
		info = NULL;
		sr_spew("genericdmm: Returning samplerate: 0.");
		break;
	default:
		/* Unknown device info ID. */
		sr_err("genericdmm: Unknown device info ID: %d.", dev_info_id);
		info = NULL;
		break;
	}

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(gdi->instances, dev_index))) {
		sr_err("genericdmm: sdi was NULL, device not found.");
		return SR_ST_NOT_FOUND;
	}

	sr_dbg("genericdmm: Returning status: %d.", sdi->status);

	return sdi->status;
}

static const int *hw_hwcap_get_all(void)
{
	sr_spew("genericdmm: Returning list of device capabilities.");

	return hwcaps;
}

static int parse_conn_vidpid(struct sr_dev_inst *sdi, const char *conn)
{
	struct context *ctx;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	GRegex *reg;
	GMatchInfo *match;
	int vid, pid, found, err, i;
	char *vidstr, *pidstr;

	found = FALSE;

	reg = g_regex_new(DMM_CONN_USB_VIDPID, 0, 0, NULL);
	if (g_regex_match(reg, conn, 0, &match)) {
		/* Extract VID. */
		if (!(vidstr = g_match_info_fetch(match, 0))) {
			sr_err("failed to fetch VID from regex");
			goto err;
		}
		vid = strtoul(vidstr, NULL, 16);
		g_free(vidstr);
		if (vid > 0xffff) {
			sr_err("invalid VID");
			goto err;
		}

		/* Extract PID. */
		if (!(pidstr = g_match_info_fetch(match, 0))) {
			sr_err("failed to fetch PID from regex");
			goto err;
		}
		pid = strtoul(pidstr, NULL, 16);
		g_free(pidstr);
		if (pid > 0xffff) {
			sr_err("invalid PID");
			goto err;
		}

		/* Looks like a valid VID:PID, but is it connected? */
		libusb_get_device_list(genericdmm_usb_context, &devlist);
		for (i = 0; devlist[i]; i++) {
			if ((err = libusb_get_device_descriptor(devlist[i], &des))) {
				sr_err("genericdmm: failed to get device descriptor: %d", err);
				goto err;
			}

			if (des.idVendor == vid && des.idProduct == pid) {
				ctx = sdi->priv;
				ctx->usb = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]),
						libusb_get_device_address(devlist[i]), NULL);
				found = TRUE;
				break;
			}
		}
		libusb_free_device_list(devlist, 1);
	}

err:
	if (match)
		g_match_info_unref(match);
	g_regex_unref(reg);

	return found;
}

static int parse_conn_busaddr(struct sr_dev_inst *sdi, const char *conn)
{
	struct context *ctx;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	GRegex *reg;
	GMatchInfo *match;
	int bus, addr, found, err, i;
	char *busstr, *addrstr;

	found = FALSE;

	reg = g_regex_new(DMM_CONN_USB_BUSADDR, 0, 0, NULL);
	if (g_regex_match(reg, conn, 0, &match)) {
		/* Extract bus. */
		if (!(busstr = g_match_info_fetch(match, 0))) {
			sr_err("failed to fetch bus from regex");
			goto err;
		}
		bus = strtoul(busstr, NULL, 16);
		g_free(busstr);
		if (bus > 64) {
			sr_err("invalid bus");
			goto err;
		}

		/* Extract address. */
		if (!(addrstr = g_match_info_fetch(match, 0))) {
			sr_err("failed to fetch address from regex");
			goto err;
		}
		addr = strtoul(addrstr, NULL, 16);
		g_free(addrstr);
		if (addr > 127) {
			sr_err("invalid address");
			goto err;
		}

		/* Looks like a valid bus/address, but is it connected? */
		libusb_get_device_list(genericdmm_usb_context, &devlist);
		for (i = 0; devlist[i]; i++) {
			if ((err = libusb_get_device_descriptor(devlist[i], &des))) {
				sr_err("genericdmm: failed to get device descriptor: %d", err);
				goto err;
			}

			if (libusb_get_bus_number(devlist[i]) == bus
					&& libusb_get_device_address(devlist[i]) == addr) {
				ctx = sdi->priv;
				ctx->usb = sr_usb_dev_inst_new(bus, addr, NULL);
				found = TRUE;
				break;
			}
		}
		libusb_free_device_list(devlist, 1);
	}

err:
	if (match)
		g_match_info_unref(match);
	g_regex_unref(reg);

	return found;
}

static int parse_conn_serial(struct sr_dev_inst *sdi, const char *conn)
{
	int found;

	found = FALSE;

	/* TODO */

	return found;
}

static int parse_conn(struct sr_dev_inst *sdi, const char *conn)
{

	if (parse_conn_vidpid(sdi, conn))
		return SR_OK;

	if (parse_conn_busaddr(sdi, conn))
		return SR_OK;

	if (parse_conn_serial(sdi, conn))
		return SR_OK;

	sr_err("Invalid connection specification");

	return SR_ERR;
}

static int parse_serialcomm(struct sr_dev_inst *sdi, const char *conn)
{

	/* TODO */
	/* set ctx->serial_* */

	return SR_OK;
}

static int hw_dev_config_set(int dev_index, int hwcap, const void *value)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int i;

	if (!(sdi = sr_dev_inst_get(gdi->instances, dev_index))) {
		sr_err("genericdmm: sdi was NULL.");
		return SR_ERR_BUG;
	}

	if (!(ctx = sdi->priv)) {
		sr_err("genericdmm: sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_spew("genericdmm: dev_index %d, hwcap %d.", dev_index, hwcap);

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
	case SR_HWCAP_MODEL:
		for (i = 0; dev_profiles[i].model; i++) {
			if (!strcasecmp(dev_profiles[i].model, value)) {
				ctx->profile = &dev_profiles[i];
				/* Frontends access these fields directly, so we
				 * need to copy them over. */
				sdi->vendor = g_strdup(dev_profiles[i].vendor);
				sdi->model = g_strdup(dev_profiles[i].model);
				/* This is the first time we actually know which
				 * DMM chip we're talking to, so let's init
				 * anything specific to it now */
				if (ctx->profile->chip->init)
					if (ctx->profile->chip->init(ctx) != SR_OK)
						return SR_ERR;
				break;
			}
		}
		if (!ctx->profile) {
			sr_err("unknown model %s", value);
			return SR_ERR;
		}
		break;
	case SR_HWCAP_CONN:
		if (parse_conn(sdi, value) != SR_OK)
			return SR_ERR_ARG;
		break;
	case SR_HWCAP_SERIALCOMM:
		if (parse_serialcomm(sdi, value) != SR_OK)
			return SR_ERR_ARG;
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

static int hw_dev_acquisition_start(int dev_index, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(gdi->instances, dev_index))) {
		sr_err("genericdmm: sdi was NULL.");
		return SR_ERR_BUG;
	}

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

static int hw_dev_acquisition_stop(int dev_index, void *cb_data)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	(void)dev_index;

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
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.instances = NULL,
};
