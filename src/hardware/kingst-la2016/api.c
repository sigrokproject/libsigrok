/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Florian Schmidt <schmidt_florian@gmx.de>
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

/*
 * This driver implementation initially was derived from the
 * src/hardware/saleae-logic16/ source code.
 */

#include <config.h>

#include <libsigrok/libsigrok.h>
#include <string.h>

#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	/* TODO: SR_CONF_CONTINUOUS, */
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET | SR_CONF_GET | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LOGIC_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LOGIC_THRESHOLD_CUSTOM | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static const char *channel_names[] = {
	"0", "1", "2", "3", "4", "5", "6", "7",
	"8", "9", "10", "11", "12", "13", "14", "15",
};

static const uint64_t samplerates_la2016[] = {
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
};

static const uint64_t samplerates_la1016[] = {
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(50),
	SR_MHZ(100),
};

static const float logic_threshold_value[] = {
	1.58,
	2.5,
	1.165,
	1.5,
	1.25,
	0.9,
	0.75,
	0.60,
	0.45,
};

static const char *logic_threshold[] = {
	"TTL 5V",
	"CMOS 5V",
	"CMOS 3.3V",
	"CMOS 3.0V",
	"CMOS 2.5V",
	"CMOS 1.8V",
	"CMOS 1.5V",
	"CMOS 1.2V",
	"CMOS 0.9V",
	"USER",
};

#define LOGIC_THRESHOLD_IDX_USER	(ARRAY_SIZE(logic_threshold) - 1)

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	GSList *l;
	GSList *devices;
	GSList *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	unsigned int i, j;
	const char *conn;
	char connection_id[64];
	uint64_t fw_uploaded;
	unsigned int dev_addr;

	drvc = di->context;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all LA2016 devices, optionally upload firmware to them. */
	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i]) &&
				    usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l) {
				/*
				 * A connection parameter was specified and
				 * this device does not match the filter.
				 */
				continue;
			}
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID)
			continue;

		/* USB identification matches, a device was found. */
		sr_dbg("Found a device (USB identification).");
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->connection_id = g_strdup(connection_id);

		fw_uploaded = 0;
		dev_addr = libusb_get_device_address(devlist[i]);
		if (des.iProduct != LA2016_IPRODUCT_INDEX) {
			sr_info("Device at '%s' has no firmware loaded.", connection_id);

			if (la2016_upload_firmware(drvc->sr_ctx, devlist[i], des.idProduct) != SR_OK) {
				sr_err("MCU firmware upload failed.");
				g_free(sdi->connection_id);
				g_free(sdi);
				continue;
			}
			fw_uploaded = g_get_monotonic_time();
			/* Will re-enumerate. Mark as "unknown address yet". */
			dev_addr = 0xff;
		}

		sdi->vendor = g_strdup("Kingst");
		sdi->model = g_strdup("LA2016");

		for (j = 0; j < ARRAY_SIZE(channel_names); j++)
			sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE, channel_names[j]);

		devices = g_slist_append(devices, sdi);

		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		devc->fw_uploaded = fw_uploaded;
		devc->threshold_voltage_idx = 0;
		devc->threshold_voltage = logic_threshold_value[devc->threshold_voltage_idx];

		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;

		sdi->conn = sr_usb_dev_inst_new(
			libusb_get_bus_number(devlist[i]),
			dev_addr, NULL);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int la2016_dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct drv_context *drvc;
	int ret, i, device_count;
	char connection_id[64];

	di = sdi->driver;
	drvc = di->context;
	usb = sdi->conn;
	ret = SR_ERR;

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.", libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID)
			continue;
		if (des.iProduct != LA2016_IPRODUCT_INDEX)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) || (sdi->status == SR_ST_INACTIVE)) {
			/* Check physical USB bus/port address. */
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id)) {
				/* Not the device we looked up before. */
				continue;
			}
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff) {
				/*
				 * First encounter after firmware upload.
				 * Grab current address after enumeration.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
			}
		} else {
			sr_err("Failed to open device: %s.", libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
		if (ret == LIBUSB_ERROR_BUSY) {
			sr_err("Cannot claim USB interface. Another program or driver using it?");
			ret = SR_ERR;
			break;
		} else if (ret == LIBUSB_ERROR_NO_DEVICE) {
			sr_err("Device has been disconnected.");
			ret = SR_ERR;
			break;
		} else if (ret != 0) {
			sr_err("Cannot claim USB interface: %s.", libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		if ((ret = la2016_init_device(sdi)) != SR_OK) {
			sr_err("Cannot initialize device.");
			break;
		}

		sr_info("Opened device on %d.%d (logical) / %s (physical), interface %d.",
			usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);

		ret = SR_OK;

		break;
	}

	libusb_free_device_list(devlist, 1);

	if (ret != SR_OK) {
		if (usb->devhdl) {
			libusb_release_interface(usb->devhdl, USB_INTERFACE);
			libusb_close(usb->devhdl);
			usb->devhdl = NULL;
		}
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t reset_done, now, elapsed_ms;
	int ret;

	devc = sdi->priv;

	/*
	 * When the sigrok driver recently has uploaded MCU firmware,
	 * then wait for the FX2 to re-enumerate. Allow the USB device
	 * to vanish before it reappears. Timeouts are rough estimates
	 * after all, the imprecise time of the last check (potentially
	 * executes after the total check period) simplifies code paths
	 * with optional diagnostics. And increases the probability of
	 * successfully detecting "late/slow" devices.
	 */
	if (devc->fw_uploaded) {
		sr_info("Waiting for device to reset after firmware upload.");
		now = g_get_monotonic_time();
		reset_done = devc->fw_uploaded + RENUM_GONE_DELAY_MS * 1000;
		if (now < reset_done)
			g_usleep(reset_done - now);
		do {
			now = g_get_monotonic_time();
			elapsed_ms = (now - devc->fw_uploaded) / 1000;
			sr_spew("Waited %" PRIu64 "ms.", elapsed_ms);
			ret = la2016_dev_open(sdi);
			if (ret == SR_OK) {
				devc->fw_uploaded = 0;
				break;
			}
			g_usleep(RENUM_POLL_INTERVAL_MS * 1000);
		} while (elapsed_ms < RENUM_CHECK_PERIOD_MS);
		if (ret != SR_OK) {
			sr_err("Device failed to re-enumerate.");
			return SR_ERR;
		}
		sr_info("Device came back after %" PRIi64 "ms.", elapsed_ms);
	} else {
		ret = la2016_dev_open(sdi);
	}

	if (ret != SR_OK) {
		sr_err("Cannot open device.");
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	la2016_deinit_device(sdi);

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
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	double rounded;
	const char *label;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 0xff) {
			/*
			 * Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address.
			 */
			return SR_ERR;
		}
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		rounded = (int)(devc->threshold_voltage / 0.1) * 0.1;
		*data = std_gvar_tuple_double(rounded, rounded + 0.1);
		return SR_OK;
	case SR_CONF_LOGIC_THRESHOLD:
		label = logic_threshold[devc->threshold_voltage_idx];
		*data = g_variant_new_string(label);
		break;
	case SR_CONF_LOGIC_THRESHOLD_CUSTOM:
		*data = g_variant_new_double(devc->threshold_voltage);
		break;

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	double low, high;
	int idx;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &low, &high);
		devc->threshold_voltage = (low + high) / 2.0;
		devc->threshold_voltage_idx = LOGIC_THRESHOLD_IDX_USER;
		break;
	case SR_CONF_LOGIC_THRESHOLD: {
		idx = std_str_idx(data, ARRAY_AND_SIZE(logic_threshold));
		if (idx < 0)
			return SR_ERR_ARG;
		if (idx != LOGIC_THRESHOLD_IDX_USER) {
			devc->threshold_voltage = logic_threshold_value[idx];
		}
		devc->threshold_voltage_idx = idx;
		break;
	}
	case SR_CONF_LOGIC_THRESHOLD_CUSTOM:
		devc->threshold_voltage = g_variant_get_double(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		if (devc->max_samplerate == SR_MHZ(200)) {
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates_la2016));
		} else {
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates_la1016));
		}
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = std_gvar_tuple_u64(LA2016_NUM_SAMPLES_MIN,
			LA2016_NUM_SAMPLES_MAX);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_min_max_step_thresholds(
			LA2016_THR_VOLTAGE_MIN,
			LA2016_THR_VOLTAGE_MAX, 0.1);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_LOGIC_THRESHOLD:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(logic_threshold));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->cur_channels = 0;
	for (GSList *l = sdi->channels; l; l = l->next) {
		struct sr_channel *ch = (struct sr_channel*)l->data;
		if (ch->enabled == FALSE)
			continue;
		devc->cur_channels |= 1 << ch->index;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct dev_context *devc;
	int ret;

	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;

	if (configure_channels(sdi) != SR_OK) {
		sr_err("Cannot configure channels.");
		return SR_ERR;
	}

	devc->convbuffer_size = LA2016_CONVBUFFER_SIZE;
	devc->convbuffer = g_try_malloc(devc->convbuffer_size);
	if (!devc->convbuffer) {
		sr_err("Cannot allocate conversion buffer.");
		return SR_ERR_MALLOC;
	}

	ret = la2016_setup_acquisition(sdi);
	if (ret != SR_OK) {
		g_free(devc->convbuffer);
		devc->convbuffer = NULL;
		return ret;
	}

	devc->ctx = drvc->sr_ctx;

	ret = la2016_start_acquisition(sdi);
	if (ret != SR_OK) {
		la2016_abort_acquisition(sdi);
		return ret;
	}

	devc->completion_seen = FALSE;
	usb_source_add(sdi->session, drvc->sr_ctx, 50,
		la2016_receive_data, (void *)sdi);

	std_session_send_df_header(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	int ret;

	ret = la2016_abort_acquisition(sdi);

	return ret;
}

static struct sr_dev_driver kingst_la2016_driver_info = {
	.name = "kingst-la2016",
	.longname = "Kingst LA2016",
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
SR_REGISTER_DEV_DRIVER(kingst_la2016_driver_info);
