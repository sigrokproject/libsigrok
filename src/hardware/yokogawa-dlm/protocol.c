/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 abraxa (Soeren Apel) <soeren@apelpie.net>
 * Based on the Hameg HMO driver by poljar (Damir Jelić) <poljarinho@gmail.com>
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

/**
 * @file
 *
 * <em>Yokogawa DL/DLM series</em> oscilloscope driver
 * @internal
 */

#include "protocol.h"

static const char *dlm_coupling_options[] = {
	"AC",
	"DC",
	"DC50",
	"GND",
	NULL,
};

static const char *dlm_2ch_trigger_sources[] = {
	"1",
	"2",
	"LINE",
	"EXT",
	NULL,
};

/* TODO: Is BITx handled correctly or is Dx required? */
static const char *dlm_4ch_trigger_sources[] = {
	"1",
	"2",
	"3",
	"4",
	"LINE",
	"EXT",
	"BIT1",
	"BIT2",
	"BIT3",
	"BIT4",
	"BIT5",
	"BIT6",
	"BIT7",
	"BIT8",
	NULL,
};

/* Note: Values must correlate to the trigger_slopes values. */
const char *dlm_trigger_slopes[3] = {
	"r",
	"f",
	NULL,
};

const uint64_t dlm_timebases[36][2] = {
	/* nanoseconds */
	{ 1, 1000000000 },
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
	{ 100, 1 },
	{ 200, 1 },
	{ 500, 1 },
};

const uint64_t dlm_vdivs[17][2] = {
	/* millivolts */
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
	{ 200, 1 },
	{ 500, 1 },
};

static const char *scope_analog_channel_names[] = {
	"1",
	"2",
	"3",
	"4",
};

static const char *scope_digital_channel_names_8[] = {
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
};

static const char *scope_digital_channel_names_32[] = {
	"A0",
	"A1",
	"A2",
	"A3",
	"A4",
	"A5",
	"A6",
	"A7",
	"B0",
	"B1",
	"B2",
	"B3",
	"B4",
	"B5",
	"B6",
	"B7",
	"C0",
	"C1",
	"C2",
	"C3",
	"C4",
	"C5",
	"C6",
	"C7",
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
};

static const struct scope_config scope_models[] = {
	{
		.model_id   = {"710105",  "710115",  "710125",  NULL},
		.model_name = {"DLM2022", "DLM2032", "DLM2052", NULL},
		.analog_channels = 2,
		.digital_channels = 0,
		.pods = 0,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names_8,

		.coupling_options = &dlm_coupling_options,
		.trigger_sources = &dlm_2ch_trigger_sources,

		.num_xdivs = 10,
		.num_ydivs = 8,
	},
	{
		.model_id    = {"710110",  "710120",  "710130",  NULL},
		.model_name  = {"DLM2024", "DLM2034", "DLM2054", NULL},
		.analog_channels = 4,
		.digital_channels = 8,
		.pods = 1,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names_8,

		.coupling_options = &dlm_coupling_options,
		.trigger_sources = &dlm_4ch_trigger_sources,

		.num_xdivs = 10,
		.num_ydivs = 8,
	},
	{
		.model_id   = {"701307", "701308",  "701310", "701311",
				"701312", "701313",  NULL},
		.model_name = {"DL9040", "DL9040L", "DL9140", "DL9140L",
				"DL9240", "DL9240L", NULL},
		.analog_channels = 4,
		.digital_channels = 0,
		.pods = 0,

		.analog_names = &scope_analog_channel_names,
		.digital_names = NULL,

		.coupling_options = &dlm_coupling_options,
		.trigger_sources = &dlm_4ch_trigger_sources,

		.num_xdivs = 10,
		.num_ydivs = 8,
	},
	{
		.model_id   = {"701320",  "701321",  NULL},
		.model_name = {"DL9505L", "DL9510L", NULL},
		.analog_channels = 4,
		.digital_channels = 16,
		.pods = 4,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names_32,

		.coupling_options = &dlm_coupling_options,
		.trigger_sources = &dlm_4ch_trigger_sources,

		.num_xdivs = 10,
		.num_ydivs = 8,
	},
	{
		.model_id   = {"701330",  "701331",  NULL},
		.model_name = {"DL9705L", "DL9710L", NULL},
		.analog_channels = 4,
		.digital_channels = 32,
		.pods = 4,

		.analog_names = &scope_analog_channel_names,
		.digital_names = &scope_digital_channel_names_32,

		.coupling_options = &dlm_coupling_options,
		.trigger_sources = &dlm_4ch_trigger_sources,

		.num_xdivs = 10,
		.num_ydivs = 8,
	},
};

/**
 * Prints out the state of the device as we currently know it.
 *
 * @param config This is the scope configuration.
 * @param state The current scope state to print.
 */
static void scope_state_dump(const struct scope_config *config,
		struct scope_state *state)
{
	unsigned int i;
	char *tmp;

	for (i = 0; i < config->analog_channels; i++) {
		tmp = sr_voltage_string(dlm_vdivs[state->analog_states[i].vdiv][0],
				dlm_vdivs[state->analog_states[i].vdiv][1]);
		sr_info("State of analog channel %d -> %s : %s (coupling) %s (vdiv) %2.2e (offset)",
				i + 1, state->analog_states[i].state ? "On" : "Off",
				(*config->coupling_options)[state->analog_states[i].coupling],
				tmp, state->analog_states[i].vertical_offset);
	}

	for (i = 0; i < config->digital_channels; i++) {
		sr_info("State of digital channel %d -> %s", i,
				state->digital_states[i] ? "On" : "Off");
	}

	for (i = 0; i < config->pods; i++) {
		sr_info("State of digital POD %d -> %s", i,
				state->pod_states[i] ? "On" : "Off");
	}

	tmp = sr_period_string(dlm_timebases[state->timebase][0] *
			dlm_timebases[state->timebase][1]);
	sr_info("Current timebase: %s", tmp);
	g_free(tmp);

	tmp = sr_samplerate_string(state->sample_rate);
	sr_info("Current samplerate: %s", tmp);
	g_free(tmp);

	sr_info("Current samples per acquisition (i.e. frame): %d",
			state->samples_per_frame);

	sr_info("Current trigger: %s (source), %s (slope) %.2f (offset)",
			(*config->trigger_sources)[state->trigger_source],
			dlm_trigger_slopes[state->trigger_slope],
			state->horiz_triggerpos);
}

/**
 * Searches through an array of strings and returns the index to the
 * array where a given string is located.
 *
 * @param value The string to search for.
 * @param array The array of strings.
 * @param result The index at which value is located in array. -1 on error.
 *
 * @return SR_ERR when value couldn't be found, SR_OK otherwise.
 */
static int array_option_get(char *value, const char *(*array)[],
		int *result)
{
	unsigned int i;

	*result = -1;

	for (i = 0; (*array)[i]; i++)
		if (!g_strcmp0(value, (*array)[i])) {
			*result = i;
			break;
		}

	if (*result == -1)
		return SR_ERR;

	return SR_OK;
}

/**
 * This function takes a value of the form "2.000E-03", converts it to a
 * significand / factor pair and returns the index of an array where
 * a matching pair was found.
 *
 * It's a bit convoluted because of floating-point issues. The value "10.00E-09"
 * is parsed by g_ascii_strtod() as 0.000000009999999939, for example.
 * Therefore it's easier to break the number up into two strings and handle
 * them separately.
 *
 * @param value The string to be parsed.
 * @param array The array of s/f pairs.
 * @param array_len The number of pairs in the array.
 * @param result The index at which a matching pair was found.
 *
 * @return SR_ERR on any parsing error, SR_OK otherwise.
 */
static int array_float_get(gchar *value, const uint64_t array[][2],
		int array_len, int *result)
{
	int i;
	uint64_t f;
	float s;
	unsigned int s_int;
	gchar ss[10], es[10];

	memset(ss, 0, sizeof(ss));
	memset(es, 0, sizeof(es));

	strncpy(ss, value, 5);
	strncpy(es, &(value[6]), 3);

	if (sr_atof_ascii(ss, &s) != SR_OK)
		return SR_ERR;
	if (sr_atoi(es, &i) != SR_OK)
		return SR_ERR;

	/* Transform e.g. 10^-03 to 1000 as the array stores the inverse. */
	f = pow(10, abs(i));

	/*
	 * Adjust the significand/factor pair to make sure
	 * that f is a multiple of 1000.
	 */
	while ((int)fmod(log10(f), 3) > 0) { s *= 10; f *= 10; }

	/* Truncate s to circumvent rounding errors. */
	s_int = (unsigned int)s;

	for (i = 0; i < array_len; i++) {
		if ( (s_int == array[i][0]) && (f == array[i][1]) ) {
			*result = i;
			return SR_OK;
		}
	}

	return SR_ERR;
}

/**
 * Obtains information about all analog channels from the oscilloscope.
 * The internal state information is updated accordingly.
 *
 * @param sdi The device instance.
 * @param config The device's device configuration.
 * @param state The device's state information.
 *
 * @return SR_ERR on error, SR_OK otherwise.
 */
static int analog_channel_state_get(const struct sr_dev_inst *sdi,
		const struct scope_config *config,
		struct scope_state *state)
{
	struct sr_scpi_dev_inst *scpi;
	int i, j;
	GSList *l;
	struct sr_channel *ch;
	gchar *response;

	scpi = sdi->conn;

	for (i = 0; i < config->analog_channels; i++) {

		if (dlm_analog_chan_state_get(scpi, i + 1,
				&state->analog_states[i].state) != SR_OK)
			return SR_ERR;

		for (l = sdi->channels; l; l = l->next) {
			ch = l->data;
			if (ch->index == i) {
				ch->enabled = state->analog_states[i].state;
				break;
			}
		}

		if (dlm_analog_chan_vdiv_get(scpi, i + 1, &response) != SR_OK)
			return SR_ERR;

		if (array_float_get(response, dlm_vdivs, ARRAY_SIZE(dlm_vdivs),
				&j) != SR_OK) {
			g_free(response);
			return SR_ERR;
		}

		g_free(response);
		state->analog_states[i].vdiv = j;

		if (dlm_analog_chan_voffs_get(scpi, i + 1,
				&state->analog_states[i].vertical_offset) != SR_OK)
			return SR_ERR;

		if (dlm_analog_chan_wrange_get(scpi, i + 1,
				&state->analog_states[i].waveform_range) != SR_OK)
			return SR_ERR;

		if (dlm_analog_chan_woffs_get(scpi, i + 1,
				&state->analog_states[i].waveform_offset) != SR_OK)
			return SR_ERR;

		if (dlm_analog_chan_coupl_get(scpi, i + 1, &response) != SR_OK) {
			g_free(response);
			return SR_ERR;
		}

		if (array_option_get(response, config->coupling_options,
				&state->analog_states[i].coupling) != SR_OK) {
			g_free(response);
			return SR_ERR;
		}
		g_free(response);
	}

	return SR_OK;
}

/**
 * Obtains information about all digital channels from the oscilloscope.
 * The internal state information is updated accordingly.
 *
 * @param sdi The device instance.
 * @param config The device's device configuration.
 * @param state The device's state information.
 *
 * @return SR_ERR on error, SR_OK otherwise.
 */
static int digital_channel_state_get(const struct sr_dev_inst *sdi,
		const struct scope_config *config,
		struct scope_state *state)
{
	struct sr_scpi_dev_inst *scpi;
	int i;
	GSList *l;
	struct sr_channel *ch;

	scpi = sdi->conn;

	if (!config->digital_channels) {
		sr_warn("Tried obtaining digital channel states on a " \
				"model without digital inputs.");
		return SR_OK;
	}

	for (i = 0; i < config->digital_channels; i++) {
		if (dlm_digital_chan_state_get(scpi, i + 1,
				&state->digital_states[i]) != SR_OK) {
			return SR_ERR;
		}

		for (l = sdi->channels; l; l = l->next) {
			ch = l->data;
			if (ch->index == i + DLM_DIG_CHAN_INDEX_OFFS) {
				ch->enabled = state->digital_states[i];
				break;
			}
		}
	}

	if (!config->pods) {
		sr_warn("Tried obtaining pod states on a model without pods.");
		return SR_OK;
	}

	for (i = 0; i < config->pods; i++) {
		if (dlm_digital_pod_state_get(scpi, i + 'A',
				&state->pod_states[i]) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int dlm_channel_state_set(const struct sr_dev_inst *sdi,
		const int ch_index, gboolean ch_state)
{
	GSList *l;
	struct sr_channel *ch;
	struct dev_context *devc = NULL;
	struct scope_state *state;
	const struct scope_config *model = NULL;
	struct sr_scpi_dev_inst *scpi;
	gboolean chan_found;
	gboolean *pod_enabled;
	int i, result;

	result = SR_OK;

	scpi = sdi->conn;
	devc = sdi->priv;
	state = devc->model_state;
	model = devc->model_config;
	chan_found = FALSE;

	pod_enabled = g_malloc0(sizeof(gboolean) * model->pods);

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;

		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			if (ch->index == ch_index) {
				if (dlm_analog_chan_state_set(scpi, ch->index + 1, ch_state) != SR_OK) {
					result = SR_ERR;
					break;
				}

				ch->enabled = ch_state;
				state->analog_states[ch->index].state = ch_state;
				chan_found = TRUE;
				break;
			}
			break;
		case SR_CHANNEL_LOGIC:
			i = ch->index - DLM_DIG_CHAN_INDEX_OFFS;

			if (ch->index == ch_index) {
				if (dlm_digital_chan_state_set(scpi, i + 1, ch_state) != SR_OK) {
					result = SR_ERR;
					break;
				}

				ch->enabled = ch_state;
				state->digital_states[i] = ch_state;
				chan_found = TRUE;

				/* The corresponding pod has to be enabled also. */
				pod_enabled[i / 8] |= ch->enabled;
			} else {
				/* Also check all other channels. Maybe we can disable a pod. */
				pod_enabled[i / 8] |= ch->enabled;
			}
			break;
		default:
			result = SR_ERR_NA;
		}
	}

	for (i = 0; i < model->pods; i++) {
		if (state->pod_states[i] == pod_enabled[i])
			continue;

		if (dlm_digital_pod_state_set(scpi, i + 1, pod_enabled[i]) != SR_OK) {
			result = SR_ERR;
			break;
		}

		state->pod_states[i] = pod_enabled[i];
	}

	g_free(pod_enabled);

	if ((result == SR_OK) && !chan_found)
		result = SR_ERR_BUG;

	return result;
}

/**
 * Obtains information about the sample rate from the oscilloscope.
 * The internal state information is updated accordingly.
 *
 * @param sdi The device instance.
 *
 * @return SR_ERR on error, SR_OK otherwise.
 */
SR_PRIV int dlm_sample_rate_query(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	float tmp_float;

	devc = sdi->priv;
	state = devc->model_state;

	/*
	 * No need to find an active channel to query the sample rate:
	 * querying any channel will do, so we use channel 1 all the time.
	 */
	if (dlm_analog_chan_srate_get(sdi->conn, 1, &tmp_float) != SR_OK)
		return SR_ERR;

	state->sample_rate = tmp_float;

	return SR_OK;
}

/**
 * Obtains information about the current device state from the oscilloscope,
 * including all analog and digital channel configurations.
 * The internal state information is updated accordingly.
 *
 * @param sdi The device instance.
 *
 * @return SR_ERR on error, SR_OK otherwise.
 */
SR_PRIV int dlm_scope_state_query(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;
	gchar *response;
	int i;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	if (analog_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (digital_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (dlm_timebase_get(sdi->conn, &response) != SR_OK)
		return SR_ERR;

	if (array_float_get(response, dlm_timebases,
			ARRAY_SIZE(dlm_timebases), &i) != SR_OK) {
		g_free(response);
		return SR_ERR;
	}

	g_free(response);
	state->timebase = i;

	if (dlm_horiz_trigger_pos_get(sdi->conn, &tmp_float) != SR_OK)
		return SR_ERR;

	/* TODO: Check if the calculation makes sense for the DLM. */
	state->horiz_triggerpos = tmp_float /
			(((double)dlm_timebases[state->timebase][0] /
			dlm_timebases[state->timebase][1]) * config->num_xdivs);
	state->horiz_triggerpos -= 0.5;
	state->horiz_triggerpos *= -1;

	if (dlm_trigger_source_get(sdi->conn, &response) != SR_OK) {
		g_free(response);
		return SR_ERR;
	}

	if (array_option_get(response, config->trigger_sources,
			&state->trigger_source) != SR_OK) {
		g_free(response);
		return SR_ERR;
	}

	g_free(response);

	if (dlm_trigger_slope_get(sdi->conn, &i) != SR_OK)
		return SR_ERR;

	state->trigger_slope = i;

	if (dlm_acq_length_get(sdi->conn, &state->samples_per_frame) != SR_OK) {
		sr_err("Failed to query acquisition length.");
		return SR_ERR;
	}

	dlm_sample_rate_query(sdi);

	scope_state_dump(config, state);

	return SR_OK;
}

/**
 * Creates a new device state structure.
 *
 * @param config The device configuration to use.
 *
 * @return The newly allocated scope_state struct.
 */
static struct scope_state *dlm_scope_state_new(const struct scope_config *config)
{
	struct scope_state *state;

	state = g_malloc0(sizeof(struct scope_state));

	state->analog_states = g_malloc0(config->analog_channels *
			sizeof(struct analog_channel_state));

	state->digital_states = g_malloc0(config->digital_channels *
			sizeof(gboolean));

	state->pod_states = g_malloc0(config->pods * sizeof(gboolean));

	return state;
}

/**
 * Frees the memory that was allocated by a call to dlm_scope_state_new().
 *
 * @param state The device state structure whose memory is to be freed.
 */
SR_PRIV void dlm_scope_state_destroy(struct scope_state *state)
{
	g_free(state->analog_states);
	g_free(state->digital_states);
	g_free(state->pod_states);
	g_free(state);
}

SR_PRIV int dlm_model_get(char *model_id, char **model_name, int *model_index)
{
	unsigned int i, j;

	*model_index = -1;
	*model_name = NULL;

	for (i = 0; i < ARRAY_SIZE(scope_models); i++) {
		for (j = 0; scope_models[i].model_id[j]; j++) {
			if (!strcmp(model_id, scope_models[i].model_id[j])) {
				*model_index = i;
				*model_name = (char *)scope_models[i].model_name[j];
				break;
			}
		}
		if (*model_index != -1)
			break;
	}

	if (*model_index == -1) {
		sr_err("Found unsupported DLM device with model identifier %s.",
				model_id);
		return SR_ERR_NA;
	}

	return SR_OK;
}

/**
 * Attempts to initialize a DL/DLM device and prepares internal structures
 * if a suitable device was found.
 *
 * @param sdi The device instance.
 */
SR_PRIV int dlm_device_init(struct sr_dev_inst *sdi, int model_index)
{
	char tmp[25];
	int i;
	struct sr_channel *ch;
	struct dev_context *devc;

	devc = sdi->priv;

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
			scope_models[model_index].analog_channels);

	devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group*) *
			scope_models[model_index].pods);

	/* Add analog channels, each in its own group. */
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
	for (i = 0; i < scope_models[model_index].pods; i++) {
		g_snprintf(tmp, sizeof(tmp), "POD%d", i);

		devc->digital_groups[i] = g_malloc0(sizeof(struct sr_channel_group));
		if (!devc->digital_groups[i])
			return SR_ERR_MALLOC;

		devc->digital_groups[i]->name = g_strdup(tmp);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				devc->digital_groups[i]);
	}

	/* Add digital channels. */
	for (i = 0; i < scope_models[model_index].digital_channels; i++) {
		ch = sr_channel_new(sdi, DLM_DIG_CHAN_INDEX_OFFS + i,
				SR_CHANNEL_LOGIC, TRUE,
				(*scope_models[model_index].digital_names)[i]);

		devc->digital_groups[i / 8]->channels = g_slist_append(
				devc->digital_groups[i / 8]->channels, ch);
	}
	devc->model_config = &scope_models[model_index];
	devc->frame_limit = 0;

	if (!(devc->model_state = dlm_scope_state_new(devc->model_config)))
		return SR_ERR_MALLOC;

	/* Disable non-standard response behavior. */
	if (dlm_response_headers_set(sdi->conn, FALSE) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int dlm_channel_data_request(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int result;

	devc = sdi->priv;
	ch = devc->current_channel->data;

	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
		result = dlm_analog_data_get(sdi->conn, ch->index + 1);
		break;
	case SR_CHANNEL_LOGIC:
		result = dlm_digital_data_get(sdi->conn);
		break;
	default:
		sr_err("Invalid channel type encountered (%d).",
				ch->type);
		result = SR_ERR;
	}

	if (result == SR_OK)
		devc->data_pending = TRUE;
	else
		devc->data_pending = FALSE;

	return result;
}

/**
 * Reads and removes the block data header from a given data input.
 * Format is #ndddd... with n being the number of decimal digits d.
 * The string dddd... contains the decimal-encoded length of the data.
 * Example: #9000000013 would yield a length of 13 bytes.
 *
 * @param data The input data.
 * @param len The determined input data length.
 */
static int dlm_block_data_header_process(GArray *data, int *len)
{
	int i, n;
	gchar s[20];

	if (g_array_index(data, gchar, 0) != '#')
		return SR_ERR;

	n = (uint8_t)(g_array_index(data, gchar, 1) - '0');

	for (i = 0; i < n; i++)
		s[i] = g_array_index(data, gchar, 2 + i);
	s[i] = 0;

	if (sr_atoi(s, len) != SR_OK)
		return SR_ERR;

	g_array_remove_range(data, 0, 2 + n);

	return SR_OK;
}

/**
 * Turns raw sample data into voltages and sends them off to the session bus.
 *
 * @param data The raw sample data.
 * @ch_state Pointer to the state of the channel whose data we're processing.
 * @sdi The device instance.
 *
 * @return SR_ERR when data is trucated, SR_OK otherwise.
 */
static int dlm_analog_samples_send(GArray *data,
		struct analog_channel_state *ch_state,
		struct sr_dev_inst *sdi)
{
	uint32_t i, samples;
	float voltage, range, offset;
	GArray *float_data;
	struct dev_context *devc;
	struct scope_state *model_state;
	struct sr_channel *ch;
	struct sr_datafeed_analog analog;
	struct sr_datafeed_packet packet;

	devc = sdi->priv;
	model_state = devc->model_state;
	samples = model_state->samples_per_frame;
	ch = devc->current_channel->data;

	if (data->len < samples * sizeof(uint8_t)) {
		sr_err("Truncated waveform data packet received.");
		return SR_ERR;
	}

	range = ch_state->waveform_range;
	offset = ch_state->waveform_offset;

	/*
	 * Convert byte sample to voltage according to
	 * page 269 of the Communication Interface User's Manual.
	 */
	float_data = g_array_new(FALSE, FALSE, sizeof(float));
	for (i = 0; i < samples; i++) {
		voltage = (float)g_array_index(data, int8_t, i);
		voltage = (range * voltage /
				DLM_DIVISION_FOR_BYTE_FORMAT) + offset;
		g_array_append_val(float_data, voltage);
	}

	analog.channels = g_slist_append(NULL, ch);
	analog.num_samples = float_data->len;
	analog.data = (float*)float_data->data;
	analog.mq = SR_MQ_VOLTAGE;
	analog.unit = SR_UNIT_VOLT;
	analog.mqflags = 0;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.channels);

	g_array_free(float_data, TRUE);
	g_array_remove_range(data, 0, samples * sizeof(uint8_t));

	return SR_OK;
}

/**
 * Sends logic sample data off to the session bus.
 *
 * @param data The raw sample data.
 * @ch_state Pointer to the state of the channel whose data we're processing.
 * @sdi The device instance.
 *
 * @return SR_ERR when data is trucated, SR_OK otherwise.
 */
static int dlm_digital_samples_send(GArray *data,
		struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *model_state;
	uint32_t samples;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet packet;

	devc = sdi->priv;
	model_state = devc->model_state;
	samples = model_state->samples_per_frame;

	if (data->len < samples * sizeof(uint8_t)) {
		sr_err("Truncated waveform data packet received.");
		return SR_ERR;
	}

	logic.length = samples;
	logic.unitsize = 1;
	logic.data = data->data;
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	sr_session_send(sdi, &packet);

	g_array_remove_range(data, 0, samples * sizeof(uint8_t));

	return SR_OK;
}

/**
 * Attempts to query sample data from the oscilloscope in order to send it
 * to the session bus for further processing.
 *
 * @param fd The file descriptor used as the event source.
 * @param revents The received events.
 * @param cb_data Callback data, in this case our device instance.
 *
 * @return TRUE in case of success or a recoverable error,
 *         FALSE when a fatal error was encountered.
 */
SR_PRIV int dlm_data_receive(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct scope_state *model_state;
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_datafeed_packet packet;
	int chunk_len, num_bytes;
	static GArray *data = NULL;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return FALSE;

	if (!(devc = sdi->priv))
		return FALSE;

	if (!(model_state = (struct scope_state*)devc->model_state))
		return FALSE;

	/* Are we waiting for a response from the device? */
	if (!devc->data_pending)
		return TRUE;

	/* Check if a new query response is coming our way. */
	if (!data) {
		if (sr_scpi_read_begin(sdi->conn) == SR_OK)
			/* The 16 here accounts for the header and EOL. */
			data = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t),
					16 + model_state->samples_per_frame);
		else
			return TRUE;
	}

	/* Store incoming data. */
	chunk_len = sr_scpi_read_data(sdi->conn, devc->receive_buffer,
			RECEIVE_BUFFER_SIZE);
	if (chunk_len < 0) {
		sr_err("Error while reading data: %d", chunk_len);
		goto fail;
	}
	g_array_append_vals(data, devc->receive_buffer, chunk_len);

	/* Read the entire query response before processing. */
	if (!sr_scpi_read_complete(sdi->conn))
		return TRUE;

	/* We finished reading and are no longer waiting for data. */
	devc->data_pending = FALSE;

	/* Signal the beginning of a new frame if this is the first channel. */
	if (devc->current_channel == devc->enabled_channels) {
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);
	}

	if (dlm_block_data_header_process(data, &num_bytes) != SR_OK) {
		sr_err("Encountered malformed block data header.");
		goto fail;
	}

	if (num_bytes == 0) {
		sr_warn("Zero-length waveform data packet received. " \
				"Live mode not supported yet, stopping " \
				"acquisition and retrying.");
		/* Don't care about return value here. */
		dlm_acquisition_stop(sdi->conn);
		g_array_free(data, TRUE);
		dlm_channel_data_request(sdi);
		return TRUE;
	}

	ch = devc->current_channel->data;
	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
		if (dlm_analog_samples_send(data,
				&model_state->analog_states[ch->index],
				sdi) != SR_OK)
			goto fail;
		break;
	case SR_CHANNEL_LOGIC:
		if (dlm_digital_samples_send(data, sdi) != SR_OK)
			goto fail;
		break;
	default:
		sr_err("Invalid channel type encountered.");
		break;
	}

	g_array_free(data, TRUE);
	data = NULL;

	/*
	 * Signal the end of this frame if this was the last enabled channel
	 * and set the next enabled channel. Then, request its data.
	 */
	if (!devc->current_channel->next) {
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);
		devc->current_channel = devc->enabled_channels;

		/*
		 * As of now we only support importing the current acquisition
		 * data so we're going to stop at this point.
		 */
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	} else
		devc->current_channel = devc->current_channel->next;

	if (dlm_channel_data_request(sdi) != SR_OK) {
		sr_err("Failed to request acquisition data.");
		goto fail;
	}

	return TRUE;

fail:
	if (data) {
		g_array_free(data, TRUE);
		data = NULL;
	}

	return FALSE;
}
