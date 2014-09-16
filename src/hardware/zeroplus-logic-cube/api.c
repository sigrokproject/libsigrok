/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include "protocol.h"

#define VENDOR_NAME			"ZEROPLUS"
#define USB_INTERFACE			0
#define USB_CONFIGURATION		1
#define NUM_TRIGGER_STAGES		4
#define PACKET_SIZE			2048	/* ?? */

//#define ZP_EXPERIMENTAL

struct zp_model {
	uint16_t vid;
	uint16_t pid;
	char *model_name;
	unsigned int channels;
	unsigned int sample_depth;	/* In Ksamples/channel */
	unsigned int max_sampling_freq;
};

/*
 * Note -- 16032, 16064 and 16128 *usually* -- but not always -- have the
 * same 128K sample depth.
 */
static const struct zp_model zeroplus_models[] = {
	{0x0c12, 0x7002, "LAP-16128U",    16, 128,  200},
	{0x0c12, 0x7009, "LAP-C(16064)",  16, 64,   100},
	{0x0c12, 0x700a, "LAP-C(16128)",  16, 128,  200},
	{0x0c12, 0x700b, "LAP-C(32128)",  32, 128,  200},
	{0x0c12, 0x700c, "LAP-C(321000)", 32, 1024, 200},
	{0x0c12, 0x700d, "LAP-C(322000)", 32, 2048, 200},
	{0x0c12, 0x700e, "LAP-C(16032)",  16, 32,   100},
	{0x0c12, 0x7016, "LAP-C(162000)", 16, 2048, 200},
	{ 0, 0, 0, 0, 0, 0 }
};

static const uint32_t devopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_TRIGGER_MATCH,
	SR_CONF_CAPTURE_RATIO,
	SR_CONF_VOLTAGE_THRESHOLD,
	SR_CONF_LIMIT_SAMPLES,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
};

/*
 * ZEROPLUS LAP-C (16032) numbers the 16 channels A0-A7 and B0-B7.
 * We currently ignore other untested/unsupported devices here.
 */
static const char *channel_names[] = {
	"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
	"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7",
	"C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7",
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	NULL,
};

SR_PRIV struct sr_dev_driver zeroplus_logic_cube_driver_info;
static struct sr_dev_driver *di = &zeroplus_logic_cube_driver_info;

/*
 * The hardware supports more samplerates than these, but these are the
 * options hardcoded into the vendor's Windows GUI.
 */

static const uint64_t samplerates_100[] = {
	SR_HZ(100),
	SR_HZ(500),
	SR_KHZ(1),
	SR_KHZ(5),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(400),
	SR_KHZ(800),
	SR_MHZ(1),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(80),
	SR_MHZ(100),
};

const uint64_t samplerates_200[] = {
	SR_HZ(100),
	SR_HZ(500),
	SR_KHZ(1),
	SR_KHZ(5),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(400),
	SR_KHZ(800),
	SR_MHZ(1),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(80),
	SR_MHZ(100),
	SR_MHZ(150),
	SR_MHZ(200),
};

static int dev_close(struct sr_dev_inst *sdi);

SR_PRIV int zp_set_samplerate(struct dev_context *devc, uint64_t samplerate)
{
	int i;

	for (i = 0; ARRAY_SIZE(samplerates_200); i++)
		if (samplerate == samplerates_200[i])
			break;

	if (i == ARRAY_SIZE(samplerates_200) || samplerate > devc->max_samplerate) {
		sr_err("Unsupported samplerate: %" PRIu64 "Hz.", samplerate);
		return SR_ERR_ARG;
	}

	sr_info("Setting samplerate to %" PRIu64 "Hz.", samplerate);

	if (samplerate >= SR_MHZ(1))
		analyzer_set_freq(samplerate / SR_MHZ(1), FREQ_SCALE_MHZ);
	else if (samplerate >= SR_KHZ(1))
		analyzer_set_freq(samplerate / SR_KHZ(1), FREQ_SCALE_KHZ);
	else
		analyzer_set_freq(samplerate, FREQ_SCALE_HZ);

	devc->cur_samplerate = samplerate;

	return SR_OK;
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct drv_context *drvc;
	struct dev_context *devc;
	const struct zp_model *prof;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	GSList *devices;
	int ret, devcnt, i, j;

	(void)options;

	drvc = di->priv;

	devices = NULL;

	/* Find all ZEROPLUS analyzers and add them to device list. */
	devcnt = 0;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist); /* TODO: Errors. */

	for (i = 0; devlist[i]; i++) {
		ret = libusb_get_device_descriptor(devlist[i], &des);
		if (ret != 0) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}

		prof = NULL;
		for (j = 0; j < zeroplus_models[j].vid; j++) {
			if (des.idVendor == zeroplus_models[j].vid &&
				des.idProduct == zeroplus_models[j].pid) {
				prof = &zeroplus_models[j];
			}
		}
		/* Skip if the device was not found. */
		if (!prof)
			continue;
		sr_info("Found ZEROPLUS %s.", prof->model_name);

		/* Register the device with libsigrok. */
		if (!(sdi = sr_dev_inst_new(devcnt, SR_ST_INACTIVE,
				VENDOR_NAME, prof->model_name, NULL))) {
			sr_err("%s: sr_dev_inst_new failed", __func__);
			return NULL;
		}
		sdi->driver = di;

		/* Allocate memory for our private driver context. */
		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}

		sdi->priv = devc;
		devc->prof = prof;
		devc->num_channels = prof->channels;
#ifdef ZP_EXPERIMENTAL
		devc->max_sample_depth = 128 * 1024;
		devc->max_samplerate = 200;
#else
		devc->max_sample_depth = prof->sample_depth * 1024;
		devc->max_samplerate = prof->max_sampling_freq;
#endif
		devc->max_samplerate *= SR_MHZ(1);
		devc->memory_size = MEMORY_SIZE_8K;
		// memset(devc->trigger_buffer, 0, NUM_TRIGGER_STAGES);

		/* Fill in channellist according to this device's profile. */
		for (j = 0; j < devc->num_channels; j++) {
			if (!(ch = sr_channel_new(j, SR_CHANNEL_LOGIC, TRUE,
					channel_names[j])))
				return NULL;
			sdi->channels = g_slist_append(sdi->channels, ch);
		}

		devices = g_slist_append(devices, sdi);
		drvc->instances = g_slist_append(drvc->instances, sdi);
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(
			libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);
		devcnt++;

	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	libusb_device **devlist, *dev;
	struct libusb_device_descriptor des;
	int device_count, ret, i;

	drvc = di->priv;
	usb = sdi->conn;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx,
					      &devlist);
	if (device_count < 0) {
		sr_err("Failed to retrieve device list.");
		return SR_ERR;
	}

	dev = NULL;
	for (i = 0; i < device_count; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}
		if (libusb_get_bus_number(devlist[i]) == usb->bus
		    && libusb_get_device_address(devlist[i]) == usb->address) {
			dev = devlist[i];
			break;
		}
	}
	if (!dev) {
		sr_err("Device on bus %d address %d disappeared!",
		       usb->bus, usb->address);
		return SR_ERR;
	}

	if (!(ret = libusb_open(dev, &(usb->devhdl)))) {
		sdi->status = SR_ST_ACTIVE;
		sr_info("Opened device %d on %d.%d interface %d.",
			sdi->index, usb->bus, usb->address, USB_INTERFACE);
	} else {
		sr_err("Failed to open device: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	ret = libusb_set_configuration(usb->devhdl, USB_CONFIGURATION);
	if (ret < 0) {
		sr_err("Unable to set USB configuration %d: %s.",
		       USB_CONFIGURATION, libusb_error_name(ret));
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret != 0) {
		sr_err("Unable to claim interface: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Set default configuration after power on. */
	if (analyzer_read_status(usb->devhdl) == 0)
		analyzer_configure(usb->devhdl);

	analyzer_reset(usb->devhdl);
	analyzer_initialize(usb->devhdl);

	//analyzer_set_memory_size(MEMORY_SIZE_512K);
	// analyzer_set_freq(g_freq, g_freq_scale);
	analyzer_set_trigger_count(1);
	// analyzer_set_ramsize_trigger_address((((100 - g_pre_trigger)
	// * get_memory_size(g_memory_size)) / 100) >> 2);

#if 0
	if (g_double_mode == 1)
		analyzer_set_compression(COMPRESSION_DOUBLE);
	else if (g_compression == 1)
		analyzer_set_compression(COMPRESSION_ENABLE);
	else
#endif
	analyzer_set_compression(COMPRESSION_NONE);

	if (devc->cur_samplerate == 0) {
		/* Samplerate hasn't been set. Default to 1MHz. */
		analyzer_set_freq(1, FREQ_SCALE_MHZ);
		devc->cur_samplerate = SR_MHZ(1);
	}

	if (devc->cur_threshold == 0)
		set_voltage_threshold(devc, 1.5);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR;

	sr_info("Closing device %d on %d.%d interface %d.", sdi->index,
		usb->bus, usb->address, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_reset_device(usb->devhdl);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	return std_dev_clear(di, NULL);
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = g_variant_new_uint64(devc->cur_samplerate);
			sr_spew("Returning samplerate: %" PRIu64 "Hz.",
				devc->cur_samplerate);
		} else
			return SR_ERR_ARG;
		break;
	case SR_CONF_CAPTURE_RATIO:
		if (sdi) {
			devc = sdi->priv;
			*data = g_variant_new_uint64(devc->capture_ratio);
		} else
			return SR_ERR_ARG;
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		if (sdi) {
			GVariant *range[2];
			devc = sdi->priv;
			range[0] = g_variant_new_double(devc->cur_threshold);
			range[1] = g_variant_new_double(devc->cur_threshold);
			*data = g_variant_new_tuple(range, 2);
		} else
			return SR_ERR_ARG;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	gdouble low, high;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	switch (key) {
	case SR_CONF_SAMPLERATE:
		return zp_set_samplerate(devc, g_variant_get_uint64(data));
	case SR_CONF_LIMIT_SAMPLES:
		return set_limit_samples(devc, g_variant_get_uint64(data));
	case SR_CONF_CAPTURE_RATIO:
		return set_capture_ratio(devc, g_variant_get_uint64(data));
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &low, &high);
		return set_voltage_threshold(devc, (low + high) / 2.0);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *gvar, *grange[2];
	GVariantBuilder gvb;
	double v;
	GVariant *range[2];

	(void)cg;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	case SR_CONF_SAMPLERATE:
		devc = sdi->priv;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		if (devc->prof->max_sampling_freq == 100) {
			gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
					samplerates_100, ARRAY_SIZE(samplerates_100),
					sizeof(uint64_t));
		} else if (devc->prof->max_sampling_freq == 200) {
			gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
					samplerates_200, ARRAY_SIZE(samplerates_200),
					sizeof(uint64_t));
		} else {
			sr_err("Internal error: Unknown max. samplerate: %d.",
			       devc->prof->max_sampling_freq);
			return SR_ERR_ARG;
		}
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				trigger_matches, ARRAY_SIZE(trigger_matches),
				sizeof(int32_t));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (v = -6.0; v <= 6.0; v += 0.1) {
			range[0] = g_variant_new_double(v);
			range[1] = g_variant_new_double(v);
			gvar = g_variant_new_tuple(range, 2);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		grange[0] = g_variant_new_uint64(0);
		grange[1] = g_variant_new_uint64(devc->max_sample_depth);
		*data = g_variant_new_tuple(grange, 2);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	unsigned int samples_read;
	int res;
	unsigned int packet_num, n;
	unsigned char *buf;
	unsigned int status;
	unsigned int stop_address;
	unsigned int now_address;
	unsigned int trigger_address;
	unsigned int trigger_offset;
	unsigned int triggerbar;
	unsigned int ramsize_trigger;
	unsigned int memory_size;
	unsigned int valid_samples;
	unsigned int discard;
	int trigger_now;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (analyzer_add_triggers(sdi) != SR_OK) {
		sr_err("Failed to configure triggers.");
		return SR_ERR;
	}

	usb = sdi->conn;

	set_triggerbar(devc);

	/* Push configured settings to device. */
	analyzer_configure(usb->devhdl);

	analyzer_start(usb->devhdl);
	sr_info("Waiting for data.");
	analyzer_wait_data(usb->devhdl);

	status = analyzer_read_status(usb->devhdl);
	stop_address = analyzer_get_stop_address(usb->devhdl);
	now_address = analyzer_get_now_address(usb->devhdl);
	trigger_address = analyzer_get_trigger_address(usb->devhdl);

	triggerbar = analyzer_get_triggerbar_address();
	ramsize_trigger = analyzer_get_ramsize_trigger_address();

	n = get_memory_size(devc->memory_size);
	memory_size = n / 4;

	sr_info("Status = 0x%x.", status);
	sr_info("Stop address       = 0x%x.", stop_address);
	sr_info("Now address        = 0x%x.", now_address);
	sr_info("Trigger address    = 0x%x.", trigger_address);
	sr_info("Triggerbar address = 0x%x.", triggerbar);
	sr_info("Ramsize trigger    = 0x%x.", ramsize_trigger);
	sr_info("Memory size        = 0x%x.", memory_size);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Check for empty capture */
	if ((status & STATUS_READY) && !stop_address) {
		packet.type = SR_DF_END;
		sr_session_send(cb_data, &packet);
		return SR_OK;
	}

	if (!(buf = g_try_malloc(PACKET_SIZE))) {
		sr_err("Packet buffer malloc failed.");
		return SR_ERR_MALLOC;
	}

	/* Check if the trigger is in the samples we are throwing away */
	trigger_now = now_address == trigger_address ||
		((now_address + 1) % memory_size) == trigger_address;

	/*
	 * STATUS_READY doesn't clear until now_address advances past
	 * addr 0, but for our logic, clear it in that case
	 */
	if (!now_address)
		status &= ~STATUS_READY;

	analyzer_read_start(usb->devhdl);

	/* Calculate how much data to discard */
	discard = 0;
	if (status & STATUS_READY) {
		/*
		 * We haven't wrapped around, we need to throw away data from
		 * our current position to the end of the buffer.
		 * Additionally, the first two samples captured are always
		 * bogus.
		 */
		discard += memory_size - now_address + 2;
		now_address = 2;
	}

	/* If we have more samples than we need, discard them */
	valid_samples = (stop_address - now_address) % memory_size;
	if (valid_samples > ramsize_trigger + triggerbar) {
		discard += valid_samples - (ramsize_trigger + triggerbar);
		now_address += valid_samples - (ramsize_trigger + triggerbar);
	}

	sr_info("Need to discard %d samples.", discard);

	/* Calculate how far in the trigger is */
	if (trigger_now)
		trigger_offset = 0;
	else
		trigger_offset = (trigger_address - now_address) % memory_size;

	/* Recalculate the number of samples available */
	valid_samples = (stop_address - now_address) % memory_size;

	/* Send the incoming transfer to the session bus. */
	samples_read = 0;
	for (packet_num = 0; packet_num < n / PACKET_SIZE; packet_num++) {
		unsigned int len;
		unsigned int buf_offset;

		res = analyzer_read_data(usb->devhdl, buf, PACKET_SIZE);
		sr_info("Tried to read %d bytes, actually read %d bytes.",
			PACKET_SIZE, res);

		if (discard >= PACKET_SIZE / 4) {
			discard -= PACKET_SIZE / 4;
			continue;
		}

		len = PACKET_SIZE - discard * 4;
		buf_offset = discard * 4;
		discard = 0;

		/* Check if we've read all the samples */
		if (samples_read + len / 4 >= valid_samples)
			len = (valid_samples - samples_read) * 4;
		if (!len)
			break;

		if (samples_read < trigger_offset &&
		    samples_read + len / 4 > trigger_offset) {
			/* Send out samples remaining before trigger */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = (trigger_offset - samples_read) * 4;
			logic.unitsize = 4;
			logic.data = buf + buf_offset;
			sr_session_send(cb_data, &packet);
			len -= logic.length;
			samples_read += logic.length / 4;
			buf_offset += logic.length;
		}

		if (samples_read == trigger_offset) {
			/* Send out trigger */
			packet.type = SR_DF_TRIGGER;
			packet.payload = NULL;
			sr_session_send(cb_data, &packet);
		}

		/* Send out data (or data after trigger) */
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = len;
		logic.unitsize = 4;
		logic.data = buf + buf_offset;
		sr_session_send(cb_data, &packet);
		samples_read += len / 4;
	}
	analyzer_read_stop(usb->devhdl);
	g_free(buf);

	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

/* TODO: This stops acquisition on ALL devices, ignoring dev_index. */
static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_datafeed_packet packet;

	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	usb = sdi->conn;
	analyzer_reset(usb->devhdl);
	/* TODO: Need to cancel and free any queued up transfers. */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver zeroplus_logic_cube_driver_info = {
	.name = "zeroplus-logic-cube",
	.longname = "ZEROPLUS Logic Cube LAP-C series",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
