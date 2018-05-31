/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Christer Ekholm <christerekholm@gmail.com>
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
#include "protocol.h"

/* Max time in ms before we want to check on USB events */
#define TICK 200

#define RANGE(ch) (((float)vdivs[devc->voltage[ch]][0] / vdivs[devc->voltage[ch]][1]) * VDIV_MULTIPLIER)

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_VDIV | SR_CONF_GET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"CH1", "CH2",
};

static const char *dc_coupling[] = {
	"DC",
};

static const char *acdc_coupling[] = {
	"AC", "DC",
};

static const struct hantek_6xxx_profile dev_profiles[] = {
	{
		0x04b4, 0x6022, 0x1d50, 0x608e, 0x0001,
		"Hantek", "6022BE", "fx2lafw-hantek-6022be.fw",
		ARRAY_AND_SIZE(dc_coupling), FALSE,
	},
	{
		0x8102, 0x8102, 0x1d50, 0x608e, 0x0002,
		"Sainsmart", "DDS120", "fx2lafw-sainsmart-dds120.fw",
		ARRAY_AND_SIZE(acdc_coupling), TRUE,
	},
	{
		0x04b4, 0x602a, 0x1d50, 0x608e, 0x0003,
		"Hantek", "6022BL", "fx2lafw-hantek-6022bl.fw",
		ARRAY_AND_SIZE(dc_coupling), FALSE,
	},
	ALL_ZERO
};

static const uint64_t samplerates[] = {
	SAMPLERATE_VALUES
};

static const uint64_t vdivs[][2] = {
	VDIV_VALUES
};

static int read_channel(const struct sr_dev_inst *sdi, uint32_t amount);

static struct sr_dev_inst *hantek_6xxx_dev_new(const struct hantek_6xxx_profile *prof)
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

	for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel_names[i]);
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup(channel_names[i]);
		cg->channels = g_slist_append(cg->channels, ch);
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	devc = g_malloc0(sizeof(struct dev_context));

	for (i = 0; i < NUM_CHANNELS; i++) {
		devc->ch_enabled[i] = TRUE;
		devc->voltage[i] = DEFAULT_VOLTAGE;
		devc->coupling[i] = DEFAULT_COUPLING;
	}
	devc->coupling_vals = prof->coupling_vals;
	devc->coupling_tab_size = prof->coupling_tab_size;
	devc->has_coupling = prof->has_coupling;

	devc->sample_buf = NULL;
	devc->sample_buf_write = 0;
	devc->sample_buf_size = 0;

	devc->profile = prof;
	devc->dev_state = IDLE;
	devc->samplerate = DEFAULT_SAMPLERATE;

	sdi->priv = devc;

	return sdi;
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const GSList *l;
	int p;
	struct sr_channel *ch;
	devc = sdi->priv;

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;
	memset(devc->ch_enabled, 0, sizeof(devc->ch_enabled));

	for (l = sdi->channels, p = 0; l; l = l->next, p++) {
		ch = l->data;
		if (p < NUM_CHANNELS) {
			devc->ch_enabled[p] = ch->enabled;
			devc->enabled_channels = g_slist_append(devc->enabled_channels, ch);
		}
	}

	return SR_OK;
}

static void clear_helper(struct dev_context *devc)
{
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
	const struct hantek_6xxx_profile *prof;
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

	/* Find all Hantek 60xx devices and upload firmware to all of them. */
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
				sdi = hantek_6xxx_dev_new(prof);
				sdi->connection_id = g_strdup(connection_id);
				devices = g_slist_append(devices, sdi);
				devc = sdi->priv;
				if (ezusb_upload_firmware(drvc->sr_ctx, devlist[i],
						USB_CONFIGURATION, prof->firmware) == SR_OK)
					/* Remember when the firmware on this device was updated. */
					devc->fw_updated = g_get_monotonic_time();
				else
					sr_err("Firmware upload failed.");
				/* Dummy USB address of 0xff will get overwritten later. */
				sdi->conn = sr_usb_dev_inst_new(
						libusb_get_bus_number(devlist[i]), 0xff, NULL);
				break;
			} else if (des.idVendor == dev_profiles[j].fw_vid
				&& des.idProduct == dev_profiles[j].fw_pid
				&& des.bcdDevice == dev_profiles[j].fw_prod_ver) {
				/* Device matches the post-firmware profile. */
				prof = &dev_profiles[j];
				sr_dbg("Found a %s %s.", prof->vendor, prof->model);
				sdi = hantek_6xxx_dev_new(prof);
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
			/* Not a supported VID/PID. */
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
			if ((err = hantek_6xxx_open(sdi)) == SR_OK)
				break;
			g_usleep(100 * 1000);
			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("Waited %" PRIi64 " ms.", timediff_ms);
		}
		if (timediff_ms < MAX_RENUM_DELAY_MS)
			sr_info("Device came back after %"PRIu64" ms.", timediff_ms);
	} else {
		err = hantek_6xxx_open(sdi);
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
	hantek_6xxx_close(sdi);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	const uint64_t *vdiv;
	int ch_idx;

	switch (key) {
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(ARRAY_SIZE(vdivs));
		break;
	}

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	if (!cg) {
		switch (key) {
		case SR_CONF_SAMPLERATE:
			*data = g_variant_new_uint64(devc->samplerate);
			break;
		case SR_CONF_LIMIT_MSEC:
			*data = g_variant_new_uint64(devc->limit_msec);
			break;
		case SR_CONF_LIMIT_SAMPLES:
			*data = g_variant_new_uint64(devc->limit_samples);
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
		case SR_CONF_VDIV:
			vdiv = vdivs[devc->voltage[ch_idx]];
			*data = g_variant_new("(tt)", vdiv[0], vdiv[1]);
			break;
		case SR_CONF_COUPLING:
			*data = g_variant_new_string((devc->coupling[ch_idx] \
					== COUPLING_DC) ? "DC" : "AC");
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

	devc = sdi->priv;
	if (!cg) {
		switch (key) {
		case SR_CONF_SAMPLERATE:
			devc->samplerate = g_variant_get_uint64(data);
			hantek_6xxx_update_samplerate(sdi);
			break;
		case SR_CONF_LIMIT_MSEC:
			devc->limit_msec = g_variant_get_uint64(data);
			break;
		case SR_CONF_LIMIT_SAMPLES:
			devc->limit_samples = g_variant_get_uint64(data);
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
		case SR_CONF_VDIV:
			if ((idx = std_u64_tuple_idx(data, ARRAY_AND_SIZE(vdivs))) < 0)
				return SR_ERR_ARG;
			devc->voltage[ch_idx] = idx;
			hantek_6xxx_update_vdiv(sdi);
			break;
		case SR_CONF_COUPLING:
			if ((idx = std_str_idx(data, devc->coupling_vals,
						devc->coupling_tab_size)) < 0)
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

	devc = (sdi) ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
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
			if (!devc)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(devc->coupling_vals, devc->coupling_tab_size);
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

/* Minimise data amount for limit_samples and limit_msec limits. */
static uint32_t data_amount(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint32_t data_left, data_left_2, i;
	int32_t time_left;

	if (devc->limit_msec) {
		time_left = devc->limit_msec - (g_get_monotonic_time() - devc->aq_started) / 1000;
		data_left = devc->samplerate * MAX(time_left, 0) * NUM_CHANNELS / 1000;
	} else if (devc->limit_samples) {
		data_left = (devc->limit_samples - devc->samp_received) * NUM_CHANNELS;
	} else {
		data_left = devc->samplerate * NUM_CHANNELS;
	}

	/* Round up to nearest power of two. */
	for (i = MIN_PACKET_SIZE; i < data_left; i *= 2)
		;
	data_left_2 = i;

	sr_spew("data_amount: %u (rounded to power of 2: %u)", data_left, data_left_2);

	return data_left_2;
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

	const float ch_bit[] = { RANGE(0) / 255, RANGE(1) / 255 };
	const float ch_center[] = { RANGE(0) / 2, RANGE(1) / 2 };

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;

	analog.num_samples = num_samples;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = 0;

	analog.data = g_try_malloc(num_samples * sizeof(float));
	if (!analog.data) {
		sr_err("Analog data buffer malloc failed.");
		devc->dev_state = STOPPING;
		return;
	}

	for (int ch = 0; ch < NUM_CHANNELS; ch++) {
		if (!devc->ch_enabled[ch])
			continue;

		float vdivlog = log10f(ch_bit[ch]);
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
			 * Voltage values are encoded as a value 0-255, where the
			 * value is a point in the range represented by the vdiv
			 * setting. There are 10 vertical divs, so e.g. 500mV/div
			 * represents 5V peak-to-peak where 0 = -2.5V and 255 = +2.5V.
			 */
			((float *)analog.data)[i] = ch_bit[ch] * *(buf + i * 2 + ch) - ch_center[ch];
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
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	sdi = transfer->user_data;
	devc = sdi->priv;

	if (devc->dev_state == FLUSH) {
		g_free(transfer->buffer);
		libusb_free_transfer(transfer);
		devc->dev_state = CAPTURE;
		devc->aq_started = g_get_monotonic_time();
		read_channel(sdi, data_amount(sdi));
		return;
	}

	if (devc->dev_state != CAPTURE)
		return;

	sr_spew("receive_transfer(): calculated samplerate == %" PRIu64 "ks/s",
		(uint64_t)(transfer->actual_length * 1000 /
		(g_get_monotonic_time() - devc->read_start_ts + 1) /
		NUM_CHANNELS));

	sr_spew("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);

	if (transfer->actual_length == 0)
		/* Nothing to send to the bus. */
		return;

	unsigned samples_received = transfer->actual_length / NUM_CHANNELS;
	send_chunk(sdi, transfer->buffer, samples_received);
	devc->samp_received += samples_received;

	g_free(transfer->buffer);
	libusb_free_transfer(transfer);

	if (devc->limit_samples && devc->samp_received >= devc->limit_samples) {
		sr_info("Requested number of samples reached, stopping. %"
			PRIu64 " <= %" PRIu64, devc->limit_samples,
			devc->samp_received);
		sr_dev_acquisition_stop(sdi);
	} else if (devc->limit_msec && (g_get_monotonic_time() -
			devc->aq_started) / 1000 >= devc->limit_msec) {
		sr_info("Requested time limit reached, stopping. %d <= %d",
			(uint32_t)devc->limit_msec,
			(uint32_t)(g_get_monotonic_time() - devc->aq_started) / 1000);
		sr_dev_acquisition_stop(sdi);
	} else {
		read_channel(sdi, data_amount(sdi));
	}
}

static int read_channel(const struct sr_dev_inst *sdi, uint32_t amount)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	amount = MIN(amount, MAX_PACKET_SIZE);
	ret = hantek_6xxx_get_channeldata(sdi, receive_transfer, amount);
	devc->read_start_ts = g_get_monotonic_time();
	devc->read_data_amount = amount;

	return ret;
}

static int handle_event(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct timeval tv;
	struct sr_dev_driver *di;
	struct dev_context *devc;
	struct drv_context *drvc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;

	/* Always handle pending libusb events. */
	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	if (devc->dev_state == STOPPING) {
		/* We've been told to wind up the acquisition. */
		sr_dbg("Stopping acquisition.");

		hantek_6xxx_stop_data_collecting(sdi);
		/*
		 * TODO: Doesn't really cancel pending transfers so they might
		 * come in after SR_DF_END is sent.
		 */
		usb_source_remove(sdi->session, drvc->sr_ctx);

		std_session_send_df_end(sdi);

		devc->dev_state = IDLE;

		return TRUE;
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

	if (hantek_6xxx_init(sdi) != SR_OK)
		return SR_ERR;

	std_session_send_df_header(sdi);

	devc->samp_received = 0;
	devc->dev_state = FLUSH;

	usb_source_add(sdi->session, drvc->sr_ctx, TICK,
		       handle_event, (void *)sdi);

	hantek_6xxx_start_data_collecting(sdi);

	read_channel(sdi, FLUSH_PACKET_SIZE);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->dev_state = STOPPING;

	g_free(devc->sample_buf);
	devc->sample_buf = NULL;

	return SR_OK;
}

static struct sr_dev_driver hantek_6xxx_driver_info = {
	.name = "hantek-6xxx",
	.longname = "Hantek 6xxx",
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
SR_REGISTER_DEV_DRIVER(hantek_6xxx_driver_info);
