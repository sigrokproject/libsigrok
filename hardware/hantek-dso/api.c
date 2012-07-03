/*
 * This file is part of the sigrok project.
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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "config.h"
#include "dso.h"


/* Max time in ms before we want to check on USB events */
/* TODO tune this properly */
#define TICK    1

static const int hwcaps[] = {
	SR_HWCAP_OSCILLOSCOPE,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	SR_HWCAP_TIMEBASE,
	SR_HWCAP_BUFFERSIZE,
	SR_HWCAP_TRIGGER_SOURCE,
	SR_HWCAP_TRIGGER_SLOPE,
	SR_HWCAP_HORIZ_TRIGGERPOS,
	SR_HWCAP_FILTER,
	SR_HWCAP_VDIV,
	SR_HWCAP_COUPLING,
	0,
};

static const char *probe_names[] = {
	"CH1",
	"CH2",
	NULL,
};

static const struct dso_profile dev_profiles[] = {
	{	0x04b4, 0x2090, 0x04b5, 0x2090,
		"Hantek", "DSO-2090",
		FIRMWARE_DIR "/hantek-dso-2xxx.fw" },
	{	0x04b4, 0x2150, 0x04b5, 0x2150,
		"Hantek", "DSO-2150",
		FIRMWARE_DIR "/hantek-dso-2xxx.fw" },
	{	0x04b4, 0x2250, 0x04b5, 0x2250,
		"Hantek", "DSO-2250",
		FIRMWARE_DIR "/hantek-dso-2xxx.fw" },
	{	0x04b4, 0x5200, 0x04b5, 0x5200,
		"Hantek", "DSO-5200",
		FIRMWARE_DIR "/hantek-dso-5xxx.fw" },
	{	0x04b4, 0x520a, 0x04b5, 0x520a,
		"Hantek", "DSO-5200A",
		FIRMWARE_DIR "/hantek-dso-5xxx.fw" },
	{ 0, 0, 0, 0, 0, 0, 0 },
};

static const uint64_t buffersizes[] = {
	10240,
	32768,
	/* TODO: 65535 */
	0,
};

static const struct sr_rational timebases[] = {
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
	{ 0, 0},
};

static const struct sr_rational vdivs[] = {
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
	{ 0, 0 },
};

static const char *trigger_sources[] = {
	"CH1",
	"CH2",
	"EXT",
	/* TODO: forced */
	NULL,
};

static const char *filter_targets[] = {
	"CH1",
	"CH2",
	/* TODO: "TRIGGER", */
	NULL,
};

static const char *coupling[] = {
	"AC",
	"DC",
	"GND",
	NULL,
};

SR_PRIV libusb_context *usb_context = NULL;
SR_PRIV GSList *dev_insts = NULL;

static struct sr_dev_inst *dso_dev_new(int index, const struct dso_profile *prof)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;

	sdi = sr_dev_inst_new(index, SR_ST_INITIALIZING,
		prof->vendor, prof->model, NULL);
	if (!sdi)
		return NULL;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("hantek-dso: ctx malloc failed");
		return NULL;
	}
	ctx->profile = prof;
	ctx->dev_state = IDLE;
	ctx->timebase = DEFAULT_TIMEBASE;
	ctx->ch1_enabled = TRUE;
	ctx->ch2_enabled = TRUE;
	ctx->voltage_ch1 = DEFAULT_VOLTAGE;
	ctx->voltage_ch2 = DEFAULT_VOLTAGE;
	ctx->coupling_ch1 = DEFAULT_COUPLING;
	ctx->coupling_ch2 = DEFAULT_COUPLING;
	ctx->voffset_ch1 = DEFAULT_VERT_OFFSET;
	ctx->voffset_ch2 = DEFAULT_VERT_OFFSET;
	ctx->voffset_trigger = DEFAULT_VERT_TRIGGERPOS;
	ctx->framesize = DEFAULT_FRAMESIZE;
	ctx->triggerslope = SLOPE_POSITIVE;
	ctx->triggersource = g_strdup(DEFAULT_TRIGGER_SOURCE);
	ctx->triggerposition = DEFAULT_HORIZ_TRIGGERPOS;
	sdi->priv = ctx;
	dev_insts = g_slist_append(dev_insts, sdi);

	return sdi;
}

static int configure_probes(struct context *ctx, const GSList *probes)
{
	const struct sr_probe *probe;
	const GSList *l;

	ctx->ch1_enabled = ctx->ch2_enabled = FALSE;
	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->index == 1)
			ctx->ch1_enabled = probe->enabled;
		else if (probe->index == 2)
			ctx->ch2_enabled = probe->enabled;
	}

	return SR_OK;
}

static int hw_init(void)
{
	struct sr_dev_inst *sdi;
	struct libusb_device_descriptor des;
	const struct dso_profile *prof;
	struct context *ctx;
	libusb_device **devlist;
	int err, devcnt, i, j;

	if (libusb_init(&usb_context) != 0) {
		sr_err("hantek-dso: Failed to initialize USB.");
		return 0;
	}

	/* Find all Hantek DSO devices and upload firmware to all of them. */
	devcnt = 0;
	libusb_get_device_list(usb_context, &devlist);
	for (i = 0; devlist[i]; i++) {
		if ((err = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("hantek-dso: failed to get device descriptor: %d", err);
			continue;
		}

		prof = NULL;
		for (j = 0; dev_profiles[j].orig_vid; j++) {
			if (des.idVendor == dev_profiles[j].orig_vid
				&& des.idProduct == dev_profiles[j].orig_pid) {
				/* Device matches the pre-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("hantek-dso: Found a %s %s.", prof->vendor, prof->model);
				sdi = dso_dev_new(devcnt, prof);
				ctx = sdi->priv;
				if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION,
						prof->firmware) == SR_OK)
					/* Remember when the firmware on this device was updated */
					ctx->fw_updated = g_get_monotonic_time();
				else
					sr_err("hantek-dso: firmware upload failed for "
					       "device %d", devcnt);
				/* Dummy USB address of 0xff will get overwritten later. */
				ctx->usb = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]), 0xff, NULL);
				devcnt++;
				break;
			} else if (des.idVendor == dev_profiles[j].fw_vid
				&& des.idProduct == dev_profiles[j].fw_pid) {
				/* Device matches the post-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("hantek-dso: Found a %s %s.", prof->vendor, prof->model);
				sdi = dso_dev_new(devcnt, prof);
				sdi->status = SR_ST_INACTIVE;
				ctx = sdi->priv;
				ctx->usb = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]),
						libusb_get_device_address(devlist[i]), NULL);
				devcnt++;
				break;
			}
		}
		if (!prof)
			/* not a supported VID/PID */
			continue;
	}
	libusb_free_device_list(devlist, 1);

	return devcnt;
}

static int hw_dev_open(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int64_t timediff_us, timediff_ms;
	int err;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR_ARG;
	ctx = sdi->priv;

	/*
	 * if the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * for the FX2 to renumerate
	 */
	err = SR_ERR;
	if (ctx->fw_updated > 0) {
		sr_info("hantek-dso: waiting for device to reset");
		/* takes at least 300ms for the FX2 to be gone from the USB bus */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((err = dso_open(dev_index)) == SR_OK)
				break;
			g_usleep(100 * 1000);
			timediff_us = g_get_monotonic_time() - ctx->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("hantek-dso: waited %" PRIi64 " ms", timediff_ms);
		}
		sr_info("hantek-dso: device came back after %d ms", timediff_ms);
	} else {
		err = dso_open(dev_index);
	}

	if (err != SR_OK) {
		sr_err("hantek-dso: unable to open device");
		return SR_ERR;
	}

	err = libusb_claim_interface(ctx->usb->devhdl, USB_INTERFACE);
	if (err != 0) {
		sr_err("hantek-dso: Unable to claim interface: %d", err);
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR_ARG;

	dso_close(sdi);

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Properly close and free all devices. */
	for (l = dev_insts; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("hantek-dso: %s: sdi was NULL, continuing", __func__);
			continue;
		}
		if (!(ctx = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("hantek-dso: %s: sdi->priv was NULL, continuing", __func__);
			continue;
		}
		dso_close(sdi);
		sr_usb_dev_inst_free(ctx->usb);
		g_free(ctx->triggersource);

		sr_dev_inst_free(sdi);
	}

	g_slist_free(dev_insts);
	dev_insts = NULL;

	if (usb_context)
		libusb_exit(usb_context);
	usb_context = NULL;

	return SR_OK;
}

static const void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	const void *info;
	uint64_t tmp;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return NULL;

	info = NULL;
	switch (dev_info_id) {
	case SR_DI_INST:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES:
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		info = probe_names;
		break;
	case SR_DI_BUFFERSIZES:
		info = buffersizes;
		break;
	case SR_DI_TIMEBASES:
		info = timebases;
		break;
	case SR_DI_TRIGGER_SOURCES:
		info = trigger_sources;
		break;
	case SR_DI_FILTERS:
		info = filter_targets;
		break;
	case SR_DI_VDIVS:
		info = vdivs;
		break;
	case SR_DI_COUPLING:
		info = coupling;
		break;
	/* TODO remove this */
	case SR_DI_CUR_SAMPLERATE:
		info = &tmp;
		break;
	}

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static const int *hw_hwcap_get_all(void)
{
	return hwcaps;
}

static int hw_dev_config_set(int dev_index, int hwcap, const void *value)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	struct sr_rational tmp_rat;
	float tmp_float;
	uint64_t tmp_u64;
	int ret, i;
	char **targets;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	ret = SR_OK;
	ctx = sdi->priv;
	switch (hwcap) {
	case SR_HWCAP_LIMIT_FRAMES:
		ctx->limit_frames = *(const uint64_t *)value;
		break;
	case SR_HWCAP_PROBECONFIG:
		ret = configure_probes(ctx, (const GSList *)value);
		break;
	case SR_HWCAP_TRIGGER_SLOPE:
		tmp_u64 = *(const int *)value;
		if (tmp_u64 != SLOPE_NEGATIVE && tmp_u64 != SLOPE_POSITIVE)
			ret = SR_ERR_ARG;
		ctx->triggerslope = tmp_u64;
		break;
	case SR_HWCAP_HORIZ_TRIGGERPOS:
		tmp_float = *(const float *)value;
		if (tmp_float < 0.0 || tmp_float > 1.0) {
			sr_err("hantek-dso: trigger position should be between 0.0 and 1.0");
			ret = SR_ERR_ARG;
		} else
			ctx->triggerposition = tmp_float;
		break;
	case SR_HWCAP_BUFFERSIZE:
		tmp_u64 = *(const int *)value;
		for (i = 0; buffersizes[i]; i++) {
			if (buffersizes[i] == tmp_u64) {
				ctx->framesize = tmp_u64;
				break;
			}
		}
		if (buffersizes[i] == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_TIMEBASE:
		tmp_rat = *(const struct sr_rational *)value;
		for (i = 0; timebases[i].p && timebases[i].q; i++) {
			if (timebases[i].p == tmp_rat.p
					&& timebases[i].q == tmp_rat.q) {
				ctx->timebase = i;
				break;
			}
		}
		if (timebases[i].p == 0 && timebases[i].q == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_TRIGGER_SOURCE:
		for (i = 0; trigger_sources[i]; i++) {
			if (!strcmp(value, trigger_sources[i])) {
				ctx->triggersource = g_strdup(value);
				break;
			}
		}
		if (trigger_sources[i] == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_FILTER:
		ctx->filter_ch1 = ctx->filter_ch2 = ctx->filter_trigger = 0;
		targets = g_strsplit(value, ",", 0);
		for (i = 0; targets[i]; i++) {
			if (targets[i] == '\0')
				/* Empty filter string can be used to clear them all. */
				;
			else if (!strcmp(targets[i], "CH1"))
				ctx->filter_ch1 = TRUE;
			else if (!strcmp(targets[i], "CH2"))
				ctx->filter_ch2 = TRUE;
			else if (!strcmp(targets[i], "TRIGGER"))
				ctx->filter_trigger = TRUE;
			else {
				sr_err("invalid filter target %s", targets[i]);
				ret = SR_ERR_ARG;
			}
		}
		g_strfreev(targets);
		break;
	case SR_HWCAP_VDIV:
		/* TODO not supporting vdiv per channel yet */
		tmp_rat = *(const struct sr_rational *)value;
		for (i = 0; vdivs[i].p && vdivs[i].q; i++) {
			if (vdivs[i].p == tmp_rat.p
					&& vdivs[i].q == tmp_rat.q) {
				ctx->voltage_ch1 = i;
				ctx->voltage_ch2 = i;
				break;
			}
		}
		if (vdivs[i].p == 0 && vdivs[i].q == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_HWCAP_COUPLING:
		/* TODO not supporting coupling per channel yet */
		for (i = 0; coupling[i]; i++) {
			if (!strcmp(value, coupling[i])) {
				ctx->coupling_ch1 = i;
				ctx->coupling_ch2 = i;
				break;
			}
		}
		if (coupling[i] == 0)
			ret = SR_ERR_ARG;
		break;
	default:
		ret = SR_ERR_ARG;
	}

	return ret;
}

static void send_chunk(struct context *ctx, unsigned char *buf,
		int num_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	float ch1, ch2, range;
	int num_probes, data_offset, i;

	num_probes = (ctx->ch1_enabled && ctx->ch2_enabled) ? 2 : 1;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	/* TODO: support for 5xxx series 9-bit samples */
	analog.num_samples = num_samples;
	analog.mq = SR_MQ_VOLTAGE;
	analog.unit = SR_UNIT_VOLT;
	analog.data = g_try_malloc(analog.num_samples * sizeof(float) * num_probes);
	data_offset = 0;
	for (i = 0; i < analog.num_samples; i++) {
		/* The device always sends data for both channels. If a channel
		 * is disabled, it contains a copy of the enabled channel's
		 * data. However, we only send the requested channels to the bus.
		 *
		 * Voltage values are encoded as a value 0-255 (0-512 on the 5200*),
		 * where the value is a point in the range represented by the vdiv
		 * setting. There are 8 vertical divs, so e.g. 500mV/div represents
		 * 4V peak-to-peak where 0 = -2V and 255 = +2V.
		 */
		/* TODO: support for 5xxx series 9-bit samples */
		if (ctx->ch1_enabled) {
			range = ((float)vdivs[ctx->voltage_ch1].p / vdivs[ctx->voltage_ch1].q) * 8;
			ch1 = range / 255 * *(buf + i * 2 + 1);
			/* Value is centered around 0V. */
			ch1 -= range / 2;
			analog.data[data_offset++] = ch1;
		}
		if (ctx->ch2_enabled) {
			range = ((float)vdivs[ctx->voltage_ch2].p / vdivs[ctx->voltage_ch2].q) * 8;
			ch2 = range / 255 * *(buf + i * 2);
			ch2 -= range / 2;
			analog.data[data_offset++] = ch2;
		}
	}
	sr_session_send(ctx->cb_data, &packet);

}

/* Called by libusb (as triggered by handle_event()) when a transfer comes in.
 * Only channel data comes in asynchronously, and all transfers for this are
 * queued up beforehand, so this just needs so chuck the incoming data onto
 * the libsigrok session bus.
 */
static void receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_datafeed_packet packet;
	struct context *ctx;
	int num_samples, pre;

	ctx = transfer->user_data;
	sr_dbg("hantek-dso: receive_transfer(): status %d received %d bytes",
			transfer->status, transfer->actual_length);

	if (transfer->actual_length == 0)
		/* Nothing to send to the bus. */
		return;

	num_samples = transfer->actual_length / 2;

	sr_dbg("hantek-dso: got %d-%d/%d samples in frame", ctx->samp_received + 1,
			ctx->samp_received + num_samples, ctx->framesize);

	/* The device always sends a full frame, but the beginning of the frame
	 * doesn't represent the trigger point. The offset at which the trigger
	 * happened came in with the capture state, so we need to start sending
	 * from there up the session bus. The samples in the frame buffer before
	 * that trigger point came after the end of the device's frame buffer was
	 * reached, and it wrapped around to overwrite up until the trigger point.
	 */
	if (ctx->samp_received < ctx->trigger_offset) {
		/* Trigger point not yet reached. */
		if (ctx->samp_received + num_samples < ctx->trigger_offset) {
			/* The entire chunk is before the trigger point. */
			memcpy(ctx->framebuf + ctx->samp_buffered * 2,
					transfer->buffer, num_samples * 2);
			ctx->samp_buffered += num_samples;
		} else {
			/* This chunk hits or overruns the trigger point.
			 * Store the part before the trigger fired, and
			 * send the rest up to the session bus. */
			pre = ctx->trigger_offset - ctx->samp_received;
			memcpy(ctx->framebuf + ctx->samp_buffered * 2,
					transfer->buffer, pre * 2);
			ctx->samp_buffered += pre;

			/* The rest of this chunk starts with the trigger point. */
			sr_dbg("hantek-dso: reached trigger point, %d samples buffered",
					ctx->samp_buffered);

			/* Avoid the corner case where the chunk ended at
			 * exactly the trigger point. */
			if (num_samples > pre)
				send_chunk(ctx, transfer->buffer + pre * 2,
						num_samples - pre);
		}
	} else {
		/* Already past the trigger point, just send it all out. */
		send_chunk(ctx, transfer->buffer,
				num_samples);
	}

	ctx->samp_received += num_samples;

	/* Everything in this transfer was either copied to the buffer or
	 * sent to the session bus. */
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);

	if (ctx->samp_received >= ctx->framesize) {
		/* That was the last chunk in this frame. Send the buffered
		 * pre-trigger samples out now, in one big chunk. */
		sr_dbg("hantek-dso: end of frame, sending %d pre-trigger buffered samples",
				ctx->samp_buffered);
		send_chunk(ctx, ctx->framebuf, ctx->samp_buffered);

		/* Mark the end of this frame. */
		packet.type = SR_DF_FRAME_END;
		sr_session_send(ctx->cb_data, &packet);

		if (ctx->limit_frames && ++ctx->num_frames == ctx->limit_frames) {
			/* Terminate session */
			/* TODO: don't leave pending USB transfers hanging */
			packet.type = SR_DF_END;
			sr_session_send(ctx->cb_data, &packet);
		} else {
			ctx->dev_state = NEW_CAPTURE;
		}
	}

}

static int handle_event(int fd, int revents, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct timeval tv;
	struct context *ctx;
	int num_probes;
	uint32_t trigger_offset;
	uint8_t capturestate;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;

	/* Always handle pending libusb events. */
	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(usb_context, &tv);

	ctx = cb_data;
	/* TODO: ugh */
	if (ctx->dev_state == NEW_CAPTURE) {
		if (dso_capture_start(ctx) != SR_OK)
			return TRUE;
		if (dso_enable_trigger(ctx) != SR_OK)
			return TRUE;
//		if (dso_force_trigger(ctx) != SR_OK)
//			return TRUE;
		sr_dbg("hantek-dso: successfully requested next chunk");
		ctx->dev_state = CAPTURE;
		return TRUE;
	}
	if (ctx->dev_state != CAPTURE)
		return TRUE;

	if ((dso_get_capturestate(ctx, &capturestate, &trigger_offset)) != SR_OK)
		return TRUE;

	sr_dbg("hantek-dso: capturestate %d", capturestate);
	sr_dbg("hantek-dso: trigger offset 0x%.6x", trigger_offset);
	switch (capturestate) {
	case CAPTURE_EMPTY:
		if (++ctx->capture_empty_count >= MAX_CAPTURE_EMPTY) {
			ctx->capture_empty_count = 0;
			if (dso_capture_start(ctx) != SR_OK)
				break;
			if (dso_enable_trigger(ctx) != SR_OK)
				break;
//			if (dso_force_trigger(ctx) != SR_OK)
//				break;
			sr_dbg("hantek-dso: successfully requested next chunk");
		}
		break;
	case CAPTURE_FILLING:
		/* no data yet */
		break;
	case CAPTURE_READY_8BIT:
		/* Remember where in the captured frame the trigger is. */
		ctx->trigger_offset = trigger_offset;

		num_probes = (ctx->ch1_enabled && ctx->ch2_enabled) ? 2 : 1;
		ctx->framebuf = g_try_malloc(ctx->framesize * num_probes * 2);
		ctx->samp_buffered = ctx->samp_received = 0;

		/* Tell the scope to send us the first frame. */
		if (dso_get_channeldata(ctx, receive_transfer) != SR_OK)
			break;

		/* Don't hit the state machine again until we're done fetching
		 * the data we just told the scope to send.
		 */
		ctx->dev_state = FETCH_DATA;

		/* Tell the frontend a new frame is on the way. */
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(cb_data, &packet);
		break;
	case CAPTURE_READY_9BIT:
		/* TODO */
		sr_err("not yet supported");
		break;
	case CAPTURE_TIMEOUT:
		/* Doesn't matter, we'll try again next time. */
		break;
	default:
		sr_dbg("unknown capture state");
	}

	return TRUE;
}

static int hw_dev_acquisition_start(int dev_index, void *cb_data)
{
	const struct libusb_pollfd **lupfd;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int i;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	ctx = sdi->priv;
	ctx->cb_data = cb_data;

	if (dso_init(ctx) != SR_OK)
		return SR_ERR;

	if (dso_capture_start(ctx) != SR_OK)
		return SR_ERR;

	ctx->dev_state = CAPTURE;
	lupfd = libusb_get_pollfds(usb_context);
	for (i = 0; lupfd[i]; i++)
		sr_source_add(lupfd[i]->fd, lupfd[i]->events, TICK, handle_event,
			      ctx);
	free(lupfd);

	/* Send header packet to the session bus. */
	packet.type = SR_DF_HEADER;
	packet.payload = (unsigned char *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = NUM_PROBES;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

/* TODO: doesn't really cancel pending transfers so they might come in after
 * SR_DF_END is sent.
 */
static int hw_dev_acquisition_stop(int dev_index, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	ctx = sdi->priv;
	ctx->dev_state = IDLE;

	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver hantek_dso_driver_info = {
	.name = "hantek-dso",
	.longname = "Hantek DSO",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
