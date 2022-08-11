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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

#define USB_VENDOR_ID			0x0403
#define USB_DEVICE_ID			0x6014
#define USB_IPRODUCT			"SCANAPLUS"

#define SAMPLE_BUF_SIZE			(8 * 1024 * 1024)

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9",
};

/* Note: The IKALOGIC ScanaPLUS always samples at 100MHz. */
static const uint64_t samplerates[1] = { SR_MHZ(100) };

static void clear_helper(struct dev_context *devc)
{
	ftdi_free(devc->ftdic);
	g_free(devc->compressed_buf);
	g_free(devc->sample_buf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned int i;
	int ret;

	(void)options;

	devc = g_malloc0(sizeof(struct dev_context));

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

	if (!(devc->ftdic = ftdi_new())) {
		sr_err("Failed to initialize libftdi.");
		goto err_free_sample_buf;
	}

	ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID, USB_DEVICE_ID,
				 USB_IPRODUCT, NULL);
	if (ret < 0) {
		/* Log errors, except for -3 ("device not found"). */
		if (ret != -3)
			sr_err("Failed to open device (%d): %s", ret,
			       ftdi_get_error_string(devc->ftdic));
		goto err_free_ftdic;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("IKALOGIC");
	sdi->model = g_strdup("ScanaPLUS");
	sdi->priv = devc;

	for (i = 0; i < ARRAY_SIZE(channel_names); i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_names[i]);

	scanaplus_close(devc);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

	scanaplus_close(devc);
err_free_ftdic:
	ftdi_free(devc->ftdic);
err_free_sample_buf:
	g_free(devc->sample_buf);
err_free_compressed_buf:
	g_free(devc->compressed_buf);
err_free_devc:
	g_free(devc);

	return NULL;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	ret = ftdi_set_interface(devc->ftdic, INTERFACE_A);
	if (ret < 0) {
		sr_err("Failed to set FTDI interface A (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	ret = ftdi_usb_open_desc(devc->ftdic, USB_VENDOR_ID, USB_DEVICE_ID,
				 USB_IPRODUCT, NULL);
	if (ret < 0) {
		sr_err("Failed to open device (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	if ((ret = PURGE_FTDI_BOTH(devc->ftdic)) < 0) {
		sr_err("Failed to purge FTDI RX/TX buffers (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_RESET);
	if (ret < 0) {
		sr_err("Failed to reset the FTDI chip bitmode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_SYNCFF);
	if (ret < 0) {
		sr_err("Failed to put FTDI chip into sync FIFO mode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_set_latency_timer(devc->ftdic, 2);
	if (ret < 0) {
		sr_err("Failed to set FTDI latency timer (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_read_data_set_chunksize(devc->ftdic, 64 * 1024);
	if (ret < 0) {
		sr_err("Failed to set FTDI read data chunk size (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	/* Get the ScanaPLUS device ID from the FTDI EEPROM. */
	if ((ret = scanaplus_get_device_id(devc)) < 0) {
		sr_err("Failed to get ScanaPLUS device ID: %d.", ret);
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("Received ScanaPLUS device ID successfully: %02x %02x %02x.",
	       devc->devid[0], devc->devid[1], devc->devid[2]);

	return SR_OK;

err_dev_open_close_ftdic:
	scanaplus_close(devc);

	return SR_ERR;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	return scanaplus_close(devc);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
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

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

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
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, NO_OPTS, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if (!devc->ftdic)
		return SR_ERR_BUG;

	/* TODO: Configure channels later (thresholds etc.). */

	/* Properly reset internal variables before every new acquisition. */
	devc->compressed_bytes_ignored = 0;
	devc->samples_sent = 0;
	devc->bytes_received = 0;

	if ((ret = scanaplus_init(devc)) < 0)
		return ret;

	if ((ret = scanaplus_start_acquisition(devc)) < 0)
		return ret;

	std_session_send_df_header(sdi);

	/* Hook up a dummy handler to receive data from the device. */
	sr_session_source_add(sdi->session, -1, 0, 0, scanaplus_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver ikalogic_scanaplus_driver_info = {
	.name = "ikalogic-scanaplus",
	.longname = "IKALOGIC ScanaPLUS",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(ikalogic_scanaplus_driver_info);
