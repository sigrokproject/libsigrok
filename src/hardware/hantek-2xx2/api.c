/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 cerebralmisfire <bram@cerebralmisfire.net>
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
#include <string.h>
#include "protocol.h"

/* Max time in ms before checking on USB events. */
#define TICK 10

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_NUM_VDIV | SR_CONF_GET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = { "CH1", "CH2" };

/* Index is the device's coupling code. */
static const char *couplings[] = { "AC", "DC", "GND" };

/* 1 V/div, DC, x1 probe, 1 ms/div. */
#define DEFAULT_VDIV		6
#define DEFAULT_COUPLING	1
#define DEFAULT_PROBE		0
#define DEFAULT_TIMEBASE	16

static gboolean is_hantek_2xx2(libusb_device *dev,
		const struct libusb_device_descriptor *des)
{
	libusb_device_handle *hdl;
	char product[64];
	int ret;

	if (des->idVendor != USB_VID || des->idProduct != USB_PID)
		return FALSE;
	/* 0483 is ST's generic vendor ID; check the product string too. */
	if (libusb_open(dev, &hdl) != 0)
		return FALSE;
	ret = libusb_get_string_descriptor_ascii(hdl, des->iProduct,
		(unsigned char *)product, sizeof(product));
	libusb_close(hdl);

	return ret > 0 && strcmp(product, USB_PRODUCT) == 0;
}

static struct sr_dev_inst *dev_new(libusb_device *dev, const char *connection_id)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct dev_context *devc;
	unsigned int i;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Hantek");
	/* The 2D42, 2D72 and 2C42 report the same IDs. */
	sdi->model = g_strdup("2D42/2D72/2C42");
	sdi->inst_type = SR_INST_USB;
	sdi->connection_id = g_strdup(connection_id);
	sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(dev),
		libusb_get_device_address(dev), NULL);

	for (i = 0; i < NUM_CHANNELS; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel_names[i]);
		cg = sr_channel_group_new(sdi, channel_names[i], NULL);
		cg->channels = g_slist_append(cg->channels, ch);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	for (i = 0; i < NUM_CHANNELS; i++) {
		devc->vdiv[i] = DEFAULT_VDIV;
		devc->coupling[i] = DEFAULT_COUPLING;
		devc->probe[i] = DEFAULT_PROBE;
	}
	devc->timebase = DEFAULT_TIMEBASE;
	devc->state = IDLE;
	sdi->priv = devc;

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	GSList *l, *devices, *conn_devices;
	const char *conn;
	char connection_id[64];
	int i;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN)
			conn = g_variant_get_string(src->data, NULL);
	}
	conn_devices = conn ? sr_usb_find(drvc->sr_ctx->libusb_ctx, conn) : NULL;

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
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);
		if (!is_hantek_2xx2(devlist[i], &des))
			continue;
		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		sr_dbg("Found a Hantek 2xx2 at %s.", connection_id);
		devices = g_slist_append(devices, dev_new(devlist[i], connection_id));
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static void clear_helper(struct dev_context *devc)
{
	libusb_free_transfer(devc->xfer_out);
	libusb_free_transfer(devc->xfer_in);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc = sdi->driver->context;
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE)) != 0) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		sr_usb_close(usb);
		return SR_ERR;
	}

	if (!devc->xfer_out)
		devc->xfer_out = libusb_alloc_transfer(0);
	if (!devc->xfer_in)
		devc->xfer_in = libusb_alloc_transfer(0);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	sr_usb_close(usb);

	return SR_OK;
}

static int cg_index(const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int i = g_slist_index(sdi->channel_groups, (gpointer)cg);

	return (i >= 0 && i < NUM_CHANNELS) ? i : -1;
}

static int num_enabled(const struct sr_dev_inst *sdi)
{
	const GSList *l;
	int n = 0;

	for (l = sdi->channels; l; l = l->next)
		n += ((struct sr_channel *)l->data)->enabled ? 1 : 0;

	return n;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ch;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_CONN:
			usb = sdi->conn;
			*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
			break;
		case SR_CONF_LIMIT_FRAMES:
			*data = g_variant_new_uint64(devc->limit_frames);
			break;
		case SR_CONF_TIMEBASE:
			*data = g_variant_new("(tt)",
				hantek_2xx2_timebases[devc->timebase][0],
				hantek_2xx2_timebases[devc->timebase][1]);
			break;
		case SR_CONF_SAMPLERATE:
			*data = g_variant_new_uint64(hantek_2xx2_samplerate(devc));
			break;
		case SR_CONF_NUM_HDIV:
			/* A frame is one block: its length in divisions. */
			*data = g_variant_new_int32(REQUEST_BYTES
				/ MAX(1, num_enabled(sdi)) / SAMPLES_PER_DIV);
			break;
		case SR_CONF_NUM_VDIV:
			*data = g_variant_new_int32(NUM_VDIV);
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	if ((ch = cg_index(sdi, cg)) < 0)
		return SR_ERR_ARG;
	switch (key) {
	case SR_CONF_VDIV:
		*data = g_variant_new("(tt)", hantek_2xx2_vdivs[devc->vdiv[ch]][0],
			hantek_2xx2_vdivs[devc->vdiv[ch]][1]);
		break;
	case SR_CONF_COUPLING:
		*data = g_variant_new_string(couplings[devc->coupling[ch]]);
		break;
	case SR_CONF_PROBE_FACTOR:
		*data = g_variant_new_uint64(hantek_2xx2_probe_factors[devc->probe[ch]]);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	int ch, idx;

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_FRAMES:
			devc->limit_frames = g_variant_get_uint64(data);
			break;
		case SR_CONF_TIMEBASE:
			if ((idx = std_u64_tuple_idx(data, hantek_2xx2_timebases,
					hantek_2xx2_num_timebases)) < 0)
				return SR_ERR_ARG;
			devc->timebase = idx;
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	if ((ch = cg_index(sdi, cg)) < 0)
		return SR_ERR_ARG;
	switch (key) {
	case SR_CONF_VDIV:
		if ((idx = std_u64_tuple_idx(data, hantek_2xx2_vdivs,
				hantek_2xx2_num_vdivs)) < 0)
			return SR_ERR_ARG;
		devc->vdiv[ch] = idx;
		break;
	case SR_CONF_COUPLING:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(couplings))) < 0)
			return SR_ERR_ARG;
		devc->coupling[ch] = idx;
		break;
	case SR_CONF_PROBE_FACTOR:
		if ((idx = std_u64_idx(data, hantek_2xx2_probe_factors,
				hantek_2xx2_num_probe_factors)) < 0)
			return SR_ERR_ARG;
		devc->probe[ch] = idx;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_TIMEBASE:
			*data = std_gvar_tuple_array(hantek_2xx2_timebases,
				hantek_2xx2_num_timebases);
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
		break;
	case SR_CONF_VDIV:
		*data = std_gvar_tuple_array(hantek_2xx2_vdivs, hantek_2xx2_num_vdivs);
		break;
	case SR_CONF_COUPLING:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(couplings));
		break;
	case SR_CONF_PROBE_FACTOR:
		*data = std_gvar_array_u64(hantek_2xx2_probe_factors,
			hantek_2xx2_num_probe_factors);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;
	const GSList *l;
	struct sr_channel *ch;

	devc->num_enabled = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		devc->ch_enabled[ch->index] = ch->enabled;
		devc->num_enabled += ch->enabled ? 1 : 0;
	}
	if (devc->num_enabled == 0) {
		sr_err("No channels enabled.");
		return SR_ERR;
	}

	if (hantek_2xx2_apply_settings(sdi) != SR_OK)
		return SR_ERR;

	std_session_send_df_header(sdi);
	usb_source_add(sdi->session, drvc->sr_ctx, TICK,
		hantek_2xx2_receive_data, (void *)sdi);

	return hantek_2xx2_start_capture(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	/* The transfer callbacks see this and stop resubmitting. */
	devc->state = STOPPING;

	return SR_OK;
}

static struct sr_dev_driver hantek_2xx2_driver_info = {
	.name = "hantek-2xx2",
	.longname = "Hantek 2D42/2D72/2C42",
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
SR_REGISTER_DEV_DRIVER(hantek_2xx2_driver_info);
