/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024-2026 PXView Developers <913461865@qq.com>
 * Copyright (C) 2026 dogtopus <dogtopus@users.noreply.github.com>
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

#define PXLOGIC_VID 0x16c0
#define PXLOGIC_PID 0x05dc

#define PXLOGIC_OLD_VID 0x1a86
#define PXLOGIC_OLD_PID 0x5237

#define USB_INTERFACE_MAIN 0
#define USB_INTERFACE_DEBUG 1

#define MCU_CHECK_PERIOD_US 200000
#define MCU_CHECK_COUNT 25

#define GS SR_GHZ

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CONTINUOUS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_FILTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_pwm[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DUTY_CYCLE | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,    SR_TRIGGER_ONE,  SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_MHZ(1),   SR_MHZ(2),	  SR_MHZ(4),   SR_MHZ(5),
	SR_MHZ(10),  SR_MHZ(20),  SR_MHZ(25),  SR_MHZ(50),
	SR_MHZ(100), SR_MHZ(125), SR_MHZ(200), SR_MHZ(250),
	SR_MHZ(400), SR_MHZ(500), SR_MHZ(800), SR_GHZ(1),
};

static const char *clock_edges[] = {
	"rising",
	"falling",
};

static const char *trigger_slopes[] = {
	[EXT_TRIG_OFF] = "off", [EXT_TRIG_RISING] = "r",
	[EXT_TRIG_HIGH] = "1",	[EXT_TRIG_FALLING] = "f",
	[EXT_TRIG_LOW] = "0",	[EXT_TRIG_EDGE] = "e",
};

static const char *variant_names[] = {
	[VARIANT_32] = "Logic 32",
	[VARIANT_16_PRO] = "Logic 16 Pro",
	[VARIANT_16_PLUS] = "Logic 16 Plus",
	[VARIANT_16_BASE] = "Logic 16 Base",
};

static int variant_logic_channels[] = {
	[VARIANT_32] = 32,
	[VARIANT_16_PRO] = 16,
	[VARIANT_16_PLUS] = 16,
	[VARIANT_16_BASE] = 16,
};

static unsigned int variant_samplerate_cutoff[] = {
	[VARIANT_32] = ARRAY_SIZE(samplerates),
	[VARIANT_16_PRO] = ARRAY_SIZE(samplerates),
	[VARIANT_16_PLUS] = ARRAY_SIZE(samplerates) - 2,
	[VARIANT_16_BASE] = ARRAY_SIZE(samplerates) - 4,
};

static const uint64_t variant_depth[] = {
	[VARIANT_32] = GS(4),
	[VARIANT_16_PRO] = GS(4),
	[VARIANT_16_PLUS] = GS(2),
	[VARIANT_16_BASE] = GS(1),
};

static struct sr_dev_driver px_logic_driver_info;

static inline gboolean check_vid_pid(struct libusb_device_descriptor *des)
{
	return (des->idVendor == PXLOGIC_VID &&
		des->idProduct == PXLOGIC_PID) ||
	       (des->idVendor == PXLOGIC_OLD_VID &&
		des->idProduct == PXLOGIC_OLD_PID);
}

static gboolean in_conn_devices(GSList *conn_devices, libusb_device *dev)
{
	GSList *l;
	struct sr_usb_dev_inst *usb;
	usb = NULL;
	for (l = conn_devices; l; l = l->next) {
		usb = l->data;
		if (usb->bus == libusb_get_bus_number(dev) &&
		    usb->address == libusb_get_device_address(dev))
			return TRUE;
	}
	/* This device matched none of the ones that matched the conn
	   specification. */
	return FALSE;
}

/**
 * Match descriptor text fields and extract the device serial number.
 * 
 * @param dev 
 * @param serial_num 
 * @return TRUE if everything is OK.
 */
static gboolean process_descriptor(libusb_device *dev, char serial_num[64])
{
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	gboolean ret;
	unsigned char strdesc[64];

	hdl = NULL;
	ret = FALSE;
	while (!ret) {
		/* Assume strings won't match, unless proven wrong. */
		libusb_get_device_descriptor(dev, &des);

		if (libusb_open(dev, &hdl) != 0)
			break;

		if (libusb_get_string_descriptor_ascii(hdl, des.iManufacturer,
						       strdesc,
						       sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, "PX"))
			break;

		/* Extract serial number. */
		if (libusb_get_string_descriptor_ascii(
			    hdl, des.iSerialNumber, (unsigned char *)serial_num,
			    sizeof(strdesc)) < 0)
			break;

		/* If we made it here, the strings must match. */
		ret = TRUE;
	}
	if (hdl)
		libusb_close(hdl);

	return ret;
}

/**
 * Detect the device variant and populate channels, etc.
 * 
 * @param sdi Half-initialized device instance.
 * @param dev libusb device context.
 * @retval SR_OK Device variant was determined and capability set.
 * @retval SR_ERR Caller should abort further instance creation and ignore this device.
 */
static int probe_device(struct sr_dev_inst *sdi, struct drv_context *drvc,
			libusb_device *dev)
{
	struct sr_usb_dev_inst *const usb = sdi->conn;
	struct dev_context *const devc = sdi->priv;

	struct sr_channel *ch;
	struct sr_channel_group *cg;
	int result_call, result;
	int i, ch_offset;
	enum device_variant variant;
	gboolean claimed;
	char name[8];

	/* Make a temporary connection to send register read request. */
	result_call = libusb_open(dev, &usb->devhdl);

	if (result_call != LIBUSB_SUCCESS) {
		sr_err("%s: Failed to open device for detection: %s.", __func__,
		       libusb_error_name(result_call));
		return SR_ERR;
	}

	result = SR_ERR;
	claimed = FALSE;

	result_call = libusb_claim_interface(usb->devhdl, USB_INTERFACE_MAIN);
	if (result_call == LIBUSB_SUCCESS) {
		claimed = TRUE;
		variant = px_logic_probe_variant(usb->devhdl);

		/* Change the variant. */
		devc->config.variant = variant;
		if (variant == VARIANT_UNKNOWN)
			goto done;
		if (sdi->model != NULL)
			g_free(sdi->model);
		sdi->model = g_strdup(variant_names[variant]);

		devc->config.max_buffer_depth = variant_depth[variant];

		ch_offset = 0;

		/* Add logic channels based on variant. */
		cg = sr_channel_group_new(sdi, "Logic", NULL);
		devc->cg_logic = cg;

		devc->config.channels = variant_logic_channels[variant];

		for (i = 0; i < variant_logic_channels[variant]; i++) {
			g_snprintf(name, sizeof(name) - 1, "%d", i);
			name[sizeof(name) - 1] = '\0';
			ch = sr_channel_new(sdi, ch_offset, SR_CHANNEL_LOGIC,
					    TRUE, name);
			cg->channels = g_slist_append(cg->channels, ch);
			ch_offset++;
		}

		/* Add PWM channels. */
		cg = sr_channel_group_new(sdi, "PWM0", NULL);

		ch = sr_channel_new(sdi, ch_offset, SR_CHANNEL_ANALOG, FALSE,
				    "P0");
		cg->channels = g_slist_append(cg->channels, ch);
		ch_offset++;

		cg = sr_channel_group_new(sdi, "PWM1", NULL);

		ch = sr_channel_new(sdi, ch_offset, SR_CHANNEL_ANALOG, FALSE,
				    "P1");
		cg->channels = g_slist_append(cg->channels, ch);
		ch_offset++;

		/* Probe MCU firmware version and upload the local firmware
		   image to the device when needed. */
		result_call = px_logic_probe_mcu(drvc->sr_ctx, usb->devhdl);
		if (result_call == SR_OK)
			/* Change device state to INACTIVE to mark that it's
			   ready to be opened. */
			sdi->status = SR_ST_INACTIVE;
		else if (result_call != SR_ERR_DEV_CLOSED)
			/* Other unhandled error. */
			goto done;

		result = SR_OK;
	} else {
		sr_err("%s: Failed to claim the main interface: %s.", __func__,
		       libusb_error_name(result_call));
	}

done:
	if (claimed)
		libusb_release_interface(usb->devhdl, USB_INTERFACE_MAIN);

	libusb_close(usb->devhdl);
	usb->devhdl = NULL;

	return result;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	unsigned int i;
	const char *conn;
	char connection_id[64], serial_number[64];
	int res;

	devices = NULL;
	drvc = di->context;
	devc = NULL;
	drvc->instances = NULL;

	/* Check for manually set connection strings in device config. */
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

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);

	for (i = 0; devlist[i]; i++) {
		if (conn && !in_conn_devices(conn_devices, devlist[i]))
			continue;

		libusb_get_device_descriptor(devlist[i], &des);

		if (usb_get_port_path(devlist[i], connection_id,
				      sizeof(connection_id)) < 0)
			continue;

		if (!check_vid_pid(&des))
			continue;

		if (!process_descriptor(devlist[i], serial_number))
			continue;

		/* We now have pretty high confidence that this is a device
		   we can talk to. Try to create a device instance and get more
		   info from the device */
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup("PX");
		sdi->model = g_strdup("Logic");
		sdi->serial_num = g_strdup((const char *)serial_number);
		sdi->connection_id = g_strdup(connection_id);
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(
			libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);
		devc = g_malloc0(sizeof(struct dev_context));
		devc->config.vid = des.idVendor;
		devc->config.pid = des.idProduct;
		devc->voltage_threshold = VREF_DEFAULT;
		devc->config.speed = libusb_get_device_speed(devlist[i]);
		sdi->priv = devc;

		if (devc->config.speed < LIBUSB_SPEED_HIGH) {
			sr_err("USB link too slow. Avoiding this device.");
			g_free(devc);
			g_free(sdi);
			continue;
		} else if (devc->config.speed == LIBUSB_SPEED_HIGH)
			sr_warn("USB running in HighSpeed mode. Expect "
				"degraded performance.");

		res = probe_device(sdi, drvc, devlist[i]);
		if (res != SR_OK) {
			g_free(devc);
			g_free(sdi);
			continue;
		}

		devices = g_slist_append(devices, sdi);
	}

	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *const usb = sdi->conn;
	struct drv_context *const drvc = sdi->driver->context;
	int ret, fail_count;
	gboolean reprogram_fpga;

	reprogram_fpga = FALSE;

	if (sdi->status == SR_ST_INITIALIZING)
		sr_info("Waiting for device to reappear...");

	fail_count = 0;

	do {
		if (fail_count >= MCU_CHECK_COUNT) {
			sr_err("Timeout waiting for device.");
			sdi->status = SR_ST_NOT_FOUND;
			return SR_ERR_TIMEOUT;
		}

		ret = px_logic_dev_open(sdi);
		if (sdi->status == SR_ST_INITIALIZING && ret != SR_OK) {
			fail_count++;
			g_usleep(MCU_CHECK_PERIOD_US);
			continue;
		} else if (ret != SR_OK) {
			sr_err("Unable to open device.");
			return SR_ERR;
		}

		if (sdi->status == SR_ST_INITIALIZING) {
			sr_info("Device reappeared.");
			reprogram_fpga = TRUE;
			sdi->status = SR_ST_INACTIVE;
		}
	} while (sdi->status == SR_ST_INITIALIZING);

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE_MAIN);
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

	/* FPGA initialization and register pull. */
	ret = px_logic_probe_fpga(drvc->sr_ctx, usb->devhdl, reprogram_fpga);
	if (ret != SR_OK)
		return ret;

	ret = px_logic_receive_config(sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *const usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	sr_info("Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE_MAIN);
	libusb_release_interface(usb->devhdl, USB_INTERFACE_MAIN);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;

	return SR_OK;
}

static int config_get_general(uint32_t key, GVariant **data,
			      const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	struct sr_usb_dev_inst *usb;
	int ret;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 255)
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
			return SR_ERR;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	case SR_CONF_CONTINUOUS:
		*data = g_variant_new_boolean(devc->streaming);
		break;
	case SR_CONF_FILTER:
		*data = g_variant_new_boolean(devc->filter);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_tuple_double(devc->voltage_threshold,
					      devc->voltage_threshold);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_string(clock_edges[devc->invert_clock]);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string(trigger_slopes[devc->ext_slope]);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_get_pwm(uint32_t key, GVariant **data,
			  const struct sr_dev_inst *sdi, uint8_t index)
{
	struct dev_context *const devc = sdi->priv;

	int ret;

	if (index >= NUM_PWM_CHANNELS)
		return SR_ERR_ARG;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(devc->pwm[index].enabled);
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_double(devc->pwm[index].freq);
		break;
	case SR_CONF_DUTY_CYCLE:
		*data = g_variant_new_double(devc->pwm[index].duty * 100.0);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set_general(uint32_t key, GVariant *data,
			      const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	int ret, idx;
	double l, h;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_CONTINUOUS:
		devc->streaming = g_variant_get_boolean(data);
		break;
	case SR_CONF_FILTER:
		devc->filter = g_variant_get_boolean(data);
		break;
	case SR_CONF_SAMPLERATE:
		devc->samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &l, &h);
		devc->voltage_threshold = l;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_CLOCK_EDGE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(clock_edges));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->invert_clock = !!idx;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->ext_slope = idx;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_set_pwm(uint32_t key, GVariant *data,
			  const struct sr_dev_inst *sdi, uint8_t index)
{
	struct dev_context *const devc = sdi->priv;

	if (index >= NUM_PWM_CHANNELS)
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_ENABLED:
		devc->pwm[index].enabled = g_variant_get_boolean(data);
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		devc->pwm[index].freq = g_variant_get_double(data);
		break;
	case SR_CONF_DUTY_CYCLE:
		devc->pwm[index].duty = g_variant_get_double(data) / 100.0;
		break;
	default:
		return SR_ERR_NA;
	}

	return px_logic_send_config_pwm(sdi, index);
}

static int config_list_general(uint32_t key, GVariant **data,
			       const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	ret = SR_OK;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, NULL, scanopts, drvopts,
				       devopts);
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_min_max_step_thresholds(VREF_MIN, VREF_MAX,
							 VREF_STEP);
		break;
	case SR_CONF_SAMPLERATE:
		if (sdi == NULL) {
			sr_err("Listing options of SR_CONF_SAMPLERATE without "
			       "sdi. This should not happen.");
			return SR_ERR_NA;
		}

		devc = sdi->priv;
		*data = std_gvar_samplerates(
			samplerates,
			variant_samplerate_cutoff[devc->config.variant]);
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(clock_edges));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_slopes));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_list_pwm(uint32_t key, GVariant **data,
			   const struct sr_dev_inst *sdi)
{
	(void)sdi;
	int ret;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_pwm));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	if (!cg)
		return config_get_general(key, data, sdi);
	else if (g_strcmp0(cg->name, "PWM0") == 0)
		return config_get_pwm(key, data, sdi, 0);
	else if (g_strcmp0(cg->name, "PWM1") == 0)
		return config_get_pwm(key, data, sdi, 1);

	return SR_ERR_NA;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	if (!cg)
		return config_set_general(key, data, sdi);
	else if (g_strcmp0(cg->name, "PWM0") == 0)
		return config_set_pwm(key, data, sdi, 0);
	else if (g_strcmp0(cg->name, "PWM1") == 0)
		return config_set_pwm(key, data, sdi, 1);

	return SR_ERR_NA;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	if (!cg)
		return config_list_general(key, data, sdi);
	else if (g_strcmp0(cg->name, "PWM0") == 0)
		return config_list_pwm(key, data, sdi);
	else if (g_strcmp0(cg->name, "PWM1") == 0)
		return config_list_pwm(key, data, sdi);

	return SR_ERR_NA;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	return px_logic_acquisition_start(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	return px_logic_acquisition_stop(sdi);
}

static struct sr_dev_driver px_logic_driver_info = {
	.name = "px-logic",
	.longname = "PX Logic",
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
SR_REGISTER_DEV_DRIVER(px_logic_driver_info);
