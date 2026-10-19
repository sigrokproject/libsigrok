/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Miguel Angel Ajo Pelayo <miguelangel@ajo.es>
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
#include <libusb.h>
#include "protocol.h"

/*
 * Intronix LogicPort LA1034: 34 channels (D0-D31, CLK1, CLK2), 2048
 * samples deep, up to 500MHz timing or 200MHz state mode.
 *
 * Hardware features without a sigrok equivalent, and thus not exposed:
 * trigger occurrence counts, durations and value (bus range) terms, the
 * "A or B" and "A and B in any order" trigger combinations, the state
 * mode qualifier and setup/hold sampling points, per channel input
 * inversion, the frequency counter and live input activity.
 */

#define USB_STR_MAXLEN		64

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RLE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_EXTERNAL_CLOCK_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const char *clock_sources[] = {
	"CLK1", "CLK2",
};

static const char *clock_edges[] = {
	"rising", "falling",
};

static const struct logicport_rate *find_rate(uint64_t samplerate)
{
	size_t i;

	for (i = 0; i < logicport_rates_count; i++)
		if (logicport_rates[i].samplerate == samplerate)
			return &logicport_rates[i];

	return NULL;
}

static gboolean conn_matches(GSList *conn_devices, libusb_device *dev)
{
	const struct sr_usb_dev_inst *usb;
	GSList *l;

	for (l = conn_devices; l; l = l->next) {
		usb = l->data;
		if (usb->bus == libusb_get_bus_number(dev) &&
		    usb->address == libusb_get_device_address(dev))
			return TRUE;
	}

	return FALSE;
}

static struct sr_dev_inst *scan_device(struct ftdi_context *ftdic,
	libusb_device *dev)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	char vendor[USB_STR_MAXLEN], model[USB_STR_MAXLEN];
	char serial[USB_STR_MAXLEN], name[8];
	int i, ret;

	ret = ftdi_usb_get_strings(ftdic, dev, vendor, sizeof(vendor),
		model, sizeof(model), serial, sizeof(serial));
	if (ret < 0) {
		sr_dbg("Failed to get USB strings (%d): %s.",
			ret, ftdi_get_error_string(ftdic));
		serial[0] = '\0';
	} else if (strcmp(model, LOGICPORT_USB_DESC)) {
		sr_dbg("Unexpected product string '%s'.", model);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	devc->usb_bus = libusb_get_bus_number(dev);
	devc->usb_address = libusb_get_device_address(dev);
	devc->segment = -1;
	devc->fpga_image = -1;
	devc->rate = find_rate(SR_MHZ(100));
	devc->capture_ratio = 50;
	devc->threshold = 1.5;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Intronix");
	sdi->model = g_strdup("LogicPort");
	sdi->serial_num = serial[0] ? g_strdup(serial) : NULL;
	sdi->connection_id = g_strdup_printf("%u.%u",
		devc->usb_bus, devc->usb_address);
	sdi->priv = devc;

	for (i = 0; i < 32; i++) {
		g_snprintf(name, sizeof(name), "D%d", i);
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, name);
	}
	sr_channel_new(sdi, LOGICPORT_CH_CLK1, SR_CHANNEL_LOGIC, TRUE, "CLK1");
	sr_channel_new(sdi, LOGICPORT_CH_CLK2, SR_CHANNEL_LOGIC, TRUE, "CLK2");

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_config *src;
	struct ftdi_context *ftdic;
	struct ftdi_device_list *devlist, *curdev;
	struct sr_dev_inst *sdi;
	GSList *devices, *conn_devices, *l;
	const char *conn;
	int ret;

	drvc = di->context;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN)
			conn = g_variant_get_string(src->data, NULL);
	}
	conn_devices = NULL;
	if (conn) {
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
		if (!conn_devices)
			return NULL;
	}

	ftdic = ftdi_new();
	if (!ftdic) {
		sr_err("Failed to initialize libftdi.");
		g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
		return NULL;
	}

	devices = NULL;
	devlist = NULL;
	ret = ftdi_usb_find_all(ftdic, &devlist, LOGICPORT_USB_VENDOR,
		LOGICPORT_USB_PRODUCT);
	if (ret < 0)
		sr_err("Failed to list devices (%d): %s.",
			ret, ftdi_get_error_string(ftdic));
	for (curdev = devlist; ret > 0 && curdev; curdev = curdev->next) {
		if (conn && !conn_matches(conn_devices, curdev->dev))
			continue;
		sdi = scan_device(ftdic, curdev->dev);
		devices = g_slist_append(devices, sdi);
	}

	ftdi_list_free(&devlist);
	ftdi_free(ftdic);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	devc->ftdic = ftdi_new();
	if (!devc->ftdic) {
		sr_err("Failed to initialize libftdi.");
		return SR_ERR;
	}

	ret = logicport_open(devc);
	if (ret != SR_OK) {
		ftdi_free(devc->ftdic);
		devc->ftdic = NULL;
		return ret;
	}

	logicport_read_calibration(devc);

	/*
	 * An unconfigured FPGA (or one that was left halfway through
	 * reconfiguration) doesn't drain the FIFO, so retrying won't help.
	 * That's not an error, it gets configured when acquisition starts.
	 */
	devc->fpga_image = -1;
	devc->threshold_dac = -1;
	if (logicport_resync(devc, FALSE, 1) == SR_OK)
		logicport_probe_image(devc);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	if (!devc->ftdic)
		return SR_ERR_BUG;

	logicport_close(devc);
	ftdi_free(devc->ftdic);
	devc->ftdic = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	const struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->rate->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->limit_frames);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_tuple_double(devc->threshold, devc->threshold);
		break;
	case SR_CONF_RLE:
		*data = g_variant_new_boolean(devc->rle);
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		*data = g_variant_new_boolean(devc->external_clock);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		*data = g_variant_new_string(clock_sources[devc->clock_source]);
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_string(clock_edges[devc->clock_edge]);
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
	const struct logicport_rate *rate;
	double low, high, threshold;
	uint64_t ratio;
	int idx;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		rate = find_rate(g_variant_get_uint64(data));
		if (!rate)
			return SR_ERR_SAMPLERATE;
		devc->rate = rate;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = g_variant_get_uint64(data);
		break;
	case SR_CONF_CAPTURE_RATIO:
		ratio = g_variant_get_uint64(data);
		if (ratio > 100)
			return SR_ERR_ARG;
		devc->capture_ratio = ratio;
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &low, &high);
		threshold = (low + high) / 2.0;
		if (threshold < LOGICPORT_THRESHOLD_MIN ||
		    threshold > LOGICPORT_THRESHOLD_MAX)
			return SR_ERR_ARG;
		devc->threshold = threshold;
		break;
	case SR_CONF_RLE:
		devc->rle = g_variant_get_boolean(data);
		break;
	case SR_CONF_EXTERNAL_CLOCK:
		devc->external_clock = g_variant_get_boolean(data);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(clock_sources));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->clock_source = idx;
		break;
	case SR_CONF_CLOCK_EDGE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(clock_edges));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->clock_edge = idx;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	uint64_t samplerates[32];
	size_t i;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		for (i = 0; i < logicport_rates_count && i < ARRAY_SIZE(samplerates); i++)
			samplerates[i] = logicport_rates[i].samplerate;
		*data = std_gvar_samplerates(samplerates, i);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_min_max_step_thresholds(LOGICPORT_THRESHOLD_MIN,
			LOGICPORT_THRESHOLD_MAX, LOGICPORT_THRESHOLD_STEP);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(clock_sources));
		break;
	case SR_CONF_CLOCK_EDGE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(clock_edges));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* Load the FPGA image the current settings need, then write the settings. */
static int prepare(const struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	int ret;

	drvc = sdi->driver->context;
	devc = sdi->priv;

	ret = logicport_configure_fpga(drvc->sr_ctx, devc,
		logicport_select_image(devc));
	if (ret != SR_OK)
		return ret;

	return logicport_program(devc);
}

/* Bit k set for each enabled channel k; all channels if none is enabled. */
static uint64_t enabled_channels_mask(const struct sr_dev_inst *sdi)
{
	const struct sr_channel *ch;
	const GSList *l;
	uint64_t mask;

	mask = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->enabled && ch->index < LOGICPORT_NUM_CHANNELS)
			mask |= UINT64_C(1) << ch->index;
	}

	return mask ? mask : LOGICPORT_ALL_CHANNELS;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	devc->compression_mask = enabled_channels_mask(sdi);

	if (devc->rle && !devc->external_clock &&
	    devc->rate->samplerate > LOGICPORT_RLE_MAX_RATE) {
		sr_err("RLE is only available up to %" PRIu64 " MHz.",
			LOGICPORT_RLE_MAX_RATE / SR_MHZ(1));
		return SR_ERR_ARG;
	}

	ret = logicport_convert_trigger(sdi);
	if (ret != SR_OK)
		return ret;

	ret = prepare(sdi);
	if (ret == SR_ERR_IO || ret == SR_ERR_TIMEOUT) {
		/* The link may be out of step, resync and try once more. */
		sr_dbg("Retrying acquisition setup after resync.");
		if (logicport_resync(devc, TRUE, 1) == SR_OK)
			logicport_probe_image(devc);
		ret = prepare(sdi);
	}
	if (ret == SR_OK)
		ret = logicport_start(devc);
	if (ret != SR_OK) {
		sr_err("Failed to start acquisition.");
		return ret;
	}
	devc->acq_running = TRUE;
	devc->frames_done = 0;
	sr_dbg("Acquisition armed.");

	std_session_send_df_header(sdi);

	return sr_session_source_add(sdi->session, -1, 0, 10,
		logicport_receive_data, (void *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->acq_running) {
		logicport_halt(devc);
		devc->acq_running = FALSE;
	}

	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver intronix_logicport_driver_info = {
	.name = "intronix-logicport",
	.longname = "Intronix LogicPort",
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
SR_REGISTER_DEV_DRIVER(intronix_logicport_driver_info);
