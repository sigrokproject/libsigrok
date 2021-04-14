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

static const uint32_t scanopts[] = { // setup the communication options, USB
	SR_CONF_CONN,
//	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = { // This driver is for a logic analyzer
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	// These are the options on the tinyLogicFriend that can be set
	// These need to be verified and testing ***
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET, // confirm this
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST, // confirm this
//	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
//	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
//	SR_CONF_SWAP | SR_CONF_SET,
	SR_CONF_RLE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_FILTER | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

// Starting point is from rohde_schwarz_sme_0x driver, which uses
// USB TMC (Test and Measurement Class) communication

// *** needs to be updated, rs_init_device is called in function: probe_device
static int tlf_init_device(struct sr_dev_inst *sdi)
{

	// This may not be necessary, could probably do this in probe_devices
	//struct dev_context *devc;
	uint8_t model_found;

	//devc = sdi->priv;
	model_found = 0;

	if (g_ascii_strcasecmp(sdi->model, "tiny")  &&   // check that model includes
		g_ascii_strcasecmp(sdi->model, "Logic") &&   // tiny, logic and friend, any order or case
		g_ascii_strcasecmp(sdi->model, "Friend") ) {
		model_found = 1;
		//strcpy(devc->model_config, sdi->model); // is this necessary ***
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
	double value_f;
	value_f = 0; // delete this **

	(void) cg;
	(void) sdi; // delete this ****

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		// rs_sme0x_get_freq(sdi, &value_f);
		*data = g_variant_new_double(value_f);
		break;
	case SR_CONF_AMPLITUDE:
		// rs_sme0x_get_power(sdi, &value_f);
		*data = g_variant_new_double(value_f);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

// *** needs a bunch of configuration parameters added
static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	double value_f;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		value_f = g_variant_get_double(data);
		// rs_sme0x_set_freq(sdi, value_f);
		break;
	case SR_CONF_AMPLITUDE:
		value_f = g_variant_get_double(data);
		// rs_sme0x_set_power(sdi, value_f);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}


static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
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
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(tiny_logic_friend_la_driver_info);
