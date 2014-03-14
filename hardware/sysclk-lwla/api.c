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

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_EXTERNAL_CLOCK,
	SR_CONF_CLOCK_EDGE,
	SR_CONF_TRIGGER_TYPE,
	SR_CONF_TRIGGER_SOURCE,
	SR_CONF_TRIGGER_SLOPE,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_LIMIT_SAMPLES,
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

static GSList *gen_probe_list(int num_probes)
{
	GSList *list;
	struct sr_probe *probe;
	int i;
	char name[8];

	list = NULL;

	for (i = num_probes; i > 0; --i) {
		/* The LWLA series simply number probes from CH1 to CHxx. */
		g_snprintf(name, sizeof(name), "CH%d", i);

		probe = sr_probe_new(i - 1, SR_PROBE_LOGIC, TRUE, name);
		list = g_slist_prepend(list, probe);
	}

	return list;
}

static struct sr_dev_inst *dev_inst_new(int device_index)
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
	sdi = sr_dev_inst_new(device_index, SR_ST_INACTIVE,
			      VENDOR_NAME, MODEL_NAME, NULL);
	if (!sdi) {
		sr_err("Failed to instantiate device.");
		g_free(devc);
		return NULL;
	}

	/* Enable all channels to match the default probe configuration. */
	devc->channel_mask = ALL_CHANNELS_MASK;
	devc->samplerate = DEFAULT_SAMPLERATE;

	sdi->priv = devc;
	sdi->probes = gen_probe_list(NUM_PROBES);

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
	int device_index;

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
	device_index = g_slist_length(drvc->instances);

	for (node = usb_devices; node != NULL; node = node->next) {
		usb = node->data;

		/* Create sigrok device instance. */
		sdi = dev_inst_new(device_index);
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

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *channel_group)
{
	struct dev_context *devc;
	size_t idx;

	(void)channel_group;

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

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *channel_group)
{
	uint64_t value;
	struct dev_context *devc;
	int idx;

	(void)channel_group;

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

static int config_probe_set(const struct sr_dev_inst *sdi,
			    struct sr_probe *probe, unsigned int changes)
{
	uint64_t probe_bit;
	uint64_t trigger_mask;
	uint64_t trigger_values;
	uint64_t trigger_edge_mask;
	struct dev_context *devc;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_DEV_CLOSED;

	if (probe->index < 0 || probe->index >= NUM_PROBES) {
		sr_err("Probe index %d out of range.", probe->index);
		return SR_ERR_BUG;
	}
	probe_bit = (uint64_t)1 << probe->index;

	if ((changes & SR_PROBE_SET_ENABLED) != 0) {
		/* Enable or disable input channel for this probe. */
		if (probe->enabled)
			devc->channel_mask |= probe_bit;
		else
			devc->channel_mask &= ~probe_bit;
	}

	if ((changes & SR_PROBE_SET_TRIGGER) != 0) {
		trigger_mask = devc->trigger_mask & ~probe_bit;
		trigger_values = devc->trigger_values & ~probe_bit;
		trigger_edge_mask = devc->trigger_edge_mask & ~probe_bit;

		if (probe->trigger && probe->trigger[0] != '\0') {
			if (probe->trigger[1] != '\0') {
				sr_warn("Trigger configuration \"%s\" with "
					"multiple stages is not supported.",
					probe->trigger);
				return SR_ERR_ARG;
			}
			/* Enable trigger for this probe. */
			trigger_mask |= probe_bit;

			/* Configure edge mask and trigger value. */
			switch (probe->trigger[0]) {
			case '1': trigger_values |= probe_bit;
			case '0': break;

			case 'r': trigger_values |= probe_bit;
			case 'f': trigger_edge_mask |= probe_bit;
				  break;
			default:
				sr_warn("Trigger type '%c' is not supported.",
					probe->trigger[0]);
				return SR_ERR_ARG;
			}
		}
		/* Store validated trigger setup. */
		devc->trigger_mask = trigger_mask;
		devc->trigger_values = trigger_values;
		devc->trigger_edge_mask = trigger_edge_mask;
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

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *channel_group)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)channel_group;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, G_N_ELEMENTS(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, G_N_ELEMENTS(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
				samplerates, G_N_ELEMENTS(samplerates),
				sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_TYPE:
		*data = g_variant_new_string(TRIGGER_TYPES);
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
	usb_source_add(drvc->sr_ctx, 100, &lwla_receive_data,
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
	.config_probe_set = config_probe_set,
	.config_commit = config_commit,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
