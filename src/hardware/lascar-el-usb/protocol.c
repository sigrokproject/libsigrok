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
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

extern struct sr_dev_driver lascar_el_usb_driver_info;
struct sr_dev_driver *di = &lascar_el_usb_driver_info;

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
	{ 11, "EL-USB-CO300", LOG_CO },
	{ 12, "EL-USB-2-LCD", LOG_TEMP_RH },
	{ 13, "EL-USB-2+", LOG_TEMP_RH },
	{ 14, "EL-USB-1-PRO", LOG_UNSUPPORTED },
	{ 15, "EL-USB-TC-LCD", LOG_UNSUPPORTED },
	{ 16, "EL-USB-2-LCD+", LOG_TEMP_RH },
	{ 17, "EL-USB-5", LOG_UNSUPPORTED },
	{ 18, "EL-USB-1-RCG", LOG_UNSUPPORTED },
	{ 19, "EL-USB-1-LCD", LOG_UNSUPPORTED },
	{ 20, "EL-OEM-3", LOG_UNSUPPORTED },
	{ 21, "EL-USB-1-LCD", LOG_UNSUPPORTED },
	{ 0, NULL, 0 }
};

static libusb_device_handle *lascar_open(struct libusb_device *dev)
{
	libusb_device_handle *dev_hdl;
	int ret;

	if ((ret = libusb_open(dev, &dev_hdl)) != 0) {
		sr_dbg("failed to open device for scan: %s",
				libusb_error_name(ret));
		return NULL;
	}

	/* Some of these fail, but it needs doing -- some sort of mode
	 * setup for the SiLabs F32x. */
	libusb_control_transfer(dev_hdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x00, 0xffff, 0x00, NULL, 0, 50);
	libusb_control_transfer(dev_hdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x02, 0x0002, 0x00, NULL, 0, 50);
	libusb_control_transfer(dev_hdl, LIBUSB_REQUEST_TYPE_VENDOR,
			0x02, 0x0001, 0x00, NULL, 0, 50);

	return dev_hdl;
}

static void LIBUSB_CALL mark_xfer(struct libusb_transfer *xfer)
{

	xfer->user_data = GINT_TO_POINTER(1);

}

SR_PRIV int lascar_get_config(libusb_device_handle *dev_hdl,
		unsigned char *configblock, int *configlen)
{
	struct drv_context *drvc;
	struct libusb_transfer *xfer_in, *xfer_out;
	struct timeval tv;
	int64_t start;
	int buflen;
	unsigned char cmd[3], buf[MAX_CONFIGBLOCK_SIZE];

	sr_spew("Reading config block.");

	drvc = di->context;
	*configlen = 0;

	if (!(xfer_in = libusb_alloc_transfer(0)) ||
			!(xfer_out = libusb_alloc_transfer(0)))
		return SR_ERR;

	/* Flush anything the F321 still has queued. */
	while (libusb_bulk_transfer(dev_hdl, LASCAR_EP_IN, buf, 256, &buflen,
			5) == 0 && buflen > 0)
		;

	/* Keep a read request waiting in the wings, ready to pounce
	 * the moment the device sends something. */
	libusb_fill_bulk_transfer(xfer_in, dev_hdl, LASCAR_EP_IN,
			buf, 256, mark_xfer, 0, BULK_XFER_TIMEOUT);
	if (libusb_submit_transfer(xfer_in) != 0)
		goto cleanup;

	/* Request device configuration structure. */
	cmd[0] = 0x00;
	cmd[1] = 0xff;
	cmd[2] = 0xff;
	libusb_fill_bulk_transfer(xfer_out, dev_hdl, LASCAR_EP_OUT,
			cmd, 3, mark_xfer, 0, 100);
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
		g_usleep(SLEEP_US_LONG);
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
	sr_spew("Response to config request: 0x%.2x 0x%.2x 0x%.2x ",
			buf[0], buf[1], buf[2]);
	buflen = buf[1] | (buf[2] << 8);
	if (buf[0] != 0x02 || buflen > MAX_CONFIGBLOCK_SIZE) {
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
		g_usleep(SLEEP_US_LONG);
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

	memcpy(configblock, buf, buflen);
	*configlen = buflen;

cleanup:
	if (!xfer_in->user_data || !xfer_out->user_data) {
		if (!xfer_in->user_data)
			libusb_cancel_transfer(xfer_in);
		if (!xfer_out->user_data)
			libusb_cancel_transfer(xfer_out);
		start = g_get_monotonic_time();
		while (!xfer_in->user_data || !xfer_out->user_data) {
			if (g_get_monotonic_time() - start > EVENTS_TIMEOUT)
				break;
			g_usleep(SLEEP_US_SHORT);
			libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
		}
	}
	libusb_free_transfer(xfer_in);
	libusb_free_transfer(xfer_out);

	return *configlen ? SR_OK : SR_ERR;
}

static int lascar_save_config(libusb_device_handle *dev_hdl,
		unsigned char *config, int configlen)
{
	struct drv_context *drvc;
	struct libusb_transfer *xfer_in, *xfer_out;
	struct timeval tv;
	int64_t start;
	int buflen, ret;
	unsigned char cmd[3], buf[256];

	sr_spew("Writing config block.");

	drvc = di->context;

	if (!(xfer_in = libusb_alloc_transfer(0)) ||
			!(xfer_out = libusb_alloc_transfer(0)))
		return SR_ERR;

	/* Flush anything the F321 still has queued. */
	while (libusb_bulk_transfer(dev_hdl, LASCAR_EP_IN, buf, 256, &buflen,
			5) == 0 && buflen > 0)
		;
	ret = SR_OK;

	/* Keep a read request waiting in the wings, ready to pounce
	 * the moment the device sends something. */
	libusb_fill_bulk_transfer(xfer_in, dev_hdl, LASCAR_EP_IN,
			buf, 256, mark_xfer, 0, BULK_XFER_TIMEOUT);
	if (libusb_submit_transfer(xfer_in) != 0) {
		ret = SR_ERR;
		goto cleanup;
	}

	/* Request device configuration structure. */
	cmd[0] = 0x01;
	cmd[1] = configlen & 0xff;
	cmd[2] = (configlen >> 8) & 0xff;
	libusb_fill_bulk_transfer(xfer_out, dev_hdl, LASCAR_EP_OUT,
			cmd, 3, mark_xfer, 0, 100);
	if (libusb_submit_transfer(xfer_out) != 0) {
		ret = SR_ERR;
		goto cleanup;
	}
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	while (!xfer_out->user_data) {
		g_usleep(SLEEP_US_LONG);
		libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
	}

	libusb_fill_bulk_transfer(xfer_out, dev_hdl, LASCAR_EP_OUT,
			config, configlen, mark_xfer, 0, 100);
	if (libusb_submit_transfer(xfer_out) != 0) {
		ret = SR_ERR;
		goto cleanup;
	}
	while (!xfer_in->user_data || !xfer_out->user_data) {
		g_usleep(SLEEP_US_LONG);
		libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
	}

	if (xfer_in->actual_length != 1 || buf[0] != 0xff) {
		sr_dbg("unexpected response after transfer");
		ret = SR_ERR;
	}

cleanup:
	if (!xfer_in->user_data || !xfer_out->user_data) {
		if (!xfer_in->user_data)
			libusb_cancel_transfer(xfer_in);
		if (!xfer_out->user_data)
			libusb_cancel_transfer(xfer_out);
		start = g_get_monotonic_time();
		while (!xfer_in->user_data || !xfer_out->user_data) {
			if (g_get_monotonic_time() - start > EVENTS_TIMEOUT)
				break;
			g_usleep(SLEEP_US_SHORT);
			libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);
		}
	}
	libusb_free_transfer(xfer_in);
	libusb_free_transfer(xfer_out);

	return ret;
}

static struct sr_dev_inst *lascar_identify(unsigned char *config)
{
	struct dev_context *devc;
	const struct elusb_profile *profile;
	struct sr_dev_inst *sdi;
	int modelid, i;
	char firmware[5];

	modelid = config[0];
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

		i = config[52] | (config[53] << 8);
		memcpy(firmware, config + 0x30, 4);
		firmware[4] = '\0';
		sr_dbg("found %s with firmware version %s serial %d",
				profile->modelname, firmware, i);

		if (profile->logformat == LOG_UNSUPPORTED) {
			sr_dbg("unsupported EL-USB logformat for %s", profile->modelname);
			return NULL;
		}

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(LASCAR_VENDOR);
		sdi->model = g_strdup(profile->modelname);
		sdi->version = g_strdup(firmware);
		sdi->driver = di;

		if (profile->logformat == LOG_TEMP_RH) {
			/* Model this as two channels: temperature and humidity. */
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "Temp");
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "Hum");
		} else if (profile->logformat == LOG_CO) {
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CO");
		} else {
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
		}

		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		devc->profile = profile;
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
	int dummy, ret, i;
	unsigned char config[MAX_CONFIGBLOCK_SIZE];

	drvc = di->context;
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

		if (!(dev_hdl = lascar_open(devlist[i])))
			continue;

		if (lascar_get_config(dev_hdl, config, &dummy) != SR_OK)
			continue;

		libusb_close(dev_hdl);
		sdi = lascar_identify(config);
	}

	return sdi;
}

static void lascar_el_usb_dispatch(struct sr_dev_inst *sdi, unsigned char *buf,
		int buflen)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_channel *ch;
	float *temp, *rh;
	uint16_t s;
	int samples, samples_left, i, j;

	devc = sdi->priv;

	samples = buflen / devc->sample_size;
	samples_left = devc->logged_samples - devc->rcvd_samples;
	if (samples_left < samples)
		samples = samples_left;
	switch (devc->profile->logformat) {
	case LOG_TEMP_RH:
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.mqflags = 0;
		if (!(temp = g_try_malloc(sizeof(float) * samples)))
			break;
		if (!(rh = g_try_malloc(sizeof(float) * samples)))
			break;
		for (i = 0, j = 0; i < samples; i++) {
			/* Both Celsius and Fahrenheit stored at base -40. */
			if (devc->temp_unit == 0)
				/* Celsius is stored in half-degree increments. */
				temp[j] = buf[i * 2] / 2 - 40;
			else
				temp[j] = buf[i * 2] - 40;

			rh[j] = buf[i * 2 + 1] / 2;

			if (temp[j] == 0.0 && rh[j] == 0.0)
				/* Skip invalid measurement. */
				continue;
			j++;
		}
		analog.num_samples = j;

		ch = sdi->channels->data;
		if (ch->enabled) {
			analog.channels = g_slist_append(NULL, ch);
			analog.mq = SR_MQ_TEMPERATURE;
			if (devc->temp_unit == 1)
				analog.unit = SR_UNIT_FAHRENHEIT;
			else
				analog.unit = SR_UNIT_CELSIUS;
			analog.data = temp;
			sr_session_send(devc->cb_data, &packet);
			g_slist_free(analog.channels);
		}

		ch = sdi->channels->next->data;
		if (ch->enabled) {
			analog.channels = g_slist_append(NULL, ch);
			analog.mq = SR_MQ_RELATIVE_HUMIDITY;
			analog.unit = SR_UNIT_PERCENTAGE;
			analog.data = rh;
			sr_session_send(devc->cb_data, &packet);
			g_slist_free(analog.channels);
		}

		g_free(temp);
		g_free(rh);
		break;
	case LOG_CO:
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.channels = sdi->channels;
		analog.num_samples = samples;
		analog.mq = SR_MQ_CARBON_MONOXIDE;
		analog.unit = SR_UNIT_CONCENTRATION;
		analog.mqflags = 0;
		if (!(analog.data = g_try_malloc(sizeof(float) * samples)))
			break;
		for (i = 0; i < samples; i++) {
			s = (buf[i * 2] << 8) | buf[i * 2 + 1];
			analog.data[i] = (s * devc->co_high + devc->co_low) / (1000 * 1000);
			if (analog.data[i] < 0.0)
				analog.data[i] = 0.0;
		}
		sr_session_send(devc->cb_data, &packet);
		g_free(analog.data);
		break;
	default:
		/* How did we even get this far? */
		break;
	}
	devc->rcvd_samples += samples;

}

SR_PRIV int lascar_el_usb_handle_events(int fd, int revents, void *cb_data)
{
	struct drv_context *drvc = di->context;
	struct sr_datafeed_packet packet;
	struct sr_dev_inst *sdi;
	struct timeval tv;

	(void)fd;
	(void)revents;

	sdi = cb_data;

	if (sdi->status == SR_ST_STOPPING) {
		usb_source_remove(sdi->session, drvc->sr_ctx);

		packet.type = SR_DF_END;
		sr_session_send(cb_data, &packet);
	}

	memset(&tv, 0, sizeof(struct timeval));
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
					       NULL);

	return TRUE;
}

SR_PRIV void LIBUSB_CALL lascar_el_usb_receive_transfer(struct libusb_transfer *transfer)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	int ret;
	gboolean packet_has_error;

	sdi = transfer->user_data;
	devc = sdi->priv;

	packet_has_error = FALSE;
	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		/* USB device was unplugged. */
		dev_acquisition_stop(sdi, sdi);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (!packet_has_error) {
		if (devc->rcvd_samples < devc->logged_samples)
			lascar_el_usb_dispatch(sdi, transfer->buffer,
					transfer->actual_length);
		devc->rcvd_bytes += transfer->actual_length;
		sr_spew("received %d/%d bytes (%d/%d samples)",
				devc->rcvd_bytes, devc->log_size,
				devc->rcvd_samples, devc->logged_samples);
		if (devc->rcvd_bytes >= devc->log_size)
			dev_acquisition_stop(sdi, sdi);
	}

	if (sdi->status == SR_ST_ACTIVE) {
		/* Send the same request again. */
		if ((ret = libusb_submit_transfer(transfer) != 0)) {
			sr_err("Unable to resubmit transfer: %s.",
			       libusb_error_name(ret));
			g_free(transfer->buffer);
			libusb_free_transfer(transfer);
			dev_acquisition_stop(sdi, sdi);
		}
	} else {
		/* This was the last transfer we're going to receive, so
		 * clean up now. */
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
	}

}

static int get_flags(unsigned char *configblock)
{
	int flags;

	flags = (configblock[32] | (configblock[33] << 8)) & 0x1fff;
	sr_spew("Read flags (0x%.4x).", flags);

	return flags;
}

static int set_flags(unsigned char *configblock, int flags)
{

	sr_spew("Setting flags to 0x%.4x.", flags);
	configblock[32] = flags & 0xff;
	configblock[33] = (flags >> 8) & 0x1f;

	return flags;
}

SR_PRIV int lascar_is_logging(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int dummy, flags, ret;

	devc = sdi->priv;
	usb = sdi->conn;

	if (lascar_get_config(usb->devhdl, devc->config, &dummy) != SR_OK)
		return -1;

	flags = get_flags(devc->config);
	if (flags & 0x0100)
		ret = 1;
	else
		ret = 0;

	return ret;
}

SR_PRIV int lascar_start_logging(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int len, flags, ret;

	devc = sdi->priv;
	usb = sdi->conn;

	if (lascar_get_config(usb->devhdl, devc->config, &len) != SR_OK)
		return SR_ERR;

	/* Turn on logging. */
	flags = get_flags(devc->config);
	flags |= 0x0100;
	set_flags(devc->config, flags);

	/* Start logging in 0 seconds. */
	memset(devc->config + 24, 0, 4);

	ret = lascar_save_config(usb->devhdl, devc->config, len);
	sr_info("Started internal logging.");

	return ret;
}

SR_PRIV int lascar_stop_logging(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int len, flags, ret;

	devc = sdi->priv;
	usb = sdi->conn;

	if (lascar_get_config(usb->devhdl, devc->config, &len) != SR_OK)
		return SR_ERR;

	flags = get_flags(devc->config);
	flags &= ~0x0100;
	set_flags(devc->config, flags);

	ret = lascar_save_config(usb->devhdl, devc->config, len);
	sr_info("Stopped internal logging.");

	return ret;
}
