/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Daniel <1824222@stud.hs-mannheim.de>
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

SR_PRIV void agilent_54621d_queue_logic_data(struct dev_context *devc,
				  size_t group, GByteArray *pod_data);
SR_PRIV void agilent_54621d_send_logic_packet(struct sr_dev_inst *sdi,
				   struct dev_context *devc);		
SR_PRIV void agilent_54621d_cleanup_logic_data(struct dev_context *devc);
		  
static struct scope_state *scope_state_new(const struct scope_config *config);

static int analog_channel_state_get(struct sr_dev_inst *sdi, const struct scope_config *config, struct scope_state *state);
static int digital_channel_state_get(struct sr_dev_inst *sdi, const struct scope_config *config, struct scope_state *state);
static int array_float_get(gchar *value, const uint64_t array[][2], int array_len, unsigned int *result);
static void scope_state_dump(const struct scope_config *config, struct scope_state *state);
static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi, const char *command, const char *(*array)[], unsigned int n, int *result);

#define MAX_COMMAND_SIZE 128
#define LOGIC_GET_THRESHOLD_SETTING "USER" //Threshold Setting that should be reported by the driver. Has to be included in logic_threshold. Has to be done in that hacky way since the device does only report threshold voltage level, yet not threshold setting

#define WAIT_FOR_CAPTURE_COMPLETE_RETRIES 100
#define WAIT_FOR_CAPTURE_COMPLETE_DELAY (100*1000)

static const char *agilent_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		      = ":FORM UINT,8;:POD%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		      = ":TIM:SCAL?",				//OK
	[SCPI_CMD_SET_TIMEBASE]		      = ":TIM:SCAL %s",				//OK
	[SCPI_CMD_GET_COUPLING]		      = ":CHAN%d:COUP?",			//OK
	[SCPI_CMD_SET_COUPLING]		      = ":CHAN%d:COUP %s",			//OK
	[SCPI_CMD_GET_SAMPLE_RATE]	      = ":ACQ:SRAT?",				//OK
	[SCPI_CMD_GET_ANALOG_DATA]	      = ":FORM:BORD %s;" \
					        ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL?",		//OK
	[SCPI_CMD_SET_VERTICAL_SCALE]	      = ":CHAN%d:SCAL %s",		//OK
	[SCPI_CMD_GET_DIG_POD_STATE]	      = ":POD%d:DISP?",			//Fixed
	[SCPI_CMD_SET_DIG_POD_STATE]	      = ":POD%d:DISP %d",		//Fixed
	[SCPI_CMD_GET_TRIGGER_SOURCE]	      = ":TRIG:SOUR?",			//Fixed
	[SCPI_CMD_SET_TRIGGER_SOURCE]	      = ":TRIG:SOUR %s",		//Fixed
	[SCPI_CMD_GET_TRIGGER_SLOPE]	      = ":TRIG:SLOP?",			//Fixed
	[SCPI_CMD_SET_TRIGGER_SLOPE]	      = ":TRIG:MODE EDGE;:TRIG:SLOP %s", 	//Fixed
	[SCPI_CMD_GET_TRIGGER_PATTERN]	      = ":TRIG:A:PATT:SOUR?",
	[SCPI_CMD_SET_TRIGGER_PATTERN]	      = ":TRIG:A:TYPE LOGIC;" \
					        ":TRIG:A:PATT:FUNC AND;" \
					        ":TRIG:A:PATT:COND \"TRUE\";" \
					        ":TRIG:A:PATT:MODE OFF;" \
					        ":TRIG:A:PATT:SOUR \"%s\"",
	[SCPI_CMD_GET_HIGH_RESOLUTION]	      = ":ACQ:HRES?",
	[SCPI_CMD_SET_HIGH_RESOLUTION]	      = ":ACQ:HRES %s",
	[SCPI_CMD_GET_PEAK_DETECTION]	      = ":ACQ:TYPE?",				//Fixed
	[SCPI_CMD_SET_PEAK_DETECTION]	      = ":ACQ:TYPE PEAK",			//Fixed
	[SCPI_CMD_GET_DIG_CHAN_STATE]	      = ":DIG%d:DISP?",				//Fixed
	[SCPI_CMD_SET_DIG_CHAN_STATE]	      = ":DIG%d:DISP %s",			//Fixed
	[SCPI_CMD_GET_VERTICAL_OFFSET]	      = ":CHAN%d:OFFS?",			//Fixed
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	      = ":TIM:POS?",				//OK
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	      = ":TIM:POS %s",				//OK
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]      = ":CHAN%d:DISP?",			//Fixed
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]      = ":CHAN%d:DISP %s",			//Fixed
	[SCPI_CMD_GET_PROBE_UNIT]	      	  = ":CHAN%d:UNIT?",			//Fixed
	[SCPI_CMD_GET_DIG_POD_THRESHOLD]      = ":POD%d:THR?",				//OK
	[SCPI_CMD_SET_DIG_POD_THRESHOLD]      = ":POD%d:THR %s",			//OK
};

static const uint32_t devopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
//	SR_CONF_TRIGGER_PATTERN | SR_CONF_GET | SR_CONF_SET,			//ToDo: Implement pattern trigger
	SR_CONF_PEAK_DETECTION | SR_CONF_GET | SR_CONF_SET,
//	SR_CONF_AVERAGING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVG_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST, 
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,				//The device doesn't actually support limiting samples, but will allways capture the maximum available amout of samples. However the driver can selectively transfere a subset of samples inorder to reduce transfer times.
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	//SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_digital[] = {
	SR_CONF_LOGIC_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LOGIC_THRESHOLD_CUSTOM | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const char *coupling_options[] = {
	"AC",
	"DC",
	"GND",
	//Agilent 5464x Scopes also allow 50Ohm Termination, however this is configured using the termination command. If someone wants to implement support for that you need to figure out the best way to do that 
};

static const char *scope_trigger_slopes[] = {
	"POS",
	"NEG",
};

static const char *logic_threshold[] = {
	"USER",
	"TTL",
	"ECL",
	"CMOS",
};

static const char *trigger_sources[] = {
	"CHAN1", "CHAN2",
	"LINE", "EXT", "NONE",
	"DIG0", "DIG1", "DIG2", "DIG3", "DIG4", "DIG5", "DIG6", "DIG7", "DIG8", "DIG9", "DIG10", "DIG11", "DIG12", "DIG13", "DIG14", "DIG15",
};

/* This is not currently used */
static const char *trigger_mode[] = {
	"EDGE",
	"GLIT",
	"PATT",
	"CAN",
	"DUR",
	"IIC",
	"LIN",
	"SEQ",
	"SPI",
	"TV",
	"USB",
};

static const uint64_t scope_timebases[][2] = {
	/* nanoseconds */
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

static const uint64_t vdivs[][2] = {
	/* millivolts */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
	{ 100, 1 },
};

static const char *scope_analog_channel_names[] = {
	"CHAN1", "CHAN2",
};

static const char *scope_digital_channel_names[] = {
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
	"D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",	
}; 

static struct scope_config scope_models[] = {
	{
		/* Agilent 54621D/54622D Models only differ in Bandwidth; everything else should be the same*/
		.name = {"54621D", "54622D", NULL},
		.analog_channels = 2,
		.digital_channels = 16,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &devopts,
		.num_devopts = ARRAY_SIZE(devopts),

		.devopts_cg_analog = &devopts_cg_analog,
		.num_devopts_cg_analog = ARRAY_SIZE(devopts_cg_analog),

		.devopts_cg_digital = &devopts_cg_digital,
		.num_devopts_cg_digital = ARRAY_SIZE(devopts_cg_digital),

		.digital_pods = 2,

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.logic_threshold = &logic_threshold,
		.num_logic_threshold = ARRAY_SIZE(logic_threshold),
		.logic_threshold_for_pod = TRUE,

		.trigger_sources = &trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &scope_timebases,
		.num_timebases = ARRAY_SIZE(scope_timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_xdivs = 10,
		.num_ydivs = 8,

		.scpi_dialect = &agilent_scpi_dialect,
	},
	//ToDo: Implement other scope models
};

SR_PRIV int agilent_54621d_update_sample_rate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	int tmp_int;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	if (sr_scpi_get_int(sdi->conn,
			      (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE],
			      &tmp_int) != SR_OK)
		return SR_ERR;

	state->sample_rate = tmp_int;

	if(devc->sample_rate_limit > (uint64_t)tmp_int)
		devc->sample_rate_limit = tmp_int;

	return SR_OK;
}

SR_PRIV int agilent_54621d_init_device(struct sr_dev_inst *sdi)
{
	int model_index;
	unsigned int i, j, group;
	struct sr_channel *ch;
	struct dev_context *devc;
	const char *cg_name;
	int ret;

	devc = sdi->priv;
	model_index = -1;

	/* find model  */
	for(i = 0; i < ARRAY_SIZE(scope_models); i++) {
		for(j = 0; scope_models[i].name[j]; j++) {
			if(!strcmp(sdi->model, scope_models[i].name[j])) {
				model_index = i;
			}
		}
		if(model_index != -1)
			break;
	}

	if(model_index == -1){
		sr_dbg("Unsupported device.");
		return SR_ERR_NA;
	}

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *scope_models[model_index].analog_channels);
	devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group*) *scope_models[model_index].digital_pods);
	if(!devc->analog_groups || !devc->digital_groups){
		g_free(devc->analog_groups);
		g_free(devc->digital_groups);
		return SR_ERR_MALLOC;
	}

	/* Add analog channels. */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
			   (*scope_models[model_index].analog_names)[i]);

		cg_name = (*scope_models[model_index].analog_names)[i];
		devc->analog_groups[i] = sr_channel_group_new(sdi, cg_name, NULL);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);
		//devc->analog_groups[i]->priv = (void *)g_malloc0(sizeof(struct analog_channel_transfer_info));
	}

	/* Add digital channel groups. */
	ret = SR_OK;
	for (i = 0; i < scope_models[model_index].digital_pods; i++) {
		devc->digital_groups[i] = sr_channel_group_new(sdi, NULL, NULL);
		if (!devc->digital_groups[i]) {
			ret = SR_ERR_MALLOC;
			break;
		}
		devc->digital_groups[i]->name = g_strdup_printf("POD%d", i + 1);
	}
	if (ret != SR_OK)
		return ret;

	/* Add digital channels. */
	for (i = 0; i < scope_models[model_index].digital_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
			   (*scope_models[model_index].digital_names)[i]);

		group = i / DIGITAL_CHANNELS_PER_POD;
		devc->digital_groups[group]->channels = g_slist_append(
			devc->digital_groups[group]->channels, ch);
	}

	devc->model_config = &scope_models[model_index];
	devc->samples_limit = 2000;
	devc->frame_limit = 0;
	devc->data_source = DATA_SOURCE_LIVE;
	devc->data = g_malloc(2000*sizeof(float));
	devc->sample_rate_limit = SR_MHZ(200);

	if (!(devc->model_state = scope_state_new(devc->model_config)))
		return SR_ERR_MALLOC;


	return SR_OK;
}

static struct scope_state *scope_state_new(const struct scope_config *config) {
	struct scope_state *state;

	state = g_malloc0(sizeof(struct scope_state));
	state->analog_channels = g_malloc0_n(config->analog_channels, sizeof(struct analog_channel_state));
	state->digital_channels = g_malloc0_n(config->digital_channels, sizeof(gboolean) * MAX_DIGITAL_CHANNEL_COUNT);			//ToDo: Why ony one bool size for the digital channel state? Shouldn't it be 1bool per channel?
	state->digital_pods = g_malloc0_n(config->digital_pods, sizeof(struct digital_pod_state));

	return state;
}

SR_PRIV int agilent_54621d_scope_state_get(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;
	unsigned int i;
	char *tmp_str;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	sr_info("Fetching scope state");

	//get analog channel state
	if (analog_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	//get digital channel state
	if (digital_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	//get timebase
	if (sr_scpi_get_string(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TIMEBASE],
			&tmp_str) != SR_OK)
		return SR_ERR;

	if (array_float_get(tmp_str, ARRAY_AND_SIZE(scope_timebases), &i) != SR_OK) {
		g_free(tmp_str);
		sr_err("Could not determine array index for time base.");
		return SR_ERR;
	}
	g_free(tmp_str);

	state->timebase = i;

	//get trigger horizontal position
	if(sr_scpi_get_float(sdi->conn, (*config->scpi_dialect)[SCPI_CMD_GET_HORIZ_TRIGGERPOS], &tmp_float) != SR_OK)
		return SR_ERR;
	
	state->horiz_triggerpos = tmp_float /
		(((double) (*config->timebases)[state->timebase][0] /
		  (*config->timebases)[state->timebase][1]) * config->num_xdivs);
	state->horiz_triggerpos -= 0.5;
	state->horiz_triggerpos *= -1;
	//ToDo: This might be dependent on time ref setting

	if (scope_state_get_array_option(sdi->conn,	(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SOURCE], config->trigger_sources, config->num_trigger_sources, &state->trigger_source) != SR_OK)
		return SR_ERR;

	if (scope_state_get_array_option(sdi->conn, (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SLOPE], config->trigger_slopes, config->num_trigger_slopes, &state->trigger_slope) != SR_OK)
		return SR_ERR;

	//ToDo: get trigger pattern
	//documentation for reading the trigger pattern is a little wonky so I need to test how this is done
	strncpy(state->trigger_pattern, "00000000000000000000", MAX_ANALOG_CHANNEL_COUNT + MAX_DIGITAL_CHANNEL_COUNT+1); 


	//ToDo: get current resolution
	//Default resolution is 8bit. aquiring at 8 bit also can increase transfer speed, since only a byte of data per point has to be transmitted
	//Resolution is a function of sweep speed and number of averages
	//Resolution > 8bit if acq mode == avg && (timebase >= 5us/div || num_avg>1) 
	state->high_resolution = FALSE;


	//get peak detection
	if (sr_scpi_get_string(sdi->conn,
			     (*config->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION],
			     &tmp_str) != SR_OK)
		return SR_ERR;
	if (!strcmp("PEAK", tmp_str))
		state->peak_detection = TRUE;
	else
		state->peak_detection = FALSE;
	g_free(tmp_str);

	if(agilent_54621d_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	sr_info("Fetching finished.");

	scope_state_dump(config, state);

	return SR_OK;
}

static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi, const char *command, const char *(*array)[], unsigned int n, int *result)
{
	char *tmp;
	int idx;

	if (sr_scpi_get_string(scpi, command, &tmp) != SR_OK)
		return SR_ERR;

	if ((idx = std_str_idx_s(tmp, *array, n)) < 0) {
		g_free(tmp);
		return SR_ERR_ARG;
	}

	*result = idx;

	g_free(tmp);

	return SR_OK;
}


/*
*This function is a helper function to output the scope configuration to the info log
*
*
*
*/
static void scope_state_dump(const struct scope_config *config, struct scope_state *state)
{
	unsigned int i;
	char *tmp;

	for (i = 0; i < config->analog_channels; i++) {
		tmp = sr_voltage_string((*config->vdivs)[state->analog_channels[i].vdiv][0],
					     (*config->vdivs)[state->analog_channels[i].vdiv][1]);
		sr_info("State of analog channel %d -> %s : %s (coupling) %s (vdiv) %2.2e (offset)",
			i + 1, state->analog_channels[i].state ? "On" : "Off",
			(*config->coupling_options)[state->analog_channels[i].coupling],
			tmp, state->analog_channels[i].vertical_offset);
	}

	for (i = 0; i < config->digital_channels; i++) {
		sr_info("State of digital channel %d -> %s", i,
			state->digital_channels[i] ? "On" : "Off");
	}

	for (i = 0; i < config->digital_pods; i++) {
		if (!strncmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold], 4) ||
		    !strcmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			sr_info("State of digital POD %d -> %s : %E (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				state->digital_pods[i].user_threshold);
		else
			sr_info("State of digital POD %d -> %s : %s (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				(*config->logic_threshold)[state->digital_pods[i].threshold]);
	}

	tmp = sr_period_string((*config->timebases)[state->timebase][0],
			       (*config->timebases)[state->timebase][1]);
	sr_info("Current timebase: %s", tmp);
	g_free(tmp);

	tmp = sr_samplerate_string(state->sample_rate);
	sr_info("Current samplerate: %s", tmp);
	g_free(tmp);

	if (!strcmp("PATT", (*config->trigger_sources)[state->trigger_source]))
		sr_info("Current trigger: %s (pattern), %.2f (offset)",
			state->trigger_pattern,
			state->horiz_triggerpos);
	else // Edge (slope) trigger
		sr_info("Current trigger: %s (source), %s (slope) %.2f (offset)",
			(*config->trigger_sources)[state->trigger_source],
			(*config->trigger_slopes)[state->trigger_slope],
			state->horiz_triggerpos);
}

/**
 * This function takes a value of the form "2.000E-03" and returns the index
 * of an array where a matching pair was found.
 *
 * @param value The string to be parsed.
 * @param array The array of s/f pairs.
 * @param array_len The number of pairs in the array.
 * @param result The index at which a matching pair was found.
 *
 * @return SR_ERR on any parsing error, SR_OK otherwise.
 */
static int array_float_get(gchar *value, const uint64_t array[][2], int array_len, unsigned int *result)
{
	struct sr_rational rval;
	struct sr_rational aval;

	if (sr_parse_rational(value, &rval) != SR_OK)
		return SR_ERR;

	for (int i = 0; i < array_len; i++) {
		sr_rational_set(&aval, array[i][0], array[i][1]);
		if (sr_rational_eq(&rval, &aval)) {
			*result = i;
			return SR_OK;
		}
	}

	return SR_ERR;
}

static struct sr_channel *get_channel_by_index_and_type(GSList *channel_lhead,
							int index, int type)
{
	while (channel_lhead) {
		struct sr_channel *ch = channel_lhead->data;
		if (ch->index == index && ch->type == type)
			return ch;

		channel_lhead = channel_lhead->next;
	}

	return 0;
}

static int analog_channel_state_get(struct sr_dev_inst *sdi, const struct scope_config *config, struct scope_state *state)
{
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	char *tmp_str;
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	for(i = 0; i < config->analog_channels; i++){
		//get channel enabled (visible)
		g_snprintf(command, sizeof(command), (*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_CHAN_STATE], i+1);

		if(sr_scpi_get_bool(scpi, command, &state->analog_channels[i].state) != SR_OK)
			return SR_ERR;
		
		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_ANALOG);
		if(ch)
			ch->enabled = state->analog_channels[i].state;
		
		//get vertical scale (v/div)
		g_snprintf(command, sizeof(command), (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_SCALE], i+1);

		if(sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if(array_float_get(tmp_str, *(config->vdivs), config->num_vdivs, &j) != SR_OK){
			g_free(tmp_str);
			sr_err("could not determine array index for vertical div scale.");
			return SR_ERR;
		}

		g_free(tmp_str);
		state->analog_channels[i].vdiv = j;

		//Get vertical offset
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_OFFSET],
			   i + 1);

		if (sr_scpi_get_float(scpi, command,
				     &state->analog_channels[i].vertical_offset) != SR_OK)
			return SR_ERR;

		//get coupling
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_COUPLING],
			   i + 1);

		if (scope_state_get_array_option(scpi, command, config->coupling_options,
					 config->num_coupling_options,
					 &state->analog_channels[i].coupling) != SR_OK)
			return SR_ERR;

		//get Unit (Amp/Volt)
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_PROBE_UNIT],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (tmp_str[0] == 'A')
			state->analog_channels[i].probe_unit = 'A';
		else
			state->analog_channels[i].probe_unit = 'V';
		g_free(tmp_str);
	}

	return SR_OK;
}

static int digital_channel_state_get(struct sr_dev_inst *sdi, const struct scope_config *config, struct scope_state *state)
{
	unsigned int i, user_index;
	char command[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	//get enabled channels
	for (i = 0; i < config->digital_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
			   i);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_channels[i]) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_LOGIC);
		if (ch)
			ch->enabled = state->digital_channels[i];
	}

	for (user_index = 0; user_index < ARRAY_SIZE(logic_threshold); user_index++){
		if(!strcmp(logic_threshold[i], LOGIC_GET_THRESHOLD_SETTING))
			break;
	}

	for (i = 0; i < config->digital_pods; i++){
		//get enabled pods
		g_snprintf(command, sizeof(command), (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_STATE], i+1);
		if (sr_scpi_get_bool(scpi, command, &state->digital_pods[i].state) != SR_OK)
			return SR_ERR;

		//get logic threshold
		//the device driver will allways report logic threshold to be "USER" with the currently set actual level saved as user level, since the device only reports current voltage but not the current setting		
		state->digital_pods[i].threshold = user_index;
		g_snprintf(command, sizeof(command), (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_THRESHOLD], i+1);
		
		if(sr_scpi_get_float(scpi, command, &state->digital_pods[i].user_threshold) != SR_OK)
			return SR_ERR;
		
	}
	return SR_OK;
}

/* Queue data of one channel group, for later submission. */
SR_PRIV void agilent_54621d_queue_logic_data(struct dev_context *devc,
				  size_t group, GByteArray *pod_data)
{
	size_t size;
	GByteArray *store;
	uint8_t *logic_data;
	size_t idx, logic_step;

	/*
	 * Upon first invocation, allocate the array which can hold the
	 * combined logic data for all channels. Assume that each channel
	 * will yield an identical number of samples per receive call.
	 *
	 * As a poor man's safety measure: (Silently) skip processing
	 * for unexpected sample counts, and ignore samples for
	 * unexpected channel groups. Don't bother with complicated
	 * resize logic, considering that many models only support one
	 * pod, and the most capable supported models have two pods of
	 * identical size. We haven't yet seen any "odd" configuration.
	 */
	if (!devc->logic_data) {
		size = pod_data->len * devc->pod_count; //ToDo: check if podcount is correctly set
		store = g_byte_array_sized_new(size);
		memset(store->data, 0, size);
		store = g_byte_array_set_size(store, size);
		devc->logic_data = store;
	} else {
		store = devc->logic_data;
		size = store->len / devc->pod_count;
		if (group >= devc->pod_count)
			return;
	}

	/*
	 * Fold the data of the most recently received channel group into
	 * the storage, where data resides for all channels combined.
	 */
	logic_data = store->data;
	logic_data += group;
	logic_step = devc->pod_count;
	for (idx = 0; idx < pod_data->len; idx++) {
		*logic_data = pod_data->data[idx];
		logic_data += logic_step;
	}

	/* Truncate acquisition if a smaller number of samples has been requested. */
	if (devc->samples_limit > 0 && devc->logic_data->len > devc->samples_limit * devc->pod_count)
		devc->logic_data->len = devc->samples_limit * devc->pod_count;
}

/* Submit data for all channels, after the individual groups got collected. */
SR_PRIV void agilent_54621d_send_logic_packet(struct sr_dev_inst *sdi,
				   struct dev_context *devc)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (!devc->logic_data)
		return;

	logic.data = devc->logic_data->data;
	logic.length = devc->logic_data->len;
	logic.unitsize = devc->pod_count;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;

	sr_session_send(sdi, &packet);
}

/* Undo previous resource allocation. */
SR_PRIV void agilent_54621d_cleanup_logic_data(struct dev_context *devc)
{

	if (devc->logic_data) {
		g_byte_array_free(devc->logic_data, TRUE);
		devc->logic_data = NULL;
	}
	/*
	 * Keep 'pod_count'! It's required when more frames will be
	 * received, and does not harm when kept after acquisition.
	 */
}


SR_PRIV int agilent_54621d_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_channel *ch;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	GByteArray *data;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	size_t group;
	int i;
	struct analog_channel_transfer_info *info;
	signed int tmp_int;
	char command[MAX_COMMAND_SIZE];
	float timebase_offset;
	char *tmp_string;

	tmp_string = NULL;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	ch = devc->current_channel->data;

	switch (ch->type){
		case SR_CHANNEL_ANALOG:
			info = (struct analog_channel_transfer_info *)ch->priv;
			data = NULL;
			if(sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK){
				if(data)
					g_byte_array_free(data, TRUE);
				sr_err("Failed to retreive block");
				devc->failcount++;
				if(devc->failcount>=3){
					sr_scpi_send(sdi->conn, ":WAV:DATA?");
					devc->failcount=0;
				}
				
				return TRUE;
			}
			devc->failcount=0;

			//Append downloaded block to buffer -- This should no longer be neccessary
			//g_byte_array_append(devc->buffer, data->data, data->len);


			sr_dbg("yRef: %d, yInc: %f, yOri: %f", info->yReference, info->yIncrement, info->yOrigin);
			for(i = 0; i<(int)data->len; i++){
				tmp_int=(int8_t)data->data[i];
				devc->data[i] = (((float)(tmp_int) - info->yReference) * info->yIncrement) + info->yOrigin;
			}
			sr_analog_init(&analog, &encoding, &meaning, &spec, 2); //ToDo: 2 digits is just placeholder. needs to be calculated correctly
			analog.meaning->channels = g_slist_append(NULL, ch);
			analog.num_samples = data->len;
			analog.data = devc->data;
			analog.meaning->mq = SR_MQ_VOLTAGE;
			analog.meaning->unit = SR_UNIT_VOLT;
			analog.meaning->mqflags = 0;

			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			sr_session_send(sdi, &packet);
			
			g_slist_free(analog.meaning->channels);

			
			g_byte_array_free(data, TRUE);
			data = NULL;
			break;
		case SR_CHANNEL_LOGIC:
			data = NULL;
			if(sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK){
				if(data)
					g_byte_array_free(data, TRUE);
				sr_err("Failed to retreive block");
				devc->failcount++;
				if(devc->failcount>=3){
					sr_scpi_send(sdi->conn, ":WAV:DATA?");
					devc->failcount=0;
				}
				
				return TRUE;
			}
			devc->failcount=0;

			group = ch->index / DIGITAL_CHANNELS_PER_POD;
			agilent_54621d_queue_logic_data(devc, group, data);


			break;
		default:
			sr_err("Invalid channel type");
			break;
	}

	//Sometimes the trailing \nl on a datablock is received delayed an not read by the sr_get_data_block. therefore we try to read a response and just dismiss it if there is any
	if(sr_scpi_read_data(sdi->conn, tmp_string, 1) == SR_OK)
		sr_info("Received delayed NL on block download");

	//if more channels need to be downloaded for the current frame
	if(devc->current_channel->next){
		devc->current_channel = devc->current_channel->next;
		//sr_scpi_send(sdi->conn, ":WAV:SOUR %s;DATA?", ((struct sr_channel *)devc->current_channel->data)->name);
		ch = (struct sr_channel *)devc->current_channel->data;
		if(ch->type == SR_CHANNEL_LOGIC){
			group = ch->index/DIGITAL_CHANNELS_PER_POD+1;
			g_snprintf(command, sizeof(command), ":WAV:SOUR POD%ld;DATA?", group);
		} else {
			g_snprintf(command, sizeof(command), ":WAV:SOUR %s;UNS 0;DATA?", ch->name);	
		}
		sr_scpi_send(sdi->conn, command);
		return TRUE;
	}
	agilent_54621d_send_logic_packet(sdi, devc);

	//if more blocks need to be downloaded
	sr_dbg("Downloaded %d/%d datablocks", devc->num_blocks_downloaded, devc->num_block_to_download);
	if(devc->num_blocks_downloaded+1 < devc->num_block_to_download){
		devc->num_blocks_downloaded++;
		devc->current_channel = devc->enabled_channels;
		timebase_offset = devc->timebaseLbound+(devc->num_blocks_downloaded+0.5)*devc->block_deltaT;
		sr_scpi_send(sdi->conn, ":TIM:DEL %f", timebase_offset);
		sr_scpi_send(sdi->conn, ":SYST:DSP \"Reading Block %d/%d\"", devc->num_blocks_downloaded+1, devc->num_block_to_download);
		//sr_scpi_send(sdi->conn, ":WAV:SOUR %s;DATA?", ((struct sr_channel *)devc->current_channel->data)->name);
		ch = (struct sr_channel *)devc->current_channel->data;
		if(ch->type == SR_CHANNEL_LOGIC){
			group = ch->index/DIGITAL_CHANNELS_PER_POD+1;
			g_snprintf(command, sizeof(command), ":WAV:SOUR POD%ld;DATA?", group);
		} else {
			g_snprintf(command, sizeof(command), ":WAV:SOUR %s;UNS 0;DATA?", ch->name);	
		}
		sr_scpi_send(sdi->conn, command);
		
		return TRUE;
	} else {
		agilent_54621d_cleanup_logic_data(devc);
		std_session_send_df_frame_end(sdi);
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}
	return FALSE;
}

