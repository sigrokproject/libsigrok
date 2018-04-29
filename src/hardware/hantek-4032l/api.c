/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Andreas Zschunke <andreas.zschunke@gmx.net>
 * Copyright (C) 2017 Andrej Valek <andy@skyrain.eu>
 * Copyright (C) 2017 Uwe Hermann <uwe@hermann-uwe.de>
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

#define USB_INTERFACE 0
#define NUM_CHANNELS 32

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_KHZ(1),
	SR_KHZ(2),
	SR_KHZ(4),
	SR_KHZ(8),
	SR_KHZ(16),
	SR_HZ(31250),
	SR_HZ(62500),
	SR_KHZ(125),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_KHZ(625),
	SR_HZ(781250),
	SR_MHZ(1),
	SR_KHZ(1250),
	SR_HZ(1562500),
	SR_MHZ(2),
	SR_KHZ(2500),
	SR_KHZ(3125),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_KHZ(6250),
	SR_MHZ(10),
	SR_KHZ(12500),
	SR_MHZ(20),
	SR_MHZ(25),
	SR_MHZ(40),
	SR_MHZ(50),
	SR_MHZ(80),
	SR_MHZ(100),
	SR_MHZ(160),
	SR_MHZ(200),
	SR_MHZ(320),
	SR_MHZ(400),
};

static const uint64_t samplerates_hw[] = {
	SR_MHZ(100),
	SR_MHZ(50),
	SR_MHZ(25),
	SR_KHZ(12500),
	SR_KHZ(6250),
	SR_KHZ(3125),
	SR_HZ(1562500),
	SR_HZ(781250),
	SR_MHZ(80),
	SR_MHZ(40),
	SR_MHZ(20),
	SR_MHZ(10),
	SR_MHZ(5),
	SR_KHZ(2500),
	SR_KHZ(1250),
	SR_KHZ(625),
	SR_MHZ(4),
	SR_MHZ(2),
	SR_MHZ(1),
	SR_KHZ(500),
	SR_KHZ(250),
	SR_KHZ(125),
	SR_HZ(62500),
	SR_HZ(31250),
	SR_KHZ(16),
	SR_KHZ(8),
	SR_KHZ(4),
	SR_KHZ(2),
	SR_KHZ(1),
	0,
	0,
	0,
	SR_MHZ(200),
	SR_MHZ(160),
	SR_MHZ(400),
	SR_MHZ(320),
};

SR_PRIV struct sr_dev_driver hantek_4032l_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	GSList *l, *devices, *conn_devices;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	const char *conn;
	int i;
	char connection_id[64];
	struct sr_channel_group *cg;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;

	devices = NULL;
	conn_devices = NULL;
	drvc->instances = NULL;
	conn = NULL;

	for (l = options; l; l = l->next) {
		struct sr_config *src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			struct sr_usb_dev_inst *usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i]) &&
				    usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != H4032L_USB_VENDOR ||
		    des.idProduct != H4032L_USB_PRODUCT)
			continue;

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->driver = &hantek_4032l_driver_info;
		sdi->vendor = g_strdup("Hantek");
		sdi->model = g_strdup("4032L");
		sdi->connection_id = g_strdup(connection_id);

		struct sr_channel_group *channel_groups[2];
		for (int j = 0; j < 2; j++) {
			cg = g_malloc0(sizeof(struct sr_channel_group));
			cg->name = g_strdup_printf("%c", 'A' + j);
			channel_groups[j] = cg;
			sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
		}

		/* Assemble channel list and add channel to channel groups. */
		for (int j = 0; j < NUM_CHANNELS; j++) {
			char channel_name[4];
			sprintf(channel_name, "%c%d", 'A' + (j & 1), j / 2);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE, channel_name);
			cg = channel_groups[j & 1];
			cg->channels = g_slist_append(cg->channels, ch);
		}

		struct dev_context *devc = g_malloc0(sizeof(struct dev_context));

		/* Initialize command packet. */
		devc->cmd_pkt.magic = H4032L_CMD_PKT_MAGIC;
		devc->cmd_pkt.pwm_a = h4032l_voltage2pwm(2.5);
		devc->cmd_pkt.pwm_b = h4032l_voltage2pwm(2.5);
		devc->cmd_pkt.sample_size = 16384;
		devc->cmd_pkt.pre_trigger_size = 1024;

		devc->status = H4032L_STATUS_IDLE;

		devc->capture_ratio = 5;

		devc->usb_transfer = libusb_alloc_transfer(0);

		sdi->priv = devc;
		devices = g_slist_append(devices, sdi);

		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(
			libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);
	}

	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	ret = h4032l_dev_open(sdi);
	if (ret != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret != 0) {
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

	/* Get FPGA version. */
	if ((ret = h4032l_get_fpga_version(sdi)) != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	sr_info("Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(samplerates_hw[devc->cmd_pkt.sample_rate]);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->cmd_pkt.sample_size);
		break;
	case SR_CONF_CONN:
		if (!sdi || !(usb = sdi->conn))
			return SR_ERR_ARG;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
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
	struct h4032l_cmd_pkt *cmd_pkt = &devc->cmd_pkt;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE: {
			uint64_t sample_rate = g_variant_get_uint64(data);
			uint8_t i = 0;
			while (i < ARRAY_SIZE(samplerates_hw) && samplerates_hw[i] != sample_rate)
				i++;

			if (i == ARRAY_SIZE(samplerates_hw) || sample_rate == 0) {
				sr_err("Invalid sample rate.");
				return SR_ERR_SAMPLERATE;
			}
			cmd_pkt->sample_rate = i;
			break;
		}
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES: {
			uint64_t number_samples = g_variant_get_uint64(data);
			number_samples += 511;
			number_samples &= 0xfffffe00;
			if (number_samples < 2048 ||
			    number_samples > 64 * 1024 * 1024) {
				sr_err("Invalid sample range 2k...64M: %"
				       PRIu64 ".", number_samples);
				return SR_ERR;
			}
			cmd_pkt->sample_size = number_samples;
			break;
		}
	case SR_CONF_VOLTAGE_THRESHOLD: {
			double d1, d2;
			g_variant_get(data, "(dd)", &d1, &d2);
			devc->cmd_pkt.pwm_a = h4032l_voltage2pwm(d1);
			devc->cmd_pkt.pwm_b = h4032l_voltage2pwm(d2);
			break;
		}
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_tuple_double(2.5, 2.5);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger = sr_session_trigger_get(sdi->session);
	struct h4032l_cmd_pkt *cmd_pkt = &devc->cmd_pkt;

	/* Initialize variables. */
	devc->acq_aborted = FALSE;

	/* Calculate packet ratio. */
	cmd_pkt->pre_trigger_size = (cmd_pkt->sample_size * devc->capture_ratio) / 100;

	cmd_pkt->trig_flags.enable_trigger1 = 0;
	cmd_pkt->trig_flags.enable_trigger2 = 0;
	cmd_pkt->trig_flags.trigger_and_logic = 0;

	if (trigger && trigger->stages) {
		GSList *stages = trigger->stages;
		struct sr_trigger_stage *stage1 = stages->data;
		if (stages->next) {
			sr_err("Only one trigger stage supported for now.");
			return SR_ERR;
		}
		cmd_pkt->trig_flags.enable_trigger1 = 1;
		cmd_pkt->trigger[0].flags.edge_type = H4032L_TRIGGER_EDGE_TYPE_DISABLED;
		cmd_pkt->trigger[0].flags.data_range_enabled = 0;
		cmd_pkt->trigger[0].flags.time_range_enabled = 0;
		cmd_pkt->trigger[0].flags.combined_enabled = 0;
		cmd_pkt->trigger[0].flags.data_range_type = H4032L_TRIGGER_DATA_RANGE_TYPE_MAX;
		cmd_pkt->trigger[0].data_range_mask = 0;
		cmd_pkt->trigger[0].data_range_max = 0;

		/* Initialize range mask values. */
		uint32_t range_mask = 0;
		uint32_t range_value = 0;

		GSList *channel = stage1->matches;
		while (channel) {
			struct sr_trigger_match *match = channel->data;

			switch (match->match) {
			case SR_TRIGGER_ZERO:
				range_mask |= (1 << match->channel->index);
				break;
			case SR_TRIGGER_ONE:
				range_mask |= (1 << match->channel->index);
				range_value |= (1 << match->channel->index);
				break;
			case SR_TRIGGER_RISING:
				if (cmd_pkt->trigger[0].flags.edge_type != H4032L_TRIGGER_EDGE_TYPE_DISABLED) {
					sr_err("Only one trigger signal with fall/rising/edge allowed.");
					return SR_ERR;
				}
				cmd_pkt->trigger[0].flags.edge_type = H4032L_TRIGGER_EDGE_TYPE_RISE;
				cmd_pkt->trigger[0].flags.edge_signal = match->channel->index;
				break;
			case SR_TRIGGER_FALLING:
				if (cmd_pkt->trigger[0].flags.edge_type != H4032L_TRIGGER_EDGE_TYPE_DISABLED) {
					sr_err("Only one trigger signal with fall/rising/edge allowed.");
					return SR_ERR;
				}
				cmd_pkt->trigger[0].flags.edge_type = H4032L_TRIGGER_EDGE_TYPE_FALL;
				cmd_pkt->trigger[0].flags.edge_signal = match->channel->index;
				break;
			case SR_TRIGGER_EDGE:
				if (cmd_pkt->trigger[0].flags.edge_type != H4032L_TRIGGER_EDGE_TYPE_DISABLED) {
					sr_err("Only one trigger signal with fall/rising/edge allowed.");
					return SR_ERR;
				}
				cmd_pkt->trigger[0].flags.edge_type = H4032L_TRIGGER_EDGE_TYPE_TOGGLE;
				cmd_pkt->trigger[0].flags.edge_signal = match->channel->index;
				break;
			default:
				sr_err("Unknown trigger value.");
				return SR_ERR;
			}

			channel = channel->next;
		}

		/* Compress range mask value and apply range settings. */
		if (range_mask) {
			cmd_pkt->trigger[0].flags.data_range_enabled = 1;
			cmd_pkt->trigger[0].data_range_mask |= (range_mask);

			uint32_t new_range_value = 0;
			uint32_t bit_mask = 1;
			while (range_mask) {
				if ((range_mask & 1) != 0) {
					new_range_value <<= 1;
					if ((range_value & 1) != 0)
						new_range_value |= bit_mask;
					bit_mask <<= 1;
				}
				range_mask >>= 1;
				range_value >>= 1;
			}
			cmd_pkt->trigger[0].data_range_max |= range_value;
		}
	}

	usb_source_add(sdi->session, drvc->sr_ctx, 1000,
		h4032l_receive_data, sdi->driver->context);

	/* Start capturing. */
	return h4032l_start(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->acq_aborted = TRUE;
	if (devc->usb_transfer)
		libusb_cancel_transfer(devc->usb_transfer);

	devc->status = H4032L_STATUS_IDLE;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver hantek_4032l_driver_info = {
	.name = "hantek-4032l",
	.longname = "Hantek 4032L",
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
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hantek_4032l_driver_info);
