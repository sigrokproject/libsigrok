/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

/* Max time in ms before we want to check on USB events */
/* TODO tune this properly */
#define TICK 1

#define NUM_TIMEBASE 10
#define NUM_VDIV     8

#define NUM_BUFFER_SIZES 2

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_BUFFERSIZE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FILTER | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"CH1", "CH2",
};

static const uint64_t buffersizes_32k[] = {
	(10 * 1024), (32 * 1024),
};
static const uint64_t buffersizes_512k[] = {
	(10 * 1024), (512 * 1024),
};
static const uint64_t buffersizes_14k[] = {
	(10 * 1024), (14 * 1024),
};

static const struct dso_profile dev_profiles[] = {
	{	0x04b4, 0x2090, 0x04b5, 0x2090,
		"Hantek", "DSO-2090",
		buffersizes_32k,
		"hantek-dso-2090.fw" },
	{	0x04b4, 0x2150, 0x04b5, 0x2150,
		"Hantek", "DSO-2150",
		buffersizes_32k,
		"hantek-dso-2150.fw" },
	{	0x04b4, 0x2250, 0x04b5, 0x2250,
		"Hantek", "DSO-2250",
		buffersizes_512k,
		"hantek-dso-2250.fw" },
	{	0x04b4, 0x5200, 0x04b5, 0x5200,
		"Hantek", "DSO-5200",
		buffersizes_14k,
		"hantek-dso-5200.fw" },
	{	0x04b4, 0x520a, 0x04b5, 0x520a,
		"Hantek", "DSO-5200A",
		buffersizes_512k,
		"hantek-dso-5200A.fw" },
	ALL_ZERO
};

static const uint64_t timebases[][2] = {
	/* microseconds */
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 40, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 400, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 4, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 40, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 400, 1000 },
};

static const uint64_t samplerates[] = {
	SR_KHZ(20),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(125),
	/* Fast mode not supported yet.
	SR_MHZ(200),
	SR_MHZ(250), */
};

static const uint64_t vdivs[][2] = {
	/* millivolts */
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
};

static const char *trigger_sources[] = {
	"CH1", "CH2", "EXT",
	/* TODO: forced */
};

static const char *trigger_slopes[] = {
	"r", "f",
};

static const char *coupling[] = {
	"AC", "DC", "GND",
};

static struct sr_dev_inst *dso_dev_new(const struct dso_profile *prof)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct dev_context *devc;
	unsigned int i;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INITIALIZING;
	sdi->vendor = g_strdup(prof->vendor);
	sdi->model = g_strdup(prof->model);

	/*
	 * Add only the real channels -- EXT isn't a source of data, only
	 * a trigger source internal to the device.
	 */
	for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel_names[i]);
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup(channel_names[i]);
		cg->channels = g_slist_append(cg->channels, ch);
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	devc->profile = prof;
	devc->dev_state = IDLE;
	devc->timebase = DEFAULT_TIMEBASE;
	devc->samplerate = DEFAULT_SAMPLERATE;
	devc->ch_enabled[0] = TRUE;
	devc->ch_enabled[1] = TRUE;
	devc->voltage[0] = DEFAULT_VOLTAGE;
	devc->voltage[1] = DEFAULT_VOLTAGE;
	devc->coupling[0] = DEFAULT_COUPLING;
	devc->coupling[1] = DEFAULT_COUPLING;
	devc->voffset_ch1 = DEFAULT_VERT_OFFSET;
	devc->voffset_ch2 = DEFAULT_VERT_OFFSET;
	devc->voffset_trigger = DEFAULT_VERT_TRIGGERPOS;
	devc->framesize = DEFAULT_FRAMESIZE;
	devc->triggerslope = SLOPE_POSITIVE;
	devc->triggersource = g_strdup(DEFAULT_TRIGGER_SOURCE);
	devc->capture_ratio = DEFAULT_CAPTURE_RATIO;
	sdi->priv = devc;

	return sdi;
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const GSList *l;
	int p;

	devc = sdi->priv;

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;
	devc->ch_enabled[0] = devc->ch_enabled[1] = FALSE;
	for (l = sdi->channels, p = 0; l; l = l->next, p++) {
		ch = l->data;
		if (p == 0)
			devc->ch_enabled[0] = ch->enabled;
		else
			devc->ch_enabled[1] = ch->enabled;
		if (ch->enabled)
			devc->enabled_channels = g_slist_append(devc->enabled_channels, ch);
	}

	return SR_OK;
}

static void clear_helper(struct dev_context *devc)
{
	g_free(devc->triggersource);
	g_slist_free(devc->enabled_channels);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	const struct dso_profile *prof;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int i, j;
	const char *conn;
	char connection_id[64];

	drvc = di->context;

	devices = 0;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all Hantek DSO devices and upload firmware to all of them. */
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
					&& usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		prof = NULL;
		for (j = 0; dev_profiles[j].orig_vid; j++) {
			if (des.idVendor == dev_profiles[j].orig_vid
				&& des.idProduct == dev_profiles[j].orig_pid) {
				/* Device matches the pre-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("Found a %s %s.", prof->vendor, prof->model);
				sdi = dso_dev_new(prof);
				sdi->connection_id = g_strdup(connection_id);
				devices = g_slist_append(devices, sdi);
				devc = sdi->priv;
				if (ezusb_upload_firmware(drvc->sr_ctx, devlist[i],
						USB_CONFIGURATION, prof->firmware) == SR_OK)
					/* Remember when the firmware on this device was updated */
					devc->fw_updated = g_get_monotonic_time();
				else
					sr_err("Firmware upload failed");
				/* Dummy USB address of 0xff will get overwritten later. */
				sdi->conn = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]), 0xff, NULL);
				break;
			} else if (des.idVendor == dev_profiles[j].fw_vid
				&& des.idProduct == dev_profiles[j].fw_pid) {
				/* Device matches the post-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("Found a %s %s.", prof->vendor, prof->model);
				sdi = dso_dev_new(prof);
				sdi->connection_id = g_strdup(connection_id);
				sdi->status = SR_ST_INACTIVE;
				devices = g_slist_append(devices, sdi);
				sdi->inst_type = SR_INST_USB;
				sdi->conn = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]),
						libusb_get_device_address(devlist[i]), NULL);
				break;
			}
		}
		if (!prof)
			/* not a supported VID/PID */
			continue;
	}
	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int64_t timediff_us, timediff_ms;
	int err;

	devc = sdi->priv;
	usb = sdi->conn;

	/*
	 * If the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * for the FX2 to renumerate.
	 */
	err = SR_ERR;
	if (devc->fw_updated > 0) {
		sr_info("Waiting for device to reset.");
		/* Takes >= 300ms for the FX2 to be gone from the USB bus. */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((err = dso_open(sdi)) == SR_OK)
				break;
			g_usleep(100 * 1000);
			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("Waited %" PRIi64 " ms.", timediff_ms);
		}
		sr_info("Device came back after %" PRIi64 " ms.", timediff_ms);
	} else {
		err = dso_open(sdi);
	}

	if (err != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	err = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_err("Unable to claim interface: %s.",
			libusb_error_name(err));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	dso_close(sdi);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	const char *s;
	const uint64_t *vdiv;
	int ch_idx;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(NUM_TIMEBASE);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(NUM_VDIV);
		break;
	}

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	if (!cg) {
		switch (key) {
		case SR_CONF_TRIGGER_LEVEL:
			*data = g_variant_new_double(devc->voffset_trigger);
			break;
		case SR_CONF_CONN:
			if (!sdi->conn)
				return SR_ERR_ARG;
			usb = sdi->conn;
			if (usb->address == 255)
				/* Device still needs to re-enumerate after firmware
				 * upload, so we don't know its (future) address. */
				return SR_ERR;
			*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
			break;
		case SR_CONF_TIMEBASE:
			*data = g_variant_new("(tt)", timebases[devc->timebase][0],
					timebases[devc->timebase][1]);
			break;
		case SR_CONF_SAMPLERATE:
			*data = g_variant_new_uint64(devc->samplerate);
			break;
		case SR_CONF_BUFFERSIZE:
			*data = g_variant_new_uint64(devc->framesize);
			break;
		case SR_CONF_TRIGGER_SOURCE:
			*data = g_variant_new_string(devc->triggersource);
			break;
		case SR_CONF_TRIGGER_SLOPE:
			s = (devc->triggerslope == SLOPE_POSITIVE) ? "r" : "f";
			*data = g_variant_new_string(s);
			break;
		case SR_CONF_CAPTURE_RATIO:
			*data = g_variant_new_uint64(devc->capture_ratio);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		if (sdi->channel_groups->data == cg)
			ch_idx = 0;
		else if (sdi->channel_groups->next->data == cg)
			ch_idx = 1;
		else
			return SR_ERR_ARG;
		switch (key) {
		case SR_CONF_FILTER:
			*data = g_variant_new_boolean(devc->filter[ch_idx]);
			break;
		case SR_CONF_VDIV:
			vdiv = vdivs[devc->voltage[ch_idx]];
			*data = g_variant_new("(tt)", vdiv[0], vdiv[1]);
			break;
		case SR_CONF_COUPLING:
			*data = g_variant_new_string(coupling[devc->coupling[ch_idx]]);
			break;
		}
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ch_idx, idx;
	float flt;

	devc = sdi->priv;
	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_FRAMES:
			devc->limit_frames = g_variant_get_uint64(data);
			break;
		case SR_CONF_TRIGGER_LEVEL:
			flt = g_variant_get_double(data);
			if (flt < 0.0 || flt > 1.0) {
				sr_err("Trigger level must be in [0.0,1.0].");
				return SR_ERR_ARG;
			}
			devc->voffset_trigger = flt;
			if (dso_set_voffsets(sdi) != SR_OK)
				return SR_ERR;
			break;
		case SR_CONF_TRIGGER_SLOPE:
			if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes))) < 0)
				return SR_ERR_ARG;
			devc->triggerslope = idx;
			break;
		case SR_CONF_CAPTURE_RATIO:
			devc->capture_ratio = g_variant_get_uint64(data);
			break;
		case SR_CONF_BUFFERSIZE:
			if ((idx = std_u64_idx(data, devc->profile->buffersizes, NUM_BUFFER_SIZES)) < 0)
				return SR_ERR_ARG;
			devc->framesize = devc->profile->buffersizes[idx];
			break;
		case SR_CONF_TIMEBASE:
			if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(timebases))) < 0)
				return SR_ERR_ARG;
			devc->timebase = idx;
			break;
		case SR_CONF_SAMPLERATE:
			if ((idx = std_u64_idx(data, ARRAY_AND_SIZE(samplerates))) < 0)
				return SR_ERR_ARG;
			devc->samplerate = samplerates[idx];
			if (dso_set_trigger_samplerate(sdi) != SR_OK)
				return SR_ERR;
			break;
		case SR_CONF_TRIGGER_SOURCE:
			if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_sources))) < 0)
				return SR_ERR_ARG;
			devc->triggersource = g_strdup(trigger_sources[idx]);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		if (sdi->channel_groups->data == cg)
			ch_idx = 0;
		else if (sdi->channel_groups->next->data == cg)
			ch_idx = 1;
		else
			return SR_ERR_ARG;
		switch (key) {
		case SR_CONF_FILTER:
			devc->filter[ch_idx] = g_variant_get_boolean(data);
			break;
		case SR_CONF_VDIV:
			if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(vdivs))) < 0)
				return SR_ERR_ARG;
			devc->voltage[ch_idx] = idx;
			break;
		case SR_CONF_COUPLING:
			if ((idx = std_str_idx(data, ARRAY_AND_SIZE(coupling))) < 0)
				return SR_ERR_ARG;
			devc->coupling[ch_idx] = idx;
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_BUFFERSIZE:
			if (!sdi)
				return SR_ERR_ARG;
			devc = sdi->priv;
			*data = std_gvar_array_u64(devc->profile->buffersizes, NUM_BUFFER_SIZES);
			break;
		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
			break;
		case SR_CONF_TIMEBASE:
			*data = std_gvar_tuple_array(ARRAY_AND_SIZE(timebases));
			break;
		case SR_CONF_TRIGGER_SOURCE:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_sources));
			break;
		case SR_CONF_TRIGGER_SLOPE:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_slopes));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_COUPLING:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(coupling));
			break;
		case SR_CONF_VDIV:
			*data = std_gvar_tuple_array(ARRAY_AND_SIZE(vdivs));
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static void send_chunk(struct sr_dev_inst *sdi, unsigned char *buf,
		int num_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc = sdi->priv;
	GSList *channels = devc->enabled_channels;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	/* TODO: support for 5xxx series 9-bit samples */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	analog.num_samples = num_samples;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = 0;
	/* TODO: Check malloc return value. */
	analog.data = g_try_malloc(num_samples * sizeof(float));

	for (int ch = 0; ch < NUM_CHANNELS; ch++) {
		if (!devc->ch_enabled[ch])
			continue;

		float range = ((float)vdivs[devc->voltage[ch]][0] / vdivs[devc->voltage[ch]][1]) * 8;
		float vdivlog = log10f(range / 255);
		int digits = -(int)vdivlog + (vdivlog < 0.0);
		analog.encoding->digits = digits;
		analog.spec->spec_digits = digits;
		analog.meaning->channels = g_slist_append(NULL, channels->data);

		for (int i = 0; i < num_samples; i++) {
			/*
			 * The device always sends data for both channels. If a channel
			 * is disabled, it contains a copy of the enabled channel's
			 * data. However, we only send the requested channels to
			 * the bus.
			 *
			 * Voltage values are encoded as a value 0-255 (0-512 on the
			 * DSO-5200*), where the value is a point in the range
			 * represented by the vdiv setting. There are 8 vertical divs,
			 * so e.g. 500mV/div represents 4V peak-to-peak where 0 = -2V
			 * and 255 = +2V.
			 */
			/* TODO: Support for DSO-5xxx series 9-bit samples. */
			((float *)analog.data)[i] = range / 255 * *(buf + i * 2 + 1 - ch) - range / 2;
		}
		sr_session_send(sdi, &packet);
		g_slist_free(analog.meaning->channels);

		channels = channels->next;
	}
	g_free(analog.data);
}

/*
 * Called by libusb (as triggered by handle_event()) when a transfer comes in.
 * Only channel data comes in asynchronously, and all transfers for this are
 * queued up beforehand, so this just needs to chuck the incoming data onto
 * the libsigrok session bus.
 */
static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_datafeed_packet packet;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int num_samples, pre;

	sdi = transfer->user_data;
	devc = sdi->priv;
	sr_spew("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);

	if (transfer->actual_length == 0)
		/* Nothing to send to the bus. */
		return;

	num_samples = transfer->actual_length / 2;

	sr_spew("Got %d-%d/%d samples in frame.", devc->samp_received + 1,
		devc->samp_received + num_samples, devc->framesize);

	/*
	 * The device always sends a full frame, but the beginning of the frame
	 * doesn't represent the trigger point. The offset at which the trigger
	 * happened came in with the capture state, so we need to start sending
	 * from there up the session bus. The samples in the frame buffer
	 * before that trigger point came after the end of the device's frame
	 * buffer was reached, and it wrapped around to overwrite up until the
	 * trigger point.
	 */
	if (devc->samp_received < devc->trigger_offset) {
		/* Trigger point not yet reached. */
		if (devc->samp_received + num_samples < devc->trigger_offset) {
			/* The entire chunk is before the trigger point. */
			memcpy(devc->framebuf + devc->samp_buffered * 2,
					transfer->buffer, num_samples * 2);
			devc->samp_buffered += num_samples;
		} else {
			/*
			 * This chunk hits or overruns the trigger point.
			 * Store the part before the trigger fired, and
			 * send the rest up to the session bus.
			 */
			pre = devc->trigger_offset - devc->samp_received;
			memcpy(devc->framebuf + devc->samp_buffered * 2,
					transfer->buffer, pre * 2);
			devc->samp_buffered += pre;

			/* The rest of this chunk starts with the trigger point. */
			sr_dbg("Reached trigger point, %d samples buffered.",
				devc->samp_buffered);

			/* Avoid the corner case where the chunk ended at
			 * exactly the trigger point. */
			if (num_samples > pre)
				send_chunk(sdi, transfer->buffer + pre * 2,
						num_samples - pre);
		}
	} else {
		/* Already past the trigger point, just send it all out. */
		send_chunk(sdi, transfer->buffer, num_samples);
	}

	devc->samp_received += num_samples;

	/* Everything in this transfer was either copied to the buffer or
	 * sent to the session bus. */
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);

	if (devc->samp_received >= devc->framesize) {
		/* That was the last chunk in this frame. Send the buffered
		 * pre-trigger samples out now, in one big chunk. */
		sr_dbg("End of frame, sending %d pre-trigger buffered samples.",
			devc->samp_buffered);
		send_chunk(sdi, devc->framebuf, devc->samp_buffered);
		g_free(devc->framebuf);
		devc->framebuf = NULL;

		/* Mark the end of this frame. */
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);

		if (devc->limit_frames && ++devc->num_frames >= devc->limit_frames) {
			/* Terminate session */
			devc->dev_state = STOPPING;
		} else {
			devc->dev_state = NEW_CAPTURE;
		}
	}
}

static int handle_event(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct sr_datafeed_packet packet;
	struct timeval tv;
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;
	int num_channels;
	uint32_t trigger_offset;
	uint8_t capturestate;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;
	if (devc->dev_state == STOPPING) {
		/* We've been told to wind up the acquisition. */
		sr_dbg("Stopping acquisition.");
		/*
		 * TODO: Doesn't really cancel pending transfers so they might
		 * come in after SR_DF_END is sent.
		 */
		usb_source_remove(sdi->session, drvc->sr_ctx);

		std_session_send_df_end(sdi);

		devc->dev_state = IDLE;

		return TRUE;
	}

	/* Always handle pending libusb events. */
	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	/* TODO: ugh */
	if (devc->dev_state == NEW_CAPTURE) {
		if (dso_capture_start(sdi) != SR_OK)
			return TRUE;
		if (dso_enable_trigger(sdi) != SR_OK)
			return TRUE;
//		if (dso_force_trigger(sdi) != SR_OK)
//			return TRUE;
		sr_dbg("Successfully requested next chunk.");
		devc->dev_state = CAPTURE;
		return TRUE;
	}
	if (devc->dev_state != CAPTURE)
		return TRUE;

	if ((dso_get_capturestate(sdi, &capturestate, &trigger_offset)) != SR_OK)
		return TRUE;

	sr_dbg("Capturestate %d.", capturestate);
	sr_dbg("Trigger offset 0x%.6x.", trigger_offset);
	switch (capturestate) {
	case CAPTURE_EMPTY:
		if (++devc->capture_empty_count >= MAX_CAPTURE_EMPTY) {
			devc->capture_empty_count = 0;
			if (dso_capture_start(sdi) != SR_OK)
				break;
			if (dso_enable_trigger(sdi) != SR_OK)
				break;
//			if (dso_force_trigger(sdi) != SR_OK)
//				break;
			sr_dbg("Successfully requested next chunk.");
		}
		break;
	case CAPTURE_FILLING:
		/* No data yet. */
		break;
	case CAPTURE_READY_8BIT:
	case CAPTURE_READY_2250:
		/* Remember where in the captured frame the trigger is. */
		devc->trigger_offset = trigger_offset;

		num_channels = (devc->ch_enabled[0] && devc->ch_enabled[1]) ? 2 : 1;
		devc->framebuf = g_malloc(devc->framesize * num_channels * 2);
		devc->samp_buffered = devc->samp_received = 0;

		/* Tell the scope to send us the first frame. */
		if (dso_get_channeldata(sdi, receive_transfer) != SR_OK)
			break;

		/*
		 * Don't hit the state machine again until we're done fetching
		 * the data we just told the scope to send.
		 */
		devc->dev_state = FETCH_DATA;

		/* Tell the frontend a new frame is on the way. */
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);
		break;
	case CAPTURE_READY_9BIT:
		/* TODO */
		sr_err("Not yet supported.");
		break;
	case CAPTURE_TIMEOUT:
		/* Doesn't matter, we'll try again next time. */
		break;
	default:
		sr_dbg("Unknown capture state: %d.", capturestate);
		break;
	}

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;

	devc = sdi->priv;

	if (configure_channels(sdi) != SR_OK) {
		sr_err("Failed to configure channels.");
		return SR_ERR;
	}

	if (dso_init(sdi) != SR_OK)
		return SR_ERR;

	if (dso_capture_start(sdi) != SR_OK)
		return SR_ERR;

	devc->dev_state = CAPTURE;
	usb_source_add(sdi->session, drvc->sr_ctx, TICK, handle_event, (void *)sdi);

	std_session_send_df_header(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->dev_state = STOPPING;
	devc->num_frames = 0;

	return SR_OK;
}

static struct sr_dev_driver hantek_dso_driver_info = {
	.name = "hantek-dso",
	.longname = "Hantek DSO",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hantek_dso_driver_info);
