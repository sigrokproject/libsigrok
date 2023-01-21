/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Daniel Echt <taragor83@gmail.com>
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
#include "scpi.h"
#include <math.h>

#define WAIT_FOR_CAPTURE_COMPLETE_RETRIES 100
#define WAIT_FOR_CAPTURE_COMPLETE_DELAY (100*1000)

static struct sr_dev_driver agilent_54621d_driver_info;

static const char *manufacturers[] = {
	"AGILENT TECHNOLOGIES",
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LOGIC_ANALYZER,
};

enum {
	CG_INVALID = -1,
	CG_NONE,
	CG_ANALOG,
	CG_DIGITAL,
};

//Do not change order
static const char *data_sources[] = {
	"Single",
	"Memory",
};

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_MHZ(200),
	SR_HZ(1),
};

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't  get IDN response.");
		goto fail;
	}

	if (std_str_idx_s(hw_info->manufacturer, ARRAY_AND_SIZE(manufacturers)) < 0)
		goto fail;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->driver = &agilent_54621d_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));

	sdi->priv = devc;

	if (agilent_54621d_init_device(sdi) != SR_OK)
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
	sr_info("Scanning for agilent 54621d");
	return sr_scpi_scan(di->context, options, probe_device);
}

static void clear_helper(struct dev_context *devc)
{
	agilent_54621d_scope_state_free(devc->model_state);
	g_free(devc->analog_groups);
	g_free(devc->digital_groups);
	g_free(devc->data);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if ((ret = sr_scpi_open(scpi)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	if ((ret = agilent_54621d_scope_state_get(sdi)) < 0) {
		sr_err("Failed to get device config: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

static int check_channel_group(struct dev_context *devc,
			     const struct sr_channel_group *cg)
{
	const struct scope_config *model;

	model = devc->model_config;

	if (!cg)
		return CG_NONE;

	if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) >= 0)
		return CG_ANALOG;

	if (std_cg_idx(cg, devc->digital_groups, model->digital_pods) >= 0)
		return CG_DIGITAL;

	sr_err("Invalid channel group specified.");

	return CG_INVALID;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int cg_type, idx;
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	state = devc->model_state;

	switch (key) {
		case SR_CONF_NUM_HDIV:
			*data = g_variant_new_int32(model->num_xdivs);
			break;
		case SR_CONF_TIMEBASE:
			*data = g_variant_new("(tt)", (*model->timebases)[state->timebase][0],
						(*model->timebases)[state->timebase][1]);
			break;
		case SR_CONF_NUM_VDIV:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (cg_type != CG_ANALOG)
				return SR_ERR_NA;
			if (std_cg_idx(cg, devc->analog_groups, model->analog_channels) < 0)
				return SR_ERR_ARG;
			*data = g_variant_new_int32(model->num_ydivs);
			break;
		case SR_CONF_VDIV:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (cg_type != CG_ANALOG)
				return SR_ERR_NA;
			if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
				return SR_ERR_ARG;
			*data = g_variant_new("(tt)",
						(*model->vdivs)[state->analog_channels[idx].vdiv][0],
						(*model->vdivs)[state->analog_channels[idx].vdiv][1]);
			break;
		case SR_CONF_TRIGGER_SOURCE:
			*data = g_variant_new_string((*model->trigger_sources)[state->trigger_source]);
			break;
		case SR_CONF_TRIGGER_SLOPE:
			*data = g_variant_new_string((*model->trigger_slopes)[state->trigger_slope]);
			break;
		case SR_CONF_PEAK_DETECTION:
			*data = g_variant_new_boolean(state->peak_detection);
			break;
		case SR_CONF_HORIZ_TRIGGERPOS:
			*data = g_variant_new_double(state->horiz_triggerpos);
			break;
		case SR_CONF_ENABLED:
			sr_info("get channel enabled");
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (cg_type == CG_DIGITAL){
				sr_info("get digital channel enabled");
				if ((idx = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
					return SR_ERR_ARG;
				*data = g_variant_new_boolean(state->digital_pods[idx].state);
			}
			else if (cg_type == CG_ANALOG){
				sr_info("get analog channel enabled");
				if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
					return SR_ERR_ARG;
				*data = g_variant_new_boolean(state->analog_channels[idx].state);
			} else {
				return SR_ERR;
			}
			break;
		case SR_CONF_COUPLING:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (cg_type != CG_ANALOG)
				return SR_ERR_NA;
			if ((idx = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
				return SR_ERR_ARG;
			*data = g_variant_new_string((*model->coupling_options)[state->analog_channels[idx].coupling]);
			break;
		case SR_CONF_SAMPLERATE:
			*data = g_variant_new_uint64(devc->sample_rate_limit); //state->sample_rate
			break;
		case SR_CONF_LOGIC_THRESHOLD:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (cg_type != CG_DIGITAL)
				return SR_ERR_NA;
			if (!model)
				return SR_ERR_ARG;
			if ((idx = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
				return SR_ERR_ARG;
			*data = g_variant_new_string((*model->logic_threshold)[state->digital_pods[idx].threshold]);
			break;
		case SR_CONF_LOGIC_THRESHOLD_CUSTOM:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (cg_type != CG_DIGITAL)
				return SR_ERR_NA;
			if (!model)
				return SR_ERR_ARG;
			if ((idx = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
				return SR_ERR_ARG;
			*data = g_variant_new_double(state->digital_pods[idx].user_threshold);
			break;
		case SR_CONF_LIMIT_SAMPLES:
			*data = g_variant_new_uint64(devc->samples_limit);
			break;
		case SR_CONF_DATA_SOURCE:
			if (devc->data_source == DATA_SOURCE_LIVE)
				*data = g_variant_new_string("Single");
			else if (devc->data_source == DATA_SOURCE_MEMORY)
				*data = g_variant_new_string("Memory");
			break;
		//ToDo: Check if all options are implemented
		default:
			sr_err("could not find requested parameter: %d", key);
			return SR_ERR_NA;
	}

	return SR_OK;
}


static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret, cg_type, idx, i, j;
	char command[MAX_COMMAND_SIZE];
	char float_str[30];
	struct dev_context *devc;
	const struct scope_config *model;
	struct scope_state *state;
	double tmp_d, tmp_d2;
	gboolean update_sample_rate, tmp_bool;
	int tmp_int;
	const char *tmp_str;

	ret = SR_ERR;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
		return SR_ERR;

	model = devc->model_config;
	state = devc->model_state;
	update_sample_rate = FALSE;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		if(state->sample_rate < g_variant_get_uint64(data)){
			devc->sample_rate_limit = state->sample_rate;
			ret = SR_OK; 
		} else {
			devc->sample_rate_limit = g_variant_get_uint64(data);
			ret = SR_OK;
		}
		break;
	case SR_CONF_LIMIT_SAMPLES:
		tmp_int = g_variant_get_uint64(data);
		tmp_d = (double)tmp_int/devc->sample_rate_limit;														 //total time to be transmitted
		tmp_d2 = 10*((float) (*model->timebases)[state->timebase][0] / (*model->timebases)[state->timebase][1]); //total time shown in display
		if(tmp_d > tmp_d2){			//dont allow to transmit more data than shown in the main view. One could zoom out to increase the amount of data shown, but implementing that would probably be a bitch so this driver wont allow to download more data than shown in main view
			devc->samples_limit = tmp_d2*devc->sample_rate_limit;
		} else {
			devc->samples_limit = g_variant_get_uint64(data);
		}
		ret = SR_OK;
		break;
	case SR_CONF_VDIV:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((idx = std_u64_tuple_idx(data, *model->vdivs, model->num_vdivs)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		g_ascii_formatd(float_str, sizeof(float_str), "%E",
			(float) (*model->vdivs)[idx][0] / (*model->vdivs)[idx][1]);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_VERTICAL_SCALE],
			   j + 1, float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->analog_channels[j].vdiv = idx;
		ret = SR_OK;
		break;
	case SR_CONF_TIMEBASE:
		if ((idx = std_u64_tuple_idx(data, *model->timebases, model->num_timebases)) < 0)
			return SR_ERR_ARG;
		g_ascii_formatd(float_str, sizeof(float_str), "%E",
			(float) (*model->timebases)[idx][0] / (*model->timebases)[idx][1]);
		g_snprintf(command, sizeof(command),
			(*model->scpi_dialect)[SCPI_CMD_SET_TIMEBASE],
			float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
			sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->timebase = idx;
		ret = SR_OK;
		update_sample_rate = TRUE;
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_d = g_variant_get_double(data);
		if (tmp_d < 0.0 || tmp_d > 1.0)
			return SR_ERR;
		tmp_d2 = -(tmp_d - 0.5) *
			((double) (*model->timebases)[state->timebase][0] /
			(*model->timebases)[state->timebase][1])
			 * model->num_xdivs;
		g_ascii_formatd(float_str, sizeof(float_str), "%E", tmp_d2);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_HORIZ_TRIGGERPOS],
			   float_str);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->horiz_triggerpos = tmp_d;
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if ((idx = std_str_idx(data, *model->trigger_sources, model->num_trigger_sources)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_SOURCE],
			   (*model->trigger_sources)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->trigger_source = idx;
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, *model->trigger_slopes, model->num_trigger_slopes)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_TRIGGER_SLOPE],
			   (*model->trigger_slopes)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->trigger_slope = idx;
		ret = SR_OK;
		break;
	case SR_CONF_PEAK_DETECTION:
		tmp_bool = g_variant_get_boolean(data);
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_PEAK_DETECTION],
			   tmp_bool ? "AUTO" : "OFF");
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		/* Peak Detection automatically switches off High Resolution mode. */
		if (tmp_bool) {
			g_snprintf(command, sizeof(command),
				   (*model->scpi_dialect)[SCPI_CMD_SET_HIGH_RESOLUTION],
				   "OFF");
			if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					 sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;
			state->high_resolution = FALSE;
		}
		state->peak_detection = tmp_bool;
		ret = SR_OK;
		break;
	case SR_CONF_ENABLED:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type == CG_DIGITAL){
			if ((j = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
				return SR_ERR_ARG;
			//enable digital channel
		}
		else if (cg_type == CG_ANALOG){
			if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
				return SR_ERR_ARG;
			g_snprintf(command, sizeof(command), (*model->scpi_dialect)[SCPI_CMD_SET_ANALOG_CHAN_STATE], j+1, g_variant_get_boolean(data));
			if(sr_scpi_send(sdi->conn, command) != SR_OK || sr_scpi_get_opc(sdi->conn) != SR_OK)
				return SR_ERR;
			state->analog_channels[j].state = g_variant_get_boolean(data);
			update_sample_rate = TRUE;
			ret = SR_OK;
			//enable analog channel
		} else {
			return SR_ERR;
		}
		break;
	case SR_CONF_COUPLING:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if ((idx = std_str_idx(data, *model->coupling_options, model->num_coupling_options)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->analog_groups, model->analog_channels)) < 0)
			return SR_ERR_ARG;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_COUPLING],
			   j + 1, (*model->coupling_options)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->analog_channels[j].coupling = idx;
		ret = SR_OK;
		break;	
	case SR_CONF_LOGIC_THRESHOLD:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		if (!model)
			return SR_ERR_ARG;
		if ((idx = std_str_idx(data, *model->logic_threshold, model->num_logic_threshold)) < 0)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
			return SR_ERR_ARG;
                /* Check if the threshold command is based on the POD or digital channel index. */
		if (model->logic_threshold_for_pod)
			i = j + 1;
		else
			i = j * DIGITAL_CHANNELS_PER_POD;
		g_snprintf(command, sizeof(command),
			   (*model->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
			   i, (*model->logic_threshold)[idx]);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK)
			return SR_ERR;
		state->digital_pods[j].threshold = idx;
		ret = SR_OK;
		break;
	case SR_CONF_LOGIC_THRESHOLD_CUSTOM:
		if (!cg)
			return SR_ERR_CHANNEL_GROUP;
		if (cg_type != CG_DIGITAL)
			return SR_ERR_NA;
		if (!model)
			return SR_ERR_ARG;
		if ((j = std_cg_idx(cg, devc->digital_groups, model->digital_pods)) < 0)
			return SR_ERR_ARG;
		//ToDo: implement logic
		ret = SR_OK;
		break;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		sr_dbg("Setting data source to: '%s'", tmp_str);
		if(!strcmp(tmp_str, "Single"))
			devc->data_source = DATA_SOURCE_LIVE;
		else if(!strcmp(tmp_str, "Memory"))
			devc->data_source = DATA_SOURCE_MEMORY;
		else {
			sr_err("Unknown data source: '%s'", tmp_str);
			return SR_ERR;
		} 
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
		break;
	}

	if (ret == SR_OK && update_sample_rate)
		ret = agilent_54621d_update_sample_rate(sdi);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	int cg_type = CG_NONE;
	struct dev_context *devc = NULL;
	const struct scope_config *model = NULL;

	if (sdi) {
		devc = sdi->priv;
		if ((cg_type = check_channel_group(devc, cg)) == CG_INVALID)
			return SR_ERR;

		model = devc->model_config;
	}

	ret = SR_OK;
	switch (key) {
		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates)); 
			break;
		case SR_CONF_SCAN_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(scanopts));
			break;
		case SR_CONF_DEVICE_OPTIONS:
			if (!cg) {
				if (model)
					*data = std_gvar_array_u32(*model->devopts, model->num_devopts);
				else
					*data = std_gvar_array_u32(ARRAY_AND_SIZE(drvopts));
			} else if (cg_type == CG_ANALOG) {
				*data = std_gvar_array_u32(*model->devopts_cg_analog, model->num_devopts_cg_analog);
			} else if (cg_type == CG_DIGITAL) {
				*data = std_gvar_array_u32(*model->devopts_cg_digital, model->num_devopts_cg_digital);
			} else {
				*data = std_gvar_array_u32(NULL, 0);
			}
			break;
		case SR_CONF_COUPLING:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(*model->coupling_options, model->num_coupling_options);
			break;
		case SR_CONF_TRIGGER_SOURCE:
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(*model->trigger_sources, model->num_trigger_sources);
			break;
		case SR_CONF_TRIGGER_SLOPE:
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(*model->trigger_slopes, model->num_trigger_slopes);
			break;
		case SR_CONF_TIMEBASE:
			if (!model)
				return SR_ERR_ARG;
			*data = std_gvar_tuple_array(*model->timebases, model->num_timebases);
			break;
		case SR_CONF_VDIV:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (!model)
				return SR_ERR_ARG;
			*data = std_gvar_tuple_array(*model->vdivs, model->num_vdivs);
			break;
		case SR_CONF_LOGIC_THRESHOLD:
			if (!cg)
				return SR_ERR_CHANNEL_GROUP;
			if (!model)
				return SR_ERR_ARG;
			*data = g_variant_new_strv(*model->logic_threshold, model->num_logic_threshold);
			break;
		case SR_CONF_DATA_SOURCE:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(data_sources));
			break;
	/* TODO Check if all relevant option are present here */
	default:
		sr_info("Trying to query for config list for:");
		return SR_ERR_NA;
	}

	return ret;
}


static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	GSList *l;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct scope_state *state;
	const struct scope_config *model;
	int tmp_int;
	gboolean tmp_bool;
	char command[MAX_COMMAND_SIZE];
	float xinc;
	int points;
	char *tmp_string;
	float tmp_float;
	struct analog_channel_transfer_info *encoding;
	gboolean digital_added[MAX_DIGITAL_GROUP_COUNT];
	size_t group, pod_count;


	scpi = sdi->conn;
	devc = sdi->priv;
	state = devc->model_state;
	model = devc->model_config;

	devc->num_samples = 0;
	devc->num_frames = 0;

	//reset to empty
	for (group = 0; group < ARRAY_SIZE(digital_added); group++)
		digital_added[group] = FALSE;
	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;

	pod_count = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		sr_dbg("initializing channel %s", ch->name);
		if(ch->type == SR_CHANNEL_ANALOG) {
			if(ch->enabled)
				devc->enabled_channels = g_slist_append(devc->enabled_channels, ch);
			//Enable/disable channel if neccessary
			if(ch->enabled != state->analog_channels[ch->index].state) {
				if( sr_scpi_send(scpi, (*model->scpi_dialect)[SCPI_CMD_SET_ANALOG_CHAN_STATE], ch->index + 1, ch->enabled ? "ON" : "OFF") != SR_OK || 
						sr_scpi_get_opc(scpi) != SR_OK)
					return SR_ERR;
				state->analog_channels[ch->index].state = ch->enabled;
			}	
		} else if (ch->type == SR_CHANNEL_LOGIC) {
			group = ch->index / DIGITAL_CHANNELS_PER_POD;
			if(ch->enabled && !digital_added[group]){ 						//Only add a single channel per pod
				devc->enabled_channels = g_slist_append(devc->enabled_channels, ch);
				digital_added[group] = TRUE;
				pod_count++;
			}									
			//Enable/disable channel if neccessary
			if(ch->enabled != state->digital_channels[ch->index]) {
				if( sr_scpi_send(scpi, (*model->scpi_dialect)[SCPI_CMD_SET_DIG_CHAN_STATE], ch->index, ch->enabled ? "ON" : "OFF") != SR_OK || 
						sr_scpi_get_opc(scpi) != SR_OK)
					return SR_ERR;
				state->digital_channels[ch->index] = ch->enabled;
			}
		}	
	}
	devc->current_channel = devc->enabled_channels;
	devc->pod_count = pod_count;


	if(devc->data_source == DATA_SOURCE_LIVE){
		sr_scpi_get_bool(scpi, ":TER?", &tmp_bool);
		sr_scpi_send(scpi, ":SING");
		sr_scpi_get_bool(scpi, ":TER?", &tmp_bool);
		while(!tmp_bool){
			g_usleep(WAIT_FOR_CAPTURE_COMPLETE_DELAY);
			sr_scpi_get_bool(scpi, ":TER?", &tmp_bool);
		}
	} else if (devc->data_source == DATA_SOURCE_MEMORY) {
		ch = (struct sr_channel *)devc->current_channel->data;
		g_snprintf(command, sizeof(command), ":WAV:SOUR %s", ch->name);
		sr_scpi_send(scpi, command);
		sr_scpi_get_int(scpi, ":WAV:POIN? MAX", &tmp_int);
		if(tmp_int <= 0){
			sr_err("No waveform in Memory");
			return SR_OK;
		}
	} else {
		sr_err("Unknown datasource");
		return SR_ERR;
	}

	sr_dbg("determine steps to download data");

	/*
	* Downloading data is done in a quite manual way to speed up downloading. The device only allows downloading upto 2k points from the view-buffer, or the complete captured waveform (~1m points)
	* The logic is, that if we want less than the complete waveform we can switch to window view, calculate the settings to have the window show exactly 2k points of the wv, and then transfer these 2k points.
	* Then we can move the window delay and download the next 2k points. We can repeat this until we have the desired amount of points. 
	* This is also how the scope transfers the full waveform, however transfering the full wavefrom cannot be interrupted, so the manual approach is better
	*/

	//Reset parameters
	devc->num_samples = 0;
	devc->num_blocks_downloaded = 0;
	//devc->buffer = g_malloc0(sizeof(char) * devc->samples_limit);
	devc->headerSent = FALSE;

	//set waveform source channel to the first channel to be downloaded
	ch = (struct sr_channel *)devc->enabled_channels->data;
	if(ch->type == SR_CHANNEL_LOGIC){
		group = ch->index/DIGITAL_CHANNELS_PER_POD+1;
		g_snprintf(command, sizeof(command), ":WAV:SOUR POD%ld", group);
	} else {
		g_snprintf(command, sizeof(command), ":WAV:SOUR %s", ch->name);	
	}
	sr_scpi_send(scpi, command);

	//get required data for calculations
	sr_scpi_send(scpi, ":TIM:MODE MAIN;:WAV:POIN MAX");
	sr_scpi_send(scpi, ":WAV:UNS 0");
	g_usleep(300000);											//Apparently the device needs a little time to do the wav:sour and tim:mode setup before requesting wav:poin? max. waiting 30ms should be enough
	if(sr_scpi_get_int(scpi, ":WAV:POIN? MAX", &points) != SR_OK){
		sr_err("Couldn't get max Points");
		return SR_ERR;
	}
	if(sr_scpi_get_float(scpi, ":WAV:XINC?", &xinc) != SR_OK){	//ToDo: is currently unneccessarysince the value needs to be updated after zooming
		sr_err("Couldn't get x inc");
		return SR_ERR;
	}
	if(sr_scpi_get_string(scpi, ":TIM:REF?", &tmp_string) != SR_OK){
		sr_err("Couldn't get time ref");
		return SR_ERR;
	}
	if(sr_scpi_get_float(scpi, ":TIM:SCAL?", &tmp_float) != SR_OK){
		sr_err("Couldn't get time scale");
		return SR_ERR;
	}


	//set main timebase to show entire waveform. This is neccessary since the the windowed view can not be delayed outside the main view
	//g_snprintf(command, sizeof(command), ":TIM:RANG %f", (1.0/((struct scope_state*)devc->model_state)->sample_rate)*points);
	//sr_scpi_send(scpi, command);
	sr_dbg("time ref is %s", tmp_string);
	if(!strcmp(tmp_string, "LEFT")){
		sr_dbg("left");
		devc->refPos = -1;
	} else if(!strcmp(tmp_string, "CENT")){
		sr_dbg("cent");
		devc->refPos = -5;
	} else {
		sr_dbg("right");
		devc->refPos = -9;
	}
	devc->timebaseLbound = tmp_float*devc->refPos;
	devc->block_deltaT = 2000.0/((int)devc->sample_rate_limit);
	
	devc->num_block_to_download = ceil(devc->samples_limit/2000); //ToDo: This needs some sanitization to make sure only available points are being downloaded
	devc->trigger_at_sample = (uint64_t)((-devc->timebaseLbound)*(devc->sample_rate_limit));
	sr_dbg("Sample rate is: %ld", state->sample_rate);
	//Setup window view for first block download
	g_snprintf(command, sizeof(command), ":TIM:MODE MAIN;:TIM:RANG %f;:TIM:DEL %f", devc->block_deltaT, devc->timebaseLbound-devc->refPos*devc->block_deltaT*0.1);
	sr_scpi_send(scpi, command);

	sr_dbg("Download %d packets with a width of %f. Maxpoints are %d. Lbound is %f. Trigger at sample %ld", devc->num_block_to_download, devc->block_deltaT, points, devc->timebaseLbound, devc->trigger_at_sample);

	sr_dbg("beginning data download");
	//make final setup before download
	sr_scpi_send(scpi, ":WAV:FORM BYTE;BYT MSBF;UNS 0;POIN 2000");	//ToDo: when downloading high resolution data format needs to be word.

	//get header data and store it in channel->priv so the receive_data function can submit it. This only needs to be done for analog channels
	while(devc->current_channel){
		ch = (struct sr_channel *)devc->current_channel->data; 
		if(ch->type == SR_CHANNEL_ANALOG){
			ch->priv = (void *)g_malloc0(sizeof(struct analog_channel_transfer_info));
			encoding = (struct analog_channel_transfer_info *)ch->priv;
			sr_scpi_send(scpi, ":WAV:SOUR %s", ch->name);
			
			//get signed
			sr_scpi_get_bool(scpi, ":WAV:UNS?", &tmp_bool);
			encoding->is_unsigned = tmp_bool;
			
			//get transmission format. Byte is 1 byte per point, Word is 2 Byte per point
			sr_scpi_get_string(scpi, ":WAV:FORM?", &tmp_string);
			if(!strcmp("BYTE", tmp_string))
				encoding->is_eightbit = TRUE;
			else if(!strcmp("WORD", tmp_string))
				encoding->is_eightbit = FALSE;
			else{
				sr_err("unknown transmission format");
			}

			sr_scpi_get_float(scpi, ":WAV:YINC?", &tmp_float);
			encoding->yIncrement = tmp_float;

			sr_scpi_get_float(scpi, ":WAV:YOR?", &tmp_float);
			encoding->yOrigin = tmp_float;

			sr_scpi_get_int(scpi, ":WAV:YREF?", &tmp_int);
			sr_dbg("yRef is: %d", tmp_int);
			encoding->yReference = tmp_int;

			//ToDo: read data and write to encoding. dont forget to set and reset wav:source
			sr_dbg("Reading data complete");
		}
		devc->current_channel = devc->current_channel->next;	
	};
	devc->current_channel = devc->enabled_channels;
	ch = (struct sr_channel *)devc->current_channel->data;
	devc->failcount = 0;
	devc->trigger_sent = FALSE;
	
	
	//set waveform source channel to the first channel to be downloaded
	ch = (struct sr_channel *)devc->enabled_channels->data;
	if(ch->type == SR_CHANNEL_LOGIC){
		group = ch->index/DIGITAL_CHANNELS_PER_POD+1;
		g_snprintf(command, sizeof(command), ":WAV:SOUR POD%ld", group);
	} else {
		g_snprintf(command, sizeof(command), ":WAV:SOUR %s", ch->name);	
	}
	sr_scpi_send(scpi, command);

	//register callback
	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 100, agilent_54621d_receive_data, (void *)sdi);
	
	
	std_session_send_df_header(sdi);

	//std_session_send_df_header(sdi);
	std_session_send_df_frame_begin(sdi);
	
	//request data from instrument
	sr_scpi_send(scpi, ":SYST:DSP \"Reading Block 1/%d\"", devc->num_block_to_download);
	sr_scpi_send(scpi, ":WAV:DATA?");



	return SR_OK;
	
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	const struct scope_config *model;
	struct scope_state *state;
	float timebase;
	struct sr_channel *ch;

	devc = sdi->priv;
	model = devc->model_config;
	state = devc->model_state;

	std_session_send_df_end(sdi);

	timebase = (float) (*model->timebases)[state->timebase][0] / (*model->timebases)[state->timebase][1];

	while(devc->current_channel){
		ch = (struct sr_channel *)devc->current_channel->data; 
			if(ch->type == SR_CHANNEL_ANALOG)
				g_free((struct analog_channel_transfer_info *)ch->priv);
		devc->current_channel=devc->current_channel->next;
	}

	g_slist_free(devc->enabled_channels);
	devc->enabled_channels = NULL;
	scpi = sdi->conn;
	sr_scpi_source_remove(sdi->session, scpi);
	sr_scpi_send(scpi, ":SYST:DSP \"\"");
	sr_scpi_send(scpi, ":TIM:SCAL %f; :TIM:DEL 0", timebase);
	sr_scpi_send(scpi, ":TIM:MODE MAIN");
	return SR_OK;
}

static struct sr_dev_driver agilent_54621d_driver_info = {
	.name = "agilent-54621d",
	.longname = "Agilent 54621D",
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
SR_REGISTER_DEV_DRIVER(agilent_54621d_driver_info);
