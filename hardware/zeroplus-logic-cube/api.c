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
#define TRIGGER_TYPE 			"01"
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
	/* TODO: We don't know anything about these.
	{0x0c12, 0x700b, "LAP-C(32128)",  32, 128,  200},
	{0x0c12, 0x700c, "LAP-C(321000)", 32, 1024, 200},
	{0x0c12, 0x700d, "LAP-C(322000)", 32, 2048, 200},
	*/
	{0x0c12, 0x700e, "LAP-C(16032)",  16, 32,   100},
	{0x0c12, 0x7016, "LAP-C(162000)", 16, 2048, 200},
	{ 0, 0, 0, 0, 0, 0 }
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_CAPTURE_RATIO,
	SR_CONF_LIMIT_SAMPLES,
};

/*
 * ZEROPLUS LAP-C (16032) numbers the 16 probes A0-A7 and B0-B7.
 * We currently ignore other untested/unsupported devices here.
 */
static const char *probe_names[] = {
	"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
	"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7",
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

static int hw_dev_close(struct sr_dev_inst *sdi);

#if 0
static int configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct sr_probe *probe;
	const GSList *l;
	int probe_bit, stage, i;
	char *tc;

	/* Note: sdi and sdi->priv are non-NULL, the caller checked this. */
	devc = sdi->priv;

	devc->probe_mask = 0;
	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		devc->trigger_mask[i] = 0;
		devc->trigger_value[i] = 0;
	}

	stage = -1;
	for (l = sdi->probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;
		probe_bit = 1 << (probe->index);
		devc->probe_mask |= probe_bit;

		if (probe->trigger) {
			stage = 0;
			for (tc = probe->trigger; *tc; tc++) {
				devc->trigger_mask[stage] |= probe_bit;
				if (*tc == '1')
					devc->trigger_value[stage] |= probe_bit;
				stage++;
				if (stage > NUM_TRIGGER_STAGES)
					return SR_ERR;
			}
		}
	}

	return SR_OK;
}
#endif

static int configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const GSList *l;
	const struct sr_probe *probe;
	char *tc;
	int type;

	/* Note: sdi and sdi->priv are non-NULL, the caller checked this. */
	devc = sdi->priv;

	for (l = sdi->probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;

		if ((tc = probe->trigger)) {
			switch (*tc) {
			case '1':
				type = TRIGGER_HIGH;
				break;
			case '0':
				type = TRIGGER_LOW;
				break;
#if 0
			case 'r':
				type = TRIGGER_POSEDGE;
				break;
			case 'f':
				type = TRIGGER_NEGEDGE;
				break;
			case 'c':
				type = TRIGGER_ANYEDGE;
				break;
#endif
			default:
				return SR_ERR;
			}
			analyzer_add_trigger(probe->index, type);
			devc->trigger = 1;
		}
	}

	return SR_OK;
}

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

static int clear_instances(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	drvc = di->priv;
	for (l = drvc->instances; l; l = l->next) {
		sdi = l->data;
		if (!(devc = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("%s: sdi->priv was NULL, continuing", __func__);
			continue;
		}
		usb = sdi->conn;
		sr_usb_dev_inst_free(usb);
		/* Properly close all devices... */
		hw_dev_close(sdi);
		/* ...and free all their memory. */
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, "zeroplus: ");
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
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

	clear_instances();

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
		devc->max_memory_size = 128 * 1024;
		devc->max_samplerate = 200;
#else
		devc->max_memory_size = prof->sample_depth * 1024;
		devc->max_samplerate = prof->max_sampling_freq;
#endif
		devc->max_samplerate *= SR_MHZ(1);
		devc->memory_size = MEMORY_SIZE_8K;
		// memset(devc->trigger_buffer, 0, NUM_TRIGGER_STAGES);

		/* Fill in probelist according to this device's profile. */
		for (j = 0; j < devc->num_channels; j++) {
			if (!(probe = sr_probe_new(j, SR_PROBE_LOGIC, TRUE,
					probe_names[j])))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

		devices = g_slist_append(devices, sdi);
		drvc->instances = g_slist_append(drvc->instances, sdi);
		sdi->conn = sr_usb_dev_inst_new(
			libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);
		devcnt++;

	}
	libusb_free_device_list(devlist, 1);

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
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

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
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

static int hw_cleanup(void)
{
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		return SR_OK;

	clear_instances();

	return SR_OK;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = g_variant_new_uint64(devc->cur_samplerate);
			sr_spew("Returning samplerate: %" PRIu64 "Hz.",
				devc->cur_samplerate);
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	switch (id) {
	case SR_CONF_SAMPLERATE:
		return zp_set_samplerate(devc, g_variant_get_uint64(data));
	case SR_CONF_LIMIT_SAMPLES:
		return set_limit_samples(devc, g_variant_get_uint64(data));
	case SR_CONF_CAPTURE_RATIO:
		return set_capture_ratio(devc, g_variant_get_uint64(data));
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GVariant *gvar;
	GVariantBuilder gvb;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
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
	case SR_CONF_TRIGGER_TYPE:
		*data = g_variant_new_string(TRIGGER_TYPE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	//uint64_t samples_read;
	int res;
	unsigned int packet_num, n;
	unsigned char *buf;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
		return SR_ERR;
	}

	usb = sdi->conn;

	set_triggerbar(devc);

	/* Push configured settings to device. */
	analyzer_configure(usb->devhdl);

	analyzer_start(usb->devhdl);
	sr_info("Waiting for data.");
	analyzer_wait_data(usb->devhdl);

	sr_info("Stop address    = 0x%x.",
		analyzer_get_stop_address(usb->devhdl));
	sr_info("Now address     = 0x%x.",
		analyzer_get_now_address(usb->devhdl));
	sr_info("Trigger address = 0x%x.",
		analyzer_get_trigger_address(usb->devhdl));

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	if (!(buf = g_try_malloc(PACKET_SIZE))) {
		sr_err("Packet buffer malloc failed.");
		return SR_ERR_MALLOC;
	}

	//samples_read = 0;
	analyzer_read_start(usb->devhdl);
	/* Send the incoming transfer to the session bus. */
	n = get_memory_size(devc->memory_size);
	if (devc->max_memory_size * 4 < n)
		n = devc->max_memory_size * 4;
	for (packet_num = 0; packet_num < n / PACKET_SIZE; packet_num++) {
		res = analyzer_read_data(usb->devhdl, buf, PACKET_SIZE);
		sr_info("Tried to read %d bytes, actually read %d bytes.",
			PACKET_SIZE, res);

		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = PACKET_SIZE;
		logic.unitsize = 4;
		logic.data = buf;
		sr_session_send(cb_data, &packet);
		//samples_read += res / 4;
	}
	analyzer_read_stop(usb->devhdl);
	g_free(buf);

	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

/* TODO: This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
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
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = hw_cleanup,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
