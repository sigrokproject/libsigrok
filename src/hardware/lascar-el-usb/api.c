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

#include <config.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

SR_PRIV struct sr_dev_driver lascar_el_usb_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_THERMOMETER,
	SR_CONF_HYGROMETER,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATALOG | SR_CONF_GET | SR_CONF_SET,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	GSList *usb_devices, *devices, *l;
	const char *conn;

	drvc = di->context;

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
			sdi->inst_type = SR_INST_USB;
			sdi->conn = usb;
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
		}
		g_slist_free(usb_devices);
	} else
		g_slist_free_full(usb_devices, g_free);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	if (!(drvc = di->context)) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_claim_interface(usb->devhdl, LASCAR_INTERFACE))) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sdi->status = SR_ST_ACTIVE;

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct sr_usb_dev_inst *usb;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (!usb->devhdl)
		/*  Nothing to do. */
		return SR_OK;

	libusb_release_interface(usb->devhdl, LASCAR_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->context))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	ret = std_dev_clear(di, NULL);
	g_free(drvc);

	return ret;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret;
	char str[128];

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		snprintf(str, 128, "%d.%d", usb->bus, usb->address);
		*data = g_variant_new_string(str);
		break;
	case SR_CONF_DATALOG:
		if (!sdi)
			return SR_ERR_ARG;
		if ((ret = lascar_is_logging(sdi)) == -1)
			return SR_ERR;
		*data = g_variant_new_boolean(ret ? TRUE : FALSE);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_dev_driver *di = sdi->driver;
	struct dev_context *devc;
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {
	case SR_CONF_DATALOG:
		if (g_variant_get_boolean(data))
			ret = lascar_start_logging(sdi);
		else
			ret = lascar_stop_logging(sdi);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void LIBUSB_CALL mark_xfer(struct libusb_transfer *xfer)
{

	if (xfer->status == LIBUSB_TRANSFER_COMPLETED)
		xfer->user_data = GINT_TO_POINTER(1);
	else
		xfer->user_data = GINT_TO_POINTER(-1);

}

/* The Lascar software, in its infinite ignorance, reads a set of four
 * bytes from the device config struct and interprets it as a float.
 * That only works because they only use windows, and only on x86. However
 * we may be running on any architecture, any operating system. So we have
 * to convert these four bytes as the Lascar software would on windows/x86,
 * to the local representation of a float.
 * The source format is little-endian, with IEEE 754-2008 BINARY32 encoding. */
static float binary32_le_to_float(unsigned char *buf)
{
	GFloatIEEE754 f;

	f.v_float = 0;
	f.mpn.sign = (buf[3] & 0x80) ? 1 : 0;
	f.mpn.biased_exponent = (buf[3] << 1) | (buf[2] >> 7);
	f.mpn.mantissa = buf[0] | (buf[1] << 8) | ((buf[2] & 0x7f) << 16);

	return f.v_float;
}

static int lascar_proc_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int dummy, ret;

	devc = sdi->priv;
	usb = sdi->conn;

	if (lascar_get_config(usb->devhdl, devc->config, &dummy) != SR_OK)
		return SR_ERR;

	ret = SR_OK;
	switch (devc->profile->logformat) {
	case LOG_TEMP_RH:
		devc->sample_size = 2;
		devc->temp_unit = devc->config[0x2e] | (devc->config[0x2f] << 8);
		if (devc->temp_unit != 0 && devc->temp_unit != 1) {
			sr_dbg("invalid temperature unit %d", devc->temp_unit);
			/* Default to Celsius, we're all adults here. */
			devc->temp_unit = 0;
		} else
			sr_dbg("temperature unit is %s", devc->temp_unit
					? "Fahrenheit" : "Celsius");
		break;
	case LOG_CO:
		devc->sample_size = 2;
		devc->co_high = binary32_le_to_float(devc->config + 0x24);
		devc->co_low = binary32_le_to_float(devc->config + 0x28);
		sr_dbg("EL-USB-CO calibration high %f low %f", devc->co_high,
				devc->co_low);
		break;
	default:
		ret = SR_ERR_ARG;
	}
	devc->logged_samples = devc->config[0x1e] | (devc->config[0x1f] << 8);
	sr_dbg("device log contains %d samples.", devc->logged_samples);

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_dev_driver *di = sdi->driver;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *xfer_in, *xfer_out;
	struct timeval tv;
	uint64_t interval;
	int ret;
	unsigned char cmd[3], resp[4], *buf;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;
	devc->cb_data = cb_data;

	if (lascar_proc_config(sdi) != SR_OK)
		return SR_ERR;

	sr_dbg("Starting log retrieval.");

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	interval = (devc->config[0x1c] | (devc->config[0x1d] << 8)) * 1000;
	packet.type = SR_DF_META;
	packet.payload = &meta;
	src = sr_config_new(SR_CONF_SAMPLE_INTERVAL, g_variant_new_uint64(interval));
	meta.config = g_slist_append(NULL, src);
	sr_session_send(devc->cb_data, &packet);
	g_free(src);

	if (devc->logged_samples == 0) {
		/* This ensures the frontend knows the session is done. */
		packet.type = SR_DF_END;
		sr_session_send(devc->cb_data, &packet);
		return SR_OK;
	}

	if (!(xfer_in = libusb_alloc_transfer(0)) ||
			!(xfer_out = libusb_alloc_transfer(0)))
		return SR_ERR;

	libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x00, 0xffff, 0x00, NULL, 0, 50);
	libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x02, 0x0002, 0x00, NULL, 0, 50);
	libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x02, 0x0001, 0x00, NULL, 0, 50);

	/* Flush input. The F321 requires this. */
	while (libusb_bulk_transfer(usb->devhdl, LASCAR_EP_IN, resp,
			256, &ret, 5) == 0 && ret > 0)
		;

	libusb_fill_bulk_transfer(xfer_in, usb->devhdl, LASCAR_EP_IN,
			resp, sizeof(resp), mark_xfer, 0, BULK_XFER_TIMEOUT);
	if (libusb_submit_transfer(xfer_in) != 0) {
		libusb_free_transfer(xfer_in);
		libusb_free_transfer(xfer_out);
		return SR_ERR;
	}

	cmd[0] = 0x03;
	cmd[1] = 0xff;
	cmd[2] = 0xff;
	libusb_fill_bulk_transfer(xfer_out, usb->devhdl, LASCAR_EP_OUT,
			cmd, 3, mark_xfer, 0, 100);
	if (libusb_submit_transfer(xfer_out) != 0) {
		libusb_free_transfer(xfer_in);
		libusb_free_transfer(xfer_out);
		return SR_ERR;
	}

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	while (!xfer_in->user_data || !xfer_out->user_data) {
		g_usleep(SLEEP_US_LONG);
		libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
	}
	if (xfer_in->user_data != GINT_TO_POINTER(1) ||
			xfer_in->user_data != GINT_TO_POINTER(1)) {
		sr_dbg("no response to log transfer request");
		libusb_free_transfer(xfer_in);
		libusb_free_transfer(xfer_out);
		return SR_ERR;
	}
	if (xfer_in->actual_length != 3 || xfer_in->buffer[0] != 2) {
		sr_dbg("invalid response to log transfer request");
		libusb_free_transfer(xfer_in);
		libusb_free_transfer(xfer_out);
		return SR_ERR;
	}
	devc->log_size = xfer_in->buffer[1] + (xfer_in->buffer[2] << 8);
	libusb_free_transfer(xfer_out);

	usb_source_add(sdi->session, drvc->sr_ctx, 100,
			lascar_el_usb_handle_events, (void *)sdi);

	buf = g_malloc(4096);
	libusb_fill_bulk_transfer(xfer_in, usb->devhdl, LASCAR_EP_IN,
			buf, 4096, lascar_el_usb_receive_transfer, cb_data, 100);
	if ((ret = libusb_submit_transfer(xfer_in) != 0)) {
		sr_err("Unable to submit transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(xfer_in);
		g_free(buf);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_dev_driver *di = sdi->driver;
	(void)cb_data;

	if (!di->context) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	sdi->status = SR_ST_STOPPING;
	/* TODO: free ongoing transfers? */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver lascar_el_usb_driver_info = {
	.name = "lascar-el-usb",
	.longname = "Lascar EL-USB",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
