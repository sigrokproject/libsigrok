/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 taorye <taorye@outlook.com>
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

/* Note: No spaces allowed because of sigrok-cli. */
static const char *logic_pattern_str[] = {
	"1ch",
	"2ch",
	"4ch",
	"8ch",
	// "16ch",
};

static const uint32_t scanopts[] = {
	SR_CONF_NUM_LOGIC_CHANNELS,
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_logic[] = {
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	// SR_KHZ(20),
	// SR_KHZ(25),
	// SR_KHZ(50),
	// SR_KHZ(100),
	// SR_KHZ(200),
	// SR_KHZ(250),
	// SR_KHZ(500),
	/* 160M = 2*2*2*2*2*5M */
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_MHZ(16),
	SR_MHZ(20),
	SR_MHZ(32),
	SR_MHZ(40),
	/* must less than 47MHZ */
	SR_MHZ(80),
	SR_MHZ(160),
};

static struct sr_dev_driver sipeed_slogic_analyzer_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	GSList *devices;

	(void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/* TODO: scan for devices, either based on a SR_CONF_CONN option
	 * or on a USB scan. */
	const char *conn = NULL;
	int num_logic_channels = 8;
	for (GSList *l = options; l; l = l->next) {
		struct sr_config *src = l->data;DBG_VAL(src->key);
		switch (src->key) {
		case SR_CONF_NUM_LOGIC_CHANNELS:
			num_logic_channels = g_variant_get_int32(src->data);
			break;
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	
	if(!conn) {
		conn = "359f.0300";
	}

	/* Find all slogic compatible devices. */
	GSList * conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	for(GSList *l = conn_devices; l; l = l->next) {
		struct sr_usb_dev_inst *usb = l->data;
		if (SR_OK != sr_usb_open(drvc->sr_ctx->libusb_ctx, usb))
			continue;

		unsigned char iManufacturer[64], iProduct[64], iSerialNumber[64];
		unsigned char connection_id[64];
		struct libusb_device_descriptor des;
		libusb_get_device_descriptor(libusb_get_device(usb->devhdl), &des);
		if (libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iManufacturer, iManufacturer, sizeof(iManufacturer)) < 0)
			continue;
		if (libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iProduct, iProduct, sizeof(iProduct)) < 0)
			continue;
		if (libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iSerialNumber, iSerialNumber, sizeof(iSerialNumber)) < 0)
			continue;
		if (usb_get_port_path(libusb_get_device(usb->devhdl),
				connection_id, sizeof(connection_id)) < 0)
			continue;
		sr_usb_close(usb);

		struct sr_dev_inst *sdi = sr_dev_inst_user_new(iManufacturer, iProduct, NULL);
		sdi->serial_num = g_strdup(iSerialNumber);
		sdi->connection_id = g_strdup(connection_id);

		sdi->inst_type = SR_INST_USB;
		sdi->status = SR_ST_INACTIVE;
		sdi->conn = usb;

		struct dev_context *devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		devc->profile = NULL;

		if (num_logic_channels > 0) {
			/* Logic channels, all in one channel group. */
			struct sr_channel_group *cg = sr_channel_group_new(sdi, "Logic", NULL);
			for (int i = 0; i < num_logic_channels; i++) {
				char channel_name[16];
				sprintf(channel_name, "D%d", i);
				struct sr_channel *ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name);
				cg->channels = g_slist_append(cg->channels, ch);
			}
		}

		devices = g_slist_append(devices, sdi);
	}
	// g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and open it. */
	int ret;
	struct sr_usb_dev_inst *usb= sdi->conn;
	struct dev_context *devc= sdi->priv;
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);DBG_VAL(ret);
	if (ret != SR_OK)
		return ret;

	ret = libusb_claim_interface(usb->devhdl, 0);DBG_VAL(ret);
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

	devc->logic_pattern = 3;  /* 2^3 = 8 default */
	devc->cur_samplerate = samplerates[0];
	devc->limit_samples = 0;
	devc->num_frames = 0;
	devc->limit_frames = 1;
	devc->capture_ratio = 0;
	
	return std_dummy_dev_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO: get handle from sdi->conn and close it. */
	int ret;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc= sdi->priv;

	ret = libusb_release_interface(usb->devhdl, 0);DBG_VAL(ret);
	if (ret != LIBUSB_SUCCESS) {
		sr_err("Unable to release Interface for %s.",
			       libusb_error_name(ret));
		return SR_ERR;
	}

	sr_usb_close(usb);

	return std_dummy_dev_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc= sdi->priv;
	struct sr_channel *ch;
	ret = SR_OK;DBG_VAL(key);
	switch (key) {
	/* TODO */
	case SR_CONF_CONN:
		if (usb->address == 0xff)
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
			return SR_ERR;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		/* Any channel in the group will do. */
		ch = cg->channels->data;
		if (ch->type == SR_CHANNEL_LOGIC) {
			int pattern = devc->logic_pattern;
			*data = g_variant_new_string(logic_pattern_str[pattern]);
		} else
			return SR_ERR_BUG;
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_tuple_double(devc->voltage_threshold[0], devc->voltage_threshold[1]);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	struct dev_context *devc= sdi->priv;
	int logic_pattern;
	ret = SR_OK;DBG_VAL(key);
	switch (key) {
	/* TODO */
	case SR_CONF_SAMPLERATE:
		if (std_u64_idx(data, ARRAY_AND_SIZE(samplerates)) < 0)
			return SR_ERR_ARG;
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		logic_pattern = std_str_idx(data, ARRAY_AND_SIZE(logic_pattern_str));
		if (logic_pattern < 0)
			return SR_ERR_ARG;
		if (((struct sr_channel *)cg->channels->data)->type == SR_CHANNEL_LOGIC) {
			sr_dbg("Setting logic pattern to %s",
					logic_pattern_str[logic_pattern]);
			devc->logic_pattern = logic_pattern;
			/* Might as well do this now, these are static. */
			size_t idx = 0;
			for (GSList *l = cg->channels; l; l = l->next, idx += 1) {
				struct sr_channel *ch = l->data;
				if (ch->type == SR_CHANNEL_LOGIC) {
					/* Might as well do this now, these are static. */
					sr_dev_channel_enable(ch, (idx >= (1 << (devc->logic_pattern))) ? FALSE : TRUE);
				} else
					return SR_ERR_BUG;
			}
		}
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &devc->voltage_threshold[0], &devc->voltage_threshold[1]);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	struct sr_channel *ch;
	ret = SR_OK;DBG_VAL(key);
	switch (key) {
	/* TODO */
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg)
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		ch = cg->channels->data;
		if (ch->type == SR_CHANNEL_LOGIC)
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_logic));
		else
			return SR_ERR_BUG;
		break;
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_PATTERN_MODE:
		if (!cg)
			return SR_ERR_NA;
		ch = cg->channels->data;
		if (ch->type == SR_CHANNEL_LOGIC)
			*data = g_variant_new_strv(ARRAY_AND_SIZE(logic_pattern_str));
		else
			return SR_ERR_BUG;
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static struct sr_dev_driver sipeed_slogic_analyzer_driver_info = {
	.name = "sipeed-slogic-analyzer",
	.longname = "Sipeed Slogic Analyzer",
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
