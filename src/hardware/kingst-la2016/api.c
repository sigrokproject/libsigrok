/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Gerhard Sittig <gerhard.sittig@gmx.net>
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
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
#if WITH_THRESHOLD_DEVCFG
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
#endif
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONTINUOUS | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_logic[] = {
#if !WITH_THRESHOLD_DEVCFG
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
#endif
};

static const uint32_t devopts_cg_pwm[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DUTY_CYCLE | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static const char *channel_names_logic[] = {
	"CH0", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7",
	"CH8", "CH9", "CH10", "CH11", "CH12", "CH13", "CH14", "CH15",
	"CH16", "CH17", "CH18", "CH19", "CH20", "CH21", "CH22", "CH23",
	"CH24", "CH25", "CH26", "CH27", "CH28", "CH29", "CH30", "CH31",
};

static const char *channel_names_pwm[] = {
	"PWM1", "PWM2",
};

/*
 * The devices have an upper samplerate limit of 100/200/500 MHz each.
 * But their hardware uses different base clocks (100/200/800MHz, this
 * is _not_ a typo) and a 16bit divider. Which results in per-model ranges
 * of supported rates which not only differ in the upper boundary, but
 * also at the lower boundary. It's assumed that the 10kHz rate is not
 * useful enough to provide by all means. Starting at 20kHz for all models
 * simplfies the implementation of the config API routines, and eliminates
 * redundancy in these samplerates tables.
 *
 * Streaming mode is constrained by the channel count and samplerate
 * product (the bits per second which need to travel the USB connection
 * while the acquisition is executing). Because streaming mode does not
 * compress the capture data, a later implementation may desire a finer
 * resolution. For now let's just stick with the 1/2/5 steps.
 */

static const uint64_t rates_500mhz[] = {
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
	SR_MHZ(500),
};

static const uint64_t rates_200mhz[] = {
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
};

static const uint64_t rates_100mhz[] = {
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(50),
	SR_MHZ(100),
};

/*
 * Only list a few discrete voltages, to form a useful set which covers
 * most logic families. Too many choices can make some applications use
 * a slider again. Which may lack a scale for the current value, and
 * leave users without feedback what the currently used value might be.
 */
static const double threshold_ranges[][2] = {
	{ 0.4, 0.4, },
	{ 0.6, 0.6, },
	{ 0.9, 0.9, },
	{ 1.2, 1.2, },
	{ 1.4, 1.4, }, /* Default, 1.4V, index 4. */
	{ 2.0, 2.0, },
	{ 2.5, 2.5, },
	{ 4.0, 4.0, },
};
#define LOGIC_THRESHOLD_IDX_DFLT	4

static double threshold_voltage(const struct sr_dev_inst *sdi, double *high)
{
	struct dev_context *devc;
	size_t idx;
	double voltage;

	devc = sdi->priv;
	idx = devc->threshold_voltage_idx;
	voltage = threshold_ranges[idx][0];
	if (high)
		*high = threshold_ranges[idx][1];

	return voltage;
}

/* Convenience. Release an allocated devc from error paths. */
static void kingst_la2016_free_devc(struct dev_context *devc)
{
	if (!devc)
		return;
	g_free(devc->mcu_firmware);
	g_free(devc->fpga_bitstream);
	g_free(devc);
}

/* Convenience. Release an allocated sdi from error paths. */
static void kingst_la2016_free_sdi(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return;
	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi->serial_num);
	g_free(sdi->connection_id);
	sr_usb_dev_inst_free(sdi->conn);
	kingst_la2016_free_devc(sdi->priv);
}

/* Convenience. Open a USB device (including claiming an interface). */
static int la2016_open_usb(struct sr_usb_dev_inst *usb,
	libusb_device *dev, gboolean show_message)
{
	int ret;

	ret = libusb_open(dev, &usb->devhdl);
	if (ret != 0) {
		if (show_message) {
			sr_err("Cannot open device: %s.",
				libusb_error_name(ret));
		}
		return SR_ERR_IO;
	}

	if (usb->address == 0xff) {
		/*
		 * First encounter after firmware upload.
		 * Grab current address after enumeration.
		 */
		usb->address = libusb_get_device_address(dev);
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret == LIBUSB_ERROR_BUSY) {
		sr_err("Cannot claim USB interface. Another program or driver using it?");
		return SR_ERR_IO;
	} else if (ret == LIBUSB_ERROR_NO_DEVICE) {
		sr_err("Device has been disconnected.");
		return SR_ERR_IO;
	} else if (ret != 0) {
		sr_err("Cannot claim USB interface: %s.",
			libusb_error_name(ret));
		return SR_ERR_IO;
	}

	return SR_OK;
}

/* Convenience. Close an opened USB device (and release the interface). */
static void la2016_close_usb(struct sr_usb_dev_inst *usb)
{

	if (!usb)
		return;

	if (usb->devhdl) {
		libusb_release_interface(usb->devhdl, USB_INTERFACE);
		libusb_close(usb->devhdl);
		usb->devhdl = NULL;
	}
}

/* Communicate to an USB device to identify the Kingst LA model. */
static int la2016_identify_read(struct sr_dev_inst *sdi,
	struct sr_usb_dev_inst *usb, libusb_device *dev,
	gboolean show_message)
{
	int ret;

	ret = la2016_open_usb(usb, dev, show_message);
	if (ret != SR_OK) {
		if (show_message)
			sr_err("Cannot communicate to MCU firmware.");
		return ret;
	}

	/*
	 * Also complete the hardware configuration (FPGA bitstream)
	 * when MCU firmware communication became operational. Either
	 * failure is considered fatal when probing for the device.
	 */
	ret = la2016_identify_device(sdi, show_message);
	if (ret == SR_OK) {
		ret = la2016_init_hardware(sdi);
	}

	la2016_close_usb(usb);

	return ret;
}

/* Find given conn_id in another USB enum. Identify Kingst LA model. */
static int la2016_identify_enum(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct sr_context *ctx;
	libusb_device **devlist, *dev;
	struct libusb_device_descriptor des;
	int ret, id_ret;
	size_t device_count, dev_idx;
	char conn_id[64];

	di = sdi->driver;
	drvc = di->context;
	ctx = drvc->sr_ctx;;

	ret = libusb_get_device_list(ctx->libusb_ctx, &devlist);
	if (ret < 0)
		return SR_ERR_IO;
	device_count = ret;
	if (!device_count)
		return SR_ERR_IO;
	id_ret = SR_ERR_IO;
	for (dev_idx = 0; dev_idx < device_count; dev_idx++) {
		dev = devlist[dev_idx];
		libusb_get_device_descriptor(dev, &des);
		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID)
			continue;
		if (des.iProduct != LA2016_IPRODUCT_INDEX)
			continue;
		ret = usb_get_port_path(dev, conn_id, sizeof(conn_id));
		if (ret < 0)
			continue;
		if (strcmp(sdi->connection_id, conn_id) != 0)
			continue;
		id_ret = la2016_identify_read(sdi, sdi->conn, dev, FALSE);
		break;
	}
	libusb_free_device_list(devlist, 1);

	return id_ret;
}

/* Wait for a device to re-appear after firmware upload. */
static int la2016_identify_wait(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t reset_done, now, elapsed_ms;
	int ret;

	devc = sdi->priv;

	sr_info("Waiting for device to reset after firmware upload.");
	now = g_get_monotonic_time();
	reset_done = devc->fw_uploaded + RENUM_GONE_DELAY_MS * 1000;
	if (now < reset_done)
		g_usleep(reset_done - now);
	do {
		now = g_get_monotonic_time();
		elapsed_ms = (now - devc->fw_uploaded) / 1000;
		sr_spew("Waited %" PRIu64 "ms.", elapsed_ms);
		ret = la2016_identify_enum(sdi);
		if (ret == SR_OK) {
			devc->fw_uploaded = 0;
			break;
		}
		g_usleep(RENUM_POLL_INTERVAL_MS * 1000);
	} while (elapsed_ms < RENUM_CHECK_PERIOD_MS);
	if (ret != SR_OK) {
		sr_err("Device failed to re-enumerate.");
		return ret;
	}
	sr_info("Device came back after %" PRIi64 "ms.", elapsed_ms);

	return SR_OK;
}

/*
 * Open given conn_id from another USB enum. Used by dev_open(). Similar
 * to, and should be kept in sync with la2016_identify_enum().
 */
static int la2016_open_enum(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct sr_context *ctx;
	libusb_device **devlist, *dev;
	struct libusb_device_descriptor des;
	int ret, open_ret;
	size_t device_count, dev_idx;
	char conn_id[64];

	di = sdi->driver;
	drvc = di->context;
	ctx = drvc->sr_ctx;;

	ret = libusb_get_device_list(ctx->libusb_ctx, &devlist);
	if (ret < 0)
		return SR_ERR_IO;
	device_count = ret;
	if (!device_count)
		return SR_ERR_IO;
	open_ret = SR_ERR_IO;
	for (dev_idx = 0; dev_idx < device_count; dev_idx++) {
		dev = devlist[dev_idx];
		libusb_get_device_descriptor(dev, &des);
		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID)
			continue;
		if (des.iProduct != LA2016_IPRODUCT_INDEX)
			continue;
		ret = usb_get_port_path(dev, conn_id, sizeof(conn_id));
		if (ret < 0)
			continue;
		if (strcmp(sdi->connection_id, conn_id) != 0)
			continue;
		open_ret = la2016_open_usb(sdi->conn, dev, TRUE);
		break;
	}
	libusb_free_device_list(devlist, 1);

	return open_ret;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_context *ctx;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	GSList *l;
	GSList *devices, *found_devices, *renum_devices;
	GSList *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist, *dev;
	size_t dev_count, dev_idx, ch_idx;
	uint8_t bus, addr;
	uint16_t pid;
	const char *conn;
	char conn_id[64];
	int ret;
	size_t ch_off, ch_max;
	struct sr_channel *ch;
	struct sr_channel_group *cg;

	drvc = di->context;
	ctx = drvc->sr_ctx;;

	conn = NULL;
	conn_devices = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(ctx->libusb_ctx, conn);
	if (conn && !conn_devices) {
		sr_err("Cannot find the specified connection '%s'.", conn);
		return NULL;
	}

	/*
	 * Find all LA2016 devices, optionally upload firmware to them.
	 * Defer completion of sdi/devc creation until all (selected)
	 * devices were found in a usable state, and their models got
	 * identified which affect their feature set. It appears that
	 * we cannot communicate to the device within the same USB enum
	 * cycle, needs another USB enumeration after firmware upload.
	 */
	devices = NULL;
	found_devices = NULL;
	renum_devices = NULL;
	ret = libusb_get_device_list(ctx->libusb_ctx, &devlist);
	if (ret < 0) {
		sr_err("Cannot get device list: %s.", libusb_error_name(ret));
		return devices;
	}
	dev_count = ret;
	for (dev_idx = 0; dev_idx < dev_count; dev_idx++) {
		dev = devlist[dev_idx];
		bus = libusb_get_bus_number(dev);
		addr = libusb_get_device_address(dev);

		/* Filter by connection when externally specified. */
		for (l = conn_devices; l; l = l->next) {
			usb = l->data;
			if (usb->bus == bus && usb->address == addr)
				break;
		}
		if (conn_devices && !l) {
			sr_spew("Bus %hhu, addr %hhu do not match specified filter.",
				bus, addr);
			continue;
		}

		/* Check USB VID:PID. Get the connection string. */
		libusb_get_device_descriptor(dev, &des);
		if (des.idVendor != LA2016_VID || des.idProduct != LA2016_PID)
			continue;
		pid = des.idProduct;
		ret = usb_get_port_path(dev, conn_id, sizeof(conn_id));
		if (ret < 0)
			continue;
		sr_dbg("USB enum found %04x:%04x at path %s, %d.%d.",
			des.idVendor, des.idProduct, conn_id, bus, addr);
		usb = sr_usb_dev_inst_new(bus, addr, NULL);

		sdi = g_malloc0(sizeof(*sdi));
		sdi->driver = di;
		sdi->status = SR_ST_INITIALIZING;
		sdi->inst_type = SR_INST_USB;
		sdi->connection_id = g_strdup(conn_id);
		sdi->conn = usb;

		devc = g_malloc0(sizeof(*devc));
		sdi->priv = devc;

		/*
		 * Load MCU firmware if it is currently missing. Which
		 * makes the device disappear and renumerate in USB.
		 * We need to come back another time to communicate to
		 * this device.
		 */
		devc->fw_uploaded = 0;
		devc->usb_pid = pid;
		if (des.iProduct != LA2016_IPRODUCT_INDEX) {
			sr_info("Uploading MCU firmware to '%s'.", conn_id);
			ret = la2016_upload_firmware(sdi, ctx, dev, FALSE);
			if (ret != SR_OK) {
				sr_err("MCU firmware upload failed.");
				kingst_la2016_free_sdi(sdi);
				continue;
			}
			devc->fw_uploaded = g_get_monotonic_time();
			usb->address = 0xff;
			renum_devices = g_slist_append(renum_devices, sdi);
			continue;
		} else {
			ret = la2016_upload_firmware(sdi, NULL, NULL, TRUE);
			if (ret != SR_OK) {
				sr_err("MCU firmware filename check failed.");
				kingst_la2016_free_sdi(sdi);
				continue;
			}
		}

		/*
		 * Communicate to the MCU firmware to access EEPROM data
		 * which lets us identify the device type. Then stop, to
		 * share remaining sdi/devc creation with those devices
		 * which had their MCU firmware uploaded above and which
		 * get revisited later.
		 */
		ret = la2016_identify_read(sdi, usb, dev, TRUE);
		if (ret != SR_OK || !devc->model) {
			sr_err("Unknown or unsupported device type.");
			kingst_la2016_free_sdi(sdi);
			continue;
		}
		found_devices = g_slist_append(found_devices, sdi);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, sr_usb_dev_inst_free_cb);

	/*
	 * Wait for devices to re-appear after firmware upload. Append
	 * the yet unidentified device to the list of found devices, or
	 * release the previously allocated sdi/devc.
	 */
	for (l = renum_devices; l; l = l->next) {
		sdi = l->data;
		devc = sdi->priv;
		ret = la2016_identify_wait(sdi);
		if (ret != SR_OK || !devc->model) {
			sr_dbg("Skipping unusable '%s'.", sdi->connection_id);
			kingst_la2016_free_sdi(sdi);
			continue;
		}
		found_devices = g_slist_append(found_devices, sdi);
	}
	g_slist_free(renum_devices);

	/*
	 * All found devices got identified, their type is known here.
	 * Complete the sdi/devc creation. Assign default settings
	 * because the vendor firmware would not let us read back the
	 * previously written configuration.
	 */
	for (l = found_devices; l; l = l->next) {
		sdi = l->data;
		devc = sdi->priv;

		sdi->vendor = g_strdup("Kingst");
		sdi->model = g_strdup(devc->model->name);
		ch_off = 0;

		/* Create the "Logic" channel group. */
		ch_max = ARRAY_SIZE(channel_names_logic);
		if (ch_max > devc->model->channel_count)
			ch_max = devc->model->channel_count;
		cg = sr_channel_group_new(sdi, "Logic", NULL);
		devc->cg_logic = cg;
		for (ch_idx = 0; ch_idx < ch_max; ch_idx++) {
			ch = sr_channel_new(sdi, ch_off,
				SR_CHANNEL_LOGIC, TRUE,
				channel_names_logic[ch_idx]);
			ch_off++;
			cg->channels = g_slist_append(cg->channels, ch);
		}

		/* Create the "PWMx" channel groups. */
		ch_max = ARRAY_SIZE(channel_names_pwm);
		for (ch_idx = 0; ch_idx < ch_max; ch_idx++) {
			const char *name;
			name = channel_names_pwm[ch_idx];
			cg = sr_channel_group_new(sdi, name, NULL);
			if (!devc->cg_pwm)
				devc->cg_pwm = cg;
			ch = sr_channel_new(sdi, ch_off,
				SR_CHANNEL_ANALOG, FALSE, name);
			ch_off++;
			cg->channels = g_slist_append(cg->channels, ch);
		}

		/*
		 * Ideally we'd get the previous configuration from the
		 * hardware, but this device is write-only. So we have
		 * to assign a fixed set of initial configuration values.
		 */
		sr_sw_limits_init(&devc->sw_limits);
		devc->sw_limits.limit_samples = 0;
		devc->capture_ratio = 50;
		devc->samplerate = devc->model->samplerate;
		if (!devc->model->memory_bits)
			devc->continuous = TRUE;
		devc->threshold_voltage_idx = LOGIC_THRESHOLD_IDX_DFLT;
		if  (ARRAY_SIZE(devc->pwm_setting) >= 1) {
			devc->pwm_setting[0].enabled = FALSE;
			devc->pwm_setting[0].freq = SR_KHZ(1);
			devc->pwm_setting[0].duty = 50;
		}
		if  (ARRAY_SIZE(devc->pwm_setting) >= 2) {
			devc->pwm_setting[1].enabled = FALSE;
			devc->pwm_setting[1].freq = SR_KHZ(100);
			devc->pwm_setting[1].duty = 50;
		}

		sdi->status = SR_ST_INACTIVE;
		devices = g_slist_append(devices, sdi);
	}
	g_slist_free(found_devices);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	size_t ch;

	devc = sdi->priv;

	ret = la2016_open_enum(sdi);
	if (ret != SR_OK) {
		sr_err("Cannot open device.");
		return ret;
	}

	/* Send most recent PWM configuration to the device. */
	for (ch = 0; ch < ARRAY_SIZE(devc->pwm_setting); ch++) {
		ret = la2016_write_pwm_config(sdi, ch);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	la2016_release_resources(sdi);

	if (WITH_DEINIT_IN_CLOSE)
		la2016_deinit_hardware(sdi);

	sr_info("Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	la2016_close_usb(sdi->conn);

	return SR_OK;
}

/* Config API helper. Get type and index of a channel group. */
static int get_cg_index(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg,
	int *type, size_t *logic, size_t *analog)
{
	struct dev_context *devc;
	GSList *l;
	size_t idx;

	/* Preset return values. */
	if (type)
		*type = 0;
	if (logic)
		*logic = 0;
	if (analog)
		*analog = 0;

	/* Start categorizing the received cg. */
	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!cg)
		return SR_OK;
	l = sdi->channel_groups;

	/* First sdi->channelgroups item is "Logic". */
	if (!l)
		return SR_ERR_BUG;
	if (cg == l->data) {
		if (type)
			*type = SR_CHANNEL_LOGIC;
		if (logic)
			*logic = 0;
		return SR_OK;
	}
	l = l->next;

	/* Next sdi->channelgroups items are "PWMx". */
	idx = 0;
	while (l && l->data != cg) {
		idx++;
		l = l->next;
	}
	if (l && idx < ARRAY_SIZE(devc->pwm_setting)) {
		if (type)
			*type = SR_CHANNEL_ANALOG;
		if (analog)
			*analog = idx;
		return SR_OK;
	}

	return SR_ERR_ARG;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret, cg_type;
	size_t logic_idx, analog_idx;
	struct pwm_setting *pwm;
	struct sr_usb_dev_inst *usb;
	double voltage, rounded;

	(void)rounded;
	(void)voltage;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	/* Check for types (and index) of channel groups. */
	ret = get_cg_index(sdi, cg, &cg_type, &logic_idx, &analog_idx);
	if (cg && ret != SR_OK)
		return SR_ERR_ARG;

	/* Handle requests for the "Logic" channel group. */
	if (cg && cg_type == SR_CHANNEL_LOGIC) {
		switch (key) {
#if !WITH_THRESHOLD_DEVCFG
		case SR_CONF_VOLTAGE_THRESHOLD:
			voltage = threshold_voltage(sdi, NULL);
			*data = std_gvar_tuple_double(voltage, voltage);
			break;
#endif /* WITH_THRESHOLD_DEVCFG */
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	/* Handle requests for the "PWMx" channel groups. */
	if (cg && cg_type == SR_CHANNEL_ANALOG) {
		pwm = &devc->pwm_setting[analog_idx];
		switch (key) {
		case SR_CONF_ENABLED:
			*data = g_variant_new_boolean(pwm->enabled);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			*data = g_variant_new_double(pwm->freq);
			break;
		case SR_CONF_DUTY_CYCLE:
			*data = g_variant_new_double(pwm->duty);
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_CONN:
		usb = sdi->conn;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->sw_limits, key, data);
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
#if WITH_THRESHOLD_DEVCFG
	case SR_CONF_VOLTAGE_THRESHOLD:
		voltage = threshold_voltage(sdi, NULL);
		*data = std_gvar_tuple_double(voltage, voltage);
		break;
#endif /* WITH_THRESHOLD_DEVCFG */
	case SR_CONF_CONTINUOUS:
		*data = g_variant_new_boolean(devc->continuous);
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
	int ret, cg_type;
	size_t logic_idx, analog_idx;
	struct pwm_setting *pwm;
	double value_f;
	int idx;
	gboolean on;

	devc = sdi->priv;

	/* Check for types (and index) of channel groups. */
	ret = get_cg_index(sdi, cg, &cg_type, &logic_idx, &analog_idx);
	if (cg && ret != SR_OK)
		return SR_ERR_ARG;

	/* Handle requests for the "Logic" channel group. */
	if (cg && cg_type == SR_CHANNEL_LOGIC) {
		switch (key) {
#if !WITH_THRESHOLD_DEVCFG
		case SR_CONF_LOGIC_THRESHOLD:
			idx = std_double_tuple_idx(data,
				ARRAY_AND_SIZE(threshold_ranges));
			if (idx < 0)
				return SR_ERR_ARG;
			devc->threshold_voltage_idx = idx;
			break;
#endif /* WITH_THRESHOLD_DEVCFG */
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	/* Handle requests for the "PWMx" channel groups. */
	if (cg && cg_type == SR_CHANNEL_ANALOG) {
		pwm = &devc->pwm_setting[analog_idx];
		switch (key) {
		case SR_CONF_ENABLED:
			pwm->enabled = g_variant_get_boolean(data);
			ret = la2016_write_pwm_config(sdi, analog_idx);
			if (ret != SR_OK)
				return ret;
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			value_f = g_variant_get_double(data);
			if (value_f <= 0.0 || value_f > MAX_PWM_FREQ)
				return SR_ERR_ARG;
			pwm->freq = value_f;
			ret = la2016_write_pwm_config(sdi, analog_idx);
			if (ret != SR_OK)
				return ret;
			break;
		case SR_CONF_DUTY_CYCLE:
			value_f = g_variant_get_double(data);
			if (value_f <= 0.0 || value_f > 100.0)
				return SR_ERR_ARG;
			pwm->duty = value_f;
			ret = la2016_write_pwm_config(sdi, analog_idx);
			if (ret != SR_OK)
				return ret;
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->sw_limits, key, data);
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
#if WITH_THRESHOLD_DEVCFG
	case SR_CONF_VOLTAGE_THRESHOLD:
		idx = std_double_tuple_idx(data,
			ARRAY_AND_SIZE(threshold_ranges));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->threshold_voltage_idx = idx;
		break;
#endif /* WITH_THRESHOLD_DEVCFG */
	case SR_CONF_CONTINUOUS:
		on = g_variant_get_boolean(data);
		if (!devc->model->memory_bits && !on)
			return SR_ERR_ARG;
		devc->continuous = on;
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
	int ret, cg_type;
	size_t logic_idx, analog_idx;

	devc = sdi ? sdi->priv : NULL;

	/* Check for types (and index) of channel groups. */
	ret = get_cg_index(sdi, cg, &cg_type, &logic_idx, &analog_idx);
	if (cg && ret != SR_OK)
		return SR_ERR_ARG;

	/* Handle requests for the "Logic" channel group. */
	if (cg && cg_type == SR_CHANNEL_LOGIC) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			if (ARRAY_SIZE(devopts_cg_logic) == 0)
				return SR_ERR_NA;
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts_cg_logic, ARRAY_SIZE(devopts_cg_logic),
				sizeof(devopts_cg_logic[0]));
			break;
#if !WITH_THRESHOLD_DEVCFG
		case SR_CONF_VOLTAGE_THRESHOLD:
			*data = std_gvar_thresholds(ARRAY_AND_SIZE(threshold_ranges));
			break;
#endif /* WITH_THRESHOLD_DEVCFG */
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	/* Handle requests for the "PWMx" channel groups. */
	if (cg && cg_type == SR_CHANNEL_ANALOG) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts_cg_pwm, ARRAY_SIZE(devopts_cg_pwm),
				sizeof(devopts_cg_pwm[0]));
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_ARG;
		if (devc->model->samplerate == SR_MHZ(500))
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(rates_500mhz));
		else if (devc->model->samplerate == SR_MHZ(200))
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(rates_200mhz));
		else if (devc->model->samplerate == SR_MHZ(100))
			*data = std_gvar_samplerates(ARRAY_AND_SIZE(rates_100mhz));
		else
			return SR_ERR_BUG;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = std_gvar_tuple_u64(0, LA2016_NUM_SAMPLES_MAX);
		break;
#if WITH_THRESHOLD_DEVCFG
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_thresholds(ARRAY_AND_SIZE(threshold_ranges));
		break;
#endif /* WITH_THRESHOLD_DEVCFG */
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct sr_context *ctx;
	struct dev_context *devc;
	size_t unitsize;
	double voltage;
	int ret;

	di = sdi->driver;
	drvc = di->context;
	ctx = drvc->sr_ctx;;
	devc = sdi->priv;

	if (!devc->feed_queue) {
		if (devc->model->channel_count == 32)
			unitsize = sizeof(uint32_t);
		else if (devc->model->channel_count == 16)
			unitsize = sizeof(uint16_t);
		else
			return SR_ERR_ARG;
		devc->feed_queue = feed_queue_logic_alloc(sdi,
			LA2016_CONVBUFFER_SIZE, unitsize);
		if (!devc->feed_queue) {
			sr_err("Cannot allocate buffer for session feed.");
			return SR_ERR_MALLOC;
		}
		devc->packets_per_chunk = TRANSFER_PACKET_LENGTH;
		devc->packets_per_chunk--;
		devc->packets_per_chunk /= unitsize + sizeof(uint8_t);
	}

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	voltage = threshold_voltage(sdi, NULL);
	ret = la2016_setup_acquisition(sdi, voltage);
	if (ret != SR_OK) {
		feed_queue_logic_free(devc->feed_queue);
		devc->feed_queue = NULL;
		return ret;
	}

	ret = la2016_start_acquisition(sdi);
	if (ret != SR_OK) {
		la2016_abort_acquisition(sdi);
		feed_queue_logic_free(devc->feed_queue);
		devc->feed_queue = NULL;
		return ret;
	}

	devc->completion_seen = FALSE;
	usb_source_add(sdi->session, ctx, 50,
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
