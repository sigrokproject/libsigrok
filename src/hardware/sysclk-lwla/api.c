/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
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

#include "protocol.h"
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include <glib.h>
#include <libusb.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

/* The hardware supports more samplerates than these, but these are the
 * options hardcoded into the vendor's Windows GUI.
 */
static const uint64_t samplerates[] = {
	SR_MHZ(125), SR_MHZ(100),
	SR_MHZ(50),  SR_MHZ(20),  SR_MHZ(10),
	SR_MHZ(5),   SR_MHZ(2),   SR_MHZ(1),
	SR_KHZ(500), SR_KHZ(200), SR_KHZ(100),
	SR_KHZ(50),  SR_KHZ(20),  SR_KHZ(10),
	SR_KHZ(5),   SR_KHZ(2),   SR_KHZ(1),
	SR_HZ(500),  SR_HZ(200),  SR_HZ(100),
};

/* Names assigned to available trigger sources.  Indices must match
 * trigger_source enum values.
 */
static const char *const trigger_source_names[] = { "CH", "TRG" };

/* Names assigned to available trigger slope choices.  Indices must
 * match the signal_edge enum values.
 */
static const char *const signal_edge_names[] = { "r", "f" };

SR_PRIV struct sr_dev_driver sysclk_lwla_driver_info;
static struct sr_dev_driver *const di = &sysclk_lwla_driver_info;

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *gen_channel_list(int num_channels)
{
	GSList *list;
	struct sr_channel *ch;
	int i;
	char name[8];

	list = NULL;

	for (i = num_channels; i > 0; --i) {
		/* The LWLA series simply number channels from CH1 to CHxx. */
		g_snprintf(name, sizeof(name), "CH%d", i);

		ch = sr_channel_new(i - 1, SR_CHANNEL_LOGIC, TRUE, name);
		list = g_slist_prepend(list, ch);
	}

	return list;
}

static struct sr_dev_inst *dev_inst_new()
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	/* Allocate memory for our private driver context. */
	devc = g_try_new0(struct dev_context, 1);
	if (!devc) {
		sr_err("Device context malloc failed.");
		return NULL;
	}

	/* Register the device with libsigrok. */
	sdi = sr_dev_inst_new(SR_ST_INACTIVE,
			      VENDOR_NAME, MODEL_NAME, NULL);
	if (!sdi) {
		sr_err("Failed to instantiate device.");
		g_free(devc);
		return NULL;
	}

	/* Enable all channels to match the default channel configuration. */
	devc->channel_mask = ALL_CHANNELS_MASK;
	devc->samplerate = DEFAULT_SAMPLERATE;

	sdi->priv = devc;
	sdi->channels = gen_channel_list(NUM_CHANNELS);

	return sdi;
}

static GSList *scan(GSList *options)
{
	GSList *usb_devices, *devices, *node;
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	const char *conn;

	drvc = di->priv;
	conn = USB_VID_PID;

	for (node = options; node != NULL; node = node->next) {
		src = node->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	devices = NULL;

	for (node = usb_devices; node != NULL; node = node->next) {
		usb = node->data;

		/* Create sigrok device instance. */
		sdi = dev_inst_new();
		if (!sdi) {
			sr_usb_dev_inst_free(usb);
			continue;
		}
		sdi->driver = di;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;

		/* Register device instance with driver. */
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	g_slist_free(usb_devices);

	return devices;
}

static GSList *dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static void clear_dev_context(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	sr_dbg("Device context cleared.");

	lwla_free_acquisition_state(devc->acquisition);
	g_free(devc);
}

static int dev_clear(void)
{
	return std_dev_clear(di, &clear_dev_context);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	drvc = di->priv;

	if (!drvc) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret < 0) {
		sr_err("Failed to claim interface: %s.",
			libusb_error_name(ret));
		return SR_ERR;
	}

	sdi->status = SR_ST_INITIALIZING;

	ret = lwla_init_device(sdi);

	if (ret == SR_OK)
		sdi->status = SR_ST_ACTIVE;

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;
	if (!usb->devhdl)
		return SR_OK;

	sdi->status = SR_ST_INACTIVE;

	/* Trigger download of the shutdown bitstream. */
	if (lwla_set_clock_config(sdi) != SR_OK)
		sr_err("Unable to shut down device.");

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);

	usb->devhdl = NULL;

	return SR_OK;
}

static int cleanup(void)
{
	return dev_clear();
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	size_t idx;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		*data = g_variant_new_boolean(devc->cfg_clock_source
						== CLOCK_EXT_CLK);
		break;
	case SR_CONF_CLOCK_EDGE:
		idx = devc->cfg_clock_edge;
		if (idx >= G_N_ELEMENTS(signal_edge_names))
			return SR_ERR_BUG;
		*data = g_variant_new_string(signal_edge_names[idx]);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		idx = devc->cfg_trigger_source;
		if (idx >= G_N_ELEMENTS(trigger_source_names))
			return SR_ERR_BUG;
		*data = g_variant_new_string(trigger_source_names[idx]);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		idx = devc->cfg_trigger_slope;
		if (idx >= G_N_ELEMENTS(signal_edge_names))
			return SR_ERR_BUG;
		*data = g_variant_new_string(signal_edge_names[idx]);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* Helper for mapping a string-typed configuration value to an index
 * within a table of possible values.
 */
static int lookup_index(GVariant *value, const char *const *table, int len)
{
	const char *entry;
	int i;

	entry = g_variant_get_string(value, NULL);
	if (!entry)
		return -1;

	/* Linear search is fine for very small tables. */
	for (i = 0; i < len; ++i) {
		if (strcmp(entry, table[i]) == 0)
			return i;
	}
	return -1;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	uint64_t value;
	struct dev_context *devc;
	int idx;

	(void)cg;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_DEV_CLOSED;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		if (value < samplerates[G_N_ELEMENTS(samplerates) - 1]
				|| value > samplerates[0])
			return SR_ERR_SAMPLERATE;
		devc->samplerate = value;
		break;
	case SR_CONF_LIMIT_MSEC:
		value = g_variant_get_uint64(data);
		if (value > MAX_LIMIT_MSEC)
			return SR_ERR_ARG;
		devc->limit_msec = value;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		value = g_variant_get_uint64(data);
		if (value > MAX_LIMIT_SAMPLES)
			return SR_ERR_ARG;
		devc->limit_samples = value;
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		devc->cfg_clock_source = (g_variant_get_boolean(data))
			? CLOCK_EXT_CLK : CLOCK_INTERNAL;
		break;
	case SR_CONF_CLOCK_EDGE:
		idx = lookup_index(data, signal_edge_names,
				   G_N_ELEMENTS(signal_edge_names));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->cfg_clock_edge = idx;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		idx = lookup_index(data, trigger_source_names,
				   G_N_ELEMENTS(trigger_source_names));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->cfg_trigger_source = idx;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		idx = lookup_index(data, signal_edge_names,
				   G_N_ELEMENTS(signal_edge_names));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->cfg_trigger_slope = idx;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
		struct sr_channel *ch, unsigned int changes)
{
	uint64_t channel_bit;
	struct dev_context *devc;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_DEV_CLOSED;

	if (ch->index < 0 || ch->index >= NUM_CHANNELS) {
		sr_err("Channel index %d out of range.", ch->index);
		return SR_ERR_BUG;
	}
	channel_bit = (uint64_t)1 << ch->index;

	if ((changes & SR_CHANNEL_SET_ENABLED) != 0) {
		/* Enable or disable input channel for this channel. */
		if (ch->enabled)
			devc->channel_mask |= channel_bit;
		else
			devc->channel_mask &= ~channel_bit;
	}

	return SR_OK;
}

static int config_commit(const struct sr_dev_inst *sdi)
{
	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device not ready (status %d).", (int)sdi->status);
		return SR_ERR;
	}

	return lwla_set_clock_config(sdi);
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, G_N_ELEMENTS(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				samplerates, G_N_ELEMENTS(samplerates),
				sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				trigger_matches, ARRAY_SIZE(trigger_matches),
				sizeof(int32_t));
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_strv(trigger_source_names,
					   G_N_ELEMENTS(trigger_source_names));
		break;
	case SR_CONF_TRIGGER_SLOPE:
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_strv(signal_edge_names,
					   G_N_ELEMENTS(signal_edge_names));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct acquisition_state *acq;
	int ret;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	drvc = di->priv;

	if (devc->acquisition) {
		sr_err("Acquisition still in progress?");
		return SR_ERR;
	}
	acq = lwla_alloc_acquisition_state();
	if (!acq)
		return SR_ERR_MALLOC;

	devc->stopping_in_progress = FALSE;
	devc->transfer_error = FALSE;

	sr_info("Starting acquisition.");

	devc->acquisition = acq;
	lwla_convert_trigger(sdi);
	ret = lwla_setup_acquisition(sdi);
	if (ret != SR_OK) {
		sr_err("Failed to set up acquisition.");
		devc->acquisition = NULL;
		lwla_free_acquisition_state(acq);
		return ret;
	}

	ret = lwla_start_acquisition(sdi);
	if (ret != SR_OK) {
		sr_err("Failed to start acquisition.");
		devc->acquisition = NULL;
		lwla_free_acquisition_state(acq);
		return ret;
	}
	usb_source_add(sdi->session, drvc->sr_ctx, 100, &lwla_receive_data,
		       (struct sr_dev_inst *)sdi);

	sr_info("Waiting for data.");

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	sr_dbg("Stopping acquisition.");

	sdi->status = SR_ST_STOPPING;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver sysclk_lwla_driver_info = {
	.name = "sysclk-lwla",
	.longname = "SysClk LWLA series",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_commit = config_commit,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
