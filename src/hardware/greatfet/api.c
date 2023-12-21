/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Katherine J. Temkin <k@ktemkin.com>
 * Copyright (C) 2019 Mikaela Szekely <qyriad@gmail.com>
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include "config.h"

#include "protocol.h"

#define DEFAULT_CONN		"1d50.60e6"
#define CONTROL_INTERFACE	0
#define SAMPLES_INTERFACE	1

#define VENDOR_TEXT		"Great Scott Gadgets"
#define MODEL_TEXT		"GreatFET"

#define BUFFER_SIZE		(4 * 1024 * 1024)

#define DEFAULT_SAMPLERATE	SR_KHZ(34000)
#define BANDWIDTH_THRESHOLD	(SR_MHZ(42) * 8)

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_PROBE_NAMES,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_GET,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	/* EMPTY */
};

static const char *channel_names[] = {
	"SGPIO0", "SGPIO1", "SGPIO2", "SGPIO3",
	"SGPIO4", "SGPIO5", "SGPIO6", "SGPIO7",
	"SGPIO8", "SGPIO9", "SGPIO10", "SGPIO11",
	"SGPIO12", "SGPIO13", "SGPIO14", "SGPIO15",
};

/*
 * The seemingly odd samplerates result from the 204MHz base clock and
 * a 12bit integer divider. Theoretical minimum could be 50kHz but we
 * don't bother to provide so low a selection item here.
 *
 * When users specify different samplerates, device firmware will pick
 * the minimum rate which satisfies the user's request.
 */
static const uint64_t samplerates[] = {
	SR_KHZ(1000),	/*   1.0MHz */
	SR_KHZ(2000),	/*   2.0MHz */
	SR_KHZ(4000),	/*   4.0MHz */
	SR_KHZ(8500),	/*   8.5MHz */
	SR_KHZ(10200),	/*  10.2MHz */
	SR_KHZ(12000),	/*  12.0MHz */
	SR_KHZ(17000),	/*  17.0MHz */
	SR_KHZ(20400),	/*  20.4MHz, the maximum for 16 channels */
	SR_KHZ(25500),	/*  25.5MHz */
	SR_KHZ(34000),	/*  34.0MHz */
	SR_KHZ(40800),	/*  40.8MHz, the maximum for 8 channels */
	SR_KHZ(51000),	/*  51.0MHz */
	SR_KHZ(68000),	/*  68.0MHz, the maximum for 4 channels */
	SR_KHZ(102000),	/* 102.0MHz, the maximum for 2 channels */
	SR_KHZ(204000),	/* 204.0MHz, the maximum for 1 channel */
};

static void greatfet_free_devc(struct dev_context *devc)
{

	if (!devc)
		return;

	if (devc->sdi)
		devc->sdi->priv = NULL;

	g_string_free(devc->usb_comm_buffer, TRUE);
	g_free(devc->firmware_version);
	g_free(devc->serial_number);
	sr_free_probe_names(devc->channel_names);
	feed_queue_logic_free(devc->acquisition.feed_queue);
	g_free(devc->transfers.transfers);
	g_free(devc->transfers.transfer_buffer);
	/*
	 * USB transfers should not have been allocated when we get here
	 * during device probe/scan, or during shutdown after acquisition
	 * has terminated.
	 */

	g_free(devc);
}

static void greatfet_free_sdi(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;

	if (!sdi)
		return;

	usb = sdi->conn;
	sdi->conn = NULL;
	if (usb && usb->devhdl)
		sr_usb_close(usb);
	sr_usb_dev_inst_free(usb);

	devc = sdi->priv;
	sdi->priv = NULL;
	greatfet_free_devc(devc);

	sr_dev_inst_free(sdi);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_context *ctx;
	GSList *devices;
	const char *conn, *probe_names;
	const char *want_snr;
	struct sr_config *src;
	GSList *conn_devices, *l;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	gboolean skip_device;
	struct libusb_device *dev;
	struct libusb_device_descriptor des;
	char *match;
	char serno_txt[64], conn_id[64];
	int ret;
	size_t ch_off, ch_max, ch_idx;
	gboolean enabled;
	struct sr_channel *ch;
	struct sr_channel_group *cg;

	drvc = di->context;
	ctx = drvc->sr_ctx;

	devices = NULL;

	/* Accept user specs for conn= and probe names. */
	conn = DEFAULT_CONN;
	probe_names = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_PROBE_NAMES:
			probe_names = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	/*
	 * By default search for all devices with the expected VID/PID.
	 * Accept external specs in either "bus.addr" or "vid.pid" form.
	 * As an alternative accept "sn=..." specs and keep using the
	 * default VID/PID in that case. This should result in maximum
	 * usability while still using a maximum amount of common code.
	 */
	want_snr = NULL;
	if (g_str_has_prefix(conn, "sn=")) {
		want_snr = conn + strlen("sn=");
		conn = DEFAULT_CONN;
		sr_info("Searching default %s and serial number %s.",
			conn, want_snr);
	}
	conn_devices = sr_usb_find(ctx->libusb_ctx, conn);
	if (!conn_devices)
		return devices;

	/*
	 * Iterate over all devices that have the matching VID/PID.
	 * Skip those which we cannot open. Skip those which don't
	 * match additional serial number conditions. Allocate the
	 * structs for found devices "early", to re-use common code
	 * for communication to the firmware. Release these structs
	 * when identification fails or the device does not match.
	 *
	 * Notice that the scan for devices uses the USB string for
	 * the serial number, and does a weak check (partial match).
	 * This allows users to either use lsusb(8) or gf(1) output
	 * as well as match lazily when only part of the serial nr is
	 * known and becomes unique. Matching against serial nr and
	 * finding multiple devices is as acceptable, just might be a
	 * rare use case. Failure in this stage is silent, there are
	 * legal reasons why we cannot access a device during scan.
	 *
	 * Once a device was found usable, we get its serial number
	 * and version details by means of firmware communication.
	 * To verify that the firmware is operational and that the
	 * protocol works to a minimum degree. And to present data
	 * in --scan output which matches the vendor's gf(1) utility.
	 * This version detail is _not_ checked against conn= specs
	 * because users may specify the longer text string with
	 * more leading digits from lsusb(8) output. That test would
	 * fail when executed against the shorter firmware output.
	 */
	for (l = conn_devices; l; l = l->next) {
		usb = l->data;

		ret = sr_usb_open(ctx->libusb_ctx, usb);
		if (ret != SR_OK)
			continue;

		skip_device = FALSE;
		if (want_snr) do {
			dev = libusb_get_device(usb->devhdl);
			ret = libusb_get_device_descriptor(dev, &des);
			if (ret != 0 || !des.iSerialNumber) {
				skip_device = TRUE;
				break;
			}
			ret = libusb_get_string_descriptor_ascii(usb->devhdl,
				des.iSerialNumber,
				(uint8_t *)serno_txt, sizeof(serno_txt));
			if (ret < 0) {
				skip_device = TRUE;
				break;
			}
			match = strstr(serno_txt, want_snr);
			skip_device = !match;
			sr_dbg("got serno %s, checking %s, match %d",
				serno_txt, want_snr, !!match);
		} while (0);
		if (skip_device) {
			sr_usb_close(usb);
			continue;
		}

		sdi = g_malloc0(sizeof(*sdi));
		sdi->conn = usb;
		sdi->inst_type = SR_INST_USB;
		sdi->status = SR_ST_INACTIVE;
		devc = g_malloc0(sizeof(*devc));
		sdi->priv = devc;
		devc->sdi = sdi;
		devc->usb_comm_buffer = NULL;

		/*
		 * Get the serial number by way of device communication.
		 * Get the firmware version. Failure is fatal.
		 */
		ret = greatfet_get_serial_number(sdi);
		if (ret != SR_OK || !devc->serial_number) {
			sr_err("Cannot get serial number.");
			greatfet_free_sdi(sdi);
			continue;
		}
		ret = greatfet_get_version_number(sdi);
		if (ret != SR_OK || !devc->firmware_version) {
			sr_err("Cannot get firmware version.");
			greatfet_free_sdi(sdi);
			continue;
		}

		/* Continue filling in sdi and devc. */
		snprintf(conn_id, sizeof(conn_id), "%u.%u",
			usb->bus, usb->address);
		sdi->connection_id = g_strdup(conn_id);
		sr_usb_close(usb);

		sdi->vendor = g_strdup(VENDOR_TEXT);
		sdi->model = g_strdup(MODEL_TEXT);
		sdi->version = g_strdup(devc->firmware_version);
		sdi->serial_num = g_strdup(devc->serial_number);

		/* Create the "Logic" channel group. */
		ch_off = 0;
		ch_max = ARRAY_SIZE(channel_names);
		devc->channel_names = sr_parse_probe_names(probe_names,
			channel_names, ch_max, ch_max, &ch_max);
		devc->channel_count = ch_max;
		cg = sr_channel_group_new(sdi, "Logic", NULL);
		for (ch_idx = 0; ch_idx < ch_max; ch_idx++) {
			enabled = ch_idx < 8;
			ch = sr_channel_new(sdi, ch_off,
				SR_CHANNEL_LOGIC, enabled,
				devc->channel_names[ch_idx]);
			ch_off++;
			cg->channels = g_slist_append(cg->channels, ch);
		}
		devc->feed_unit_size = (ch_max + 8 - 1) / 8;

		sr_sw_limits_init(&devc->sw_limits);
		devc->samplerate = DEFAULT_SAMPLERATE;
		devc->acquisition.bandwidth_threshold = BANDWIDTH_THRESHOLD;
		devc->acquisition.control_interface = CONTROL_INTERFACE;
		devc->acquisition.samples_interface = SAMPLES_INTERFACE;
		devc->acquisition.acquisition_state = ACQ_IDLE;

		devices = g_slist_append(devices, sdi);
	}
	g_slist_free(conn_devices);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct sr_context *ctx;
	struct sr_usb_dev_inst *usb;

	di = sdi->driver;
	drvc = di->context;
	ctx = drvc->sr_ctx;
	usb = sdi->conn;

	return sr_usb_open(ctx->libusb_ctx, usb);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct dev_acquisition_t *acq;

	if (!sdi)
		return SR_ERR_ARG;
	usb = sdi->conn;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	acq = &devc->acquisition;

	greatfet_release_resources(sdi);

	if (!usb->devhdl)
		return SR_ERR_BUG;

	sr_info("Closing device on %s interface %d.",
		sdi->connection_id, acq->control_interface);
	if (acq->control_interface_claimed) {
		libusb_release_interface(usb->devhdl, acq->control_interface);
		acq->control_interface_claimed = FALSE;
	}
	sr_usb_close(usb);

	return SR_OK;
}

static void clear_helper(struct dev_context *devc)
{
	greatfet_free_devc(devc);
}

static int dev_clear(const struct sr_dev_driver *driver)
{
	return std_dev_clear_with_callback(driver,
		(std_dev_clear_callback)clear_helper);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	/* Handle requests for the "Logic" channel group. */
	if (cg) {
		switch (key) {
		default:
			return SR_ERR_NA;
		}
	}

	/* Handle global options for the device. */
	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->connection_id)
			return SR_ERR_NA;
		*data = g_variant_new_string(sdi->connection_id);
		return SR_OK;
	case SR_CONF_CONTINUOUS:
		*data = g_variant_new_boolean(TRUE);
		return SR_OK;
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_NA;
		*data = g_variant_new_uint64(devc->samplerate);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		if (!devc)
			return SR_ERR_NA;
		return sr_sw_limits_config_get(&devc->sw_limits, key, data);
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = sdi->priv;

	/* Handle requests for the "Logic" channel group. */
	if (cg) {
		switch (key) {
		default:
			return SR_ERR_NA;
		}
	}

	/* Handle global options for the device. */
	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_NA;
		devc->samplerate = g_variant_get_uint64(data);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		if (!devc)
			return SR_ERR_NA;
		return sr_sw_limits_config_set(&devc->sw_limits, key, data);
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	/* Handle requests for the "Logic" channel group. */
	if (cg) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			if (ARRAY_SIZE(devopts_cg) == 0)
				return SR_ERR_NA;
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				ARRAY_AND_SIZE(devopts_cg),
				sizeof(devopts_cg[0]));
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	/* Handle global options for the device. */
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct sr_context *ctx;
	struct dev_context *devc;
	struct dev_acquisition_t *acq;
	int ret;

	if (!sdi || !sdi->driver || !sdi->priv)
		return SR_ERR_ARG;
	di = sdi->driver;
	drvc = di->context;
	ctx = drvc->sr_ctx;
	devc = sdi->priv;
	acq = &devc->acquisition;

	acq->acquisition_state = ACQ_PREPARE;

	ret = greatfet_setup_acquisition(sdi);
	if (ret != SR_OK)
		return ret;

	if (!acq->feed_queue) {
		acq->feed_queue = feed_queue_logic_alloc(sdi,
			BUFFER_SIZE, devc->feed_unit_size);
		if (!acq->feed_queue) {
			sr_err("Cannot allocate session feed buffer.");
			return SR_ERR_MALLOC;
		}
	}

	sr_sw_limits_acquisition_start(&devc->sw_limits);

	ret = greatfet_start_acquisition(sdi);
	acq->start_req_sent = ret == SR_OK;
	if (ret != SR_OK) {
		greatfet_abort_acquisition(sdi);
		feed_queue_logic_free(acq->feed_queue);
		acq->feed_queue = NULL;
		return ret;
	}
	acq->acquisition_state = ACQ_RECEIVE;

	usb_source_add(sdi->session, ctx, 50,
		greatfet_receive_data, (void *)sdi);

	ret = std_session_send_df_header(sdi);
	acq->frame_begin_sent = ret == SR_OK;
	(void)sr_session_send_meta(sdi, SR_CONF_SAMPLERATE,
		g_variant_new_uint64(acq->capture_samplerate));

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	greatfet_abort_acquisition(sdi);
	return SR_OK;
}

static struct sr_dev_driver greatfet_driver_info = {
	.name = "greatfet",
	.longname = "Great Scott Gadgets GreatFET One",
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
SR_REGISTER_DEV_DRIVER(greatfet_driver_info);
