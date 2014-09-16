/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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

#include "protocol.h"

#define USB_VENDOR_ID			0x0403
#define USB_DEVICE_ID			0x6014
#define USB_VENDOR_NAME			"IKALOGIC"
#define USB_MODEL_NAME			"ScanaPLUS"
#define USB_IPRODUCT			"SCANAPLUS"

#define SAMPLE_BUF_SIZE			(8 * 1024 * 1024)

static const uint32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS, // TODO?
};

/* Channels are numbered 1-9. */
static const char *channel_names[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9",
	NULL,
};

/* Note: The IKALOGIC ScanaPLUS always samples at 100MHz. */
static uint64_t samplerates[1] = { SR_MHZ(100) };

SR_PRIV struct sr_dev_driver ikalogic_scanaplus_driver_info;
static struct sr_dev_driver *di = &ikalogic_scanaplus_driver_info;

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	ftdi_free(devc->ftdic);
	g_free(devc->compressed_buf);
	g_free(devc->sample_buf);
	g_free(devc);
}

static int dev_clear(void)
{
	return std_dev_clear(di, clear_helper);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *devices;
	unsigned int i;
	int ret;

	(void)options;

	drvc = di->priv;

	devices = NULL;

	/* Allocate memory for our private device context. */
	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto err_free_nothing;
	}

	/* Allocate memory for the incoming compressed samples. */
	if (!(devc->compressed_buf = g_try_malloc0(COMPRESSED_BUF_SIZE))) {
		sr_err("compressed_buf malloc failed.");
		goto err_free_devc;
	}

	/* Allocate memory for the uncompressed samples. */
	if (!(devc->sample_buf = g_try_malloc0(SAMPLE_BUF_SIZE))) {
		sr_err("sample_buf malloc failed.");
		goto err_free_compressed_buf;
	}

	/* Allocate memory for the FTDI context (ftdic) and initialize it. */
	if (!(devc->ftdic = ftdi_new())) {
		sr_err("Failed to initialize libftdi.");
		goto err_free_sample_buf;
	}

	/* Check for the device and temporarily open it. */
	ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID, USB_DEVICE_ID,
				 USB_IPRODUCT, NULL);
	if (ret < 0) {
		/* Log errors, except for -3 ("device not found"). */
		if (ret != -3)
			sr_err("Failed to open device (%d): %s", ret,
			       ftdi_get_error_string(devc->ftdic));
		goto err_free_ftdic;
	}

	/* Register the device with libsigrok. */
	sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, NULL);
	if (!sdi) {
		sr_err("Failed to create device instance.");
		goto err_close_ftdic;
	}
	sdi->driver = di;
	sdi->priv = devc;

	for (i = 0; channel_names[i]; i++) {
		if (!(ch = sr_channel_new(i, SR_CHANNEL_LOGIC, TRUE,
					   channel_names[i])))
			return NULL;
		sdi->channels = g_slist_append(sdi->channels, ch);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);

	/* Close device. We'll reopen it again when we need it. */
	scanaplus_close(devc);

	return devices;

err_close_ftdic:
	scanaplus_close(devc);
err_free_ftdic:
	ftdi_free(devc->ftdic); /* NOT free() or g_free()! */
err_free_sample_buf:
	g_free(devc->sample_buf);
err_free_compressed_buf:
	g_free(devc->compressed_buf);
err_free_devc:
	g_free(devc);
err_free_nothing:

	return NULL;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	/* Select interface A, otherwise communication will fail. */
	ret = ftdi_set_interface(devc->ftdic, INTERFACE_A);
	if (ret < 0) {
		sr_err("Failed to set FTDI interface A (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	sr_dbg("FTDI chip interface A set successfully.");

	/* Open the device. */
	ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID, USB_DEVICE_ID,
				 USB_IPRODUCT, NULL);
	if (ret < 0) {
		sr_err("Failed to open device (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	sr_dbg("FTDI device opened successfully.");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0) {
		sr_err("Failed to purge FTDI RX/TX buffers (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip buffers purged successfully.");

	/* Reset the FTDI bitmode. */
	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_RESET);
	if (ret < 0) {
		sr_err("Failed to reset the FTDI chip bitmode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip bitmode reset successfully.");

	/* Set FTDI bitmode to "sync FIFO". */
	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_SYNCFF);
	if (ret < 0) {
		sr_err("Failed to put FTDI chip into sync FIFO mode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip sync FIFO mode entered successfully.");

	/* Set the FTDI latency timer to 2. */
	ret = ftdi_set_latency_timer(devc->ftdic, 2);
	if (ret < 0) {
		sr_err("Failed to set FTDI latency timer (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip latency timer set successfully.");

	/* Set the FTDI read data chunk size to 64kB. */
	ret = ftdi_read_data_set_chunksize(devc->ftdic, 64 * 1024);
	if (ret < 0) {
		sr_err("Failed to set FTDI read data chunk size (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip read data chunk size set successfully.");

	/* Get the ScanaPLUS device ID from the FTDI EEPROM. */
	if ((ret = scanaplus_get_device_id(devc)) < 0) {
		sr_err("Failed to get ScanaPLUS device ID: %d.", ret);
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("Received ScanaPLUS device ID successfully: %02x %02x %02x.",
	       devc->devid[0], devc->devid[1], devc->devid[2]);

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;

err_dev_open_close_ftdic:
	scanaplus_close(devc);
	return SR_ERR;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	ret = SR_OK;
	devc = sdi->priv;

	if (sdi->status == SR_ST_ACTIVE) {
		sr_dbg("Status ACTIVE, closing device.");
		ret = scanaplus_close(devc);
	} else {
		sr_spew("Status not ACTIVE, nothing to do.");
	}

	sdi->status = SR_ST_INACTIVE;

	return ret;
}

static int cleanup(void)
{
	return dev_clear();
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		/* The ScanaPLUS samplerate is 100MHz and can't be changed. */
		*data = g_variant_new_uint64(SR_MHZ(100));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (g_variant_get_uint64(data) != SR_MHZ(100)) {
			sr_err("ScanaPLUS only supports samplerate = 100MHz.");
			return SR_ERR_ARG;
		}
		/* Nothing to do, the ScanaPLUS samplerate is always 100MHz. */
		break;
	case SR_CONF_LIMIT_MSEC:
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				samplerates, ARRAY_SIZE(samplerates),
				sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	int ret;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv))
		return SR_ERR_BUG;

	if (!devc->ftdic)
		return SR_ERR_BUG;

	/* TODO: Configure channels later (thresholds etc.). */

	devc->cb_data = cb_data;

	/* Properly reset internal variables before every new acquisition. */
	devc->compressed_bytes_ignored = 0;
	devc->samples_sent = 0;
	devc->bytes_received = 0;

	if ((ret = scanaplus_init(devc)) < 0)
		return ret;

	if ((ret = scanaplus_start_acquisition(devc)) < 0)
		return ret;

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);

	/* Hook up a dummy handler to receive data from the device. */
	sr_session_source_add(sdi->session, -1, G_IO_IN, 0, scanaplus_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)cb_data;

	sr_dbg("Stopping acquisition.");
	sr_session_source_remove(sdi->session, -1);

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver ikalogic_scanaplus_driver_info = {
	.name = "ikalogic-scanaplus",
	.longname = "IKALOGIC ScanaPLUS",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
