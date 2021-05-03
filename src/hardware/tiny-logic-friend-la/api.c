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
	// SR_CONF_CONN,
	// SR_CONF_NUM_LOGIC_CHANNELS, // todo - double check this (taken from beaglelogic)
};

static const uint32_t drvopts[] = { // This driver is for a logic analyzer
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	// These are the options on the tinyLogicFriend that can be set
	// These need to be verified and testing ***
//	SR_CONF_CONTINUOUS,
//  SR_CONF_RLE | SR_CONF_GET | SR_CONF_SET, // for example, see pipistrello-ols
//  SR_CONF_FILTER | SR_CONF_GET | SR_CONF_SET,
//  SR_CONF_ENABLED | SR_CONF_SET,
//  SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
    SR_CONF_TRIGGER_MATCH | SR_CONF_LIST, // Need to update and verify this.
};


// // todo - make this variable based on the trigger options returned from a scan
// static const int32_t trigger_matches[] = {
// 	SR_TRIGGER_ZERO,
// 	SR_TRIGGER_ONE,
// 	SR_TRIGGER_RISING,
// 	SR_TRIGGER_FALLING,
// 	SR_TRIGGER_EDGE,
// };

// Starting point is from rohde_schwarz_sme_0x driver, which uses
// USB TMC (Test and Measurement Class) communication


static int tlf_get_lists(struct sr_dev_inst *sdi)
{
	// This initializes all the device settings, collects all the key device
	// parameters and current values and stores them into the appropriate variables
	// in the private device context sdi->priv
	//
	// The private device context structure is defined in protocol.h:
	// see ``struct dev_context``

	uint8_t model_found;
	//struct device_context *devc;

	sr_spew("-> Enter tlf_init_device");

	// devc = sdi->priv; // this is the local context, do we need need to reference this directly
	// 				  // maybe send devc to the ``collect`` functions
	model_found = 0;

	sr_spew("-> Enter tlf_init_device 1");
	// check if the model is a tinyLogicFriend
	if (g_ascii_strcasecmp(sdi->model, "tiny")  &&   // check that model includes
		g_ascii_strcasecmp(sdi->model, "Logic") &&   // tiny, logic and friend, any order or case
		g_ascii_strcasecmp(sdi->model, "Friend") ) {
		model_found = 1;
	}

	sr_spew("-> Enter tlf_init_device 2");
	if (!model_found) {
		sr_dbg("Device %s is not supported by this driver.",
			sdi->model);
		return SR_ERR_NA;
	}

	sr_spew("-> Enter tlf_init_device 3");

	// perform any other initialization here, get channel list, etc.
	if (!(tlf_channels_list(sdi) == SR_OK)) {
		return SR_ERR_NA;
	}
	sr_spew("-> Enter tlf_init_device 4");

	if (!(tlf_samplerates_list(sdi) == SR_OK)) {
		return SR_ERR_NA;
	}
	sr_spew("-> Enter tlf_init_device 5");
	if (!(tlf_trigger_list(sdi) == SR_OK)) {
		return SR_ERR_NA;
	}

	sr_spew("-> Enter tlf_init_device 6");

	return SR_OK;
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sr_spew("-> Enter probe_device");

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

	// allocate the device context
	devc = g_malloc0(sizeof(struct dev_context)); // header in protocol.h
	sdi->priv = devc;
	devc->trigger_matches_count = TRIGGER_MATCHES_COUNT;

	if (tlf_get_lists(sdi) != SR_OK) // verify this device is a tinyLogicFriend
									   // and initialize all device options and get current settings
		goto fail;

	// DEBUG ** todo

	GSList *l;
	for (l = sdi->channels; l; l = l->next) {
		sr_err("** probe_device channel found");
	}

	return sdi;

fail:
	sr_scpi_hw_info_free(hw_info);
	sr_dev_inst_free(sdi);
	g_free(devc);
	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	//struct sr_dev_inst *sdi;
	sr_spew("-> Enter scan");
	// sr_scpi_scan(di->context, options, probe_device);

	// return std_scan_complete(di, g_slist_append(NULL, sdi));
	// set all important setup parameters into the probe_device function


	return sr_scpi_scan(di->context, options, probe_device);

}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;

	sr_spew("-> Enter dev_open");
	// return sr_scpi_open(sdi->conn);

	if ((ret = sr_scpi_open(sdi->conn)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	sr_spew("-> Enter dev_close");
	return sr_scpi_close(sdi->conn);
}

// *** needs a bunch of configuration parameters added
static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct sr_channel *ch;
	uint64_t buf_int;
	int32_t buf_32;
	gboolean enable_status;
	enable_status = FALSE;

	sr_spew("-> Enter config_get");

	if (!sdi) {
		sr_err("Must call `scan` prior to calling `config_get`.");
		return SR_ERR_ARG;
	}

	sr_spew("-> Enter config_get 2");

	if (!cg) {
		switch (key) {
		case SR_CONF_SAMPLERATE:
			sr_spew("  -> SR_CONF_SAMPLERATE");
			if (tlf_samplerate_get(sdi, &buf_int) != SR_OK) {
				return SR_ERR;
			}
			*data = g_variant_new_uint64(buf_int);
			sr_spew("config_get: returning samplerate");
			break;
		case SR_CONF_NUM_LOGIC_CHANNELS: // see Beaglelogic
			sr_spew("  -> SR_CONF_NUM_LOGIC_CHANNELS");
			*data = g_variant_new_uint32(g_slist_length(sdi->channels));
			break;

		case SR_CONF_LIMIT_SAMPLES:  // * todo cleanup switch statement
			sr_spew("  -> SR_CONF_LIMIT_SAMPLES");
			if (tlf_samples_get(sdi, &buf_32) != SR_OK) {
				return SR_ERR;
			}
			*data = g_variant_new_uint64(buf_32);
			break;
		default:
			sr_dbg("(1) Unsupported key: %d ", key);
			return SR_ERR_NA;
			break;
		}
	} else {
		switch (key) {
		case SR_CONF_ENABLED:
			sr_spew("  -> SR_CONF_ENABLED");
			ch = cg->channels->data;
			if ( tlf_channel_state_get(sdi, ch->index, &enable_status) != SR_OK ) {
				return SR_ERR;
			}
			*data = g_variant_new_boolean(enable_status);
			break;
		case SR_CONF_NUM_LOGIC_CHANNELS: // see Beaglelogic
			sr_spew("  -> SR_CONF_NUM_LOGIC_CHANNELS");
			*data = g_variant_new_uint32(g_slist_length(sdi->channels));
			break;
		case SR_CONF_LIMIT_SAMPLES: // * todo cleanup switch statement
			sr_spew("  -> SR_CONF_LIMIT_SAMPLES");
			if (tlf_samples_get(sdi, &buf_32) != SR_OK) {
				return SR_ERR;
			}
			*data = g_variant_new_uint64(buf_32);
			break;
		default:
			sr_dbg("(2) Unsupported key: %d ", key);
			return SR_ERR_NA;
			break;
		}
	}

	return SR_OK;
}

static int config_channel_set(const struct sr_dev_inst *sdi,
	struct sr_channel *ch, unsigned int changes)
{
	sr_spew("-> Enter config_channel_set");

	/* Currently we only handle SR_CHANNEL_SET_ENABLED. */ // todo - update to trigger set
	switch(changes) {
		case SR_CHANNEL_SET_ENABLED:
			sr_spew("  -> SR_CHANNEL_SET_ENABLED");
			return tlf_channel_state_set(sdi, ch->index, ch->enabled);
			break;
		default:
			return SR_ERR_NA;
	}

}

// *** needs a bunch of configuration parameters added
static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	uint64_t value;

	if (!sdi) {
		sr_err("Must call `scan` prior to calling `config_set`.");
		return SR_ERR_NA;
	}

	sr_spew("-> Enter config_set");

	devc = sdi->priv;

	if (!sdi)
		return SR_ERR_NA;

	if (!cg) {
		switch (key) {
		case SR_CONF_SAMPLERATE:
			sr_spew("  -> SR_CONF_SAMPLERATE");
			value = g_variant_get_uint64(data);
			if ( (value < devc->samplerate_range[0]) ||
				 (value > devc->samplerate_range[1]) ) {
				return SR_ERR_SAMPLERATE;
			}
			return tlf_samplerate_set(sdi, value);
			break;
		case SR_CONF_LIMIT_SAMPLES: // * todo cleanup switch statement
			sr_spew("  -> SR_CONF_LIMIT_SAMPLES");
			if (tlf_samples_set(sdi, g_variant_get_uint64(data)) != SR_OK) {
				return SR_ERR;
			}
			break;
		default:
			sr_dbg("Unsupported key: %d ", key);
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_ENABLED: // see `rigol-dg`
			ch = cg->channels->data;
			sr_spew("  -> SR_CONF_ENABLED");
			if (tlf_channel_state_set(sdi, ch->index, g_variant_get_boolean(data)) != SR_OK) {
				return SR_ERR;
			}
			break;
		case SR_CONF_LIMIT_SAMPLES: // * todo cleanup switch statement
			sr_spew("  -> SR_CONF_LIMIT_SAMPLES");
			if (tlf_samples_set(sdi, g_variant_get_uint64(data)) != SR_OK) {
				return SR_ERR;
			}
			break;
		default:
			sr_dbg("Unsupported key: %d ", key);
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	sr_spew("-> config_list");
	struct dev_context *devc;
	// uint32_t buf_32;

	// if (!sdi) {
	// 	sr_err("Must call `scan` prior to calling `config_list`.");
	// return SR_ERR_NA;
	// }

	sr_spew("-> Enter config_list");

	switch(key) {
	case SR_CONF_SCAN_OPTIONS:
		sr_spew("  -> SR_CONF_SCAN_OPTIONS");
		// return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, NO_OPTS, NO_OPTS);
	case SR_CONF_DEVICE_OPTIONS:
		sr_spew("  -> SR_CONF_DEVICE_OPTIONS");
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		sr_spew("  -> SR_CONF_SAMPLERATE");
		if (!sdi) {
			sr_err("Must call `scan` prior to calling `config_list`.");
		return SR_ERR_NA;
		}
		devc = sdi->priv;
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(devc->samplerate_range));
		break;
	case SR_CONF_TRIGGER_MATCH:
		sr_spew("  -> SR_CONF_TRIGGER_MATCH");
		if (!sdi) {
			sr_err("Must call `scan` prior to calling `config_list`.");
		return SR_ERR_NA;
		}
		devc = sdi->priv;
		tlf_trigger_list(sdi);
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(devc->trigger_matches));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (!sdi)
			return SR_ERR_ARG;
		else {
			devc=sdi->priv;
			if (tlf_maxsamples_get(sdi) != SR_OK) {
				return SR_ERR;
			}
			*data = std_gvar_tuple_u64(100, devc->max_samples);
			sr_dbg("max_samples: %llu", devc->max_samples);
			break;
		}
	default:
		sr_dbg("Unsupported key: %d ", key);
		return SR_ERR_NA;
	}
	sr_spew("<- Leaving config_list");

	return SR_OK;
}


static int dev_acquisition_start(const struct sr_dev_inst *sdi)
	// modified from yokogawa-dlm
{
	// GSList *l;
	// gboolean digital_added;
	// struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	sr_spew("->dev_acquisition_start");

	scpi = sdi->conn;
	devc = sdi->priv;
	// digital_added = FALSE;

	// g_slist_free(devc->enabled_channels);
	// devc->enabled_channels = NULL;

	// for (l = sdi->channels; l; l = l->next) {
	// 	ch = l->data;
	// 	if (!ch->enabled)
	// 		continue;
	// 	/* Only add a single digital channel. */
	// 	if (ch->type != SR_CHANNEL_LOGIC || !digital_added) {
	// 		devc->enabled_channels = g_slist_append(
	// 			devc->enabled_channels, ch);
	// 		if (ch->type == SR_CHANNEL_LOGIC)
	// 			digital_added = TRUE;
	// 	}
	// }

	// if (!devc->enabled_channels)
	// 	return SR_ERR;

	// if (dlm_check_channels(devc->enabled_channels) != SR_OK) {
	// 	sr_err("Invalid channel configuration specified!");
	// 	return SR_ERR_NA;
	// }

	//  Request data for the first enabled channel.
	// devc->current_channel = devc->enabled_channels;
	// dlm_channel_data_request(sdi);

	devc->data_pending = TRUE; // initialize all the variables before reading data.
	devc->measured_samples = 0;
	devc->last_sample = 0;
	devc->last_timestamp = 0;



	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			tlf_receive_data, (void *)sdi);

	sr_spew("->dev_acquisition_start 2");

	// sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 5,
			// tlf_receive_data, (void *)sdi);

	std_session_send_df_header(sdi); // sends the SR_DF_HEADER command to the session

	sr_spew("->dev_acquisition_start 3");

	sr_spew("Go RUN");

	std_session_send_df_frame_begin(sdi);

	return tlf_exec_run(sdi);
}




// static int dev_acquisition_start(const struct sr_dev_inst *sdi)
// {
// 	/* TODO: configure hardware, reset acquisition state, set up
// 	 * callbacks and send header packet. */
// 	char buffer[555];
// 	int len;


// 	sr_spew("-> Enter dev_acquisition_start");
// 	tlf_exec_run(sdi);

// 	// todo setup triggers here

// 	// read measurements

// 	// /*
// 	//  * Start acquisition on the first enabled channel. The
// 	//  * receive routine will continue driving the acquisition.
// 	//  */
// 	// sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
// 	// 		lecroy_xstream_receive_data, (void *)sdi);

// 	// std_session_send_df_header(sdi);

// 	// devc->current_channel = devc->enabled_channels;

// 	// return lecroy_xstream_request_data(sdi);


// 	sr_scpi_source_add(sdi->session, sdi->conn, G_IO_IN, 50,
// 		tlf_receive_data, (void *)sdi);


// 	sr_spew("Go RUN");
// 	return tlf_exec_run(sdi);
// }

static int dev_acquisition_stop(const struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */


	sr_spew("-> Enter dev_acquisition_stop");
	std_session_send_df_frame_end(sdi);
	sr_scpi_source_remove(sdi->session, sdi->conn);
	tlf_exec_stop(sdi);
	// todo clear triggers

	return SR_OK;
}

// static int tlf_init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
// {
//  	sr_spew("-> Enter tlf_init");
//  	std_init(di, sr_ctx);
//  	sr_spew("-> Leave tlf_init");
//  	return SR_OK;
// }

// static int tlf_dev_list(const struct sr_dev_inst *sdi)
// {
//  	(void) sdi;
//  	sr_spew("-> Enter tlf_dev_list");
//  	return SR_OK;
// }

// static int tlf_dev_clear(const struct sr_dev_inst *sdi)
// {
//  	(void) sdi;
//  	sr_spew("-> Enter tlf_dev_clear");
//  	return SR_OK;
// }


static struct sr_dev_driver tiny_logic_friend_la_driver_info = {
	.name = "tiny-logic-friend-la",
	.longname = "Tiny Logic Friend-la",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_channel_set = config_channel_set,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,  // #2 This is run second
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(tiny_logic_friend_la_driver_info);
