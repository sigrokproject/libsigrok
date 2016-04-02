/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Sergey Alirzaev <zl29ah@gmail.com>
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

#include <ftdi.h>
#include <libusb.h>
#include "protocol.h"

SR_PRIV struct sr_dev_driver ftdi_la_driver_info;

static const uint32_t scanopts[] = {
	/* TODO: SR_CONF_CONN to be able to specify the USB address. */
};

static const uint32_t devopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t samplerates[] = {
	SR_HZ(3600),
	SR_MHZ(10),
	SR_HZ(1),
};

static const struct ftdi_chip_desc ft2232h_desc = {
	.vendor = 0x0403,
	.product = 0x6010,
	.samplerate_div = 20,
	.channel_names = {
		"ADBUS0",
		"ADBUS1",
		"ADBUS2",
		"ADBUS3",
		"ADBUS4",
		"ADBUS5",
		"ADBUS6",
		"ADBUS7",
		/* TODO: BDBUS[0..7] channels. */
		NULL
	}
};

static const struct ftdi_chip_desc ft232r_desc = {
	.vendor = 0x0403,
	.product = 0x6001,
	.samplerate_div = 30,
	.channel_names = {
		"TXD",
		"RI#",
		"DCD#",
		"DSR#",
		"DTR#",
		"CTS#",
		"RTS#",
		"RXD",
		NULL
	}
};

static const struct ftdi_chip_desc *chip_descs[] = {
	&ft2232h_desc,
	&ft232r_desc,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	GSList *devices;
	struct ftdi_device_list *devlist = 0;
	struct ftdi_device_list *curdev;
	struct libusb_device_descriptor usb_desc;
	const struct ftdi_chip_desc *desc;
	char *vendor, *model, *serial_num;
	int ret;

	(void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/* Allocate memory for our private device context. */
	devc = g_malloc0(sizeof(struct dev_context));

	/* Allocate memory for the incoming data. */
	devc->data_buf = g_malloc0(DATA_BUF_SIZE);

	/* Allocate memory for the FTDI context (ftdic) and initialize it. */
	devc->ftdic = ftdi_new();
	if (!devc->ftdic) {
		sr_err("Failed to initialize libftdi.");
		goto err_free_data_buf;
	}

	ret = ftdi_usb_find_all(devc->ftdic, &devlist, 0, 0);
	if (ret < 0) {
		sr_err("Failed to list devices (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		goto err_free_ftdic;
	}

	sr_dbg("Number of FTDI devices found: %d", ret);

	curdev = devlist;
	while (curdev) {
		libusb_get_device_descriptor(curdev->dev, &usb_desc);

		desc = NULL;
		for (unsigned long i = 0; i < ARRAY_SIZE(chip_descs); i++) {
			desc = chip_descs[i];
			if (desc->vendor == usb_desc.idVendor &&
				desc->product == usb_desc.idProduct)
				break;
		}

		if (!desc) {
			sr_spew("Unsupported FTDI device 0x%4x:0x%4x.",
				usb_desc.idVendor, usb_desc.idProduct);
			continue;
		}
		devc->usbdev = curdev->dev;
		devc->desc = desc;

		vendor = g_malloc(32);
		model = g_malloc(32);
		serial_num = g_malloc(32);
		ftdi_usb_get_strings(devc->ftdic, curdev->dev, vendor, 32,
				     model, 32, serial_num, 32);
		sr_dbg("Found an FTDI device: %s.", model);

		/* Register the device with libsigrok. */
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = vendor;
		sdi->model = model;
		sdi->serial_num = serial_num;
		sdi->driver = di;
		sdi->priv = devc;

		for (char *const *chan = &(desc->channel_names[0]); *chan; chan++)
			sr_channel_new(sdi, &(desc->channel_names[0]) - chan,
					SR_CHANNEL_LOGIC, TRUE, *chan);

		devices = g_slist_append(devices, sdi);
		drvc->instances = g_slist_append(drvc->instances, sdi);

		curdev = curdev->next;
	}

	return devices;

err_free_ftdic:
	ftdi_free(devc->ftdic); /* NOT free() or g_free()! */
err_free_data_buf:
	g_free(devc->data_buf);
	g_free(devc);

	return NULL;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	ftdi_free(devc->ftdic);
	g_free(devc->data_buf);
	g_free(devc);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret = SR_OK;

	devc = sdi->priv;

	ret = ftdi_usb_open_dev(devc->ftdic, devc->usbdev);
	if (ret < 0) {
		/* Log errors, except for -3 ("device not found"). */
		if (ret != -3)
			sr_err("Failed to open device (%d): %s", ret,
			       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	/* Purge RX/TX buffers in the FTDI chip. */
	ret = ftdi_usb_purge_buffers(devc->ftdic);
	if (ret < 0) {
		sr_err("Failed to purge FTDI RX/TX buffers (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip buffers purged successfully.");

	/* Reset the FTDI bitmode. */
	ret = ftdi_set_bitmode(devc->ftdic, 0x00, BITMODE_RESET);
	if (ret < 0) {
		sr_err("Failed to reset the FTDI chip bitmode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip bitmode reset successfully.");

	ret = ftdi_set_bitmode(devc->ftdic, 0x00, BITMODE_BITBANG);
	if (ret < 0) {
		sr_err("Failed to put FTDI chip into bitbang mode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}
	sr_dbg("FTDI chip bitbang mode entered successfully.");

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;

err_dev_open_close_ftdic:
	ftdi_usb_close(devc->ftdic);
	return SR_ERR;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	ftdi_usb_close(devc->ftdic);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	dev_clear(di);

	/* TODO: Free other driver resources, if any. */

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int ftdi_la_set_samplerate(struct dev_context *devc)
{
	int ret;

	ret = ftdi_set_baudrate(devc->ftdic,
			devc->cur_samplerate / devc->desc->samplerate_div);
	if (ret < 0) {
		sr_err("Failed to set baudrate (%d): %s.", devc->cur_samplerate,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;
	uint64_t value;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_MSEC:
		value = g_variant_get_uint64(data);
		/* TODO: Implement. */
		ret = SR_ERR_NA;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (g_variant_get_uint64(data) == 0)
			return SR_ERR_ARG;
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		if (value < 3600)
			return SR_ERR_SAMPLERATE;
		devc->cur_samplerate = value;
		return ftdi_la_set_samplerate(devc);
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
			samplerates, ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerate-steps", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!devc->ftdic)
		return SR_ERR_BUG;

	ftdi_set_bitmode(devc->ftdic, 0, BITMODE_BITBANG);

	devc->cb_data = cb_data;

	/* Properly reset internal variables before every new acquisition. */
	devc->samples_sent = 0;
	devc->bytes_received = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);

	/* Hook up a dummy handler to receive data from the device. */
	sr_session_source_add(sdi->session, -1, G_IO_IN, 0,
			      ftdi_la_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	sr_dbg("Stopping acquisition.");
	sr_session_source_remove(sdi->session, -1);

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver ftdi_la_driver_info = {
	.name = "ftdi-la",
	.longname = "FTDI LA",
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
	.context = NULL,
};
