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

static int slogic16U3_remote_test_mode(const struct sr_dev_inst *sdi, uint32_t mode);

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_BUFFERSIZE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t samplerates_slogiccombo8[] = {
	/** 
	 * SLogic Combo 8 (USBHS 480Mbps bw: 40MB/s)
	 *  160M = 2^5*5^1  M
	*/

	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_MHZ(16),
	SR_MHZ(20),
	SR_MHZ(32),
	/* x 8ch */
	SR_MHZ(40),
	/* x 4ch */
	SR_MHZ(80),
	/* x 2ch */
	SR_MHZ(160),
};

static const uint64_t samplechannels_slogiccombo8[] = { 2, 4, 8 };
static const uint64_t limit_samplerates_slogiccombo8[] = { SR_MHZ(160), SR_MHZ(80), SR_MHZ(40) };

static const uint64_t samplerates_slogic16u3[] = {
	/**
	 * SLogic 16U3 (USBSS 5Gbps bw: 400MB/s)
	 * 1200M = 2^4*3^1*5^2  M
	 * 1500M = 2^2*3^1*5^3  M
	 * --1600M = 2^6    *5^2  M
	*/

	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_MHZ(15),
	SR_MHZ(16),
	SR_MHZ(20),
	SR_MHZ(24),
	SR_MHZ(30),
	SR_MHZ(32),
	SR_MHZ(40),
	SR_MHZ(48),
	SR_MHZ(60),
	SR_MHZ(80),
	SR_MHZ(100),
	SR_MHZ(125),
	SR_MHZ(150),
	/* x 16ch */
	SR_MHZ(200),
	/* x 8ch */
	SR_MHZ(300),
	SR_MHZ(400),
	/* x 4ch */
	SR_MHZ(500),
	SR_MHZ(600),
	SR_MHZ(750),
	/* x 2ch */
	SR_MHZ(1200),
	SR_MHZ(1500),
};

static const uint64_t samplechannels_slogic16u3[] = { 2, 4, 8, 16 };
static const uint64_t limit_samplerates_slogic16u3[] = { SR_MHZ(1500), SR_MHZ(750), SR_MHZ(400), SR_MHZ(200) };

static const char *patterns[] = {
	[PATTERN_MODE_NOMAL] = "PATTERN_MODE_NOMAL",
	[PATTERN_MODE_TEST_MAX_SPEED] = "PATTERN_MODE_TEST_MAX_SPEED",
	[PATTERN_MODE_TEST_HARDWARE_USB_MAX_SPEED] = "PATTERN_MODE_TEST_HARDWARE_USB_MAX_SPEED",
	[PATTERN_MODE_TEST_HARDWARE_EMU_DATA] = "PATTERN_MODE_TEST_HARDWARE_EMU_DATA",
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,    SR_TRIGGER_ONE,  SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static struct sr_dev_driver sipeed_slogic_analyzer_driver_info;

static struct slogic_model *const support_models_ptr;

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

	struct slogic_model *model;
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

	for (model = support_models_ptr; model->name; model++) {
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

				devc->limit_samplechannel = devc->model->samplechannel_table[
					devc->model->samplechannel_table_size - 1];
				devc->limit_samplerate = devc->model->limit_samplerate_table[
					std_u64_idx(g_variant_new_uint64(devc->limit_samplechannel),
						devc->model->samplechannel_table, devc->model->samplechannel_table_size)
				];

				devc->cur_samplechannel =
					devc->limit_samplechannel;
				devc->cur_samplerate = devc->limit_samplerate;
				devc->cur_pattern_mode_idx = PATTERN_MODE_NOMAL;
				devc->voltage_threshold[0] =
					devc->voltage_threshold[1] = 1.6f;

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
	if (SR_OK != ret)
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
		return SR_ERR;
	}

	devc->libusb_event_thread_run = 1;
	devc->libusb_event_thread = g_thread_new("libusb_event_thread",
						 libusb_event_thread_func, sdi);
	if (!devc->libusb_event_thread) {
		devc->libusb_event_thread_run = 0;
		sr_err("Unable to new libusb_event_thread!");
		return SR_ERR_MALLOC;
	}

	if (devc->model->operation.remote_reset)
		devc->model->operation.remote_reset(sdi);

	devc->voltage_threshold[0] = devc->voltage_threshold[1] = 1.6f;
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

	devc->libusb_event_thread_run = 0;
	sr_usb_close(usb);
	if (devc->libusb_event_thread) {
		g_thread_join(devc->libusb_event_thread);
		devc->libusb_event_thread = NULL;
	}

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
	case SR_CONF_BUFFERSIZE:
		*data = g_variant_new_uint64(devc->cur_samplechannel);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_string(
			patterns[devc->cur_pattern_mode_idx]);
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
		    std_u64_idx(data, devc->model->samplerate_table, devc->model->samplerate_table_size) < 0) {
			devc->cur_samplerate = devc->limit_samplerate;
			sr_warn("Reach limit or not supported, wrap to %uMHz.",
				devc->limit_samplerate / SR_MHZ(1));
		} else {
			devc->cur_samplerate = g_variant_get_uint64(data);

			if (devc->cur_samplerate > devc->limit_samplerate)
				devc->cur_samplerate = devc->limit_samplerate;
		}

		break;
	case SR_CONF_BUFFERSIZE:
		if (std_u64_idx(data, devc->model->samplechannel_table, devc->model->samplechannel_table_size) < 0) {
			devc->cur_samplechannel = devc->limit_samplechannel;
			sr_warn("Reach limit or not supported, wrap to %uch.",
				devc->limit_samplechannel);
		} else {
			devc->cur_samplechannel = g_variant_get_uint64(data);

			devc->limit_samplerate = devc->model->limit_samplerate_table[
				std_u64_idx(g_variant_new_uint64(devc->cur_samplechannel),
					devc->model->samplechannel_table, devc->model->samplechannel_table_size)
			];

			if (devc->cur_samplerate > devc->limit_samplerate)
				devc->cur_samplerate = devc->limit_samplerate;
		}
		// [en|dis]able channels and dbg
		{
			for (GSList *l = devc->digital_group->channels; l;
			     l = l->next) {
				struct sr_channel *ch = l->data;
				if (ch->type ==
				    SR_CHANNEL_LOGIC) { /* Might as well do this now, these
                                               are static. */
					sr_dev_channel_enable(
						ch, (ch->index >=
						     devc->cur_samplechannel) ?
							    FALSE :
							    TRUE);
				} else {
					sr_warn("devc->digital_group->channels[%u] is not Logic?",
						ch->index);
				}
				sr_dbg("\tch[%2u] %-3s:%d %sabled priv:%p.",
				       ch->index, ch->name, ch->type,
				       ch->enabled ? "en" : "dis", ch->priv);
			}
		}
		break;
	case SR_CONF_PATTERN_MODE:
		devc->cur_pattern_mode_idx =
			std_str_idx(data, ARRAY_AND_SIZE(patterns));
		if (devc->cur_pattern_mode_idx < 0)
			devc->cur_pattern_mode_idx = 0;
		if (devc->model != &support_models_ptr[1]) {
			sr_warn("unsupported model: %s.", devc->model->name);
			break;
		}
		if (devc->cur_pattern_mode_idx == PATTERN_MODE_NOMAL) {
			if (devc->model->operation.remote_reset)
				devc->model->operation.remote_reset(sdi);
			slogic16U3_remote_test_mode(sdi, 0x0);
			sr_dbg("reset model: %s success.", devc->model->name);
		} else if (devc->cur_pattern_mode_idx == PATTERN_MODE_TEST_HARDWARE_USB_MAX_SPEED) {
			slogic16U3_remote_test_mode(sdi, 0x1);
		} else if (devc->cur_pattern_mode_idx == PATTERN_MODE_TEST_HARDWARE_EMU_DATA) {
			slogic16U3_remote_test_mode(sdi, 0x2);
		}
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
		*data = std_gvar_samplerates(
			devc->model->samplerate_table,
			1 + std_u64_idx(g_variant_new_uint64(
						devc->limit_samplerate),
					devc->model->samplerate_table, devc->model->samplerate_table_size));
		if (NULL == devc->model)
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_BUFFERSIZE:
		*data = std_gvar_array_u64(devc->model->samplechannel_table, devc->model->samplechannel_table_size);
		break;
	case SR_CONF_PATTERN_MODE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(patterns));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_min_max_step_thresholds(0, 3.3, 0.1);
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

static int slogic_usb_control_write(const struct sr_dev_inst *sdi,
				    uint8_t request, uint16_t value,
				    uint16_t index, uint8_t *data, size_t len,
				    int timeout)
{
	int ret;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	devc = sdi->priv;
	usb = sdi->conn;

	sr_spew("%s: req:%u value:%u index:%u %p:%u in %dms.", __func__,
		request, value, index, data, len, timeout);
	if (!data && len) {
		sr_warn("%s: Nothing to write although len(%u)>0!", __func__,
			len);
		len = 0;
	} else if (len & 0x3) {
		size_t len_aligndup = (len + 0x3) & (~0x3);
		sr_warn("%s: Align up to %u(from %u)!", __func__, len_aligndup,
			len);
		len = len_aligndup;
	}

	ret = 0;
	for (size_t i = 0; i < len; i += 4) {
		ret += libusb_control_transfer(
			usb->devhdl,
			LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
			request, value + i, index, (unsigned char *)data + i, 4,
			timeout);
		if (ret < 0) {
			sr_err("%s: failed(libusb: %s)!", __func__,
			       libusb_error_name(ret));
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int slogic_usb_control_read(const struct sr_dev_inst *sdi,
				   uint8_t request, uint16_t value,
				   uint16_t index, uint8_t *data, size_t len,
				   int timeout)
{
	int ret;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	devc = sdi->priv;
	usb = sdi->conn;

	sr_spew("%s: req:%u value:%u index:%u %p:%u in %dms.", __func__,
		request, value, index, data, len, timeout);
	if (!data && len) {
		sr_err("%s: Can't read to NULL while len(%u)>0!", __func__,
		       len);
		return SR_ERR_ARG;
	} else if (len & 0x3) {
		size_t len_aligndup = (len + 0x3) & (~0x3);
		sr_warn("%s: Align up to %u(from %u)!", __func__, len_aligndup,
			len);
		len = len_aligndup;
	}

	ret = 0;
	for (size_t i = 0; i < len; i += 4) {
		ret += libusb_control_transfer(
			usb->devhdl,
			LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
			request, value + i, index, (unsigned char *)data + i, 4,
			timeout);
		if (ret < 0) {
			sr_err("%s: failed(libusb: %s)!", __func__,
			       libusb_error_name(ret));
			return SR_ERR_NA;
		}
	}

	return ret;
}

static void slogic_submit_raw_data(void *data, size_t len,
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

	int pre_trigger_samples;
	devc->stl->unitsize = uintsize;
	int64_t trigger_offset = soft_trigger_logic_check(devc->stl, ptr, len, &pre_trigger_samples);
	if (trigger_offset > -1) {
		ret += pre_trigger_samples;

		sr_session_send(sdi, &(struct sr_datafeed_packet){
				     .type = SR_DF_LOGIC,
				     .payload = &(struct sr_datafeed_logic){
					     .length = len - trigger_offset * uintsize,
					     .unitsize = uintsize,
					     .data = ptr + trigger_offset * uintsize,
				     } });

		ret += len / uintsize - trigger_offset;
	}

	if (nCh < 8)
		free(ptr);

	return ret;
}

// #define __USE_MISC 1
// #include <endian.h>
static inline uint16_t htole16(uint16_t value)
{
	const union {
		uint16_t val;
		uint8_t bytes[2];
	} u = { .val = 0x1234 };
	if (u.bytes[0] == 0x34) { // __LITTLE_ENDIAN
		return value;
	} else {
		return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
	}
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

/* SLogic Combo 8 start */
#pragma pack(push, 1)
struct cmd_start_acquisition {
	union {
		struct {
			uint8_t sample_rate_l;
			uint8_t sample_rate_h;
		};
		uint16_t sample_rate;
	};
	uint8_t sample_channel;
};
#pragma pack(pop)

#define CMD_START 0xb1
#define CMD_STOP 0xb3

static int slogic_combo8_remote_run(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const struct cmd_start_acquisition cmd_run = {
		.sample_rate = htole16(devc->cur_samplerate /
				       SR_MHZ(1)), // force little endian
		.sample_channel = devc->cur_samplechannel,
	};
	return slogic_usb_control_write(sdi, CMD_START, 0x0000, 0x0000,
					(uint8_t *)&cmd_run, sizeof(cmd_run),
					500);
}

static int slogic_combo8_remote_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	clear_ep(sdi);
	return SR_OK;
	/* not stable, but can be ignored */
	// int ret = slogic_usb_control_write(sdi, CMD_STOP, 0x0000, 0x0000, NULL, 0,
	// 500); clear_ep(sdi); return ret;
}
/* SLogic Combo 8 end */

/* SLogic16U3 start */
#define SLOGIC16U3_CONTROL_IN_REQ_REG_READ 0x00
#define SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE 0x01

#define SLOGIC16U3_R32_CTRL 0x0004
#define SLOGIC16U3_R32_FLAG 0x0008
#define SLOGIC16U3_R32_AUX 0x000c

static int slogic16U3_remote_test_mode(const struct sr_dev_inst *sdi, uint32_t mode) {
	struct dev_context *devc = sdi->priv;
	uint8_t cmd_aux[64] = { 0 }; // configure aux

	{
		size_t retry = 0;
		memset(cmd_aux, 0, sizeof(cmd_aux));
		*(uint32_t *)(cmd_aux) = 0x00000005;
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4,
					 500);
		do {
			slogic_usb_control_read(
				sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
				SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4, 500);
			sr_dbg("[%u]read aux testmode: %08x.", retry,
			       ((uint32_t *)cmd_aux)[0]);
			retry += 1;
			if (retry > 5)
				return SR_ERR_TIMEOUT;
		} while (!(cmd_aux[2] & 0x01));

		sr_dbg("test_mode length: %u.", (*(uint16_t *)cmd_aux) >> 9);
		slogic_usb_control_read(sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4,
					(*(uint16_t *)cmd_aux) >> 9, 500);

		sr_dbg("aux rd: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);

		((uint32_t *)(cmd_aux + 4))[0] = mode;

		sr_dbg("aux wr: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX + 4, 0x0000,
					 cmd_aux + 4,
					 (*(uint16_t *)cmd_aux) >> 9, 500);

		slogic_usb_control_read(sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4,
					(*(uint16_t *)cmd_aux) >> 9, 500);
		sr_dbg("aux rd: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);

		if (mode != *(uint32_t *)(cmd_aux + 4)) {
			sr_dbg("Failed to configure test_mode.");
		} else {
			sr_dbg("Succeed to configure test_mode.");
		}
	}
}

static int slogic16U3_remote_reset(const struct sr_dev_inst *sdi) {
	struct dev_context *devc = sdi->priv;
	const uint8_t cmd_rst[] = { 0x02, 0x00, 0x00, 0x00 };
	const uint8_t cmd_derst[] = { 0x00, 0x00, 0x00, 0x00 };

	slogic_usb_control_write(sdi, SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
		SLOGIC16U3_R32_CTRL, 0x0000,
		ARRAY_AND_SIZE(cmd_rst), 500);

	return slogic_usb_control_write(sdi, SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
				 SLOGIC16U3_R32_CTRL, 0x0000,
				 ARRAY_AND_SIZE(cmd_derst), 500);
}

static int slogic16U3_remote_run(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const uint8_t cmd_run[] = { 0x01, 0x00, 0x00, 0x00 };
	uint8_t cmd_aux[64] = { 0 }; // configure aux

	{
		size_t retry = 0;
		memset(cmd_aux, 0, sizeof(cmd_aux));
		*(uint32_t *)(cmd_aux) = 0x00000001;
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4,
					 500);
		do {
			slogic_usb_control_read(
				sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
				SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4, 500);
			sr_dbg("[%u]read aux channel: %08x.", retry,
			       ((uint32_t *)cmd_aux)[0]);
			retry += 1;
			if (retry > 5)
				return SR_ERR_TIMEOUT;
		} while (!(cmd_aux[2] & 0x01));
		sr_dbg("channel length: %u.", (*(uint16_t *)cmd_aux) >> 9);
		slogic_usb_control_read(sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4,
					(*(uint16_t *)cmd_aux) >> 9, 500);

		sr_dbg("aux rd: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);

		*(uint32_t *)(cmd_aux + 4) = (1 << devc->cur_samplechannel) - 1;

		sr_dbg("aux wr: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX + 4, 0x0000,
					 cmd_aux + 4,
					 (*(uint16_t *)cmd_aux) >> 9, 500);

		slogic_usb_control_read(sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4,
					(*(uint16_t *)cmd_aux) >> 9, 500);
		sr_dbg("aux rd: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);

		if ((1 << devc->cur_samplechannel) - 1 !=
		    *(uint32_t *)(cmd_aux + 4)) {
			sr_dbg("Failed to configure sample channel.");
		} else {
			sr_dbg("Succeed to configure sample channel.");
		}
	}

	{
		size_t retry = 0;
		memset(cmd_aux, 0, sizeof(cmd_aux));
		*(uint32_t *)(cmd_aux) = 0x00000002;
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4,
					 500);
		do {
			slogic_usb_control_read(
				sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
				SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4, 500);
			sr_dbg("[%u]read aux samplerate: %08x.", retry,
			       ((uint32_t *)cmd_aux)[0]);
			retry += 1;
			if (retry > 5)
				return SR_ERR_TIMEOUT;
		} while (!(cmd_aux[2] & 0x01));
		sr_dbg("samplerate length: %u.", (*(uint16_t *)cmd_aux) >> 9);

		while (((uint16_t *)(cmd_aux + 4))[0] <= 1) {
			slogic_usb_control_read(
				sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
				SLOGIC16U3_R32_AUX + 4, 0x0000, cmd_aux + 4,
				(*(uint16_t *)cmd_aux) >> 9, 500);

			sr_dbg("aux rd: %08x %x %u %u.", ((uint32_t *)cmd_aux)[0],
			       ((uint16_t *)(cmd_aux + 4))[0],
			       ((uint16_t *)(cmd_aux + 4))[1],
			       ((uint32_t *)(cmd_aux + 4))[1]);

			uint64_t base =
				SR_MHZ(1) * ((uint16_t *)(cmd_aux + 4))[1];
			if (base % devc->cur_samplerate) {
				sr_dbg("Failed to configure samplerate from base[%u] %u.",
				       ((uint16_t *)(cmd_aux + 4))[0], base);
				((uint16_t *)(cmd_aux + 4))[0] += 1;
				slogic_usb_control_write(
					sdi,
					SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4, 4, 500);
				continue;
			}
			uint32_t div = base / devc->cur_samplerate;
			((uint32_t *)(cmd_aux + 4))[1] = div-1;

			sr_dbg("aux wr: %08x %x %u %u.", ((uint32_t *)cmd_aux)[0],
			       ((uint16_t *)(cmd_aux + 4))[0],
			       ((uint16_t *)(cmd_aux + 4))[1],
			       ((uint32_t *)(cmd_aux + 4))[1]);
			slogic_usb_control_write(
				sdi, SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
				SLOGIC16U3_R32_AUX + 4, 0x0000, cmd_aux + 4,
				(*(uint16_t *)cmd_aux) >> 9, 500);

			slogic_usb_control_read(
				sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
				SLOGIC16U3_R32_AUX + 4, 0x0000, cmd_aux + 4,
				(*(uint16_t *)cmd_aux) >> 9, 500);
			sr_dbg("aux rd: %08x %x %u %u.", ((uint32_t *)cmd_aux)[0],
			       ((uint16_t *)(cmd_aux + 4))[0],
			       ((uint16_t *)(cmd_aux + 4))[1],
			       ((uint32_t *)(cmd_aux + 4))[1]);
			break;
		}

		if (((uint16_t *)(cmd_aux + 4))[0] <= 1) {
			sr_dbg("Succeed to configure samplerate.");
		} else {
			sr_dbg("Failed to configure samplerate.");
		}
	}

	{
		size_t retry = 0;
		memset(cmd_aux, 0, sizeof(cmd_aux));
		*(uint32_t *)(cmd_aux) = 0x00000003;
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4,
					 500);
		do {
			slogic_usb_control_read(
				sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
				SLOGIC16U3_R32_AUX, 0x0000, cmd_aux, 4, 500);
			sr_dbg("[%u]read vref(/1024x1v6): %08x.", retry,
			       ((uint32_t *)cmd_aux)[0]);
			retry += 1;
			if (retry > 5)
				return SR_ERR_TIMEOUT;
		} while (!(cmd_aux[2] & 0x01));
		// *(uint16_t*)cmd_aux &= ~0xfe00;
		// *(uint16_t*)cmd_aux |= 0x800;
		sr_dbg("vref length: %u.", (*(uint16_t *)cmd_aux) >> 9);
		slogic_usb_control_read(sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4,
					(*(uint16_t *)cmd_aux) >> 9, 500);

		sr_dbg("aux rd: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);

		((uint32_t *)(cmd_aux + 4))[0] =
			(uint32_t)((devc->voltage_threshold[0] +
				    devc->voltage_threshold[1]) /
				   2 / 3.33 / 2 * 1024);

		sr_dbg("aux wr: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);
		slogic_usb_control_write(sdi,
					 SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					 SLOGIC16U3_R32_AUX + 4, 0x0000,
					 cmd_aux + 4,
					 (*(uint16_t *)cmd_aux) >> 9, 500);

		slogic_usb_control_read(sdi, SLOGIC16U3_CONTROL_IN_REQ_REG_READ,
					SLOGIC16U3_R32_AUX + 4, 0x0000,
					cmd_aux + 4,
					(*(uint16_t *)cmd_aux) >> 9, 500);
		sr_dbg("aux rd: %08x %08x.", ((uint32_t *)cmd_aux)[0], ((uint32_t *)(cmd_aux + 4))[0]);

		if (1024 != *(uint32_t *)(cmd_aux + 4)) {
			sr_dbg("Failed to configure vref.");
		} else {
			sr_dbg("Succeed to configure vref.");
		}
	}

	return slogic_usb_control_write(sdi,
					SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					SLOGIC16U3_R32_CTRL, 0x0000,
					ARRAY_AND_SIZE(cmd_run), 500);
}

static int slogic16U3_remote_stop(const struct sr_dev_inst *sdi)
{
	const uint8_t cmd_stop[] = { 0x00, 0x00, 0x00, 0x00 };
	return slogic_usb_control_write(sdi,
					SLOGIC16U3_CONTROL_OUT_REQ_REG_WRITE,
					SLOGIC16U3_R32_CTRL, 0x0000,
					ARRAY_AND_SIZE(cmd_stop), 500);
}
/* SLogic16U3 end */

static const struct slogic_model support_models[] = {
    {
        .name = "Sogic Combo 8",
        .pid = 0x0300,
        .ep_in = 0x01 | LIBUSB_ENDPOINT_IN,
        .max_bandwidth = SR_MHZ(320),
		.samplerate_table = samplerates_slogiccombo8,
		.samplerate_table_size = ARRAY_SIZE(samplerates_slogiccombo8),
		.samplechannel_table = samplechannels_slogiccombo8,
		.samplechannel_table_size = ARRAY_SIZE(samplechannels_slogiccombo8),
		.limit_samplerate_table = limit_samplerates_slogiccombo8,
        .operation =
            {
                .remote_reset = NULL,
                .remote_run = slogic_combo8_remote_run,
                .remote_stop = slogic_combo8_remote_stop,
            },
        .submit_raw_data = slogic_submit_raw_data,
    },
    {
        .name = "SLogic16U3",
        .pid = 0x3031,
        .ep_in = 0x02 | LIBUSB_ENDPOINT_IN,
        .max_bandwidth = SR_MHZ(3200),
		.samplerate_table = samplerates_slogic16u3,
		.samplerate_table_size = ARRAY_SIZE(samplerates_slogic16u3),
		.samplechannel_table = samplechannels_slogic16u3,
		.samplechannel_table_size = ARRAY_SIZE(samplechannels_slogic16u3),
		.limit_samplerate_table = limit_samplerates_slogic16u3,
        .operation =
            {
                .remote_reset = slogic16U3_remote_reset,
                .remote_run = slogic16U3_remote_run,
                .remote_stop = slogic16U3_remote_stop,
            },
        .submit_raw_data = slogic_submit_raw_data,
    },
    {
        .name = NULL,
        .pid = 0x0000,
    }};

static struct slogic_model *const support_models_ptr = &support_models[0];