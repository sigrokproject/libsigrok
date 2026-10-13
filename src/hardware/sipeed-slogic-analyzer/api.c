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
#include "slogic/slogic.h"

/* Bridge the vendored libslogic core (slogic/) to libsigrok's libusb handle.
 * The register/AUX protocol, model tables, samplerate search, VTH->DAC and the
 * Combo 8 start command all live in the shared core now; this file keeps only
 * the libsigrok-specific surface: scan/config, the sample-major data shaping,
 * and the driver-side soft trigger. */

/* Core-bridging helpers defined after the driver struct but used above it. */
static unsigned int adapter_channels(const slogic_model *m, int32_t *out);
static int slogic_dev_reset(const struct sr_dev_inst *sdi);

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
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST
};

/* Model tables (samplerates, channel modes, ceilings, pattern names) now
 * live in the shared core (slogic/) and are read through slogic_model and
 * the slogic_* accessors. */

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,    SR_TRIGGER_ONE,  SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static struct sr_dev_driver sipeed_slogic_analyzer_driver_info;

static gpointer libusb_event_thread_func(gpointer user_data)
{
	struct sr_dev_inst *sdi;
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;

	sdi = user_data;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	while (devc->libusb_event_thread_run) {
		libusb_handle_events_timeout_completed(
			drvc->sr_ctx->libusb_ctx, &(struct timeval){ 1, 0 },
			NULL);
	}

	return NULL;
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
	GSList *devices;
	GSList *l, *conn_devices;
	const char *conn;
	char cbuf[128];
	char *iManufacturer, *iProduct, *iSerialNumber, *iPortPath;

	struct sr_channel *ch;
	unsigned int i;
	gchar *channel_name;

	(void)options;

	conn = NULL;

	devices = NULL;
	drvc = di->context;
	// drvc->instances = NULL;

	/* scan for devices, either based on a SR_CONF_CONN option
   * or on a USB scan. */
	for (l = options; l; l = l->next) {
		option = l->data;
		switch (option->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(option->data, NULL);
			sr_info("Use conn: %s", conn);
			sr_err("Not supported now!");
			return NULL;
			break;
		default:
			sr_warn("Unhandled option key: %u", option->key);
		}
	}

	models = slogic_models(&nmodels);
	for (mi = 0; mi < nmodels; mi++) {
		model = models[mi];
		conn = g_strdup_printf("%04x.%04x", USB_VID_SIPEED, model->pid);
		/* Find all slogic compatible devices. */
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
		for (l = conn_devices; l; l = l->next) {
			usb = l->data;
			ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
			if (SR_OK != ret)
				continue;
			libusb_get_device_descriptor(
				libusb_get_device(usb->devhdl), &des);
			libusb_get_string_descriptor_ascii(usb->devhdl,
							   des.iManufacturer,
							   cbuf, sizeof(cbuf));
			iManufacturer = g_strdup(cbuf);
			libusb_get_string_descriptor_ascii(
				usb->devhdl, des.iProduct, cbuf, sizeof(cbuf));
			iProduct = g_strdup(cbuf);
			libusb_get_string_descriptor_ascii(usb->devhdl,
							   des.iSerialNumber,
							   cbuf, sizeof(cbuf));
			iSerialNumber = g_strdup(cbuf);
			usb_get_port_path(libusb_get_device(usb->devhdl), cbuf,
					  sizeof(cbuf));
			iPortPath = g_strdup(cbuf);

			sdi = sr_dev_inst_user_new(iManufacturer, iProduct,
						   NULL);
			sdi->serial_num = iSerialNumber;
			sdi->connection_id = iPortPath;
			sdi->status = SR_ST_INACTIVE;
			sdi->conn = usb;
			sdi->inst_type = SR_INST_USB;

			devc = g_malloc0(sizeof(struct dev_context));
			sdi->priv = devc;

			{
				devc->model = model;

				devc->limit_samplechannel =
					model->limits[model->limit_count - 1].channels;
				devc->limit_samplerate = slogic_max_rate(model,
					devc->limit_samplechannel);

				devc->cur_samplechannel =
					devc->limit_samplechannel;
				devc->cur_samplerate = devc->limit_samplerate;
				devc->cur_pattern_mode_idx = SLOGIC_PATTERN_NORMAL;
				devc->voltage_threshold[0] =
					devc->voltage_threshold[1] = 1.7000000000000004;

				devc->digital_group =
					sr_channel_group_new(sdi, "LA", NULL);
				for (i = 0; i < devc->limit_samplechannel;
				     i++) {
					channel_name =
						g_strdup_printf("D%u", i);
					ch = sr_channel_new(sdi, i,
							    SR_CHANNEL_LOGIC,
							    TRUE, channel_name);
					g_free(channel_name);
					devc->digital_group
						->channels = g_slist_append(
						devc->digital_group->channels,
						ch);
				}

				devc->speed = libusb_get_device_speed(
					libusb_get_device(usb->devhdl));
			}

			sr_usb_close(usb);
			devices = g_slist_append(devices, sdi);
		}
		// g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
		g_free(conn);
	}

	return std_scan_complete(di, devices);
}

/*
 * The analyzer re-enumerates with a new bus address after a USB reset or a
 * replug, so the bus/address recorded at scan time stops opening. Find it
 * again on the same port (connection_id), else by serial number, and refresh
 * usb->bus/address so the caller can retry.
 */
static int rebind_usb(struct sr_dev_inst *sdi, libusb_context *ctx)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_device_descriptor des;
	libusb_device **devlist, *match = NULL;
	libusb_device_handle *hdl;
	char buf[64];
	ssize_t n;

	if ((n = libusb_get_device_list(ctx, &devlist)) < 0)
		return SR_ERR;

	for (int pass = 0; pass < 2 && !match; pass++) {
		for (ssize_t i = 0; i < n && !match; i++) {
			libusb_get_device_descriptor(devlist[i], &des);
			if (des.idVendor != USB_VID_SIPEED ||
			    des.idProduct != devc->model->pid)
				continue;
			if (pass == 0) {
				if (sdi->connection_id &&
				    usb_get_port_path(devlist[i], buf, sizeof(buf)) == SR_OK &&
				    !strcmp(buf, sdi->connection_id))
					match = devlist[i];
			} else if (sdi->serial_num &&
				   libusb_open(devlist[i], &hdl) == LIBUSB_SUCCESS) {
				if (libusb_get_string_descriptor_ascii(hdl,
					des.iSerialNumber, (unsigned char *)buf,
					sizeof(buf)) > 0 &&
				    !strcmp(buf, sdi->serial_num))
					match = devlist[i];
				libusb_close(hdl);
			}
		}
	}

	if (match) {
		sr_info("Device re-enumerated: %d.%d -> %d.%d.", usb->bus,
			usb->address, libusb_get_bus_number(match),
			libusb_get_device_address(match));
		usb->bus = libusb_get_bus_number(match);
		usb->address = libusb_get_device_address(match);
	}
	libusb_free_device_list(devlist, 1);

	return match ? SR_OK : SR_ERR;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct sr_dev_driver *di;
	struct drv_context *drvc;

	usb = sdi->conn;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (SR_OK != ret) {
		if (rebind_usb(sdi, drvc->sr_ctx->libusb_ctx) != SR_OK)
			return ret;
		ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
		if (SR_OK != ret)
			return ret;
	}

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

	devc->libusb_event_thread_run = 1;
	devc->libusb_event_thread = g_thread_new("libusb_event_thread",
						 libusb_event_thread_func, sdi);
	if (!devc->libusb_event_thread) {
		devc->libusb_event_thread_run = 0;
		sr_err("Unable to new libusb_event_thread!");
		libusb_release_interface(usb->devhdl, 0);
		sr_usb_close(usb);
		return SR_ERR_MALLOC;
	}

	slogic_dev_reset(sdi);

	devc->voltage_threshold[0] = devc->voltage_threshold[1] = 1.7000000000000004;

	sr_config_set(sdi, NULL, SR_CONF_VOLTAGE_THRESHOLD,
				g_variant_new("(dd)", &devc->voltage_threshold[0],
			      &devc->voltage_threshold[1]));

	return std_dummy_dev_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct sr_dev_driver *di;
	struct drv_context *drvc;

	usb = sdi->conn;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	/*
	 * Stop and JOIN the libusb event thread BEFORE touching the handle. It sits in
	 * libusb_handle_events_timeout_completed() on this context (blocking up to 1 s per
	 * iteration), and releasing the interface / closing the handle while that call is
	 * in flight is a use-after-close — an intermittent crash on config change / run /
	 * stop, and worse on macOS's libusb backend. Join first, then release + close.
	 */
	devc->libusb_event_thread_run = 0;
	if (devc->libusb_event_thread) {
		g_thread_join(devc->libusb_event_thread);
		devc->libusb_event_thread = NULL;
	}

	ret = libusb_release_interface(usb->devhdl, 0);
	if (ret != LIBUSB_SUCCESS) {
		switch (ret) {
		case LIBUSB_ERROR_NO_DEVICE:
			sr_err("Device has been disconnected.");
			// return SR_ERR_DEV_CLOSED;
			break;
		default:
			sr_err("Unable to release Interface for %s.",
			       libusb_error_name(ret));
			break;
		}
	}

	sr_usb_close(usb);

	return std_dummy_dev_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
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
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_tuple_double(devc->voltage_threshold[0],
					      devc->voltage_threshold[1]);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (g_variant_get_uint64(data) > devc->limit_samplerate ||
		    std_u64_idx(data, devc->model->rates, devc->model->rate_count) < 0) {
			devc->cur_samplerate = devc->limit_samplerate;
			sr_warn("Reach limit or not supported, wrap to %uMHz.",
				devc->limit_samplerate / SR_MHZ(1));
		} else {
			devc->cur_samplerate = g_variant_get_uint64(data);

			if (devc->cur_samplerate > devc->limit_samplerate)
				devc->cur_samplerate = devc->limit_samplerate;
		}

		break;
	case SR_CONF_NUM_LOGIC_CHANNELS: {
		int32_t chans[8];
		unsigned int nchans = adapter_channels(devc->model, chans);
		if (std_i32_idx(data, chans, nchans) < 0) {
			devc->cur_samplechannel = devc->limit_samplechannel;
			sr_warn("Reach limit or not supported, wrap to %uch.",
				devc->limit_samplechannel);
		} else {
			devc->cur_samplechannel = g_variant_get_int32(data);
			devc->limit_samplerate = slogic_max_rate(devc->model,
				devc->cur_samplechannel);
			if (devc->cur_samplerate > devc->limit_samplerate)
				devc->cur_samplerate = devc->limit_samplerate;
		}
		/* enable exactly the selected channels */
		for (GSList *l = devc->digital_group->channels; l; l = l->next) {
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
		/* Applied together with channel/rate/vref at acquisition start
		 * (slogic_configure, canonical order per protocol.md 6.7). */
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->cur_limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &devc->voltage_threshold[0],
			      &devc->voltage_threshold[1]);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

int config_channel_set(const struct sr_dev_inst *sdi, struct sr_channel *ch, unsigned int changes) {
	struct dev_context *devc = sdi ? (sdi->priv) : NULL;
	int32_t chans[8];
	unsigned int nchans, i;
	int32_t new_samplechannel;
	GSList *l;

	(void)ch;
	if (!devc || !devc->model)
		return SR_ERR;
	if (changes != SR_CHANNEL_SET_ENABLED)
		return SR_OK;

	nchans = adapter_channels(devc->model, chans);
	new_samplechannel = chans[0];
	for (l = devc->digital_group->channels; l; l = l->next) {
		struct sr_channel *c = l->data;
		if (!c->enabled || (int32_t)c->index < new_samplechannel)
			continue;
		for (i = 0; i < nchans; i++) {
			if (chans[i] > (int32_t)c->index) {
				new_samplechannel = chans[i];
				break;
			}
		}
	}

	if (new_samplechannel > devc->cur_samplechannel) {
		devc->cur_samplechannel = new_samplechannel;
		devc->limit_samplerate = slogic_max_rate(devc->model,
			devc->cur_samplechannel);
		if (devc->cur_samplerate > devc->limit_samplerate)
			devc->cur_samplerate = devc->limit_samplerate;
	}
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;

	(void)cg;

	devc = sdi ? (sdi->priv) : NULL;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		ret = STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
				      devopts);
		break;
	case SR_CONF_SAMPLERATE:
		if (!devc->model) {
			ret = SR_ERR_ARG;
			break;
		}
		*data = std_gvar_samplerates(devc->model->rates,
			1 + std_u64_idx(g_variant_new_uint64(devc->limit_samplerate),
				devc->model->rates, devc->model->rate_count));
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS: {
		int32_t chans[8];
		unsigned int nchans = adapter_channels(devc->model, chans);
		*data = std_gvar_array_i32(chans, nchans);
		break;
	}
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_strv(slogic_pattern_names,
			SLOGIC_PATTERN_COUNT);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_min_max_step_thresholds(0, 6, 0.1);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
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

/* --- bridge to the vendored libslogic core (slogic/) --- */

static int adapter_ctrl_write(void *ctx, uint8_t b_request, uint16_t w_value,
			      uint16_t w_index, const uint8_t *data,
			      uint16_t len, unsigned timeout_ms)
{
	struct sr_usb_dev_inst *usb = ctx;

	return libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, b_request,
		w_value, w_index, (unsigned char *)data, len, (int)timeout_ms);
}

static int adapter_ctrl_read(void *ctx, uint8_t b_request, uint16_t w_value,
			     uint16_t w_index, uint8_t *data, uint16_t len,
			     unsigned timeout_ms)
{
	struct sr_usb_dev_inst *usb = ctx;

	return libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN, b_request,
		w_value, w_index, data, len, (int)timeout_ms);
}

static void adapter_transport(const struct sr_dev_inst *sdi, slogic_transport *t)
{
	t->ctx = sdi->conn; /* struct sr_usb_dev_inst */
	t->control_write = adapter_ctrl_write;
	t->control_read = adapter_ctrl_read;
}

static unsigned int adapter_channels(const slogic_model *m, int32_t *out)
{
	size_t i;

	for (i = 0; i < m->limit_count; i++)
		out[i] = m->limits[i].channels;
	return (unsigned int)m->limit_count;
}

static void adapter_config(const struct sr_dev_inst *sdi, slogic_config *c)
{
	struct dev_context *devc = sdi->priv;

	*c = (slogic_config){
		.channel_count = devc->cur_samplechannel,
		.samplerate_hz = devc->cur_samplerate,
		.threshold_v = (devc->voltage_threshold[0] +
				devc->voltage_threshold[1]) / 2.0,
		.pattern_mode = devc->cur_pattern_mode_idx,
	};
}

SR_PRIV void slogic_submit_raw_data(void *data, size_t len,
				   const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	uint8_t *ptr = data;
	uint64_t nCh = devc->cur_samplechannel;

	if (nCh < 8) {
		size_t nsp_in_bytes = 8 / nCh; // NOW must be 2 and 4
		ptr = malloc(len * nsp_in_bytes);
		for (size_t i = 0; i < len; i += nCh) {
			for (size_t j = 0; j < 8; j++) {
				ptr[i * nsp_in_bytes + j] =
					(((uint8_t *)
						  data)[i + j / nsp_in_bytes] >>
					 (j % nsp_in_bytes * nCh)) &
					((1 << nCh) - 1);
			}
		}
		len *= nsp_in_bytes; // need reshape
	}

	sr_session_send(sdi, &(struct sr_datafeed_packet){
				     .type = SR_DF_LOGIC,
				     .payload = &(struct sr_datafeed_logic){
					     .length = len,
					     .unitsize = (nCh + 7) / 8,
					     .data = ptr,
				     } });

	if (nCh < 8)
		free(ptr);
}

int slogic_soft_trigger_raw_data(void *data, size_t len,
				   const struct sr_dev_inst *sdi)
{
	int ret = 0;
	struct dev_context *devc = sdi->priv;

	uint8_t *ptr = data;
	uint64_t nCh = devc->cur_samplechannel;
	uint8_t uintsize = (nCh + 7) / 8;

	if (nCh < 8) {
		size_t nsp_in_bytes = 8 / nCh; // NOW must be 2 or 4
		ptr = malloc(len * nsp_in_bytes);
		for (size_t i = 0; i < len; i += nCh) {
			for (size_t j = 0; j < 8; j++) {
				ptr[i * nsp_in_bytes + j] =
					(((uint8_t *)
						  data)[i + j / nsp_in_bytes] >>
					 (j % nsp_in_bytes * nCh)) &
					((1 << nCh) - 1);
			}
		}
		len *= nsp_in_bytes; // need reshape
	}

	// // debug raw data
	// sr_session_send(sdi, &(struct sr_datafeed_packet){
	// 				.type = SR_DF_LOGIC,
	// 				.payload = &(struct sr_datafeed_logic){
	// 					.length = len,
	// 					.unitsize = (nCh + 7) / 8,
	// 					.data = ptr,
	// 				} });

	/* Everything here is counted in SAMPLES (devc->samples_sent), the unit of
	 * cur_limit_samples; ptr holds len / uintsize samples. On a match the soft
	 * trigger has already sent the pre-trigger samples; send the rest of this
	 * buffer from the trigger point, capped at the remaining limit. */
	int pre_trigger_samples = 0;
	int64_t trigger_offset = soft_trigger_logic_check(devc->stl, ptr, len, &pre_trigger_samples);
	if (trigger_offset > -1) {
		uint64_t remain, want;

		ret = 1;
		devc->samples_sent += pre_trigger_samples;

		remain = len / uintsize - (uint64_t)trigger_offset;
		want = devc->cur_limit_samples > devc->samples_sent ?
			devc->cur_limit_samples - devc->samples_sent : 0;
		if (remain > want)
			remain = want;
		if (remain) {
			sr_session_send(sdi, &(struct sr_datafeed_packet){
						.type = SR_DF_LOGIC,
						.payload = &(struct sr_datafeed_logic){
							.length = remain * uintsize,
							.unitsize = uintsize,
							.data = ptr + trigger_offset * uintsize,
						} });
			devc->samples_sent += remain;
		}
	}

	if (nCh < 8)
		free(ptr);

	return ret;
}

static inline void clear_ep(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint8_t ep = devc->model->ep_in;

	size_t tmp_size = 4 * 1024 * 1024;
	uint8_t *tmp = malloc(tmp_size);
	int actual_length = 0;
	do {
		libusb_bulk_transfer(usb->devhdl, ep, tmp, tmp_size,
				     &actual_length, 100);
	} while (actual_length);
	free(tmp);
	sr_dbg("Cleared EP: 0x%02x", ep);
}

/* Reset/start/stop dispatch onto the shared core (Combo 8 vs U3 handled inside
 * it). devc->model is the core model, so no per-model function table is needed. */
static int slogic_dev_reset(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	slogic_transport t;

	adapter_transport(sdi, &t);
	return slogic_reset(devc->model, &t) == SLOGIC_OK ? SR_OK : SR_ERR;
}

SR_PRIV int slogic_dev_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	slogic_transport t;
	slogic_config c;

	adapter_transport(sdi, &t);
	adapter_config(sdi, &c);
	/* Reset before (re)configuring. The device only reliably (re)starts its USB
	 * stream from a clean reset: dev_open resets once, so the first capture
	 * streams, but a second session or a stall re-arm that skips the reset finds
	 * the stream "did not start". Resetting here makes every (re)start uniform.
	 * slogic_reset is model-aware (a no-op for Combo 8), so this is safe for all. */
	if (slogic_reset(devc->model, &t) != SLOGIC_OK)
		return SR_ERR;
	if (slogic_configure(devc->model, &t, &c) != SLOGIC_OK)
		return SR_ERR;
	return slogic_run(devc->model, &t, &c) == SLOGIC_OK ? SR_OK : SR_ERR;
}

SR_PRIV int slogic_dev_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	slogic_transport t;

	if (devc->model->proto == SLOGIC_PROTO_COMBO8) {
		/* No reliable stop command; draining the EP is the stop. */
		clear_ep(sdi);
		return SR_OK;
	}
	adapter_transport(sdi, &t);
	return slogic_stop(devc->model, &t) == SLOGIC_OK ? SR_OK : SR_ERR;
}