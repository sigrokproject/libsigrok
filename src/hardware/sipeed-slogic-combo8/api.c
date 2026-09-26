/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 taorye <taorye@outlook.com>
 * Copyright (C) 2026 Lindolfo Meira <meiraa@gmail.com>
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
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,    SR_TRIGGER_ONE,  SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_MHZ(16),
	SR_MHZ(20),
	SR_MHZ(32),
	/* x 4ch */
	SR_MHZ(40),
	SR_MHZ(80),
	/* x 2ch */
	SR_MHZ(160),
};

/* Channel counts and the maximum samplerate each supports, index-aligned. */
static const int32_t samplechannels[] = { 2, 4, 8 };
static const uint64_t limit_samplerates[] = {
	SR_MHZ(160),
	SR_MHZ(80),
	SR_MHZ(40),
};

/*
 * Set the samplerate, clamping it down to the maximum the current channel
 * count supports.  The channel count itself is left unchanged.
 */
static void devc_set_samplerate(struct dev_context *devc,
				uint64_t new_samplerate)
{
	if (new_samplerate > devc->limit_samplerate) {
		sr_warn("Samplerate clamped to %" PRIu64 "MHz: current setting "
			"exceeds the device's %" PRIu64 "CH limit.",
			devc->limit_samplerate / SR_MHZ(1),
			devc->cur_samplechannel);
		new_samplerate = devc->limit_samplerate;
	}
	devc->cur_samplerate = new_samplerate;
}

/*
 * Set the active channel count and its samplerate limit.  Does not touch the
 * samplerate itself: callers that raise the channel count beyond what the
 * current rate supports must re-apply the rate with devc_set_samplerate()
 * afterwards to clamp it down.
 */
static void devc_set_samplechannel(struct dev_context *devc,
				   uint64_t new_samplechannel)
{
	size_t idx;

	for (idx = 0; idx < ARRAY_SIZE(samplechannels); idx++) {
		if ((uint64_t)samplechannels[idx] == new_samplechannel)
			break;
	}
	if (idx >= ARRAY_SIZE(samplechannels))
		return;

	devc->cur_samplechannel = new_samplechannel;
	devc->limit_samplerate = limit_samplerates[idx];
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	int ret;
	int i;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *option;
	struct libusb_device_descriptor des;
	GSList *devices;
	GSList *l, *conn_devices;
	const char *conn;
	char cbuf[128];
	char *iManufacturer, *iProduct, *iSerialNumber, *iPortPath;

	conn = NULL;
	devices = NULL;
	drvc = di->context;

	for (l = options; l; l = l->next) {
		option = l->data;
		switch (option->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(option->data, NULL);
			sr_info("Use conn: %s", conn);
			break;
		default:
			sr_warn("Unhandled option key: %u", option->key);
		}
	}

	if (!conn)
		conn = USB_CONN;

	conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	for (l = conn_devices; l; l = l->next) {
		usb = l->data;
		ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
		if (ret != SR_OK) {
			sr_usb_dev_inst_free(usb);
			continue;
		}

		libusb_get_device_descriptor(libusb_get_device(usb->devhdl),
					     &des);

		iManufacturer = NULL;
		if (des.iManufacturer) {
			cbuf[0] = '\0';
			libusb_get_string_descriptor_ascii(
				usb->devhdl, des.iManufacturer,
				(unsigned char *)cbuf, sizeof(cbuf));
			if (cbuf[0])
				iManufacturer = g_strdup(cbuf);
		}
		if (!iManufacturer)
			iManufacturer = g_strdup("Sipeed");

		iProduct = NULL;
		if (des.iProduct) {
			cbuf[0] = '\0';
			libusb_get_string_descriptor_ascii(
				usb->devhdl, des.iProduct,
				(unsigned char *)cbuf, sizeof(cbuf));
			if (cbuf[0])
				iProduct = g_strdup(cbuf);
		}
		if (!iProduct)
			iProduct = g_strdup("SLogic Analyzer");

		iSerialNumber = NULL;
		if (des.iSerialNumber) {
			cbuf[0] = '\0';
			libusb_get_string_descriptor_ascii(
				usb->devhdl, des.iSerialNumber,
				(unsigned char *)cbuf, sizeof(cbuf));
			if (cbuf[0])
				iSerialNumber = g_strdup(cbuf);
		}

		cbuf[0] = '\0';
		usb_get_port_path(libusb_get_device(usb->devhdl), cbuf,
				  sizeof(cbuf));
		iPortPath = g_strdup(cbuf);

		sr_usb_close(usb);

		sdi = sr_dev_inst_user_new(iManufacturer, iProduct, NULL);
		g_free(iManufacturer);
		g_free(iProduct);

		if (!sdi) {
			g_free(iSerialNumber);
			g_free(iPortPath);
			sr_usb_dev_inst_free(usb);
			continue;
		}

		sdi->driver = di;
		sdi->serial_num = iSerialNumber;
		sdi->connection_id = iPortPath;
		sdi->status = SR_ST_INACTIVE;
		sdi->conn = usb;
		sdi->inst_type = SR_INST_USB;

		for (i = 0; i < 8; i++) {
			sr_snprintf_ascii(cbuf, sizeof(cbuf), "D%d", i);
			sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, cbuf);
		}

		devc = g_malloc0(sizeof(struct dev_context));
		devc->capture_ratio = DEFAULT_CAPTURE_RATIO;
		devc_set_samplechannel(devc, DEFAULT_SAMPLE_CHANNEL);
		devc_set_samplerate(devc, DEFAULT_SAMPLERATE);
		sdi->priv = devc;

		devices = g_slist_append(devices, sdi);
	}

	g_slist_free(conn_devices);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct sr_dev_driver *di;
	struct drv_context *drvc;

	if (!sdi)
		return SR_ERR_DEV_CLOSED;

	usb = sdi->conn;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, 0);
	if (ret != LIBUSB_SUCCESS) {
		switch (ret) {
		case LIBUSB_ERROR_BUSY:
			sr_err("Unable to claim USB interface. Another "
			       "program or driver has already claimed it.");
			break;
		case LIBUSB_ERROR_NO_DEVICE:
			sr_err("Device has been disconnected.");
			break;
		default:
			sr_err("Unable to claim interface: %s.",
			       libusb_error_name(ret));
			break;
		}
		sr_usb_close(usb);
		return SR_ERR;
	}

	if (devc->cur_samplerate == 0)
		devc_set_samplerate(devc, DEFAULT_SAMPLERATE);

	return std_dummy_dev_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_usb_dev_inst *usb;

	if (!sdi)
		return SR_ERR_ARG;

	if (sdi->priv) {
		struct dev_context *devc = sdi->priv;
		if (devc->transfers_used > 0)
			sipeed_slogic_combo8_acquisition_stop(sdi);
	}

	usb = sdi->conn;

	if (usb && usb->devhdl) {
		/*
		 * Note: do not gate this on sdi->status.  sr_dev_close() sets
		 * the status to SR_ST_INACTIVE before invoking this callback,
		 * so gating on SR_ST_ACTIVE would leak the libusb handle and
		 * keep interface 0 claimed, making a subsequent open of the
		 * same device fail with BUSY.
		 */
		ret = libusb_release_interface(usb->devhdl, 0);
		if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_NO_DEVICE)
			sr_err("Unable to release Interface: %s.",
			       libusb_error_name(ret));
		sr_usb_close(usb);
	}

	return SR_OK;
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
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/*
 * Enable channels below cur_samplechannel and disable channels at or above,
 * matching the active capture mode.
 */
static void update_channel_enables(const struct sr_dev_inst *sdi,
				   struct dev_context *devc)
{
	struct sr_channel *ch;
	GSList *l;
	size_t idx;

	devc->updating_channels = TRUE;

	for (l = sdi->channels, idx = 0; l; l = l->next, idx++) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC) {
			sr_err("Unexpected channel type on channel %zu", idx);
			continue;
		}
		sr_dev_channel_enable(ch, idx < devc->cur_samplechannel);
	}

	devc->updating_channels = FALSE;
}

/*
 * Validate a channel count GVariant against the supported samplechannels[]
 * table.  Returns the table index, or -1 if not supported.
 */
static int samplechannel_idx(GVariant *data)
{
	int64_t val;
	size_t i;

	if (!g_variant_is_of_type(data, G_VARIANT_TYPE_INT32))
		return -1;
	val = g_variant_get_int32(data);
	for (i = 0; i < ARRAY_SIZE(samplechannels); i++) {
		if (samplechannels[i] == val)
			return (int)i;
	}
	return -1;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE: {
		uint64_t new_samplerate;
		size_t i;

		new_samplerate = g_variant_get_uint64(data);
		if (std_u64_idx(data, ARRAY_AND_SIZE(samplerates)) < 0)
			return SR_ERR_ARG;

		/*
		 * The capture mode always follows the rate: select the largest
		 * mode (highest channel count) that can sustain the selected
		 * rate.  This way lowering the rate widens the capture (160M
		 * -> 2ch, 80M -> 4ch, 40M -> 8ch) and raising it narrows it.
		 * The mode is only a hint; the device may settle on a lower
		 * effective rate, which is reflected below.
		 */
		for (i = ARRAY_SIZE(samplechannels); i-- > 0;) {
			if (limit_samplerates[i] >= new_samplerate) {
				devc_set_samplechannel(
					devc, (uint64_t)samplechannels[i]);
				break;
			}
		}
		devc_set_samplerate(devc, new_samplerate);
		update_channel_enables(sdi, devc);
		sr_info("Channel mode set to %" PRIu64 "CH to match the selected "
			"samplerate (%" PRIu64 "MHz).",
			devc->cur_samplechannel,
			devc->cur_samplerate / SR_MHZ(1));
		break;
	}
	case SR_CONF_NUM_LOGIC_CHANNELS:
		if (samplechannel_idx(data) < 0)
			return SR_ERR_ARG;
		devc_set_samplechannel(devc, g_variant_get_int32(data));
		devc_set_samplerate(devc, devc->cur_samplerate);
		update_channel_enables(sdi, devc);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		devc->limit_msec = 0;
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		devc->limit_samples = 0;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		if (devc->capture_ratio > 100) {
			sr_warn("Capture ratio clamped from %" PRIu64
				"%% to 100%%",
				devc->capture_ratio);
			devc->capture_ratio = 100;
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
				       devopts);
	case SR_CONF_SAMPLERATE:
		/*
		 * Always list the full range; config_set() picks the capture
		 * mode (channel count) that can sustain the selected rate.
		 */
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(samplechannels));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
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
	struct sr_channel *lch;
	GSList *l;
	uint64_t new_samplechannel;
	size_t i;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	/* Swallow changes made by our own bulk update. */
	if (devc->updating_channels)
		return SR_OK;

	if (changes != SR_CHANNEL_SET_ENABLED)
		return SR_OK;

	/*
	 * Only react to channels being enabled.  Disabling a channel must not
	 * shrink the capture mode, and ignoring disable events also prevents
	 * update_channel_enables() from re-raising the count while it walks the
	 * channel list disabling the channels above the new count.
	 */
	if (!ch->enabled)
		return SR_OK;

	/*
	 * Derive the channel count from the highest enabled channel, rounding
	 * up to the next supported count.  Only ever raise the count: toggling
	 * a channel off must not silently shrink the capture mode.
	 */
	new_samplechannel = devc->cur_samplechannel;
	for (l = sdi->channels; l; l = l->next) {
		lch = l->data;
		if (lch->type != SR_CHANNEL_LOGIC || !lch->enabled)
			continue;
		for (i = 0; i < ARRAY_SIZE(samplechannels); i++) {
			if ((uint64_t)samplechannels[i] >
			    (uint64_t)lch->index) {
				if ((uint64_t)samplechannels[i] >
				    new_samplechannel)
					new_samplechannel =
						(uint64_t)samplechannels[i];
				break;
			}
		}
	}

	if (new_samplechannel != devc->cur_samplechannel) {
		devc_set_samplechannel(devc, new_samplechannel);
		devc_set_samplerate(devc, devc->cur_samplerate);
	}

	return SR_OK;
}

static struct sr_dev_driver sipeed_slogic_combo8_driver_info = {
	.name = "sipeed-slogic-combo8",
	.longname = "SiPEED Slogic Combo8",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.config_channel_set = config_channel_set,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = sipeed_slogic_combo8_acquisition_start,
	.dev_acquisition_stop = sipeed_slogic_combo8_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(sipeed_slogic_combo8_driver_info);
