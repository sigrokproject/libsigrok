/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
 * Copyright (C) 2021 Eric Neulight
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
 * Notes:
 * 1.  This driver was initially written by Gerhard Sittig for the PICkit2, but had several unresolved issues.
 *     Much of his original work remains. Eric Neulight fixed up the sections necessary to make it work
 *     properly with the PICkit2, while also adding support for the PICkit3, and all of their logic analyzer features.
 *     This would have been a much bigger task for Eric, one which he probably would not have bothered to undertake,
 *     had it not been for the existence of Gerhard's initial efforts.  So, thanks to Gerhard.
 *
 * 2.  This driver works with most of the logic analyzer features of the Microchip PICkit2 and PICkit3.
 *     (Don't expect too much from these simple units though, they can only take 1024 samples at up to 1MHz rate on 3 channels.)
 *     Any unimplemented features, such as "trig_count", are due to limitations of sigrok-cli and PulseView to provide
 *     for configuring or setting/getting them.  However, the hooks are there, for instance devc->trig_count is
 *     initiallized to 1, and will operate as expected if changed to any number 1-256, but sigrok does not have a
 *     suitable key or feature for changing it (setting/getting it)...yet.
 *
 * 3.  The PICkit2 comes stock with logic analyzer firmware built-in.  The PICkit3 must be flashed with
 *     the "PICkit3 Programming App and Scripting Tool v3.10" firmware available from Microchip.  At time of
 *     writing it could be found here:
 *           https://microchipdeveloper.com/pickit3:scripttool	
 *           http://ww1.microchip.com/downloads/en/DeviceDoc/PICkit3%20Programmer%20Application%20v3.10.zip
 *
 * 4.  The PICkit3 firmware has a "bug" that does not allow for triggering on a mix of rising and falling
 *     edges across multiple channels.  Selecting only rising edges and no falling edges works as expected.
 *     Selecting only falling edges and no rising edges works as expected.  Because of this "bug", selecting
 *     any falling edge trigger will cause the PICkit3 to treat all selected edge triggers as falling.  However,
 *     this "bug" is typically not a problem, because rarely if ever would anybody want to edge trigger off more
 *     than one channel anyway.
 *
 * 5.  TODO: Maybe at some point in time, when sigrok offers more generic settings, and if the PICkit is still
 *     relevant, it might be nice to be able to read the target voltage, or set the "programming" voltage output
 *     pin.  Also, as mentioned above, the trig_count capability is ready to go, just need to add appropriate
 *     "keys" for setting/getting it.
 */

#include <config.h>
#include <libusb.h>
#include <string.h>
#include "protocol.h"

#define PICKIT_USB_INTERFACE	0

struct PICkitID_struct {
	char VidPid[16];
	char VendorName[16];
	char ProductName[16];
} PICkitIDs[] = {
	{"0000.0000#0",	"Clone",		"PICkit?"},
	{"04D8.0033#2",	"Microchip",	"PICkit2"},
	{"04D8.900A#3",	"Microchip",	"PICkit3"},
	{"", "", ""}
};

static struct sr_dev_driver microchip_pickit_driver_info;

static const char *channel_names[] = {
	"pin4", "pin5", "pin6"
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING
};

static const uint64_t samplerates[] = {
	1000000,
	 500000,
	 250000,
	 200000,
	 125000,
	 100000,
	  62500,
	  50000,
	  40000,
	  31250,
	  25000,
	  20000,
	  15625,
	  12500,
	  10000,
	   8000,
	   6250,
	   5000,
	   4000,
	   3125,
	   2500,
	   2000,
	   1600,
	   1250,
	   1000,
	    800,
	    625,
	    500,
	    400,
	    320,
	    250
};
#define PK2_SAMPLERATES	19 	/* Pk2 uses 8-bit divisor, Pk3 uses 12-bit (x16) divisor */

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc=di->context;
	struct PICkitID_struct *PICkitID=PICkitIDs;
	const char *conn;
	char VidPid[10];
	GSList *l, *devices, *usb_devices;
	struct sr_config *cfg;
	struct sr_usb_dev_inst *usb;
	struct sr_dev_inst *sdi;
	struct sr_channel_group *cg;
	size_t ch_count, ch_idx;
	struct sr_channel *ch;
	struct dev_context *devc;

	conn = NULL;
	for (l = options; l; l = l->next) {
		cfg = l->data;
		switch (cfg->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(cfg->data, NULL);
			break;
		}
	}
	if (!conn) conn=(++PICkitID)->VidPid;

	/* Scan for conn=Vid.Pid#n option first, if given, and then all known PICkitIDs */
	devices = NULL;
	do {
		strncpy(VidPid,conn,9);
		VidPid[9] = 0;
		if ((usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, VidPid))) {
			for (l = usb_devices; l; l = l->next) {
				usb = l->data;

				/* Create the device instance. */
				sdi = g_malloc0(sizeof(*sdi));
				devices = g_slist_append(devices, sdi);
				sdi->status = SR_ST_INACTIVE;
				sdi->vendor = g_strdup(PICkitID->VendorName);
				sdi->model = g_strdup(PICkitID->ProductName);
				sdi->inst_type = SR_INST_USB;
				sdi->conn = usb;
				sdi->connection_id = g_strdup(VidPid);

				/* Create the logic channels group. */
				cg = g_malloc0(sizeof(*cg));
				sdi->channel_groups = g_slist_append(NULL, cg);
				cg->name = g_strdup("Logic");
				ch_count = ARRAY_SIZE(channel_names);
				for (ch_idx = 0; ch_idx < ch_count; ch_idx++) {
					ch = sr_channel_new(sdi, ch_idx, SR_CHANNEL_LOGIC,
						TRUE, channel_names[ch_idx]);
					cg->channels = g_slist_append(cg->channels, ch);
				}

				/*
				 * Create the device context. Pre-select the highest sample rate and other sane defaults.
				 */
				devc = g_malloc0(sizeof(*devc));
				sdi->priv = devc;

				/* Check if this is a PICkit3, otherwise default to PICkit2 for compatibility with original driver */
				devc->isPk3 = false;
				if (strlen(conn)>=11 && conn[9]=='#' && conn[10]=='3' && conn[11]==0)
					devc->isPk3 = true;

				devc->sw_limits.limit_samples = PICKIT_SAMPLE_COUNT;
				devc->samplerates = samplerates;
				devc->num_samplerates = devc->isPk3 ? ARRAY_SIZE(samplerates) : PK2_SAMPLERATES;
				devc->curr_samplerate_idx = 0;
				devc->trig_count = 1;
				devc->captureratio = 1;
				devc->trig_postsamp = (1021*(100-devc->captureratio)+150)/100;
			}
		}
	} while (*(conn=(++PICkitID)->VidPid));

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

	if (libusb_kernel_driver_active(usb->devhdl, PICKIT_USB_INTERFACE) == 1) {
		ret = libusb_detach_kernel_driver(usb->devhdl, PICKIT_USB_INTERFACE);
		if (ret < 0) {
			sr_err("Cannot detach kernel driver: %s.",
				libusb_error_name(ret));
			return SR_ERR;
		}
		devc->detached_kernel_driver = TRUE;
	}

	ret = libusb_claim_interface(usb->devhdl, PICKIT_USB_INTERFACE);
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

	if (!usb)
		return SR_OK;
	if (!usb->devhdl)
		return SR_OK;

	ret = libusb_release_interface(usb->devhdl, PICKIT_USB_INTERFACE);
	if (ret) {
		sr_err("Cannot release interface: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if (devc->detached_kernel_driver) {
		ret = libusb_attach_kernel_driver(usb->devhdl, PICKIT_USB_INTERFACE);
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

	(void)cg;

	devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->conn)
			return SR_ERR_ARG;
		*data = g_variant_new_printf("%d.%d", ((struct sr_usb_dev_inst*)sdi->conn)->bus, ((struct sr_usb_dev_inst*)sdi->conn)->address);
		return SR_OK;
	case SR_CONF_SAMPLERATE:
		if (!devc)
			return SR_ERR_ARG;
		*data = g_variant_new_uint64(devc->samplerates[devc->curr_samplerate_idx]);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		if (!devc)
			return SR_ERR_ARG;
		return sr_sw_limits_config_get(&devc->sw_limits, key, data);
	case SR_CONF_CAPTURE_RATIO:
		if (!devc)
			return SR_ERR_ARG;
		*data = g_variant_new_uint64(devc->captureratio);
		return SR_OK;
	}

	return SR_ERR_NA;
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
		devc->captureratio = g_variant_get_uint64(data);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		if (!devc)
			return SR_ERR_ARG;
		return sr_sw_limits_config_set(&devc->sw_limits, key, data);
	}
	
	return SR_ERR_NA;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = sdi ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_SAMPLERATE:
			if (!devc)
				return SR_ERR_ARG;
			*data = std_gvar_samplerates(devc->samplerates, devc->num_samplerates);
			return SR_OK;
		case SR_CONF_TRIGGER_MATCH:
			*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
			return SR_OK;
		}
	}
	
	return SR_ERR_NA;
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
			devc->triggers[0],  devc->triggers[1],
			devc->triggers[2]);
	}
	
	/* Have the SETUP packet sent, then poll for the status. */
	devc->state = STATE_CONF;
	ret = microchip_pickit_setup_trigger(sdi);
	if (ret) {
		devc->state = STATE_IDLE;
		return ret;
	}
	devc->state = STATE_WAIT;

	std_session_send_df_header(sdi);
	sr_session_source_add(sdi->session, -1, 0, 20,
		microchip_pickit_receive_data, (void *)sdi);

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

static struct sr_dev_driver microchip_pickit_driver_info = {
	.name = "microchip-pickit",
	.longname = "Microchip PICkit 2 & 3",
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
SR_REGISTER_DEV_DRIVER(microchip_pickit_driver_info);
