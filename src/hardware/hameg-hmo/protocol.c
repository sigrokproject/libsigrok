/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
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
#include <math.h>
#include <stdlib.h>
#include "scpi.h"
#include "protocol.h"

static const char *hameg_scpi_dialect[] = {
	[SCPI_CMD_GET_DIG_DATA]		    = ":FORM UINT,8;:POD%d:DATA?",
	[SCPI_CMD_GET_TIMEBASE]		    = ":TIM:SCAL?",
	[SCPI_CMD_SET_TIMEBASE]		    = ":TIM:SCAL %s",
	[SCPI_CMD_GET_COUPLING]		    = ":CHAN%d:COUP?",
	[SCPI_CMD_SET_COUPLING]		    = ":CHAN%d:COUP %s",
	[SCPI_CMD_GET_SAMPLE_RATE]	    = ":ACQ:SRAT?",
	[SCPI_CMD_GET_SAMPLE_RATE_LIVE]	    = ":%s:DATA:POINTS?",
	[SCPI_CMD_GET_ANALOG_DATA]	    = ":FORM REAL,32;:CHAN%d:DATA?",
	[SCPI_CMD_GET_VERTICAL_DIV]	    = ":CHAN%d:SCAL?",
	[SCPI_CMD_SET_VERTICAL_DIV]	    = ":CHAN%d:SCAL %s",
	[SCPI_CMD_GET_DIG_POD_STATE]	    = ":POD%d:STAT?",
	[SCPI_CMD_SET_DIG_POD_STATE]	    = ":POD%d:STAT %d",
	[SCPI_CMD_GET_TRIGGER_SLOPE]	    = ":TRIG:A:EDGE:SLOP?",
	[SCPI_CMD_SET_TRIGGER_SLOPE]	    = ":TRIG:A:EDGE:SLOP %s",
	[SCPI_CMD_GET_TRIGGER_SOURCE]	    = ":TRIG:A:SOUR?",
	[SCPI_CMD_SET_TRIGGER_SOURCE]	    = ":TRIG:A:SOUR %s",
	[SCPI_CMD_GET_DIG_CHAN_STATE]	    = ":LOG%d:STAT?",
	[SCPI_CMD_SET_DIG_CHAN_STATE]	    = ":LOG%d:STAT %d",
	[SCPI_CMD_GET_VERTICAL_OFFSET]	    = ":CHAN%d:POS?",
	[SCPI_CMD_GET_HORIZ_TRIGGERPOS]	    = ":TIM:POS?",
	[SCPI_CMD_SET_HORIZ_TRIGGERPOS]	    = ":TIM:POS %s",
	[SCPI_CMD_GET_ANALOG_CHAN_STATE]    = ":CHAN%d:STAT?",
	[SCPI_CMD_SET_ANALOG_CHAN_STATE]    = ":CHAN%d:STAT %d",
	[SCPI_CMD_GET_PROBE_UNIT]	    = ":PROB%d:SET:ATT:UNIT?",
};

static const uint32_t hmo_devopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
};

static const uint32_t hmo_analog_devopts[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *hmo_coupling_options[] = {
	"AC",  // AC with 50 Ohm termination (152x, 202x, 30xx, 1202)
	"ACL", // AC with 1 MOhm termination
	"DC",  // DC with 50 Ohm termination
	"DCL", // DC with 1 MOhm termination
	"GND",
	NULL,
};

static const char *scope_trigger_slopes[] = {
	"POS",
	"NEG",
	"EITH",
	NULL,
};

static const char *hmo_compact2_trigger_sources[] = {
	"CH1",
	"CH2",
	"LINE",
	"EXT",
	"PATT",
	"BUS1",
	"BUS2",
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	NULL,
};

static const char *hmo_compact4_trigger_sources[] = {
	"CH1",
	"CH2",
	"CH3",
	"CH4",
	"LINE",
	"EXT",
	"PATT",
	"BUS1",
	"BUS2",
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	NULL,
};

static const uint64_t hmo_timebases[][2] = {
	/* nanoseconds */
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 200, 1000000000 },
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

static const uint64_t hmo_vdivs[][2] = {
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
};

static const char *scope_analog_channel_names[] = {
	"CH1",
	"CH2",
	"CH3",
	"CH4",
};

static const char *scope_digital_channel_names[] = {
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	"D8",
	"D9",
	"D10",
	"D11",
	"D12",
	"D13",
	"D14",
	"D15",
};

static const struct scope_config scope_models[] = {
	{
		/* HMO2522/3032/3042/3052 support 16 digital channels but they're not supported yet. */
		.name = {"HMO1002", "HMO722", "HMO1022", "HMO1522", "HMO2022", "HMO2522",
				"HMO3032", "HMO3042", "HMO3052", NULL},
		.analog_channels = 2,
		.digital_channels = 8,
		.digital_pods = 1,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &hmo_devopts,
		.num_devopts = ARRAY_SIZE(hmo_devopts),

		.analog_devopts = &hmo_analog_devopts,
		.num_analog_devopts = ARRAY_SIZE(hmo_analog_devopts),

		.coupling_options = &hmo_coupling_options,
		.trigger_sources = &hmo_compact2_trigger_sources,
		.trigger_slopes = &scope_trigger_slopes,

		.timebases = &hmo_timebases,
		.num_timebases = ARRAY_SIZE(hmo_timebases),

		.vdivs = &hmo_vdivs,
		.num_vdivs = ARRAY_SIZE(hmo_vdivs),

		.num_xdivs = 12,
		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
	{
		/* HMO2524/3034/3044/3054 support 16 digital channels but they're not supported yet. */
		.name = {"HMO724", "HMO1024", "HMO1524", "HMO2024", "HMO2524",
				"HMO3034", "HMO3044", "HMO3054", NULL},
		.analog_channels = 4,
		.digital_channels = 8,
		.digital_pods = 1,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names,

		.devopts = &hmo_devopts,
		.num_devopts = ARRAY_SIZE(hmo_devopts),

		.analog_devopts = &hmo_analog_devopts,
		.num_analog_devopts = ARRAY_SIZE(hmo_analog_devopts),

		.coupling_options = &hmo_coupling_options,
		.trigger_sources = &hmo_compact4_trigger_sources,
		.trigger_slopes = &scope_trigger_slopes,

		.timebases = &hmo_timebases,
		.num_timebases = ARRAY_SIZE(hmo_timebases),

		.vdivs = &hmo_vdivs,
		.num_vdivs = ARRAY_SIZE(hmo_vdivs),

		.num_xdivs = 12,
		.num_ydivs = 8,

		.scpi_dialect = &hameg_scpi_dialect,
	},
};

static void scope_state_dump(const struct scope_config *config,
			     struct scope_state *state)
{
	unsigned int i;
	char *tmp;

	for (i = 0; i < config->analog_channels; i++) {
		tmp = sr_voltage_string((*config->vdivs)[state->analog_channels[i].vdiv][0],
					     (*config->vdivs)[state->analog_channels[i].vdiv][1]);
		sr_info("State of analog channel  %d -> %s : %s (coupling) %s (vdiv) %2.2e (offset)",
			i + 1, state->analog_channels[i].state ? "On" : "Off",
			(*config->coupling_options)[state->analog_channels[i].coupling],
			tmp, state->analog_channels[i].vertical_offset);
	}

	for (i = 0; i < config->digital_channels; i++) {
		sr_info("State of digital channel %d -> %s", i,
			state->digital_channels[i] ? "On" : "Off");
	}

	for (i = 0; i < config->digital_pods; i++) {
		sr_info("State of digital POD %d -> %s", i,
			state->digital_pods[i] ? "On" : "Off");
	}

	tmp = sr_period_string((*config->timebases)[state->timebase][0] *
			       (*config->timebases)[state->timebase][1]);
	sr_info("Current timebase: %s", tmp);
	g_free(tmp);

	tmp = sr_samplerate_string(state->sample_rate);
	sr_info("Current samplerate: %s", tmp);
	g_free(tmp);

	sr_info("Current trigger: %s (source), %s (slope) %.2f (offset)",
		(*config->trigger_sources)[state->trigger_source],
		(*config->trigger_slopes)[state->trigger_slope],
		state->horiz_triggerpos);
}

static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi,
		const char *command, const char *(*array)[], int *result)
{
	char *tmp;
	unsigned int i;

	if (sr_scpi_get_string(scpi, command, &tmp) != SR_OK) {
		g_free(tmp);
		return SR_ERR;
	}

	for (i = 0; (*array)[i]; i++) {
		if (!g_strcmp0(tmp, (*array)[i])) {
			*result = i;
			g_free(tmp);
			tmp = NULL;
			break;
		}
	}

	if (tmp) {
		g_free(tmp);
		return SR_ERR;
	}

	return SR_OK;
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
static int array_float_get(gchar *value, const uint64_t array[][2],
		int array_len, unsigned int *result)
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

static int analog_channel_state_get(struct sr_scpi_dev_inst *scpi,
				    const struct scope_config *config,
				    struct scope_state *state)
{
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	char *tmp_str;

	for (i = 0; i < config->analog_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_CHAN_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->analog_channels[i].state) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_DIV],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (array_float_get(tmp_str, hmo_vdivs, ARRAY_SIZE(hmo_vdivs),
				&j) != SR_OK) {
			g_free(tmp_str);
			sr_err("Could not determine array index for vertical div scale.");
			return SR_ERR;
		}

		g_free(tmp_str);
		state->analog_channels[i].vdiv = j;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_OFFSET],
			   i + 1);

		if (sr_scpi_get_float(scpi, command,
				     &state->analog_channels[i].vertical_offset) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_COUPLING],
			   i + 1);

		if (scope_state_get_array_option(scpi, command, config->coupling_options,
					 &state->analog_channels[i].coupling) != SR_OK)
			return SR_ERR;

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

static int digital_channel_state_get(struct sr_scpi_dev_inst *scpi,
				     const struct scope_config *config,
				     struct scope_state *state)
{
	unsigned int i;
	char command[MAX_COMMAND_SIZE];

	for (i = 0; i < config->digital_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
			   i);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_channels[i]) != SR_OK)
			return SR_ERR;
	}

	for (i = 0; i < config->digital_pods; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_pods[i]) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int hmo_update_sample_rate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;

	int tmp;
	unsigned int i;
	float tmp_float;
	gboolean channel_found;
	char tmp_str[MAX_COMMAND_SIZE];
	char chan_name[20];

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;
	channel_found = FALSE;

	for (i = 0; i < config->analog_channels; i++) {
		if (state->analog_channels[i].state) {
			g_snprintf(chan_name, sizeof(chan_name), "CHAN%d", i + 1);
			g_snprintf(tmp_str, sizeof(tmp_str),
				   (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE_LIVE],
				   chan_name);
			channel_found = TRUE;
			break;
		}
	}

	if (!channel_found) {
		for (i = 0; i < config->digital_pods; i++) {
			if (state->digital_pods[i]) {
				g_snprintf(chan_name, sizeof(chan_name), "POD%d", i);
				g_snprintf(tmp_str, sizeof(tmp_str),
					   (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE_LIVE],
					   chan_name);
				channel_found = TRUE;
				break;
			}
		}
	}

	/* No channel is active, ask the instrument for the sample rate
	 * in single shot mode */
	if (!channel_found) {
		if (sr_scpi_get_float(sdi->conn,
				      (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE],
				      &tmp_float) != SR_OK)
			return SR_ERR;

		state->sample_rate = tmp_float;
	} else {
		if (sr_scpi_get_int(sdi->conn, tmp_str, &tmp) != SR_OK)
			return SR_ERR;
		state->sample_rate = tmp / (((float) (*config->timebases)[state->timebase][0] /
					     (*config->timebases)[state->timebase][1]) *
					    config->num_xdivs);
	}

	return SR_OK;
}

SR_PRIV int hmo_scope_state_get(struct sr_dev_inst *sdi)
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

	if (analog_channel_state_get(sdi->conn, config, state) != SR_OK)
		return SR_ERR;

	if (digital_channel_state_get(sdi->conn, config, state) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_float(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TIMEBASE],
			&tmp_float) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TIMEBASE],
			&tmp_str) != SR_OK)
		return SR_ERR;

	if (array_float_get(tmp_str, hmo_timebases, ARRAY_SIZE(hmo_timebases),
			&i) != SR_OK) {
		g_free(tmp_str);
		sr_err("Could not determine array index for time base.");
		return SR_ERR;
	}
	g_free(tmp_str);

	state->timebase = i;

	if (sr_scpi_get_float(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_HORIZ_TRIGGERPOS],
			&tmp_float) != SR_OK)
		return SR_ERR;
	state->horiz_triggerpos = tmp_float /
		(((double) (*config->timebases)[state->timebase][0] /
		  (*config->timebases)[state->timebase][1]) * config->num_xdivs);
	state->horiz_triggerpos -= 0.5;
	state->horiz_triggerpos *= -1;

	if (scope_state_get_array_option(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SOURCE],
			config->trigger_sources, &state->trigger_source) != SR_OK)
		return SR_ERR;

	if (scope_state_get_array_option(sdi->conn,
		(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SLOPE],
		config->trigger_slopes, &state->trigger_slope) != SR_OK)
		return SR_ERR;

	if (hmo_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	sr_info("Fetching finished.");

	scope_state_dump(config, state);

	return SR_OK;
}

static struct scope_state *scope_state_new(const struct scope_config *config)
{
	struct scope_state *state;

	state = g_malloc0(sizeof(struct scope_state));
	state->analog_channels = g_malloc0_n(config->analog_channels,
			sizeof(struct analog_channel_state));
	state->digital_channels = g_malloc0_n(
			config->digital_channels, sizeof(gboolean));
	state->digital_pods = g_malloc0_n(config->digital_pods,
			sizeof(gboolean));

	return state;
}

SR_PRIV void hmo_scope_state_free(struct scope_state *state)
{
	g_free(state->analog_channels);
	g_free(state->digital_channels);
	g_free(state->digital_pods);
	g_free(state);
}

SR_PRIV int hmo_init_device(struct sr_dev_inst *sdi)
{
	char tmp[25];
	int model_index;
	unsigned int i, j;
	struct sr_channel *ch;
	struct dev_context *devc;

	devc = sdi->priv;
	model_index = -1;

	/* Find the exact model. */
	for (i = 0; i < ARRAY_SIZE(scope_models); i++) {
		for (j = 0; scope_models[i].name[j]; j++) {
			if (!strcmp(sdi->model, scope_models[i].name[j])) {
				model_index = i;
				break;
			}
		}
		if (model_index != -1)
			break;
	}

	if (model_index == -1) {
		sr_dbg("Unsupported HMO device.");
		return SR_ERR_NA;
	}

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					scope_models[model_index].analog_channels);

	devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					 scope_models[model_index].digital_pods);

	/* Add analog channels. */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
			   (*scope_models[model_index].analog_names)[i]);

		devc->analog_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->analog_groups[i]->name = g_strdup(
			(char *)(*scope_models[model_index].analog_names)[i]);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups,
						   devc->analog_groups[i]);
	}

	/* Add digital channel groups. */
	for (i = 0; i < scope_models[model_index].digital_pods; i++) {
		g_snprintf(tmp, 25, "POD%d", i);

		devc->digital_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->digital_groups[i]->name = g_strdup(tmp);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				   devc->digital_groups[i < 8 ? 0 : 1]);
	}

	/* Add digital channels. */
	for (i = 0; i < scope_models[model_index].digital_channels; i++) {
		ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE,
			   (*scope_models[model_index].digital_names)[i]);

		devc->digital_groups[i < 8 ? 0 : 1]->channels = g_slist_append(
			devc->digital_groups[i < 8 ? 0 : 1]->channels, ch);
	}

	devc->model_config = &scope_models[model_index];
	devc->frame_limit = 0;

	if (!(devc->model_state = scope_state_new(devc->model_config)))
		return SR_ERR_MALLOC;

	return SR_OK;
}

SR_PRIV int hmo_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_channel *ch;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct scope_state *state;
	struct sr_datafeed_packet packet;
	GByteArray *data;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_datafeed_logic logic;

	(void)fd;
	(void)revents;

	data = NULL;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	/* Although this is correct in general, the USBTMC libusb implementation
	 * currently does not generate an event prior to the first read. Often
	 * it is ok to start reading just after the 50ms timeout. See bug #785.
	if (revents != G_IO_IN)
		return TRUE;
	*/

	ch = devc->current_channel->data;
	state = devc->model_state;

	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);

			return TRUE;
		}

		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);

		packet.type = SR_DF_ANALOG;

		analog.data = data->data;
		analog.num_samples = data->len / sizeof(float);
		analog.encoding = &encoding;
		analog.meaning = &meaning;
		analog.spec = &spec;

		encoding.unitsize = sizeof(float);
		encoding.is_signed = TRUE;
		encoding.is_float = TRUE;
		encoding.is_bigendian = FALSE;
		encoding.digits = 0;
		encoding.is_digits_decimal = FALSE;
		encoding.scale.p = 1;
		encoding.scale.q = 1;
		encoding.offset.p = 0;
		encoding.offset.q = 1;
		if (state->analog_channels[ch->index].probe_unit == 'V') {
			meaning.mq = SR_MQ_VOLTAGE;
			meaning.unit = SR_UNIT_VOLT;
		} else {
			meaning.mq = SR_MQ_CURRENT;
			meaning.unit = SR_UNIT_AMPERE;
		}
		meaning.mqflags = 0;
		meaning.channels = g_slist_append(NULL, ch);
		spec.spec_digits = 0;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		g_slist_free(meaning.channels);
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	case SR_CHANNEL_LOGIC:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			g_free(data);
			return TRUE;
		}

		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);

		logic.length = data->len;
		logic.unitsize = 1;
		logic.data = data->data;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		sr_session_send(sdi, &packet);
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	default:
		sr_err("Invalid channel type.");
		break;
	}

	packet.type = SR_DF_FRAME_END;
	sr_session_send(sdi, &packet);

	if (devc->current_channel->next) {
		devc->current_channel = devc->current_channel->next;
		hmo_request_data(sdi);
	} else if (++devc->num_frames == devc->frame_limit) {
		sdi->driver->dev_acquisition_stop(sdi);
	} else {
		devc->current_channel = devc->enabled_channels;
		hmo_request_data(sdi);
	}

	return TRUE;
}
