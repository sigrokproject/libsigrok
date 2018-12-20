/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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
 * TODO
 * - Data acquisition works, but triggers either seem to not take effect,
 *   or the trigger position is not in the expected spot according to the
 *   user provided acquisition parameters. More research is required. The
 *   bitmasks for enable/level/edge as well as the magic 16bit values for
 *   position may need adjustment.
 * - The trigger position logic assumes that capture ratio specs are in
 *   the range of 0-6%, which gets mapped to none/10%/50%/90%/+1W/+2W/+3W
 *   choices. This avoids issues with applications which lack support for
 *   non-contiguous discrete supported values, and values outside of the
 *   0-100% range. This is considered acceptable, to avoid the necessity
 *   to extend common infrastructure to an unusual feature of a single
 *   device of limited popularity. Just needs to get communicated to users.
 * - When a formula for the trigger position values in the SETUP packet
 *   is found, the driver may accept arbitrary values between 0-100%, but
 *   still could not express the "plus N windows" settings. Though that'd
 *   be a rather useful feature considering the very short memory depth.
 * - The current implementation assumes externally provided Vdd, without
 *   which input levels won't get detected. A future implementation could
 *   optionally power Vdd from the PICkit2 itself, according to a user
 *   provided configuration value.
 * - The current implementation silently accepts sample count limits beyond
 *   1024, just won't provide more than 1024 samples to the session. A
 *   future implementation could cap the settings upon reception. Apps
 *   like PulseView may not be able to specify 1024, and pass 1000 or
 *   2000 instead (the latter results in 1024 getting used).
 * - The manual suggests that users can assign names to devices. The
 *   current implementation supports conn= specs with USB VID:PID pairs
 *   or bus/address numbers. A future implementation could scan for user
 *   assigned names as well (when the opcode to query the name was found).
 * - The "attach kernel driver" support code probably should move to a
 *   common location, instead of getting repeated across several drivers.
 * - Diagnostics may benefit from cleanup.
 */

#include <config.h>
#include <libusb.h>
#include <string.h>
#include "protocol.h"

#define PICKIT2_VENDOR_NAME	"Microchip"
#define PICKIT2_PRODUCT_NAME	"PICkit2"

#define PICKIT2_DEFAULT_ADDRESS	"04d8.0033"
#define PICKIT2_USB_INTERFACE	0

static struct sr_dev_driver microchip_pickit2_driver_info;

static const char *pickit2_channel_names[] = {
	"pin4", "pin5", "pin6",
};

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
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

/*
 * Note that a list of 0, 10, 50, 90, 91, 92, 93, would have been nicer
 * from a user's perspective, but applications may not support a set of
 * discrete supported values, and 91+ is as much of a hack to work around
 * the "0-100%" limitation. So let's map those 0-6 "percent" to the vendor
 * app's 10/50/90/1W/2W/3W locations.
 */
static const uint64_t captureratios[] = {
	0, 1, 2, 3, 4, 5, 6,
};

static const uint64_t samplerates[] = {
	SR_KHZ(5),
	SR_KHZ(10),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	const char *conn;
	GSList *l, *devices, *usb_devices;
	struct sr_config *cfg;
	struct sr_usb_dev_inst *usb;
	struct sr_dev_inst *sdi;
	struct sr_channel_group *cg;
	size_t ch_count, ch_idx;
	struct sr_channel *ch;
	struct dev_context *devc;

	drvc = di->context;

	conn = PICKIT2_DEFAULT_ADDRESS;
	for (l = options; l; l = l->next) {
		cfg = l->data;
		switch (cfg->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(cfg->data, NULL);
			break;
		}
	}

	devices = NULL;
	usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	if (!usb_devices)
		return NULL;

	for (l = usb_devices; l; l = l->next) {
		usb = l->data;

		/* Create the device instance. */
		sdi = g_malloc0(sizeof(*sdi));
		devices = g_slist_append(devices, sdi);
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(PICKIT2_VENDOR_NAME);
		sdi->model = g_strdup(PICKIT2_PRODUCT_NAME);
		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;
		sdi->connection_id = g_strdup(conn);

		/* Create the logic channels group. */
		cg = g_malloc0(sizeof(*cg));
		sdi->channel_groups = g_slist_append(NULL, cg);
		cg->name = g_strdup("Logic");
		ch_count = ARRAY_SIZE(pickit2_channel_names);
		for (ch_idx = 0; ch_idx < ch_count; ch_idx++) {
			ch = sr_channel_new(sdi, ch_idx, SR_CHANNEL_LOGIC,
				TRUE, pickit2_channel_names[ch_idx]);
			cg->channels = g_slist_append(cg->channels, ch);
		}

		/*
		 * Create the device context. Pre-select the highest
		 * samplerate and the deepest sample count available.
		 */
		devc = g_malloc0(sizeof(*devc));
		sdi->priv = devc;
		devc->samplerates = samplerates;
		devc->num_samplerates = ARRAY_SIZE(samplerates);
		devc->curr_samplerate_idx = devc->num_samplerates - 1;
		devc->captureratios = captureratios;
		devc->num_captureratios = ARRAY_SIZE(captureratios);
		devc->curr_captureratio_idx = 0;
		devc->sw_limits.limit_samples = PICKIT2_SAMPLE_COUNT;
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;
	di = sdi->driver;
	drvc = di->context;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (ret < 0)
		return SR_ERR;

	if (libusb_kernel_driver_active(usb->devhdl, PICKIT2_USB_INTERFACE) == 1) {
		ret = libusb_detach_kernel_driver(usb->devhdl, PICKIT2_USB_INTERFACE);
		if (ret < 0) {
			sr_err("Canot detach kernel driver: %s.",
				libusb_error_name(ret));
			return SR_ERR;
		}
		devc->detached_kernel_driver = TRUE;
	}

	ret = libusb_claim_interface(usb->devhdl, PICKIT2_USB_INTERFACE);
	if (ret < 0) {
		sr_err("Cannot claim interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	int ret;

	usb = sdi->conn;
	devc = sdi->priv;

	if (!usb->devhdl)
		return SR_OK;

	ret = libusb_release_interface(usb->devhdl, PICKIT2_USB_INTERFACE);
	if (ret) {
		sr_err("Cannot release interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if (devc->detached_kernel_driver) {
		ret = libusb_attach_kernel_driver(usb->devhdl, PICKIT2_USB_INTERFACE);
		if (ret) {
			sr_err("Cannot attach kernel driver: %s.",
				libusb_error_name(ret));
			return SR_ERR;
		}
		devc->detached_kernel_driver = FALSE;
	}

	libusb_close(usb->devhdl);
	sdi->conn = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	uint64_t rate, ratio;

	(void)cg;

	devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		return SR_OK;
	case SR_CONF_SAMPLERATE:
		rate = devc->samplerates[devc->curr_samplerate_idx];
		*data = g_variant_new_uint64(rate);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_get(&devc->sw_limits, key, data);
	case SR_CONF_CAPTURE_RATIO:
		ratio = devc->captureratios[devc->curr_captureratio_idx];
		*data = g_variant_new_uint64(ratio);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int idx;

	(void)cg;

	devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_ARG;
		idx = std_u64_idx(data, devc->samplerates, devc->num_samplerates);
		if (idx < 0)
			return SR_ERR_ARG;
		devc->curr_samplerate_idx = idx;
		return SR_OK;
	case SR_CONF_CAPTURE_RATIO:
		if (!devc)
			return SR_ERR_ARG;
		idx = std_u64_idx(data, devc->captureratios, devc->num_captureratios);
		if (idx >= 0)
			devc->curr_captureratio_idx = idx;
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->sw_limits, key, data);
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_NA;
		*data = std_gvar_samplerates(devc->samplerates, devc->num_samplerates);
		return SR_OK;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		return SR_OK;
	case SR_CONF_CAPTURE_RATIO:
		*data = std_gvar_array_u64(ARRAY_AND_SIZE(captureratios));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	GSList *l;
	size_t idx;
	int ret;

	devc = sdi->priv;

	/*
	 * Query triggers, translate the more complex caller spec to
	 * "flat" internal variables, to simplify the construction of
	 * the SETUP packet elsewhere. This driver supports a single
	 * stage, with match conditions for one or multiple channels.
	 */
	memset(&devc->triggers, 0, sizeof(devc->triggers));
	trigger = sr_session_trigger_get(sdi->session);
	if (trigger) {
		if (g_slist_length(trigger->stages) > 1)
			return SR_ERR_NA;
		stage = g_slist_nth_data(trigger->stages, 0);
		if (!stage)
			return SR_ERR_ARG;
		for (l = stage->matches; l; l = l->next) {
			match = l->data;
			if (!match->match)
				continue;
			if (!match->channel->enabled)
				continue;
			idx = match->channel->index;
			devc->triggers[idx] = match->match;
		}
		sr_dbg("acq start: trigger specs: %x/%x/%x",
			devc->triggers[0], devc->triggers[1],
			devc->triggers[2]);
	}
	devc->trigpos = trigger ? devc->curr_captureratio_idx : 0;

	/* Have the SETUP packet sent, then poll for the status. */
	devc->state = STATE_CONF;
	ret = microchip_pickit2_setup_trigger(sdi);
	if (ret) {
		devc->state = STATE_IDLE;
		return ret;
	}
	devc->state = STATE_WAIT;

	std_session_send_df_header(sdi);
	sr_session_source_add(sdi->session, -1, 0, 20,
		microchip_pickit2_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	if (devc->state < STATE_CONF)
		return SR_OK;

	/*
	 * Keep up the acquisition until either data becomes available
	 * (according to the previously configured trigger condition),
	 * or until the user cancels the acquisition by pressing the
	 * device's button. This is a firmware limitation which the
	 * vendor software "suffers from" as well.
	 */
	if (devc->state == STATE_WAIT) {
		sr_err("Cannot terminate by software, need either data trigger or cancel button.");
		return SR_OK;
	}

	if (devc->state > STATE_CONF) {
		std_session_send_df_end(sdi);
	}
	sr_session_source_remove(sdi->session, -1);
	devc->state = STATE_IDLE;

	return SR_OK;
}

static struct sr_dev_driver microchip_pickit2_driver_info = {
	.name = "microchip-pickit2",
	.longname = PICKIT2_VENDOR_NAME " " PICKIT2_PRODUCT_NAME,
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
SR_REGISTER_DEV_DRIVER(microchip_pickit2_driver_info);
