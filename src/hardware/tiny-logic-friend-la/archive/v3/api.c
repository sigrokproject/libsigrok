/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Kevin Matocha <kmatocha@icloud.com>
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
#include <scpi.h>
#include <string.h>

#include "protocol.h"

static struct sr_dev_driver tiny_logic_friend_la_driver_info;

//static const char *manufacturer = "TinyLogicFriend";

static const uint32_t scanopts[] = { // setup the communication options, use USB TMC
	SR_CONF_CONN,
	SR_CONF_NUM_LOGIC_CHANNELS, // todo - double check this (taken from beaglelogic)
};

static const uint32_t drvopts[] = { // This driver is for a logic analyzer
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	// These are the options on the tinyLogicFriend that can be set
	// These need to be verified and testing ***
//	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET, // * may need to limit samples and use -> | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RLE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FILTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET, // todo - double check this (taken from beaglelogic)
											  // Does this need to be both in devopts and scanopts?
};


// todo - make this variable based on the trigger options returned from a scan
static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

// Starting point is from rohde_schwarz_sme_0x driver, which uses
// USB TMC (Test and Measurement Class) communication

// *** needs to be updated, rs_init_device is called in function: probe_device
static int tlf_init_device(struct sr_dev_inst *sdi)
{
	// This initializes all the device settings, collects all the key device
	// parameters and current values and stores them into the appropriate variables
	// in the private device context sdi->priv
	//
	// The private device context structure is defined in protocol.h:
	// see ``struct dev_context``

	// This may not be necessary, could probably do this in probe_devices
	//struct dev_context *devc;
	uint8_t model_found;
	struct device_context devc;

	devc = sdi->priv; // this is the local context, do we need need to reference this directly
					  // maybe send devc to the ``collect`` functions
	model_found = 0;

	// check if the model is a tinyLogicFriend
	if (g_ascii_strcasecmp(sdi->model, "tiny")  &&   // check that model includes
		g_ascii_strcasecmp(sdi->model, "Logic") &&   // tiny, logic and friend, any order or case
		g_ascii_strcasecmp(sdi->model, "Friend") ) {
		model_found = 1;
	}

	if (!model_found) {
		sr_dbg("Device %s is not supported by this driver.",
			sdi->model);
		return SR_ERR_NA;
	}

	// perform any other initialization here, get channel list, etc.
	if (!(tlf_collect_channels(sdi) == SR_OK)) {
		return SR_ERR_NA;
	}
	if (!(tlf_collect_samplerates(sdi) == SR_OK)) {
		return SR_ERR_NA;
	}

	return SR_OK;
}

// *** needs to be updated
static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK)
		goto fail;

	// store the information from the hardware ID (sr_scpi_get_hw_id)
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->driver = &tiny_logic_friend_la_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	sr_spew("Vendor: %s\n", sdi->vendor);
	sr_spew("Model: %s\n", sdi->model);
	sr_spew("Version: %s\n", sdi->version);
	sr_spew("Serial number: %s\n", sdi->serial_num);

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context)); // header in protocol.h
	sdi->priv = devc;


	if (tlf_init_device(sdi) != SR_OK) // verify this device is a tinyLogicFriend
									   // and initialize all device options and get current settings
		goto fail;

	return sdi;

fail:
	sr_scpi_hw_info_free(hw_info);
	sr_dev_inst_free(sdi);
	g_free(devc);
	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
	// set all important setup parameters into the probe_device function
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return sr_scpi_open(sdi->conn);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

// *** needs a bunch of configuration parameters added
static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void) cg;
	uint64_t buf_int;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (tlf_get_samplerate(sdi, &buf_int) != SR_OK) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_uint64(buf_int);
		break;
	case SR_CONF_ENABLED:

			// **** todo

		break;
	case SR_CONF_NUM_LOGIC_CHANNELS: // see Beaglelogic
		*data = g_variant_new_uint32(g_slist_length(sdi->channels));

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
	struct sr_channel *ch, unsigned int changes)
{
	/* Currently we only handle SR_CHANNEL_SET_ENABLED. */ // todo - update to trigger set
	switch(changes) {
		case SR_CHANNEL_SET_ENABLED:
			return tlf_set_channel_state(sdi, ch->index, ch->enabled);
			break;
		// case SR_CHANNEL_SET_TRIGGER:
		// 	return tlf_set_trigger(sdi, ch->index, ch->trigger);
		// 	break;
		default:
			return SR_ERR_NA;
	}

}

// *** needs a bunch of configuration parameters added
static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t value;

	(void)cg;

	devc = sdi->priv;

	if (!sdi)
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		if ( (value < samplerates[0]) ||
			 (value > samplerates[1]) ) {
			return SR_ERR_SAMPLERATE;
		}
		return tlf_set_samplerate(sdi, value);
		break;
	case SR_CONF_ENABLED:
		if(g_variant_get_boolean(data)) {
			// **** todo
		}
		break;

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	// modified from "demo/api.c"
	switch(key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
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
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;

	return SR_OK;
}

static struct sr_dev_driver tiny_logic_friend_la_driver_info = {
	.name = "tiny-logic-friend-la",
	.longname = "Tiny Logic Friend-la",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(tiny_logic_friend_la_driver_info);
