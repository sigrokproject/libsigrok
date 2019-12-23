/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
 * Copyright (C) 2019 Vitaliy Vorobyov
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
#include <stdlib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>
#include "protocol.h"

 /* Number of logic channels. */
#define NUM_CHANNELS	32

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static const uint64_t capture_ratios[] = {
	0, 10, 20, 30, 50, 70, 90, 100,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_RLE | SR_CONF_GET,
};

static const uint64_t samplerates[] = {
	SR_MHZ(500), SR_MHZ(400), SR_MHZ(250), SR_MHZ(200), SR_MHZ(100),
	SR_MHZ(50), SR_MHZ(25), SR_MHZ(20), SR_MHZ(10), SR_MHZ(5), SR_MHZ(2),
	SR_MHZ(1), SR_KHZ(500), SR_KHZ(200), SR_KHZ(100), SR_KHZ(50),
	SR_KHZ(20), SR_KHZ(10), SR_KHZ(5), SR_KHZ(2),
};

static struct sr_dev_inst *dev_inst_new(void)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int i;
	char name[8];

	devc = g_malloc0(sizeof(struct dev_context));
	devc->active_fpga_config = FPGA_NOCONF;
	devc->samplerate = samplerates[0];
	devc->limit_samples = MAX_LIMIT_SAMPLES;
	devc->capture_ratio = capture_ratios[4];
	devc->channel_mask = (UINT64_C(1) << NUM_CHANNELS) - 1;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Sysclk");
	sdi->model = g_strdup("SLA5032");
	sdi->priv = devc;

	for (i = 0; i < NUM_CHANNELS; i++) {
		g_snprintf(name, sizeof(name), "CH%d", i);
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, name);
	}

	return sdi;
}

/* Create a new device instance for a libusb device if it is a Sysclk SLA5032
 * device and also matches the connection specification.
 */
static struct sr_dev_inst *dev_inst_new_matching(GSList *conn_matches,
						 libusb_device *dev)
{
	GSList *node;
	struct sr_usb_dev_inst *usb;
	struct sr_dev_inst *sdi;
	struct libusb_device_descriptor des;
	int bus, address, ret;
	unsigned int vid, pid;

	bus = libusb_get_bus_number(dev);
	address = libusb_get_device_address(dev);

	for (node = conn_matches; node != NULL; node = node->next) {
		usb = node->data;
		if (usb && usb->bus == bus && usb->address == address)
			break; /* found */
	}
	if (conn_matches && !node)
		return NULL; /* no match */

	ret = libusb_get_device_descriptor(dev, &des);
	if (ret != 0) {
		sr_err("Failed to get USB device descriptor: %s.",
			libusb_error_name(ret));
		return NULL;
	}
	vid = des.idVendor;
	pid = des.idProduct;

	/* Create sigrok device instance. */
	if (vid == USB_VID_SYSCLK && pid == USB_PID_SLA5032) {
	} else {
		if (conn_matches)
			sr_warn("USB device %d.%d (%04x:%04x) is not a"
				" Sysclk SLA5032.", bus, address, vid, pid);
		return NULL;
	}
	sdi = dev_inst_new();

	sdi->inst_type = SR_INST_USB;
	sdi->conn = sr_usb_dev_inst_new(bus, address, NULL);

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *conn_devices, *devices, *node;
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	const char *conn;
	libusb_device **devlist;
	ssize_t num_devs, i;

	drvc = di->context;
	conn = NULL;
	conn_devices = NULL;
	devices = NULL;

	for (node = options; node != NULL; node = node->next) {
		src = node->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn) {
		/* Find devices matching the connection specification. */
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	}

	/* List all libusb devices. */
	num_devs = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (num_devs < 0) {
		sr_err("Failed to list USB devices: %s.",
			libusb_error_name(num_devs));
		g_slist_free_full(conn_devices,
			(GDestroyNotify)&sr_usb_dev_inst_free);
		return NULL;
	}

	/* Scan the USB device list for matching devices. */
	for (i = 0; i < num_devs; i++) {
		sdi = dev_inst_new_matching(conn_devices, devlist[i]);
		if (!sdi)
			continue; /* no match */

		/* Register device instance with driver. */
		devices = g_slist_append(devices, sdi);
	}

	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)&sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret;

	drvc = sdi->driver->context;
	devc = sdi->priv;
	usb = sdi->conn;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (ret != SR_OK)
		return ret;

	ret = libusb_set_configuration(usb->devhdl, USB_CONFIG);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("Failed to set USB configuration: %s.",
			libusb_error_name(ret));
		sr_usb_close(usb);
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("Failed to claim interface: %s.",
			libusb_error_name(ret));
		sr_usb_close(usb);
		return SR_ERR;
	}

	sdi->status = SR_ST_ACTIVE;

	devc->active_fpga_config = FPGA_NOCONF;
	devc->state = STATE_IDLE;

	return sla5032_apply_fpga_config(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (usb->devhdl)
		libusb_release_interface(usb->devhdl, USB_INTERFACE);

	sr_usb_close(usb);

	return SR_OK;
}

/* Check whether the device options contain a specific key.
 * Also match against get/set/list bits if specified.
 */
static int has_devopt(uint32_t key)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(devopts); i++) {
		if ((devopts[i] & (SR_CONF_MASK | key)) == key)
			return TRUE;
	}

	return FALSE;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!has_devopt(key | SR_CONF_GET))
		return SR_ERR_NA;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_RLE:
		*data = g_variant_new_boolean(TRUE);
		break;
	default:
		/* Must not happen for a key listed in devopts. */
		return SR_ERR_BUG;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	uint64_t value;
	struct dev_context *devc;
	int idx;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!has_devopt(key | SR_CONF_SET))
		return SR_ERR_NA;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		if ((idx = std_u64_idx(data, ARRAY_AND_SIZE(samplerates))) < 0)
			return SR_ERR_ARG;
		devc->samplerate = samplerates[idx];
		break;
	case SR_CONF_LIMIT_SAMPLES:
		value = g_variant_get_uint64(data);
		if (value > MAX_LIMIT_SAMPLES || value < MIN_LIMIT_SAMPLES)
			return SR_ERR_ARG;
		devc->limit_samples = value;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		/* Must not happen for a key listed in devopts. */
		return SR_ERR_BUG;
	}

	return SR_OK;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
	struct sr_channel *ch, unsigned int changes)
{
	uint64_t channel_bit;
	struct dev_context *devc;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (ch->index < 0 || ch->index >= NUM_CHANNELS) {
		sr_err("Channel index %d out of range.", ch->index);
		return SR_ERR_BUG;
	}

	if ((changes & SR_CHANNEL_SET_ENABLED) != 0) {
		channel_bit = UINT64_C(1) << ch->index;

		/* Enable or disable logic input for this channel. */
		if (ch->enabled)
			devc->channel_mask |= channel_bit;
		else
			devc->channel_mask &= ~channel_bit;
	}

	return SR_OK;
}

/* Derive trigger masks from the session's trigger configuration. */
static int prepare_trigger_masks(const struct sr_dev_inst *sdi)
{
	uint32_t trigger_mask, trigger_values, trigger_edge_mask;
	uint32_t level_bit, type_bit;
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *node;
	int idx;
	enum sr_trigger_matches trg;

	devc = sdi->priv;

	trigger_mask = 0;
	trigger_values = 0;
	trigger_edge_mask = 0;

	trigger = sr_session_trigger_get(sdi->session);
	if (!trigger || !trigger->stages) {
		goto no_triggers;
	}

	if (trigger->stages->next) {
		sr_err("This device only supports 1 trigger stage.");
		return SR_ERR_ARG;
	}
	stage = trigger->stages->data;

	for (node = stage->matches; node; node = node->next) {
		match = node->data;

		if (!match->channel->enabled)
			continue; /* Ignore disabled channel. */

		idx = match->channel->index;
		trg = match->match;

		if (idx < 0 || idx >= NUM_CHANNELS) {
			sr_err("Channel index %d out of range.", idx);
			return SR_ERR_BUG; /* Should not happen. */
		}
		if (trg != SR_TRIGGER_ZERO
			&& trg != SR_TRIGGER_ONE
			&& trg != SR_TRIGGER_RISING
			&& trg != SR_TRIGGER_FALLING) {
			sr_err("Unsupported trigger match for CH%d.", idx);
			return SR_ERR_ARG;
		}
		level_bit = (trg == SR_TRIGGER_ONE
			|| trg == SR_TRIGGER_RISING) ? 1 : 0;
		type_bit = (trg == SR_TRIGGER_RISING
			|| trg == SR_TRIGGER_FALLING) ? 1 : 0; /* 1 if edge triggered, 0 if level triggered */

		trigger_mask |= UINT32_C(1) << idx;
		trigger_values |= level_bit << idx;
		trigger_edge_mask |= type_bit << idx;
	}

no_triggers:
	devc->trigger_mask = trigger_mask;
	devc->trigger_values = trigger_values;
	devc->trigger_edge_mask = trigger_edge_mask;

	return SR_OK;
}

static int config_commit(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = prepare_trigger_masks(sdi);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_apply_fpga_config(sdi);
	if (ret != SR_OK) {
		sr_err("Failed to apply FPGA configuration.");
		return ret;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return std_opts_config_list(key, data, sdi, cg,
			ARRAY_AND_SIZE(scanopts), ARRAY_AND_SIZE(drvopts),
			(devc) ? devopts : NULL,
			(devc) ? ARRAY_SIZE(devopts) : 0);
	}

	if (!devc)
		return SR_ERR_ARG;
	if (!has_devopt(key | SR_CONF_LIST))
		return SR_ERR_NA;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = std_gvar_tuple_u64(MIN_LIMIT_SAMPLES, MAX_LIMIT_SAMPLES);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = std_gvar_array_u64(ARRAY_AND_SIZE(capture_ratios));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		/* Must not happen for a key listed in devopts. */
		return SR_ERR_BUG;
	}

	return SR_OK;
}

/* Set up the device hardware to begin capturing samples as soon as the
 * configured trigger conditions are met, or immediately if no triggers
 * are configured.
 */
static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	return sla5032_start_acquisition(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	sr_session_source_remove(sdi->session, -1);

	std_session_send_df_end(sdi);

	devc->state = STATE_IDLE;

	return SR_OK;
}

static struct sr_dev_driver sysclk_sla5032_driver_info = {
	.name = "sysclk-sla5032",
	.longname = "Sysclk SLA5032",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_commit = config_commit,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(sysclk_sla5032_driver_info);
