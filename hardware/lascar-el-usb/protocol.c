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

#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define LASCAR_VENDOR "Lascar"
#define LASCAR_INTERFACE 0
#define LASCAR_EP_IN 0x82
#define LASCAR_EP_OUT 2
/* Max 100ms for a device to positively identify. */
#define SCAN_TIMEOUT 100000

extern struct sr_dev_driver lascar_el_usb_driver_info;
static struct sr_dev_driver *di = &lascar_el_usb_driver_info;

static const struct elusb_profile profiles[] = {
	{ 1, "EL-USB-1", LOG_UNSUPPORTED },
	{ 2, "EL-USB-1", LOG_UNSUPPORTED },
	{ 3, "EL-USB-2", LOG_TEMP_RH },
	{ 4, "EL-USB-3", LOG_UNSUPPORTED },
	{ 5, "EL-USB-4", LOG_UNSUPPORTED },
	{ 6, "EL-USB-3", LOG_UNSUPPORTED },
	{ 7, "EL-USB-4", LOG_UNSUPPORTED },
	{ 8, "EL-USB-LITE", LOG_UNSUPPORTED },
	{ 9, "EL-USB-CO", LOG_CO },
	{ 10, "EL-USB-TC", LOG_UNSUPPORTED },
	{ 11, "EL-USB-CO300", LOG_UNSUPPORTED },
	{ 12, "EL-USB-2-LCD", LOG_UNSUPPORTED },
	{ 13, "EL-USB-2+", LOG_UNSUPPORTED },
	{ 14, "EL-USB-1-PRO", LOG_UNSUPPORTED },
	{ 15, "EL-USB-TC-LCD", LOG_UNSUPPORTED },
	{ 16, "EL-USB-2-LCD+", LOG_UNSUPPORTED },
	{ 17, "EL-USB-5", LOG_UNSUPPORTED },
	{ 18, "EL-USB-1-RCG", LOG_UNSUPPORTED },
	{ 19, "EL-USB-1-LCD", LOG_UNSUPPORTED },
	{ 20, "EL-OEM-3", LOG_UNSUPPORTED },
	{ 21, "EL-USB-1-LCD", LOG_UNSUPPORTED },
	{ 0, NULL, 0 }
};


static void scan_xfer(struct libusb_transfer *xfer)
{

	xfer->user_data = GINT_TO_POINTER(1);

}

static struct sr_dev_inst *lascar_identify(libusb_device_handle *dev_hdl)
{
	struct drv_context *drvc;
	const struct elusb_profile *profile;
	struct sr_dev_inst *sdi;
	struct libusb_transfer *xfer_in, *xfer_out;
	struct timeval tv;
	int64_t start;
	int modelid, buflen, i;
	unsigned char cmd[3], buf[256];
	char firmware[5];

	drvc = di->priv;
	modelid = 0;

	/* Some of these fail, but it needs doing -- some sort of mode
	 * setup for the SILabs F32x. */
	libusb_control_transfer(dev_hdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x00, 0xffff, 0x00, buf, 0, 50);
	libusb_control_transfer(dev_hdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x02, 0x0002, 0x00, buf, 0, 50);
	libusb_control_transfer(dev_hdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x02, 0x0001, 0x00, buf, 0, 50);

	if (!(xfer_in = libusb_alloc_transfer(0)) ||
			!(xfer_out = libusb_alloc_transfer(0)))
		return 0;

	/* Flush anything the F321 still has queued. */
	while (libusb_bulk_transfer(dev_hdl, LASCAR_EP_IN, buf, 256, &buflen,
			5) == 0 && buflen > 0)
		;

	/* Keep a read request waiting in the wings, ready to pounce
	 * the moment the device sends something. */
	libusb_fill_bulk_transfer(xfer_in, dev_hdl, LASCAR_EP_IN,
			buf, 256, scan_xfer, 0, 10000);
	if (libusb_submit_transfer(xfer_in) != 0)
		goto cleanup;

	/* Request device configuration structure. */
	cmd[0] = 0x00;
	cmd[1] = 0xff;
	cmd[2] = 0xff;
	libusb_fill_bulk_transfer(xfer_out, dev_hdl, LASCAR_EP_OUT,
			cmd, 3, scan_xfer, 0, 100);
	if (libusb_submit_transfer(xfer_out) != 0)
		goto cleanup;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	start = g_get_monotonic_time();
	while (!xfer_in->user_data || !xfer_out->user_data) {
		if (g_get_monotonic_time() - start > SCAN_TIMEOUT) {
			start = 0;
			break;
		}
		g_usleep(5000);
		libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
	}
	if (!start) {
		sr_dbg("no response");
		goto cleanup;
	}
	if (xfer_in->actual_length != 3) {
		sr_dbg("expected 3-byte header, got %d bytes", xfer_in->actual_length);
		goto cleanup;
	}

	/* Got configuration structure header. */
	sr_spew("response to config request: 0x%.2x 0x%.2x 0x%.2x ",
			buf[0], buf[1], buf[2]);
	buflen = buf[1] | (buf[2] << 8);
	if (buf[0] != 0x02 || buflen > 256) {
		sr_dbg("Invalid response to config request: "
				"0x%.2x 0x%.2x 0x%.2x ", buf[0], buf[1], buf[2]);
		libusb_close(dev_hdl);
		goto cleanup;
	}

	/* Get configuration structure. */
	xfer_in->length = buflen;
	xfer_in->user_data = 0;
	if (libusb_submit_transfer(xfer_in) != 0)
		goto cleanup;
	while (!xfer_in->user_data) {
		if (g_get_monotonic_time() - start > SCAN_TIMEOUT) {
			start = 0;
			break;
		}
		g_usleep(5000);
		libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
	}
	if (!start) {
		sr_dbg("Timeout waiting for configuration structure.");
		goto cleanup;
	}
	if (xfer_in->actual_length != buflen) {
		sr_dbg("expected %d-byte structure, got %d bytes", buflen,
				xfer_in->actual_length);
		goto cleanup;
	}
	modelid = buf[0];

cleanup:
	if (!xfer_in->user_data || !xfer_in->user_data) {
		if (!xfer_in->user_data)
			libusb_cancel_transfer(xfer_in);
		if (!xfer_out->user_data)
			libusb_cancel_transfer(xfer_out);
		start = g_get_monotonic_time();
		while (!xfer_in->user_data || !xfer_out->user_data) {
			if (g_get_monotonic_time() - start > 10000)
				break;
			g_usleep(1000);
			libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
		}
	}
	libusb_free_transfer(xfer_in);
	libusb_free_transfer(xfer_out);

	sdi = NULL;
	if (modelid) {
		profile = NULL;
		for (i = 0; profiles[i].modelid; i++) {
			if (profiles[i].modelid == modelid) {
				profile = &profiles[i];
				break;
			}
		}
		if (!profile) {
			sr_dbg("unknown EL-USB modelid %d", modelid);
			return NULL;
		}

		i = buf[52] | (buf[53] << 8);
		memcpy(firmware, buf + 0x30, 4);
		firmware[4] = '\0';
		sr_dbg("found %s with firmware version %s serial %d",
				profile->modelname, firmware, i);

		if (profile->logformat == LOG_UNSUPPORTED) {
			sr_dbg("unsupported EL-USB logformat for %s", profile->modelname);
			return NULL;
		}

		if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, LASCAR_VENDOR,
				profile->modelname, firmware)))
			return NULL;
		sdi->driver = di;
	}

	return sdi;
}

SR_PRIV struct sr_dev_inst *lascar_scan(int bus, int address)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct libusb_device **devlist;
	struct libusb_device_descriptor des;
	libusb_device_handle *dev_hdl;
	int ret, i;

	drvc = di->priv;
	sdi = NULL;

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %d.", ret);
			continue;
		}

		if (libusb_get_bus_number(devlist[i]) != bus ||
				libusb_get_device_address(devlist[i]) != address)
			continue;

		if ((ret = libusb_open(devlist[i], &dev_hdl)) != 0) {
			sr_dbg("failed to open device for scan: %s",
					libusb_error_name(ret));
			continue;
		}

		sdi = lascar_identify(dev_hdl);
		libusb_close(dev_hdl);
	}

	return sdi;
}


SR_PRIV int lascar_el_usb_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}
