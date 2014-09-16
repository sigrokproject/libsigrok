/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#include <string.h>
#include "protocol.h"

#define USB_CONN "1041.8101"
#define VENDOR "Kecheng"
#define USB_INTERFACE 0

static const uint32_t hwcaps[] = {
	SR_CONF_SOUNDLEVELMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
	SR_CONF_DATALOG,
	SR_CONF_SPL_WEIGHT_FREQ,
	SR_CONF_SPL_WEIGHT_TIME,
	SR_CONF_DATA_SOURCE,
};

SR_PRIV const uint64_t kecheng_kc_330b_sample_intervals[][2] = {
	{ 1, 8 },
	{ 1, 2 },
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 60, 1 },
};

static const char *weight_freq[] = {
	"A",
	"C",
};

static const char *weight_time[] = {
	"F",
	"S",
};

static const char *data_sources[] = {
	"Live",
	"Memory",
};

SR_PRIV struct sr_dev_driver kecheng_kc_330b_driver_info;
static struct sr_dev_driver *di = &kecheng_kc_330b_driver_info;


static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static int scan_kecheng(struct sr_usb_dev_inst *usb, char **model)
{
	struct drv_context *drvc;
	int len, ret;
	unsigned char cmd, buf[32];

	drvc = di->priv;
	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	cmd = CMD_IDENTIFY;
	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, &cmd, 1, &len, 5);
	if (ret != 0) {
		libusb_close(usb->devhdl);
		sr_dbg("Failed to send Identify command: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	ret = libusb_bulk_transfer(usb->devhdl, EP_IN, buf, 32, &len, 10);
	if (ret != 0) {
		libusb_close(usb->devhdl);
		sr_dbg("Failed to receive response: %s", libusb_error_name(ret));
		return SR_ERR;
	}

	libusb_close(usb->devhdl);
	usb->devhdl = NULL;

	if (len < 2 || buf[0] != (CMD_IDENTIFY | 0x80) || buf[1] > 30) {
		sr_dbg("Invalid response to Identify command");
		return SR_ERR;
	}

	buf[buf[1] + 2] = '\x0';
	*model = g_strndup((const gchar *)buf + 2, 30);

	return SR_OK;
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	GSList *usb_devices, *devices, *l;
	char *model;

	(void)options;

	drvc = di->priv;
	drvc->instances = NULL;

	devices = NULL;
	if ((usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, USB_CONN))) {
		/* We have a list of sr_usb_dev_inst matching the connection
		 * string. Wrap them in sr_dev_inst and we're done. */
		for (l = usb_devices; l; l = l->next) {
			if (scan_kecheng(l->data, &model) != SR_OK)
				continue;
			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, VENDOR,
					model, NULL)))
				return NULL;
			g_free(model);
			sdi->driver = di;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = l->data;
			if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, "SPL")))
				return NULL;
			sdi->channels = g_slist_append(sdi->channels, ch);

			if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
				sr_dbg("Device context malloc failed.");
				return NULL;
			}
			sdi->priv = devc;
			devc->limit_samples = 0;
			/* The protocol provides no way to read the current
			 * settings, so we'll enforce these. */
			devc->sample_interval = DEFAULT_SAMPLE_INTERVAL;
			devc->alarm_low = DEFAULT_ALARM_LOW;
			devc->alarm_high = DEFAULT_ALARM_HIGH;
			devc->mqflags = DEFAULT_WEIGHT_TIME | DEFAULT_WEIGHT_FREQ;
			devc->data_source = DEFAULT_DATA_SOURCE;
			devc->config_dirty = FALSE;

			/* TODO: Set date/time? */

			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
		}
		g_slist_free(usb_devices);
	} else
		g_slist_free_full(usb_devices, g_free);

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	if (!(drvc = di->priv)) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_set_configuration(usb->devhdl, 1))) {
		sr_err("Failed to set configuration: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if ((ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE))) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sdi->status = SR_ST_ACTIVE;

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	usb = sdi->conn;

	if (!usb->devhdl)
		/*  Nothing to do. */
		return SR_OK;

	/* This allows a frontend to configure the device without ever
	 * doing an acquisition step. */
	devc = sdi->priv;
	if (!devc->config_dirty)
		kecheng_kc_330b_configure(sdi);

	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;


	ret = std_dev_clear(di, NULL);
	g_free(drvc);
	di->priv = NULL;

	return ret;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *rational[2];
	const uint64_t *si;

	(void)cg;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_SAMPLE_INTERVAL:
		si = kecheng_kc_330b_sample_intervals[devc->sample_interval];
		rational[0] = g_variant_new_uint64(si[0]);
		rational[1] = g_variant_new_uint64(si[1]);
		*data = g_variant_new_tuple(rational, 2);
		break;
	case SR_CONF_DATALOG:
		/* There really isn't a way to be sure the device is logging. */
		return SR_ERR_NA;
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		if (devc->mqflags & SR_MQFLAG_SPL_FREQ_WEIGHT_A)
			*data = g_variant_new_string("A");
		else
			*data = g_variant_new_string("C");
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		if (devc->mqflags & SR_MQFLAG_SPL_TIME_WEIGHT_F)
			*data = g_variant_new_string("F");
		else
			*data = g_variant_new_string("S");
		break;
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else
			*data = g_variant_new_string("Memory");
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
	uint64_t p, q;
	unsigned int i;
	int tmp, ret;
	const char *tmp_str;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!di->priv) {
		sr_err("Driver was not initialized.");
		return SR_ERR;
	}

	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	case SR_CONF_SAMPLE_INTERVAL:
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < ARRAY_SIZE(kecheng_kc_330b_sample_intervals); i++) {
			if (kecheng_kc_330b_sample_intervals[i][0] != p || kecheng_kc_330b_sample_intervals[i][1] != q)
				continue;
			devc->sample_interval = i;
			devc->config_dirty = TRUE;
			break;
		}
		if (i == ARRAY_SIZE(kecheng_kc_330b_sample_intervals))
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "A"))
			tmp = SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		else if (!strcmp(tmp_str, "C"))
			tmp = SR_MQFLAG_SPL_FREQ_WEIGHT_C;
		else
			return SR_ERR_ARG;
		devc->mqflags &= ~(SR_MQFLAG_SPL_FREQ_WEIGHT_A | SR_MQFLAG_SPL_FREQ_WEIGHT_C);
		devc->mqflags |= tmp;
		devc->config_dirty = TRUE;
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "F"))
			tmp = SR_MQFLAG_SPL_TIME_WEIGHT_F;
		else if (!strcmp(tmp_str, "S"))
			tmp = SR_MQFLAG_SPL_TIME_WEIGHT_S;
		else
			return SR_ERR_ARG;
		devc->mqflags &= ~(SR_MQFLAG_SPL_TIME_WEIGHT_F | SR_MQFLAG_SPL_TIME_WEIGHT_S);
		devc->mqflags |= tmp;
		devc->config_dirty = TRUE;
		break;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "Live"))
			devc->data_source = DATA_SOURCE_LIVE;
		else if (!strcmp(tmp_str, "Memory"))
			devc->data_source = DATA_SOURCE_MEMORY;
		else
			return SR_ERR;
		devc->config_dirty = TRUE;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	GVariant *tuple, *rational[2];
	GVariantBuilder gvb;
	unsigned int i;

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLE_INTERVAL:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(kecheng_kc_330b_sample_intervals); i++) {
			rational[0] = g_variant_new_uint64(kecheng_kc_330b_sample_intervals[i][0]);
			rational[1] = g_variant_new_uint64(kecheng_kc_330b_sample_intervals[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_SPL_WEIGHT_FREQ:
		*data = g_variant_new_strv(weight_freq, ARRAY_SIZE(weight_freq));
		break;
	case SR_CONF_SPL_WEIGHT_TIME:
		*data = g_variant_new_strv(weight_time, ARRAY_SIZE(weight_time));
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct sr_usb_dev_inst *usb;
	GVariant *gvar, *rational[2];
	const uint64_t *si;
	int stored_mqflags, req_len, buf_len, len, ret;
	unsigned char buf[9];

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	devc->cb_data = cb_data;
	devc->num_samples = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	if (devc->data_source == DATA_SOURCE_LIVE) {
		/* Force configuration. */
		kecheng_kc_330b_configure(sdi);

		if (kecheng_kc_330b_status_get(sdi, &ret) != SR_OK)
			return SR_ERR;
		if (ret != DEVICE_ACTIVE) {
			sr_err("Device is inactive");
			/* Still continue though, since the device will
			 * just return 30.0 until the user hits the button
			 * on the device -- and then start feeding good
			 * samples back. */
		}
	} else {
		if (kecheng_kc_330b_log_info_get(sdi, buf) != SR_OK)
			return SR_ERR;
		stored_mqflags = buf[4] ? SR_MQFLAG_SPL_TIME_WEIGHT_S : SR_MQFLAG_SPL_TIME_WEIGHT_F;
		stored_mqflags |= buf[5] ? SR_MQFLAG_SPL_FREQ_WEIGHT_C : SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		devc->stored_samples = (buf[7] << 8) | buf[8];
		if (devc->stored_samples == 0) {
			/* Notify frontend of empty log by sending start/end packets. */
			packet.type = SR_DF_END;
			sr_session_send(cb_data, &packet);
			return SR_OK;
		}

		if (devc->limit_samples && devc->limit_samples < devc->stored_samples)
			devc->stored_samples = devc->limit_samples;

		si = kecheng_kc_330b_sample_intervals[buf[1]];
		rational[0] = g_variant_new_uint64(si[0]);
		rational[1] = g_variant_new_uint64(si[1]);
		gvar = g_variant_new_tuple(rational, 2);
		src = sr_config_new(SR_CONF_SAMPLE_INTERVAL, gvar);
		packet.type = SR_DF_META;
		packet.payload = &meta;
		meta.config = g_slist_append(NULL, src);
		sr_session_send(devc->cb_data, &packet);
		g_free(src);
	}

	if (!(devc->xfer = libusb_alloc_transfer(0)))
		return SR_ERR;

	usb_source_add(sdi->session, drvc->sr_ctx, 10,
		kecheng_kc_330b_handle_events, (void *)sdi);

	if (devc->data_source == DATA_SOURCE_LIVE) {
		buf[0] = CMD_GET_LIVE_SPL;
		buf_len = 1;
		devc->state = LIVE_SPL_WAIT;
		devc->last_live_request = g_get_monotonic_time() / 1000;
		req_len = 3;
	} else {
		buf[0] = CMD_GET_LOG_DATA;
		buf[1] = 0;
		buf[2] = 0;
		buf_len = 4;
		devc->state = LOG_DATA_WAIT;
		if (devc->stored_samples < 63)
			buf[3] = devc->stored_samples;
		else
			buf[3] = 63;
		/* Command ack byte + 2 bytes per sample. */
		req_len = 1 + buf[3] * 2;
	}

	ret = libusb_bulk_transfer(usb->devhdl, EP_OUT, buf, buf_len, &len, 5);
	if (ret != 0 || len != 1) {
		sr_dbg("Failed to start acquisition: %s", libusb_error_name(ret));
		libusb_free_transfer(devc->xfer);
		return SR_ERR;
	}

	libusb_fill_bulk_transfer(devc->xfer, usb->devhdl, EP_IN, devc->buf,
			req_len, kecheng_kc_330b_receive_transfer, (void *)sdi, 15);
	if (libusb_submit_transfer(devc->xfer) != 0) {
		libusb_free_transfer(devc->xfer);
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* Signal USB transfer handler to clean up and stop. */
	sdi->status = SR_ST_STOPPING;

	devc = sdi->priv;
	if (devc->data_source == DATA_SOURCE_MEMORY && devc->config_dirty) {
		/* The protocol doesn't have a command to clear stored data;
		 * it clears it whenever new configuration is set. That means
		 * we can't just configure the device any time we want when
		 * it's in DATA_SOURCE_MEMORY mode. The only safe time to do
		 * it is now, when we're sure we've pulled in all the stored
		 * data. */
		kecheng_kc_330b_configure(sdi);
	}

	return SR_OK;
}

SR_PRIV struct sr_dev_driver kecheng_kc_330b_driver_info = {
	.name = "kecheng-kc-330b",
	.longname = "Kecheng KC-330B",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
