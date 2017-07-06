/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011-2015 Uwe Hermann <uwe@hermann-uwe.de>
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

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	ftdi_free(devc->ftdic);
	g_free(devc->final_buf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, clear_helper);
}

static int add_device(int model, struct libusb_device_descriptor *des,
	const char *serial_num, const char *connection_id, libusb_device *usbdev,
	GSList **devices)
{
	int ret;
	unsigned int i;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	ret = SR_OK;

	/* Allocate memory for our private device context. */
	devc = g_malloc0(sizeof(struct dev_context));

	/* Set some sane defaults. */
	devc->prof = &cv_profiles[model];
	devc->ftdic = NULL; /* Will be set in the open() API call. */
	devc->cur_samplerate = 0; /* Set later (different for LA8/LA16). */
	devc->limit_msec = 0;
	devc->limit_samples = 0;
	memset(devc->mangled_buf, 0, BS);
	devc->final_buf = NULL;
	devc->trigger_pattern = 0x0000; /* Irrelevant, see trigger_mask. */
	devc->trigger_mask = 0x0000; /* All channels: "don't care". */
	devc->trigger_edgemask = 0x0000; /* All channels: "state triggered". */
	devc->trigger_found = 0;
	devc->done = 0;
	devc->block_counter = 0;
	devc->divcount = 0;
	devc->usb_vid = des->idVendor;
	devc->usb_pid = des->idProduct;
	memset(devc->samplerates, 0, sizeof(uint64_t) * 255);

	/* Allocate memory where we'll store the de-mangled data. */
	if (!(devc->final_buf = g_try_malloc(SDRAM_SIZE))) {
		sr_err("Failed to allocate memory for sample buffer.");
		ret = SR_ERR_MALLOC;
		goto err_free_devc;
	}

	/* We now know the device, set its max. samplerate as default. */
	devc->cur_samplerate = devc->prof->max_samplerate;

	/* Register the device with libsigrok. */
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("ChronoVu");
	sdi->model = g_strdup(devc->prof->modelname);
	sdi->serial_num = g_strdup(serial_num);
	sdi->connection_id = g_strdup(connection_id);
	sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(usbdev),
		libusb_get_device_address(usbdev), NULL);
	sdi->priv = devc;

	for (i = 0; i < devc->prof->num_channels; i++)
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
				cv_channel_names[i]);

	*devices = g_slist_append(*devices, sdi);

	if (ret == SR_OK)
		return SR_OK;

err_free_devc:
	g_free(devc);

	return ret;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	int i, ret, model;
	struct drv_context *drvc;
	GSList *devices, *conn_devices, *l;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	const char *conn;
	char product[64], serial_num[64], connection_id[64];

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
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);

	for (i = 0; devlist[i]; i++) {
		if (conn) {
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
					&& usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if ((ret = libusb_open(devlist[i], &hdl)) < 0)
			continue;

		if (des.iProduct == 0) {
			product[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, (unsigned char *)product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iSerialNumber == 0) {
			serial_num[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iSerialNumber, (unsigned char *)serial_num,
				sizeof(serial_num))) < 0) {
			sr_warn("Failed to get serial number string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		libusb_close(hdl);

		if (!strcmp(product, "ChronoVu LA8")) {
			model = 0;
		} else if (!strcmp(product, "ChronoVu LA16")) {
			model = 1;
		} else {
			sr_spew("Unknown iProduct string '%s'.", product);
			continue;
		}

		sr_dbg("Found %s (%04x:%04x, %d.%d, %s).",
		       product, des.idVendor, des.idProduct,
		       libusb_get_bus_number(devlist[i]),
		       libusb_get_device_address(devlist[i]), connection_id);

		if ((ret = add_device(model, &des, serial_num, connection_id,
					devlist[i], &devices)) < 0) {
			sr_dbg("Failed to add device: %d.", ret);
		}
	}

	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	/* Allocate memory for the FTDI context and initialize it. */
	if (!(devc->ftdic = ftdi_new())) {
		sr_err("Failed to initialize libftdi.");
		return SR_ERR;
	}

	sr_dbg("Opening %s device (%04x:%04x).", devc->prof->modelname,
	       devc->usb_vid, devc->usb_pid);

	/* Open the device. */
	if ((ret = ftdi_usb_open_desc(devc->ftdic, devc->usb_vid,
			devc->usb_pid, devc->prof->iproduct, NULL)) < 0) {
		sr_err("Failed to open FTDI device (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_ftdi_free;
	}
	sr_dbg("Device opened successfully.");

	/* Purge RX/TX buffers in the FTDI chip. */
	if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0) {
		sr_err("Failed to purge FTDI buffers (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_ftdi_free;
	}
	sr_dbg("FTDI buffers purged successfully.");

	/* Enable flow control in the FTDI chip. */
	if ((ret = ftdi_setflowctrl(devc->ftdic, SIO_RTS_CTS_HS)) < 0) {
		sr_err("Failed to enable FTDI flow control (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_ftdi_free;
	}
	sr_dbg("FTDI flow control enabled successfully.");

	/* Wait 100ms. */
	g_usleep(100 * 1000);

	sdi->status = SR_ST_ACTIVE;

	if (ret == SR_OK)
		return SR_OK;

err_ftdi_free:
	ftdi_free(devc->ftdic); /* Close device (if open), free FTDI context. */
	devc->ftdic = NULL;
	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->ftdic && (ret = ftdi_usb_close(devc->ftdic)) < 0)
		sr_err("Failed to close FTDI device (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	char str[128];

	(void)cg;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !(usb = sdi->conn))
			return SR_ERR_ARG;
		snprintf(str, 128, "%d.%d", usb->bus, usb->address);
		*data = g_variant_new_string(str);
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_BUG;
		devc = sdi->priv;
		*data = g_variant_new_uint64(devc->cur_samplerate);
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

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (cv_set_samplerate(sdi, g_variant_get_uint64(data)) < 0)
			return SR_ERR;
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
	GVariant *gvar, *grange[2];
	GVariantBuilder gvb;
	struct dev_context *devc;

	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi)
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		else
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_BUG;
		devc = sdi->priv;
		cv_fill_samplerates_if_needed(sdi);
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				devc->samplerates,
				ARRAY_SIZE(devc->samplerates),
				sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (!sdi || !sdi->priv || !(devc = sdi->priv) || !devc->prof)
			return SR_ERR_BUG;
		grange[0] = g_variant_new_uint64(0);
		if (devc->prof->model == CHRONOVU_LA8)
			grange[1] = g_variant_new_uint64(MAX_NUM_SAMPLES);
		else
			grange[1] = g_variant_new_uint64(MAX_NUM_SAMPLES / 2);
		*data = g_variant_new_tuple(grange, 2);
		break;
	case SR_CONF_TRIGGER_MATCH:
		if (!sdi || !sdi->priv || !(devc = sdi->priv) || !devc->prof)
			return SR_ERR_BUG;
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				trigger_matches, devc->prof->num_trigger_matches,
				sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	int i, ret;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data)) {
		sr_err("cb_data was NULL.");
		return FALSE;
	}

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return FALSE;
	}

	if (!devc->ftdic) {
		sr_err("devc->ftdic was NULL.");
		return FALSE;
	}

	/* Get one block of data. */
	if ((ret = cv_read_block(devc)) < 0) {
		sr_err("Failed to read data block: %d.", ret);
		sr_dev_acquisition_stop(sdi);
		return FALSE;
	}

	/* We need to get exactly NUM_BLOCKS blocks (i.e. 8MB) of data. */
	if (devc->block_counter != (NUM_BLOCKS - 1)) {
		devc->block_counter++;
		return TRUE;
	}

	sr_dbg("Sampling finished, sending data to session bus now.");

	/*
	 * All data was received and demangled, send it to the session bus.
	 *
	 * Note: Due to the method how data is spread across the 8MByte of
	 * SDRAM, we can _not_ send it to the session bus in a streaming
	 * manner while we receive it. We have to receive and de-mangle the
	 * full 8MByte first, only then the whole buffer contains valid data.
	 */
	for (i = 0; i < NUM_BLOCKS; i++)
		cv_send_block_to_session_bus(sdi, i);

	sr_dev_acquisition_stop(sdi);

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t buf[8];
	int bytes_to_write, bytes_written;

	devc = sdi->priv;

	if (!devc->ftdic) {
		sr_err("devc->ftdic was NULL.");
		return SR_ERR_BUG;
	}

	devc->divcount = cv_samplerate_to_divcount(sdi, devc->cur_samplerate);
	if (devc->divcount == 0xff) {
		sr_err("Invalid divcount/samplerate.");
		return SR_ERR;
	}

	if (cv_convert_trigger(sdi) != SR_OK) {
		sr_err("Failed to configure trigger.");
		return SR_ERR;
	}

	/* Fill acquisition parameters into buf[]. */
	if (devc->prof->model == CHRONOVU_LA8) {
		buf[0] = devc->divcount;
		buf[1] = 0xff; /* This byte must always be 0xff. */
		buf[2] = devc->trigger_pattern & 0xff;
		buf[3] = devc->trigger_mask & 0xff;
		bytes_to_write = 4;
	} else {
		buf[0] = devc->divcount;
		buf[1] = 0xff; /* This byte must always be 0xff. */
		buf[2] = (devc->trigger_pattern & 0xff00) >> 8;  /* LSB */
		buf[3] = (devc->trigger_pattern & 0x00ff) >> 0;  /* MSB */
		buf[4] = (devc->trigger_mask & 0xff00) >> 8;     /* LSB */
		buf[5] = (devc->trigger_mask & 0x00ff) >> 0;     /* MSB */
		buf[6] = (devc->trigger_edgemask & 0xff00) >> 8; /* LSB */
		buf[7] = (devc->trigger_edgemask & 0x00ff) >> 0; /* MSB */
		bytes_to_write = 8;
	}

	/* Start acquisition. */
	bytes_written = cv_write(devc, buf, bytes_to_write);

	if (bytes_written < 0 || bytes_written != bytes_to_write) {
		sr_err("Acquisition failed to start.");
		return SR_ERR;
	}

	sr_dbg("Hardware acquisition started successfully.");

	std_session_send_df_header(sdi);

	/* Time when we should be done (for detecting trigger timeouts). */
	devc->done = (devc->divcount + 1) * devc->prof->trigger_constant +
			g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND);
	devc->block_counter = 0;
	devc->trigger_found = 0;

	/* Hook up a dummy handler to receive data from the device. */
	sr_session_source_add(sdi->session, -1, 0, 0, receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver chronovu_la_driver_info = {
	.name = "chronovu-la",
	.longname = "ChronoVu LA8/LA16",
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
SR_REGISTER_DEV_DRIVER(chronovu_la_driver_info);
