/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
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

/* libsigrok surface of the SLogic driver: scan, config and device open/close.
 * Everything that talks to the device lives in the vendored libslogic core
 * (slogic/); see protocol.c for the acquisition glue. */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,    SR_TRIGGER_ONE,  SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static unsigned int adapter_channels(const slogic_model *m, int32_t *out)
{
	size_t i;

	for (i = 0; i < m->limit_count; i++)
		out[i] = m->limits[i].channels;
	return (unsigned int)m->limit_count;
}

/* Channel mode changed: recompute the rate ceiling for it and the link. */
static void apply_channel_mode(struct dev_context *devc, int32_t nch)
{
	devc->cur_samplechannel = nch;
	devc->limit_samplerate = slogic_link_max_rate(devc->model, nch, devc->link);
	if (devc->cur_samplerate > devc->limit_samplerate)
		devc->cur_samplerate = devc->limit_samplerate;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct drv_context *drvc;
	struct dev_context *devc;
	const slogic_model *model;
	const slogic_model *const *models;
	size_t mi, nmodels;
	struct sr_config *option;
	struct libusb_device_descriptor des;
	GSList *devices, *l, *conn_devices;
	char *conn;
	char cbuf[128];
	struct sr_channel *ch;
	int32_t i;
	gchar *channel_name;

	devices = NULL;
	drvc = di->context;

	for (l = options; l; l = l->next) {
		option = l->data;
		switch (option->key) {
		case SR_CONF_CONN:
			sr_err("conn= is not supported for this driver.");
			return NULL;
		default:
			sr_warn("Unhandled option key: %u", option->key);
		}
	}

	models = slogic_models(&nmodels);
	for (mi = 0; mi < nmodels; mi++) {
		model = models[mi];
		conn = g_strdup_printf("%04x.%04x", SLOGIC_VID, model->pid);
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
		g_free(conn);
		for (l = conn_devices; l; l = l->next) {
			usb = l->data;
			ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
			if (SR_OK != ret)
				continue;
			libusb_get_device_descriptor(libusb_get_device(usb->devhdl), &des);

			libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iManufacturer, (unsigned char *)cbuf, sizeof(cbuf));
			sdi = sr_dev_inst_user_new(cbuf, NULL, NULL);
			libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iProduct, (unsigned char *)cbuf, sizeof(cbuf));
			sdi->model = g_strdup(cbuf);
			libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iSerialNumber, (unsigned char *)cbuf, sizeof(cbuf));
			sdi->serial_num = g_strdup(cbuf);
			if (slogic_port_path(libusb_get_device(usb->devhdl), cbuf,
					     sizeof(cbuf)) == SLOGIC_OK)
				sdi->connection_id = g_strdup(cbuf);
			sdi->status = SR_ST_INACTIVE;
			sdi->conn = usb;
			sdi->inst_type = SR_INST_USB;

			devc = g_malloc0(sizeof(struct dev_context));
			sdi->priv = devc;
			devc->model = model;
			devc->link = slogic_usb_link(libusb_get_device(usb->devhdl));
			devc->limit_samplechannel =
				model->limits[model->limit_count - 1].channels;
			devc->cur_samplerate = UINT64_MAX;
			apply_channel_mode(devc, devc->limit_samplechannel);
			devc->cur_pattern_mode_idx = SLOGIC_PATTERN_NORMAL;
			devc->capture_ratio = 10;
			devc->voltage_threshold[0] = devc->voltage_threshold[1] =
				1.7000000000000004;

			devc->digital_group = sr_channel_group_new(sdi, "LA", NULL);
			for (i = 0; i < devc->limit_samplechannel; i++) {
				channel_name = g_strdup_printf("D%d", i);
				ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
						    channel_name);
				g_free(channel_name);
				devc->digital_group->channels = g_slist_append(
					devc->digital_group->channels, ch);
			}

			sr_usb_close(usb);
			devices = g_slist_append(devices, sdi);
		}
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;
	struct libusb_device *udev;
	int ret;

	/* By port first, then serial: the analyzer re-enumerates with a new
	 * address after a reset or a replug. */
	ret = slogic_find(drvc->sr_ctx->libusb_ctx, devc->model->pid,
			  sdi->connection_id, sdi->serial_num, &udev);
	if (ret != SLOGIC_OK) {
		sr_err("Device not found (unplugged?).");
		return SR_ERR;
	}
	ret = slogic_dev_open(drvc->sr_ctx->libusb_ctx, udev, sipeed_slogic_log,
			      sdi, &devc->sdev);
	usb->bus = libusb_get_bus_number(udev);
	usb->address = libusb_get_device_address(udev);
	libusb_unref_device(udev);
	if (ret != SLOGIC_OK)
		return SR_ERR;
	usb->devhdl = slogic_dev_handle(devc->sdev);

	devc->link = slogic_dev_link(devc->sdev);
	apply_channel_mode(devc, devc->cur_samplechannel);
	devc->voltage_threshold[0] = devc->voltage_threshold[1] = 1.7000000000000004;

	return std_dummy_dev_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc = sdi->priv;

	slogic_dev_close(devc->sdev); /* destroys a live capture first */
	devc->sdev = NULL;
	devc->cap = NULL;
	usb->devhdl = NULL;

	return std_dummy_dev_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;
	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		*data = g_variant_new_int32(devc->cur_samplechannel);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_string(
			slogic_pattern_names[devc->cur_pattern_mode_idx]);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->cur_limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_tuple_double(devc->voltage_threshold[0],
					      devc->voltage_threshold[1]);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t want;

	(void)cg;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		want = g_variant_get_uint64(data);
		if (!slogic_pick_rate(devc->model, devc->cur_samplechannel,
				      devc->link, want, 1)) {
			devc->cur_samplerate = devc->limit_samplerate;
			sr_warn("Reach limit or not supported, wrap to %" PRIu64 "MHz.",
				devc->limit_samplerate / SR_MHZ(1));
		} else {
			devc->cur_samplerate = want;
		}
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS: {
		int32_t chans[8];
		unsigned int nchans = adapter_channels(devc->model, chans);
		GSList *l;
		if (std_i32_idx(data, chans, nchans) < 0) {
			apply_channel_mode(devc, devc->limit_samplechannel);
			sr_warn("Reach limit or not supported, wrap to %dch.",
				devc->limit_samplechannel);
		} else {
			apply_channel_mode(devc, g_variant_get_int32(data));
		}
		/* enable exactly the selected channels */
		for (l = devc->digital_group->channels; l; l = l->next) {
			struct sr_channel *c = l->data;
			if (c->type == SR_CHANNEL_LOGIC)
				c->enabled = c->index < devc->cur_samplechannel;
		}
		break;
	}
	case SR_CONF_PATTERN_MODE:
		devc->cur_pattern_mode_idx =
			std_str_idx(data, (const char **)slogic_pattern_names,
				    SLOGIC_PATTERN_COUNT);
		if (devc->cur_pattern_mode_idx < 0)
			devc->cur_pattern_mode_idx = 0;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->cur_limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		want = g_variant_get_uint64(data);
		if (want > 100)
			return SR_ERR_ARG;
		devc->capture_ratio = want;
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &devc->voltage_threshold[0],
			      &devc->voltage_threshold[1]);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

/*
 * Enabling/disabling a channel picks the smallest channel mode that still
 * covers the highest enabled channel — in both directions, so the device's
 * mode always follows what the front end shows.
 */
static int config_channel_set(const struct sr_dev_inst *sdi,
			      struct sr_channel *ch, unsigned int changes)
{
	struct dev_context *devc = sdi ? sdi->priv : NULL;
	int32_t chans[8], want = 0;
	unsigned int nchans, i;
	GSList *l;

	(void)ch;
	if (!devc || !devc->model)
		return SR_ERR;
	if (changes != SR_CHANNEL_SET_ENABLED)
		return SR_OK;

	nchans = adapter_channels(devc->model, chans);
	for (l = devc->digital_group->channels; l; l = l->next) {
		struct sr_channel *c = l->data;
		if (c->enabled && (int32_t)c->index + 1 > want)
			want = (int32_t)c->index + 1;
	}
	if (!want)
		return SR_OK; /* nothing enabled: keep the mode */
	for (i = 0; i < nchans; i++) {
		if (chans[i] >= want) {
			if (chans[i] != devc->cur_samplechannel)
				apply_channel_mode(devc, chans[i]);
			break;
		}
	}
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
				       devopts);
	case SR_CONF_SAMPLERATE: {
		uint64_t rates[64];
		size_t n;
		if (!devc || !devc->model)
			return SR_ERR_ARG;
		n = slogic_rate_list(devc->model, devc->cur_samplechannel,
				     devc->link, rates, G_N_ELEMENTS(rates));
		*data = std_gvar_samplerates(rates, n);
		break;
	}
	case SR_CONF_NUM_LOGIC_CHANNELS: {
		int32_t chans[8];
		unsigned int nchans;
		if (!devc || !devc->model)
			return SR_ERR_ARG;
		nchans = adapter_channels(devc->model, chans);
		*data = std_gvar_array_i32(chans, nchans);
		break;
	}
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_strv(slogic_pattern_names, SLOGIC_PATTERN_COUNT);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_min_max_step_thresholds(0, 6, 0.1);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static struct sr_dev_driver sipeed_slogic_analyzer_driver_info = {
	.name = "sipeed-slogic-analyzer",
	.longname = "Sipeed SLogic Analyzer",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_channel_set = config_channel_set,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = sipeed_slogic_acquisition_start,
	.dev_acquisition_stop = sipeed_slogic_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(sipeed_slogic_analyzer_driver_info);
