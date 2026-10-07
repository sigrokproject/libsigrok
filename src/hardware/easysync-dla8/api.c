/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Magnus Lucchese <magnus.lucchese@gmail.com>
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
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONN | SR_CONF_GET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const char *const clock_edges[] = {
	"r",
	"f",
};

static const char *const channel_names[] = {
	"CH0", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7",
};

SR_PRIV gboolean dla8_is_4ch_mode(const struct sr_dev_inst *sdi)
{
	const GSList *l;
	const struct sr_channel *ch;

	if (!sdi)
		return FALSE;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->index >= 4 && ch->enabled)
			return FALSE;
	}

	return TRUE;
}

static void clear_helper(struct dev_context *devc)
{
	if (!devc)
		return;
	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
	if (devc->ftdic) {
		ftdi_free(devc->ftdic);
		devc->ftdic = NULL;
	}
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	GSList *devices;
	const char *conn;
	GSList *conn_devices, *l;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	char product[64], serial_num[64], connection_id[64];
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t i, ch;
	uint8_t bus, addr;
	int ret;

	drvc = di->context;
	devices = NULL;

	conn = NULL;
	(void)sr_serial_extract_options(options, &conn, NULL);
	conn_devices = NULL;
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		bus = libusb_get_bus_number(devlist[i]);
		addr = libusb_get_device_address(devlist[i]);

		if (conn) {
			for (l = conn_devices; l; l = l->next) {
				struct sr_usb_dev_inst *usb = l->data;
				if (usb->bus == bus && usb->address == addr)
					break;
			}
			if (!l)
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != DLA8_USB_VID || des.idProduct != DLA8_USB_PID)
			continue;

		if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
			sr_warn("Failed to open USB device %d.%d: %s",
				bus, addr, libusb_error_name(ret));
			continue;
		}

		product[0] = '\0';
		if (des.iProduct > 0) {
			ret = libusb_get_string_descriptor_ascii(hdl, des.iProduct,
				(unsigned char *)product, sizeof(product));
			if (ret < 0) {
				sr_warn("Failed to read product string from %d.%d", bus, addr);
				libusb_close(hdl);
				continue;
			}
		}

		serial_num[0] = '\0';
		if (des.iSerialNumber > 0) {
			ret = libusb_get_string_descriptor_ascii(hdl, des.iSerialNumber,
				(unsigned char *)serial_num, sizeof(serial_num));
			if (ret < 0)
				sr_warn("Failed to read serial number from %d.%d", bus, addr);
		}

		libusb_close(hdl);

		if (strcmp(product, DLA8_IPRODUCT) != 0) {
			sr_spew("Skipping device %d.%d with product '%s' != '%s'",
				bus, addr, product, DLA8_IPRODUCT);
			continue;
		}

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			g_snprintf(connection_id, sizeof(connection_id), "%d.%d", bus, addr);

		sr_info("Found EasySync ES-DLA-8 (%04x:%04x, %d.%d, S/N: %s)",
			des.idVendor, des.idProduct, bus, addr, serial_num);

		devc = g_malloc0(sizeof(struct dev_context));
		dla8_set_samplerate(devc, SR_MHZ(4)); /* Default to 4 MHz */

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("EasySync");
		sdi->model = g_strdup(DLA8_IPRODUCT);
		sdi->version = NULL;
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);
		sdi->conn = sr_usb_dev_inst_new(bus, addr, NULL);
		sdi->inst_type = SR_INST_USB;
		sdi->priv = devc;

		for (ch = 0; ch < DLA8_NUM_CHANNELS; ch++)
			sr_channel_new(sdi, ch, SR_CHANNEL_LOGIC, TRUE, channel_names[ch]);

		devices = g_slist_append(devices, sdi);
	}

	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	devc->ftdic = ftdi_new();
	if (!devc->ftdic) {
		sr_err("Failed to allocate FTDI context");
		return SR_ERR_MALLOC;
	}

	if (sdi->serial_num && sdi->serial_num[0] != '\0') {
		ret = ftdi_usb_open_desc(devc->ftdic, DLA8_USB_VID, DLA8_USB_PID,
			NULL, sdi->serial_num);
	} else {
		ret = ftdi_usb_open_desc(devc->ftdic, DLA8_USB_VID, DLA8_USB_PID,
			NULL, NULL);
	}

	if (ret < 0) {
		sr_err("Failed to open FTDI device: %s", ftdi_get_error_string(devc->ftdic));
		ftdi_free(devc->ftdic);
		devc->ftdic = NULL;
		return SR_ERR_IO;
	}

	if ((ret = dla8_ftdi_init(devc)) != SR_OK) {
		ftdi_usb_close(devc->ftdic);
		ftdi_free(devc->ftdic);
		devc->ftdic = NULL;
		return ret;
	}

	if ((ret = dla8_handshake(devc)) != SR_OK) {
		ftdi_usb_close(devc->ftdic);
		ftdi_free(devc->ftdic);
		devc->ftdic = NULL;
		return ret;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	if (devc->ftdic) {
		dla8_stop(devc);
		ftdi_usb_close(devc->ftdic);
		ftdi_free(devc->ftdic);
		devc->ftdic = NULL;
	}

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	(void)cg;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_CONN:
		if (!(usb = sdi->conn))
			return SR_ERR_ARG;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->sw_limits, key, data);
	case SR_CONF_EXTERNAL_CLOCK:
		*data = g_variant_new_boolean(devc->ext_clock);
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_string(devc->ext_clock_falling ? "f" : "r");
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
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
	const char *edge;

	(void)cg;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		return dla8_set_samplerate(devc, g_variant_get_uint64(data));
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->sw_limits, key, data);
	case SR_CONF_EXTERNAL_CLOCK:
		devc->ext_clock = g_variant_get_boolean(data);
		break;
	case SR_CONF_CLOCK_EDGE:
		edge = g_variant_get_string(data, NULL);
		if (!strcmp(edge, "r") || !strcmp(edge, "rising"))
			devc->ext_clock_falling = FALSE;
		else if (!strcmp(edge, "f") || !strcmp(edge, "falling"))
			devc->ext_clock_falling = TRUE;
		else
			return SR_ERR_ARG;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
	struct sr_channel *ch, unsigned int changes)
{
	struct dev_context *devc;

	if (!sdi || !(devc = sdi->priv) || !ch)
		return SR_ERR_ARG;

	if (changes & SR_CHANNEL_SET_ENABLED) {
		/*
		 * If a higher channel (CH4..CH7) was enabled while 30 MHz was active,
		 * clamp the samplerate down to 15 MHz (the 8-channel maximum rate).
		 */
		if (ch->index >= 4 && ch->enabled && devc->cur_samplerate > SR_MHZ(15)) {
			sr_info("Channel %s enabled: clamping samplerate from 30 MHz to 15 MHz (8-channel limit)",
				ch->name);
			dla8_set_samplerate(devc, SR_MHZ(15));
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (sdi && dla8_is_4ch_mode(sdi))
			*data = std_gvar_samplerates(dla8_samplerates_4ch, dla8_num_samplerates_4ch);
		else
			*data = std_gvar_samplerates(dla8_samplerates_8ch, dla8_num_samplerates_8ch);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = std_gvar_tuple_u64(0, (uint64_t)DLA8_DEPTH_MAX * DLA8_DEPTH_UNIT);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_strv(clock_edges, ARRAY_SIZE(clock_edges));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	int ret;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	devc->is_4ch = dla8_is_4ch_mode(sdi);
	if (!devc->is_4ch && devc->cur_samplerate > SR_MHZ(15)) {
		sr_err("30 MHz samplerate is only supported when 4 or fewer channels (CH0-CH3) are enabled");
		return SR_ERR_ARG;
	}

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	/* Initialize software trigger engine if a trigger is configured */
	trigger = sr_session_trigger_get(sdi->session);
	if (trigger) {
		int pre_trigger_samples = 0;
		if (devc->sw_limits.limit_samples > 0) {
			pre_trigger_samples = (int)((devc->capture_ratio *
				devc->sw_limits.limit_samples) / 100);
		}
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl) {
			sr_err("Failed to initialize software trigger engine");
			return SR_ERR_MALLOC;
		}
	} else {
		devc->stl = NULL;
	}

	std_session_send_df_header(sdi);

	if ((ret = dla8_start(sdi)) != SR_OK) {
		if (devc->stl) {
			soft_trigger_logic_free(devc->stl);
			devc->stl = NULL;
		}
		return ret;
	}

	/* Hook up receive handler with 0 timeout for immediate processing */
	sr_session_source_add(sdi->session, -1, G_IO_IN, 0, dla8_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	sr_session_source_remove(sdi->session, -1);

	dla8_stop(devc);

	std_session_send_df_end(sdi);

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}

	return SR_OK;
}

static struct sr_dev_driver easysync_dla8_driver_info = {
	.name = "easysync-dla8",
	.longname = "EasySync ES-DLA-8",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(easysync_dla8_driver_info);
