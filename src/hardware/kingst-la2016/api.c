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

/* mostly stolen from src/hardware/saleae-logic16/ */

#include <config.h>
#include <glib.h>
#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
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

#define MAX_NUM_LOGIC_THRESHOLD_ENTRIES ARRAY_SIZE(logic_threshold)

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
	int64_t fw_updated;
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

	/* Find all LA2016 devices and upload firmware to them. */
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
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
			}
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID)
			continue;

		/* Already has the firmware */
		sr_dbg("Found a LA2016 device.");
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->connection_id = g_strdup(connection_id);

		fw_updated = 0;
		dev_addr = libusb_get_device_address(devlist[i]);
		if (des.iProduct != 2) {
			sr_info("device at '%s' has no firmware loaded!", connection_id);

			if (la2016_upload_firmware(drvc->sr_ctx, devlist[i], des.idProduct) != SR_OK) {
				sr_err("uC firmware upload failed!");
				g_free(sdi->connection_id);
				g_free(sdi);
				continue;
			}
			fw_updated = g_get_monotonic_time();
			dev_addr = 0xff; /* to mark that we don't know address yet... ugly */
		}

		sdi->vendor = g_strdup("Kingst");
		sdi->model = g_strdup("LA2016");

		for (j = 0; j < ARRAY_SIZE(channel_names); j++)
			sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE, channel_names[j]);

		devices = g_slist_append(devices, sdi);

		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		devc->fw_updated = fw_updated;
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

		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID || des.iProduct != 2)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) || (sdi->status == SR_ST_INACTIVE)) {
			/*
			 * Check device by its physical USB bus/port address.
			 */
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.", libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
		if (ret == LIBUSB_ERROR_BUSY) {
			sr_err("Unable to claim USB interface. Another "
			       "program or driver has already claimed it.");
			ret = SR_ERR;
			break;
		} else if (ret == LIBUSB_ERROR_NO_DEVICE) {
			sr_err("Device has been disconnected.");
			ret = SR_ERR;
			break;
		} else if (ret != 0) {
			sr_err("Unable to claim interface: %s.", libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		if ((ret = la2016_init_device(sdi)) != SR_OK) {
			sr_err("Failed to init device.");
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
	int64_t timediff_us, timediff_ms;
	uint64_t reset_done;
	uint64_t now;
	int ret;

	devc = sdi->priv;

	/*
	 * If the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * milliseconds for the FX2 to renumerate.
	 */
	ret = SR_ERR;
	if (devc->fw_updated > 0) {
		sr_info("Waiting for device to reset after firmware upload.");
		/* Takes >= 2000ms for the uC to be gone from the USB bus. */
		reset_done = devc->fw_updated + 18 * (uint64_t)1e5; /* 1.8 seconds */
		now = g_get_monotonic_time();
		if (reset_done > now)
			g_usleep(reset_done - now);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			g_usleep(200 * 1000);

			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;

			if ((ret = la2016_dev_open(sdi)) == SR_OK)
				break;
			sr_spew("Waited %" PRIi64 "ms.", timediff_ms);
		}
		if (ret != SR_OK) {
			sr_err("Device failed to re-enumerate.");
			return SR_ERR;
		}
		sr_info("Device came back after %" PRIi64 "ms.", timediff_ms);
	} else {
		ret = la2016_dev_open(sdi);
	}

	if (ret != SR_OK) {
		sr_err("Unable to open device.");
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

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 255) {
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
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
		*data = g_variant_new_string(logic_threshold[devc->threshold_voltage_idx]);
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
		devc->threshold_voltage_idx = MAX_NUM_LOGIC_THRESHOLD_ENTRIES - 1; /* USER */
		break;
	case SR_CONF_LOGIC_THRESHOLD: {
		if ((idx = std_str_idx(data, logic_threshold, MAX_NUM_LOGIC_THRESHOLD_ENTRIES)) < 0)
			return SR_ERR_ARG;
		if (idx == MAX_NUM_LOGIC_THRESHOLD_ENTRIES - 1) {
			/* user threshold */
		} else {
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
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		if (devc->max_samplerate == SR_MHZ(200)) {
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates_la2016));
		}
		else {
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates_la1016));
		}
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = std_gvar_tuple_u64(LA2016_NUM_SAMPLES_MIN, LA2016_NUM_SAMPLES_MAX);
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
		*data = g_variant_new_strv(logic_threshold, MAX_NUM_LOGIC_THRESHOLD_ENTRIES);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static void send_chunk(struct sr_dev_inst *sdi,
	const uint8_t *packets, unsigned int num_tfers)
{
	struct dev_context *devc;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet sr_packet;
	unsigned int max_samples, n_samples, total_samples, free_n_samples;
	unsigned int i, j, k;
	int do_signal_trigger;
	uint16_t *wp;
	const uint8_t *rp;
	uint16_t state;
	uint8_t repetitions;

	devc = sdi->priv;

	logic.unitsize = 2;
	logic.data = devc->convbuffer;

	sr_packet.type = SR_DF_LOGIC;
	sr_packet.payload = &logic;

	max_samples = devc->convbuffer_size / 2;
	n_samples = 0;
	wp = (uint16_t *)devc->convbuffer;
	total_samples = 0;
	do_signal_trigger = 0;

	if (devc->had_triggers_configured && devc->reading_behind_trigger == 0 && devc->info.n_rep_packets_before_trigger == 0) {
		std_session_send_df_trigger(sdi);
		devc->reading_behind_trigger = 1;
	}

	rp = packets;
	for (i = 0; i < num_tfers; i++) {
		for (k = 0; k < NUM_PACKETS_IN_CHUNK; k++) {
			free_n_samples = max_samples - n_samples;
			if (free_n_samples < 256 || do_signal_trigger) {
				logic.length = n_samples * 2;
				sr_session_send(sdi, &sr_packet);
				n_samples = 0;
				wp = (uint16_t *)devc->convbuffer;
				if (do_signal_trigger) {
					std_session_send_df_trigger(sdi);
					do_signal_trigger = 0;
				}
			}

			state = read_u16le_inc(&rp);
			repetitions = read_u8_inc(&rp);
			for (j = 0; j < repetitions; j++)
				*wp++ = state;

			n_samples += repetitions;
			total_samples += repetitions;
			devc->total_samples += repetitions;
			if (!devc->reading_behind_trigger) {
				devc->n_reps_until_trigger--;
				if (devc->n_reps_until_trigger == 0) {
					devc->reading_behind_trigger = 1;
					do_signal_trigger = 1;
					sr_dbg("  here is trigger position after %" PRIu64 " samples, %.6fms",
					       devc->total_samples,
					       (double)devc->total_samples / devc->cur_samplerate * 1e3);
				}
			}
		}
		(void)read_u8_inc(&rp); /* Skip sequence number. */
	}
	if (n_samples) {
		logic.length = n_samples * 2;
		sr_session_send(sdi, &sr_packet);
		if (do_signal_trigger) {
			std_session_send_df_trigger(sdi);
		}
	}
	sr_dbg("send_chunk done after %d samples", total_samples);
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret;

	sdi = transfer->user_data;
	devc = sdi->priv;
	usb = sdi->conn;

	sr_dbg("receive_transfer(): status %s received %d bytes.",
	       libusb_error_name(transfer->status), transfer->actual_length);

	if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT) {
		sr_err("bulk transfer timeout!");
		devc->transfer_finished = 1;
	}
	send_chunk(sdi, transfer->buffer, transfer->actual_length / TRANSFER_PACKET_LENGTH);

	devc->n_bytes_to_read -= transfer->actual_length;
	if (devc->n_bytes_to_read) {
		uint32_t to_read = devc->n_bytes_to_read;
		/* determine read size for the next usb transfer */
		if (to_read >= LA2016_USB_BUFSZ)
			to_read = LA2016_USB_BUFSZ;
		else /* last transfer, make read size some multiple of LA2016_EP6_PKTSZ */
			to_read = (to_read + (LA2016_EP6_PKTSZ-1)) & ~(LA2016_EP6_PKTSZ-1);
		libusb_fill_bulk_transfer(
			transfer, usb->devhdl,
			0x86, transfer->buffer, to_read,
			receive_transfer, (void *)sdi, DEFAULT_TIMEOUT_MS);

		if ((ret = libusb_submit_transfer(transfer)) == 0)
			return;
		sr_err("Failed to submit further transfer: %s.", libusb_error_name(ret));
	}

	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
	devc->transfer_finished = 1;
}

static int handle_event(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct timeval tv;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	drvc = sdi->driver->context;

	if (devc->have_trigger == 0) {
		if (la2016_has_triggered(sdi) == 0) {
			/* not yet ready for download */
			return TRUE;
		}
		devc->have_trigger = 1;
		devc->transfer_finished = 0;
		devc->reading_behind_trigger = 0;
		devc->total_samples = 0;
		/* we can start retrieving data! */
		if (la2016_start_retrieval(sdi, receive_transfer) != SR_OK) {
			sr_err("failed to start retrieval!");
			return FALSE;
		}
		sr_dbg("retrieval is started...");
		std_session_send_df_frame_begin(sdi);

		return TRUE;
	}

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	if (devc->transfer_finished) {
		sr_dbg("transfer is finished!");
		std_session_send_df_frame_end(sdi);

		usb_source_remove(sdi->session, drvc->sr_ctx);
		std_session_send_df_end(sdi);

		la2016_stop_acquisition(sdi);

		g_free(devc->convbuffer);
		devc->convbuffer = NULL;

		devc->transfer = NULL;

		sr_dbg("transfer is now finished");
	}

	return TRUE;
}

static void abort_acquisition(struct dev_context *devc)
{
	if (devc->transfer)
		libusb_cancel_transfer(devc->transfer);
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->cur_channels = 0;
	devc->num_channels = 0;

	for (GSList *l = sdi->channels; l; l = l->next) {
		struct sr_channel *ch = (struct sr_channel*)l->data;
		if (ch->enabled == FALSE)
			continue;
		devc->cur_channels |= 1 << ch->index;
		devc->num_channels++;
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
		sr_err("Failed to configure channels.");
		return SR_ERR;
	}

	devc->convbuffer_size = 4 * 1024 * 1024;
	if (!(devc->convbuffer = g_try_malloc(devc->convbuffer_size))) {
		sr_err("Conversion buffer malloc failed.");
		return SR_ERR_MALLOC;
	}

	if ((ret = la2016_setup_acquisition(sdi)) != SR_OK) {
		g_free(devc->convbuffer);
		devc->convbuffer = NULL;
		return ret;
	}

	devc->ctx = drvc->sr_ctx;

	if ((ret = la2016_start_acquisition(sdi)) != SR_OK) {
		abort_acquisition(devc);
		return ret;
	}

	devc->have_trigger = 0;
	usb_source_add(sdi->session, drvc->sr_ctx, 50, handle_event, (void *)sdi);

	std_session_send_df_header(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	int ret;

	ret = la2016_abort_acquisition(sdi);
	abort_acquisition(sdi->priv);

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
