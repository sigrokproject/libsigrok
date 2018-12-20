/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Guido Trentalancia <guido@trentalancia.com>
 * Inspired by the Hameg HMO driver by poljar (Damir Jelić) <poljarinho@gmail.com>
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
#include "model_desc.h"

SR_PRIV void rs_queue_logic_data(struct dev_context *devc,
				  const GByteArray *pod_data);
SR_PRIV void rs_send_logic_packet(const struct sr_dev_inst *sdi,
				   const struct dev_context *devc);
SR_PRIV void rs_cleanup_logic_data(struct dev_context *devc);

/*
 * List of all the possible SCPI command string prefixes
 * that can be used to set the data length for the chosen
 * data format.
 *
 * These are needed to detect how many bytes are used by
 * a given dialect for digital data: see, for example,
 * SCPI_CMD_GET_DIG_DATA for the RTO series.
 *
 * At the moment, the prefixes are the same for all the
 * supported dialects.
 */

static const char *format_length_scpi_cmd_prefix[] = {
	"FORM REAL,",
	"FORM INT,",
	"FORM UINT,",
};

static void scope_state_dump(const struct scope_config *config,
			     const struct scope_state *state)
{
	unsigned int i;
	char *tmp1, *tmp2, *tmp3;

	for (i = 0; i < config->analog_channels; i++) {
		tmp1 = sr_voltage_per_div_string((*config->vscale)[state->analog_channels[i].vscale][0],
					     (*config->vscale)[state->analog_channels[i].vscale][1]);
		sr_info("State of analog channel %d -> %s : %s (coupling) %s (vscale) %2.2e (offset)",
			i + 1, state->analog_channels[i].state ? "On" : "Off",
			(*config->coupling_options)[state->analog_channels[i].coupling],
			tmp1, state->analog_channels[i].vertical_offset);
	}

	for (i = 0; i < config->digital_channels; i++) {
		sr_info("State of digital channel %d -> %s", i,
			state->digital_channels[i] ? "On" : "Off");
	}

	for (i = 0; i < config->digital_pods; i++) {
		if (!g_ascii_strncasecmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold], 4) ||
		    !g_ascii_strcasecmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold]))
			sr_info("State of digital POD %d -> %s : %E (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				state->digital_pods[i].user_threshold);
		else
			sr_info("State of digital POD %d -> %s : %s (threshold)", i + 1,
				state->digital_pods[i].state ? "On" : "Off",
				(*config->logic_threshold)[state->digital_pods[i].threshold]);
	}

	tmp1 = sr_period_string((*config->timebases)[state->timebase][0],
			       (*config->timebases)[state->timebase][1]);
	sr_info("Current timebase: %s", tmp1);
	g_free(tmp1);

	tmp1 = sr_samplerate_string(state->sample_rate);
	sr_info("Current samplerate: %s", tmp1);
	g_free(tmp1);

	if (!g_ascii_strcasecmp("PATT", (*config->trigger_sources)[state->trigger_source])) {
		sr_info("Current trigger: %s (pattern), %.2f (offset)",
			state->trigger_pattern,
			state->horiz_triggerpos);
	} else { /* Edge Trigger: slope and, when available, coupling and filters. */
		if ((*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_COUPLING] &&
		    config->edge_trigger_coupling && config->num_edge_trigger_coupling)
			tmp1 = g_strdup_printf(", %s (coupling)",
					       (*config->edge_trigger_coupling)[state->edge_trigger_coupling]);
		else
			tmp1 = g_strdup("");
		if ((*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_LOWPASS])
			tmp2 = g_strdup_printf(", low-pass filter: %s",
					       state->edge_trigger_lowpass ? "On" : "Off");
		else
			tmp2 = g_strdup("");
		if ((*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_NOISE_REJ])
			tmp3 = g_strdup_printf(", noise reject filter: %s",
					       state->edge_trigger_noise_rej ? "On" : "Off");
		else
			tmp3 = g_strdup("");
		sr_info("Current trigger: %s (source), %s (slope), %.2f (offset)%s%s%s",
			(*config->trigger_sources)[state->trigger_source],
			(*config->edge_trigger_slopes)[state->edge_trigger_slope],
			state->horiz_triggerpos,
			tmp1, tmp2, tmp3);
		g_free(tmp1);
		g_free(tmp2);
		g_free(tmp3);
	}
}

static int scope_state_get_array_option(struct sr_scpi_dev_inst *scpi,
		const char *command, const char *(*array)[], const unsigned int n, unsigned int *result)
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
		const int array_len, unsigned int *result)
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
							const int index, const int type)
{
	while (channel_lhead) {
		struct sr_channel *ch = channel_lhead->data;
		if (ch->index == index && ch->type == type)
			return ch;

		channel_lhead = channel_lhead->next;
	}

	return 0;
}

static int analog_channel_state_get(const struct sr_dev_inst *sdi,
				    const struct scope_config *config,
				    struct scope_state *state)
{
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	char *tmp_str;
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	for (i = 0; i < config->analog_channels; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_CHAN_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->analog_channels[i].state) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_ANALOG);
		if (ch)
			ch->enabled = state->analog_channels[i].state;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_SCALE],
			   i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (array_float_get(tmp_str, ARRAY_AND_SIZE(vscale), &j) != SR_OK) {
			g_free(tmp_str);
			sr_err("Could not determine array index for vertical div scale.");
			return SR_ERR;
		}

		g_free(tmp_str);
		state->analog_channels[i].vscale = j;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_VERTICAL_OFFSET],
			   i + 1);

		if (sr_scpi_get_float(scpi, command,
				     &state->analog_channels[i].vertical_offset) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_COUPLING],
			   i + 1);

		if (config->coupling_options && config->num_coupling_options)
			if (scope_state_get_array_option(scpi, command, config->coupling_options,
						 config->num_coupling_options,
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

		/* The logic threshold for analog channels is not supported on all models. */
		if ((*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_THRESHOLD]) {
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_ANALOG_THRESHOLD],
				   i + 1);

			if (sr_scpi_get_float(scpi, command,
					      &state->analog_channels[i].user_threshold) != SR_OK)
				return SR_ERR;
		}

		/* Determine the bandwidth limit. */
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_BANDWIDTH_LIMIT],
			   i + 1);

		if (scope_state_get_array_option(scpi, command,
						 config->bandwidth_limit, config->num_bandwidth_limit,
						 &state->analog_channels[i].bandwidth_limit) != SR_OK)
			return SR_ERR;
	}

	return SR_OK;
}

static int digital_channel_state_get(const struct sr_dev_inst *sdi,
				     const struct scope_config *config,
				     struct scope_state *state)
{
	unsigned int i, idx, nibble1ch2, nibble2ch2, tmp_uint;
	int result = SR_ERR;
	char *logic_threshold_short[config->num_logic_threshold];
	char command[MAX_COMMAND_SIZE];
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi = sdi->conn;
	float tmp_float;

	for (i = 0; i < config->digital_channels; i++) {
		if (g_ascii_strncasecmp("RTO", sdi->model, 3))
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
				   i);
		else
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_CHAN_STATE],
				   (i / DIGITAL_CHANNELS_PER_POD) + 1, i);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_channels[i]) != SR_OK)
			return SR_ERR;

		ch = get_channel_by_index_and_type(sdi->channels, i, SR_CHANNEL_LOGIC);
		if (ch)
			ch->enabled = state->digital_channels[i];
	}

	/* According to the SCPI standard, on models that support multiple
	 * user-defined logic threshold settings the response to the command
	 * SCPI_CMD_GET_DIG_POD_THRESHOLD might return "USER" instead of
	 * "USER1".
	 *
	 * This makes more difficult to validate the response when the logic
	 * threshold is set to "USER1" and therefore we need to prevent device
	 * opening failures in such configuration case...
	 */
	for (i = 0; i < config->num_logic_threshold; i++) {
		logic_threshold_short[i] = g_strdup((*config->logic_threshold)[i]);
		if (!g_ascii_strcasecmp("USER1", (*config->logic_threshold)[i]))
			g_strlcpy(logic_threshold_short[i],
				  (*config->logic_threshold)[i], strlen((*config->logic_threshold)[i]));
	}

	for (i = 0; i < config->digital_pods; i++) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_STATE],
			   i + 1);

		if (sr_scpi_get_bool(scpi, command,
				     &state->digital_pods[i].state) != SR_OK)
			goto exit;

		if (config->logic_threshold && config->num_logic_threshold) {
			/* Check if the threshold command is based on the POD or nibble channel index. */
			if (config->logic_threshold_pod_index) {
				idx = i + 1;
			} else {
				nibble1ch2 = i * DIGITAL_CHANNELS_PER_POD + 1;
				nibble2ch2 = (i + 1) * DIGITAL_CHANNELS_PER_POD - DIGITAL_CHANNELS_PER_NIBBLE + 1;
				idx = nibble1ch2;
			}
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_THRESHOLD],
				   idx);

			/* Check for both standard and shortened responses. */
			if (scope_state_get_array_option(scpi, command, config->logic_threshold,
							 config->num_logic_threshold,
							 &state->digital_pods[i].threshold) != SR_OK)
				if (scope_state_get_array_option(scpi, command, (const char * (*)[]) &logic_threshold_short,
								 config->num_logic_threshold,
								 &state->digital_pods[i].threshold) != SR_OK)
					goto exit;

			/* Same as above, but for the second nibble (second channel), if needed. */
			if (!config->logic_threshold_pod_index) {
				if (scope_state_get_array_option(scpi, command, config->logic_threshold,
								 config->num_logic_threshold,
								 &tmp_uint) != SR_OK)
					if (scope_state_get_array_option(scpi, command, (const char * (*)[]) &logic_threshold_short,
									 config->num_logic_threshold,
									 &tmp_uint) != SR_OK)
						goto exit;

				/* If the two nibbles don't match, use the first one. */
				if (state->digital_pods[i].threshold != tmp_uint) {
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_SET_DIG_POD_THRESHOLD],
						   nibble2ch2,
						   (*config->logic_threshold)[state->digital_pods[i].threshold]);
					if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					    sr_scpi_get_opc(sdi->conn) != SR_OK)
						goto exit;
				}
			}

			/* If used-defined or custom threshold is active, get the level. */
			if (!g_ascii_strcasecmp("USER1", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				g_snprintf(command, sizeof(command),
					   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
					   idx, 1); /* USER1 logic threshold setting. */
			} else if (!g_ascii_strcasecmp("USER2", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				g_snprintf(command, sizeof(command),
					   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
					   idx, 2); /* USER2 for custom logic_threshold setting. */
			} else if (!g_ascii_strcasecmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
				   !g_ascii_strcasecmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				if (g_ascii_strncasecmp("RTO", sdi->model, 3)) {
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
						   idx); /* USER or MAN for custom logic_threshold setting. */
				} else { /* The RTO series divides each POD in two channel groups. */
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
						   idx, idx * 2); /* MAN setting on the second channel group. */
					if (sr_scpi_get_float(scpi, command,
					    &tmp_float) != SR_OK)
						goto exit;
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_GET_DIG_POD_USER_THRESHOLD],
						   idx, idx * 2 - 1); /* MAN setting on the first channel group. */
				}
			}
			if (!g_ascii_strcasecmp("USER1", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			    !g_ascii_strcasecmp("USER2", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			    !g_ascii_strcasecmp("USER", (*config->logic_threshold)[state->digital_pods[i].threshold]) ||
			    !g_ascii_strcasecmp("MAN", (*config->logic_threshold)[state->digital_pods[i].threshold])) {
				if (sr_scpi_get_float(scpi, command,
				    &state->digital_pods[i].user_threshold) != SR_OK)
					goto exit;

				/* Set the same custom threshold on the second nibble, if needed. */
				if (!config->logic_threshold_pod_index) {
					g_snprintf(command, sizeof(command),
						   (*config->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
						   nibble2ch2,
						   (*config->logic_threshold)[state->digital_pods[i].threshold]);
					if (sr_scpi_send(sdi->conn, command) != SR_OK ||
					    sr_scpi_get_opc(sdi->conn) != SR_OK)
						goto exit;
				}

				/* On the RTO series set the same custom threshold on both channel groups of each POD. */
				if (!g_ascii_strncasecmp("RTO", sdi->model, 3)) {
					if (state->digital_pods[i].user_threshold != tmp_float) {
						g_snprintf(command, sizeof(command),
							   (*config->scpi_dialect)[SCPI_CMD_SET_DIG_POD_USER_THRESHOLD],
							   idx, idx * 2, state->digital_pods[i].user_threshold);
						if (sr_scpi_send(sdi->conn, command) != SR_OK ||
						    sr_scpi_get_opc(sdi->conn) != SR_OK)
							goto exit;
					}
				}
			}
		}
	}

	result = SR_OK;

exit:
	for (i = 0; i < config->num_logic_threshold; i++)
		g_free(logic_threshold_short[i]);

	return result;
}

SR_PRIV int rs_update_sample_rate(const struct sr_dev_inst *sdi)
{
	const struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	float tmp_float;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	if (sr_scpi_get_float(sdi->conn,
			      (*config->scpi_dialect)[SCPI_CMD_GET_SAMPLE_RATE],
			      &tmp_float) != SR_OK)
		return SR_ERR;

	state->sample_rate = tmp_float;

	return SR_OK;
}

SR_PRIV int rs_scope_state_get(const struct sr_dev_inst *sdi)
{
	const struct dev_context *devc;
	struct scope_state *state;
	struct scope_config *config;
	float tmp_float;
	unsigned int i;
	int idx;
	char *tmp_str, *tmp_str2;
	char command[MAX_COMMAND_SIZE];

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	sr_info("Fetching scope state");

	/* Save existing Math Expression. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_MATH_EXPRESSION],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_string(sdi->conn, command, &tmp_str) != SR_OK)
		return SR_ERR;

	strncpy(state->restore_math_expr,
		sr_scpi_unquote_string(tmp_str),
		MAX_COMMAND_SIZE - 1);
	g_free(tmp_str);

	/* If the oscilloscope is currently in FFT mode, switch to normal mode. */
	if (!g_ascii_strncasecmp(FFT_MATH_EXPRESSION, state->restore_math_expr, strlen(FFT_MATH_EXPRESSION))) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_SET_MATH_EXPRESSION],
			   MATH_WAVEFORM_INDEX, FFT_EXIT_MATH_EXPRESSION);
		if (sr_scpi_send(sdi->conn, command) != SR_OK ||
		    sr_scpi_get_opc(sdi->conn) != SR_OK) {
			sr_err("Failed to disable the FFT mode!");
				return SR_ERR;
		}
	}

	if (analog_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (digital_channel_state_get(sdi, config, state) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TIMEBASE],
			&tmp_str) != SR_OK)
		return SR_ERR;

	if (array_float_get(tmp_str, ARRAY_AND_SIZE(timebases), &i) != SR_OK) {
		g_free(tmp_str);
		sr_err("Could not determine array index for time base.");
		return SR_ERR;
	}
	g_free(tmp_str);

	state->timebase = i;

	/* Determine the number of horizontal (x) divisions. */
	if (sr_scpi_get_int(sdi->conn,
	    (*config->scpi_dialect)[SCPI_CMD_GET_HORIZONTAL_DIV],
	    (int *)&config->num_xdivs) != SR_OK)
		return SR_ERR;

	/* Not all models allow setting the mode for waveform acquisition
	 * rate and sample rate configuration.
	 */
	if (config->waveform_sample_rate && config->num_waveform_sample_rate &&
	    (*config->scpi_dialect)[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE]) {
		if (scope_state_get_array_option(sdi->conn,
						 (*config->scpi_dialect)[SCPI_CMD_GET_WAVEFORM_SAMPLE_RATE],
						 config->waveform_sample_rate, config->num_waveform_sample_rate,
						 &state->waveform_sample_rate) != SR_OK)
			return SR_ERR;
	}

	/* Not all models support the Automatic Record Length functionality. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_AUTO_RECORD_LENGTH]) {
		if (sr_scpi_get_bool(sdi->conn, (*config->scpi_dialect)[SCPI_CMD_GET_AUTO_RECORD_LENGTH],
		    &state->auto_record_length) != SR_OK)
			return SR_ERR;
	}

	/* The Random Sampling functionality is supported only on the HMO2524 and HMO3000 series. */
	if (config->random_sampling && config->num_random_sampling &&
	    (*config->scpi_dialect)[SCPI_CMD_GET_RANDOM_SAMPLING]) {
		if (scope_state_get_array_option(sdi->conn,
						 (*config->scpi_dialect)[SCPI_CMD_GET_RANDOM_SAMPLING],
						 config->random_sampling, config->num_random_sampling,
						 &state->random_sampling) != SR_OK)
			return SR_ERR;
	}

	/* Acquisition Mode setting is supported only on the HMO and RTC100x series. */
	if (config->acquisition_mode && config->num_acquisition_mode &&
	    (*config->scpi_dialect)[SCPI_CMD_GET_ACQUISITION_MODE]) {
		if (scope_state_get_array_option(sdi->conn,
						 (*config->scpi_dialect)[SCPI_CMD_GET_ACQUISITION_MODE],
						 config->acquisition_mode, config->num_acquisition_mode,
						 &state->acquisition_mode) != SR_OK)
			return SR_ERR;
	}

	/* Not all series support the Arithmetics Type setting. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_ARITHMETICS_TYPE]) {
		if (scope_state_get_array_option(sdi->conn,
						 (*config->scpi_dialect)[SCPI_CMD_GET_ARITHMETICS_TYPE],
						 config->arithmetics_type, config->num_arithmetics_type,
						 &state->arithmetics_type) != SR_OK)
			return SR_ERR;
	}

	if (scope_state_get_array_option(sdi->conn,
					 (*config->scpi_dialect)[SCPI_CMD_GET_INTERPOLATION_MODE],
					 config->interpolation_mode, config->num_interpolation_mode,
					 &state->interpolation_mode) != SR_OK)
		return SR_ERR;

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
			config->trigger_sources, config->num_trigger_sources,
			&state->trigger_source) != SR_OK)
		return SR_ERR;

	if (scope_state_get_array_option(sdi->conn,
			(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_SLOPE],
			config->edge_trigger_slopes, config->num_edge_trigger_slopes,
			&state->edge_trigger_slope) != SR_OK)
		return SR_ERR;

	/* Not all series support the Edge Trigger Coupling setting. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_COUPLING] &&
	    config->edge_trigger_coupling && config->num_edge_trigger_coupling) {
		if (scope_state_get_array_option(sdi->conn,
				(*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_COUPLING],
				config->edge_trigger_coupling, config->num_edge_trigger_coupling,
				&state->edge_trigger_coupling) != SR_OK)
			return SR_ERR;
	}

	/* Not all series support the Edge Trigger Low-Pass filter. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_LOWPASS]) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_LOWPASS]);

		if (sr_scpi_get_bool(sdi->conn, command,
				     &state->edge_trigger_lowpass) != SR_OK)
			return SR_ERR;
	}

	/* Not all series support the Edge Trigger Noise Reject filter. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_NOISE_REJ]) {
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_NOISE_REJ]);

		if (sr_scpi_get_bool(sdi->conn, command,
				     &state->edge_trigger_noise_rej) != SR_OK)
			return SR_ERR;
	}

	if (g_ascii_strncasecmp("RTO", sdi->model, 3)) {
		if (sr_scpi_get_string(sdi->conn,
				       (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_PATTERN],
				       &tmp_str) != SR_OK)
			return SR_ERR;
	} else { /* RTO series: A separate command needs to be issued for each bit in the pattern. */
		tmp_str = g_malloc0_n(MAX_TRIGGER_PATTERN_LENGTH, sizeof(char));
		if (!tmp_str)
			return SR_ERR_MALLOC;
		for (i = 0; i < DIGITAL_CHANNELS_PER_POD * config->digital_pods &&
		     i < MAX_TRIGGER_PATTERN_LENGTH; i++) {
			g_snprintf(command, sizeof(command),
				   (*config->scpi_dialect)[SCPI_CMD_GET_TRIGGER_PATTERN],
				   i);
			if (sr_scpi_get_string(sdi->conn, command, &tmp_str2))
				return SR_ERR;
			if (!g_ascii_strcasecmp("LOW", tmp_str2)) {
				tmp_str[i] = LOGIC_TRIGGER_ZERO;
			} else if (!g_ascii_strcasecmp("HIGH", tmp_str2)) {
				tmp_str[i] = LOGIC_TRIGGER_ONE;
			} else {
				tmp_str[i] = LOGIC_TRIGGER_DONTCARE;
			}
			g_free(tmp_str2);
		}
	}
	strncpy(state->trigger_pattern,
		sr_scpi_unquote_string(tmp_str),
		MAX_TRIGGER_PATTERN_LENGTH - 1);
	g_free(tmp_str);

	/* Not currently implemented on the RTO series. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_HIGH_RESOLUTION]) {
		if (sr_scpi_get_string(sdi->conn,
				     (*config->scpi_dialect)[SCPI_CMD_GET_HIGH_RESOLUTION],
				     &tmp_str) != SR_OK)
			return SR_ERR;
		if (!g_ascii_strcasecmp("OFF", tmp_str))
			state->high_resolution = FALSE;
		else
			state->high_resolution = TRUE;
		g_free(tmp_str);
	}

	/* Not currently implemented on the RTO series. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION]) {
		if (sr_scpi_get_string(sdi->conn,
				     (*config->scpi_dialect)[SCPI_CMD_GET_PEAK_DETECTION],
				     &tmp_str) != SR_OK)
			return SR_ERR;
		if (!g_ascii_strcasecmp("OFF", tmp_str))
			state->peak_detection = FALSE;
		else
			state->peak_detection = TRUE;
		g_free(tmp_str);
	}

	/* Determine the FFT window type. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_WINDOW_TYPE],
		   MATH_WAVEFORM_INDEX);

	if (scope_state_get_array_option(sdi->conn, command,
					 config->fft_window_types, config->num_fft_window_types,
					 &state->fft_window_type) != SR_OK)
		return SR_ERR;

	/* Determine the FFT start frequency. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_START],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_start) != SR_OK)
		return SR_ERR;

	/* Determine the FFT stop frequency. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_STOP],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_stop) != SR_OK)
		return SR_ERR;

	/* Determine the FFT frequency span. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_SPAN],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_span) != SR_OK)
		return SR_ERR;

	/* Determine the FFT center frequency. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_FREQUENCY_CENTER],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_freq_center) != SR_OK)
		return SR_ERR;

	/* Determine the FFT Resolution Bandwidth. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_RESOLUTION_BW],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &state->fft_rbw) != SR_OK)
		return SR_ERR;

	/* Determine the FFT Resolution Bandwidth / Span coupling. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_SPAN_RBW_COUPLING],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_bool(sdi->conn, command,
			     &state->fft_span_rbw_coupling) != SR_OK)
		return SR_ERR;

	/* Determine the FFT Resolution Bandwidth / Span ratio. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_FFT_SPAN_RBW_RATIO],
		   MATH_WAVEFORM_INDEX);

	if (sr_scpi_get_float(sdi->conn, command,
			      &tmp_float) != SR_OK)
		return SR_ERR;

	state->fft_span_rbw_ratio = tmp_float;

	/* Get the Automatic Measurement source and reference. */
	g_snprintf(command, sizeof(command),
		   (*config->scpi_dialect)[SCPI_CMD_GET_MEAS_SOURCE_REFERENCE],
		   AUTO_MEASUREMENT_INDEX, AUTO_MEASUREMENT_INDEX);
	if (sr_scpi_get_string(sdi->conn, command, &tmp_str) != SR_OK)
		return SR_ERR;
	tmp_str2 = strchr(tmp_str, ',');
	if (tmp_str2) {
		if ((idx = std_str_idx_s(++tmp_str2, (const char **) *config->meas_sources,
					 config->num_meas_sources)) < 0) {
			g_free(tmp_str);
			return SR_ERR_ARG;
		}
		state->meas_reference = idx;
		(--tmp_str2)[0] = '\0';
		if ((idx = std_str_idx_s(tmp_str, (const char **) *config->meas_sources,
					 config->num_meas_sources)) < 0) {
			g_free(tmp_str);
			return SR_ERR_ARG;
		}
		state->meas_source = idx;
		g_free(tmp_str);
	} else {
		g_free(tmp_str);
		return SR_ERR_ARG;
	}

	/* Not available on all series. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_SYS_BEEP_ON_TRIGGER]) {
		/* Check if the Beep On Trigger functionality is enabled or not. */
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_SYS_BEEP_ON_TRIGGER]);

		if (sr_scpi_get_bool(sdi->conn, command,
				     &state->beep_on_trigger) != SR_OK)
			return SR_ERR;
	}

	/* Not available on all series. */
	if ((*config->scpi_dialect)[SCPI_CMD_GET_SYS_BEEP_ON_ERROR]) {
		/* Check if the Beep On Error functionality is enabled or not. */
		g_snprintf(command, sizeof(command),
			   (*config->scpi_dialect)[SCPI_CMD_GET_SYS_BEEP_ON_ERROR]);

		if (sr_scpi_get_bool(sdi->conn, command,
				     &state->beep_on_error) != SR_OK)
			return SR_ERR;
	}

	if (rs_update_sample_rate(sdi) != SR_OK)
		return SR_ERR;

	sr_info("Fetching finished.");

	scope_state_dump(config, state);

	return SR_OK;
}

static struct scope_state *scope_state_new(const struct scope_config *config)
{
	struct scope_state *state = NULL;

	state = g_malloc0(sizeof(struct scope_state));
	if (!state)
		return state;

	state->analog_channels = g_malloc0_n(config->analog_channels,
			sizeof(struct analog_channel_state));
	state->digital_channels = g_malloc0_n(
			config->digital_channels, sizeof(gboolean));
	state->digital_pods = g_malloc0_n(config->digital_pods,
			sizeof(struct digital_pod_state));

	if (!state->analog_channels || !state->digital_channels || !state->digital_pods) {
		rs_scope_state_free(state);
		return NULL;
	}

	return state;
}

SR_PRIV void rs_scope_config_free(struct scope_config *config)
{
	unsigned int i;

	if (!config)
		return;

	for (i = 0; i < config->num_meas_sources; i++)
		g_free((*config->meas_sources)[i]);
	g_free(config->meas_sources);
}

SR_PRIV void rs_scope_state_free(struct scope_state *state)
{
	if (!state)
		return;

	g_free(state->analog_channels);
	g_free(state->digital_channels);
	g_free(state->digital_pods);
	g_free(state);
}

/*
 * Build an array with all possible sources for all analog channels and
 * all different waveforms supported for each channel.
 */
static char **rs_build_multi_waveform_sources(const unsigned int channels,
					      const unsigned int waveforms)
{
	unsigned int i, j;
	char **sources;

	sources = g_malloc0(sizeof(char *) * channels * waveforms);
	if (sources)
		for (i = 1; i <= channels; i++)
			for (j = 1; j <= waveforms; j++)
				sources[(i - 1) * waveforms + (j - 1)] = g_strdup_printf("C%uW%u", i, j);

	return sources;
}

SR_PRIV int rs_init_device(struct sr_dev_inst *sdi)
{
	int ret, model_index, len;
	unsigned int i, j, k, group;
	struct sr_channel *ch;
	struct dev_context *devc;
	char *tmp_str, *tmp_str2, *tmp_str3;
	char **multi_waveform_sources;

	if (!sdi)
		return SR_ERR;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	model_index = -1;

	/* Find the exact model. */
	for (i = 0; i < ARRAY_SIZE(scope_models); i++) {
		for (j = 0; scope_models[i].name[j]; j++) {
			if (!g_ascii_strcasecmp(sdi->model, scope_models[i].name[j])) {
				model_index = i;
				break;
			}
		}
		if (model_index != -1)
			break;
	}

	if (model_index == -1) {
		sr_dbg("Unsupported device.");
		return SR_ERR_NA;
	}

	/*
	 * Configure the number of analog channels (2 or 4) from the last
	 * digit of the serial number on the RTO series (1316.1000k[0-4][24]
	 * for the RTO100x or 1329.7002k[0-4][24] for the RTO200x).
	 */
	if (!g_ascii_strncasecmp("RTO", sdi->model, 3)) {
		i = strlen(sdi->serial_num);
		scope_models[model_index].analog_channels = 2;
		if (i >= 12)
			if (sdi->serial_num[11] == '4')
				scope_models[model_index].analog_channels = 4;
	}

	/* Configure the number of PODs given the number of digital channels. */
	scope_models[model_index].digital_pods = scope_models[model_index].digital_channels / DIGITAL_CHANNELS_PER_POD;

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					scope_models[model_index].analog_channels);
	devc->digital_groups = g_malloc0(sizeof(struct sr_channel_group*) *
					 scope_models[model_index].digital_pods);
	if (!devc->analog_groups || !devc->digital_groups) {
		g_free(devc->analog_groups);
		g_free(devc->digital_groups);
		return SR_ERR_MALLOC;
	}

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
	ret = SR_OK;
	for (i = 0; i < scope_models[model_index].digital_pods; i++) {
		devc->digital_groups[i] = g_malloc0(sizeof(struct sr_channel_group));
		if (!devc->digital_groups[i]) {
			ret = SR_ERR_MALLOC;
			break;
		}
		devc->digital_groups[i]->name = g_strdup_printf("POD%d", i + 1);
		sdi->channel_groups = g_slist_append(sdi->channel_groups,
				   devc->digital_groups[i]);
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

	/*
	 * Determine the digital data format for the dialect being used.
	 *
	 * The command specified in the dialect is assumed to be correct
	 * and must not fail when sent to the oscilloscope !
	 */
	scope_models[model_index].digital_data_byte_len = 1;
	if ((*scope_models[model_index].scpi_dialect)[SCPI_CMD_GET_DIG_DATA]) {
		tmp_str = g_strdup((*scope_models[model_index].scpi_dialect)[SCPI_CMD_GET_DIG_DATA]);
		/* Try to determine the digital data byte length from the SCPI command. */
		for (j = 0; j < ARRAY_SIZE(format_length_scpi_cmd_prefix); j++) {
			tmp_str2 = strstr(tmp_str, format_length_scpi_cmd_prefix[j]);
			if (tmp_str2) {
				tmp_str3 = strchr(tmp_str2, ';');
				tmp_str3[0] = '\0';
				sr_atoi(&tmp_str2[strlen(format_length_scpi_cmd_prefix[j])], &len);
				scope_models[model_index].digital_data_byte_len = len / 8;
				break;
			}
		}
		g_free(tmp_str);
	}

	/* Add special channels for the Fast Fourier Transform (FFT). */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		tmp_str = g_strdup_printf("FFT_CH%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_FFT, TRUE, tmp_str);
		g_free(tmp_str);
	}

	/* Build the Automatic Measurements signal sources and references list. */
	k = 0;
	if (g_ascii_strncasecmp("RTO", sdi->model, 3)) {
		scope_models[model_index].num_meas_sources = scope_models[model_index].analog_channels +
							     scope_models[model_index].digital_channels;
		for (i = 0; i < scope_models[model_index].analog_channels; i++) {
			j = strlen((*scope_models[model_index].analog_names)[i]);
			if (j > k)
				k = j;
		}
	} else { /* The RTO series has multiple waveforms for each analog channel. */
		scope_models[model_index].num_meas_sources = scope_models[model_index].analog_channels *
							     MAX_WAVEFORMS_PER_CHANNEL +
							     scope_models[model_index].digital_channels;
		multi_waveform_sources = rs_build_multi_waveform_sources(scope_models[model_index].analog_channels,
									 MAX_WAVEFORMS_PER_CHANNEL);
		if (!multi_waveform_sources)
			return SR_ERR_MALLOC;
		for (i = 0; i < scope_models[model_index].analog_channels * MAX_WAVEFORMS_PER_CHANNEL; i++) {
			j = strlen(multi_waveform_sources[i]);
			if (j > k)
				k = j;
		}
	}
	for (i = 0; i < scope_models[model_index].digital_channels; i++) {
		j = strlen((*scope_models[model_index].digital_names)[i]);
		if (j > k)
			k = j;
	}

	scope_models[model_index].meas_sources = g_malloc0(sizeof(char *) *
							   scope_models[model_index].num_meas_sources);
	if (!scope_models[model_index].meas_sources)
		return SR_ERR_MALLOC;
	for (i = 0; i < scope_models[model_index].num_meas_sources; i++)
		(*scope_models[model_index].meas_sources)[i] = g_malloc0(sizeof(char) * k);

	if (g_ascii_strncasecmp("RTO", sdi->model, 3)) {
		for (i = 0; i < scope_models[model_index].analog_channels; i++)
			strncpy((*scope_models[model_index].meas_sources)[i],
				(*scope_models[model_index].analog_names)[i],
				k);
		for (i = 0; i < scope_models[model_index].digital_channels; i++)
			strncpy((*scope_models[model_index].meas_sources)[i + scope_models[model_index].analog_channels],
				(*scope_models[model_index].digital_names)[i],
				k);
	} else { /* RTO series. */
		for (i = 0; i < scope_models[model_index].analog_channels * MAX_WAVEFORMS_PER_CHANNEL; i++)
			strncpy((*scope_models[model_index].meas_sources)[i],
				multi_waveform_sources[i],
				k);
		for (i = 0; i < scope_models[model_index].digital_channels; i++)
			strncpy((*scope_models[model_index].meas_sources)[i +
					scope_models[model_index].analog_channels *
					MAX_WAVEFORMS_PER_CHANNEL],
				(*scope_models[model_index].digital_names)[i],
				k);
		for (i = 0; i < scope_models[model_index].analog_channels * MAX_WAVEFORMS_PER_CHANNEL; i++)
			g_free(multi_waveform_sources[i]);
		g_free(multi_waveform_sources);
	}

	devc->model_config = &scope_models[model_index];
	devc->samples_limit = 0;
	devc->frame_limit = 0;

	if (!(devc->model_state = scope_state_new(devc->model_config)))
		return SR_ERR_MALLOC;

	return SR_OK;
}

/*
 * Queue logic data for later submission.
 *
 * When the logic data retrieval command is based on the
 * group (POD), data for the whole channel group (POD) is
 * queued on each function call.
 *
 * When the logic data retrieval command is based on the
 * individual digital channel, then data for such channel
 * only is queued each time this function is called.
 */
SR_PRIV void rs_queue_logic_data(struct dev_context *devc,
				  const GByteArray *pod_data)
{
	const struct scope_config *model;
	struct sr_channel *ch;
	size_t group, size;
	GByteArray *store;
	uint8_t *logic_data, *logic_data_next;
	size_t idx, logic_step, byte_num;

	if (!devc || !pod_data)
		return;

	model = devc->model_config;
	if (!model)
		return;

	/* Make sure the number of digital channels per POD fits the data structure. */
	if (model->digital_data_byte_len < BYTES_PER_POD) {
		sr_err("The number of digital channels per POD is larger than the data structure size!");
		return;
	}

	ch = devc->current_channel->data;
	group = ch->index / DIGITAL_CHANNELS_PER_POD;

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
		size = pod_data->len * devc->pod_count * BYTES_PER_POD / model->digital_data_byte_len;
		store = g_byte_array_sized_new(size);
		memset(store->data, 0, size);
		store = g_byte_array_set_size(store, size);
		devc->logic_data = store;
	} else {
		store = devc->logic_data;
		if (group >= devc->pod_count)
			return;
	}

	/*
	 * Fold the data of the most recently received channel group into
	 * the storage, where data resides for all channels combined.
	 */
	logic_data = store->data;
	logic_data += group * BYTES_PER_POD;
	logic_step = devc->pod_count * BYTES_PER_POD;
	idx = 0;
	while (idx < pod_data->len) {
		logic_data_next = logic_data + logic_step;
		if (model->digital_data_pod_index) { /* Data for a whole POD */
			*logic_data = pod_data->data[idx];
			for (byte_num = 1; byte_num < BYTES_PER_POD; byte_num++)
				*(++logic_data) = pod_data->data[idx + byte_num];
		} else { /* Add data for an individual channel */
			logic_data += ch->index / 8;
			*logic_data += pod_data->data[idx + ch->index / 8]&(1<<(ch->index%8));
		}
		logic_data = logic_data_next;
		idx += model->digital_data_byte_len;
	}

	/* Truncate acquisition if a smaller number of samples has been requested. */
	if (devc->samples_limit > 0 &&
	    devc->logic_data->len > devc->samples_limit * devc->pod_count * BYTES_PER_POD)
		devc->logic_data->len = devc->samples_limit * devc->pod_count * BYTES_PER_POD;
}

/* Submit data for all channels, after the individual groups got collected. */
SR_PRIV void rs_send_logic_packet(const struct sr_dev_inst *sdi,
				   const struct dev_context *devc)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (!sdi || !devc)
		return;

	if (!devc->logic_data)
		return;

	logic.data = devc->logic_data->data;
	logic.length = devc->logic_data->len;
	logic.unitsize = devc->pod_count * BYTES_PER_POD;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;

	sr_session_send(sdi, &packet);
}

/* Undo previous resource allocation. */
SR_PRIV void rs_cleanup_logic_data(struct dev_context *devc)
{
	if (!devc)
		return;

	if (devc->logic_data) {
		g_byte_array_free(devc->logic_data, TRUE);
		devc->logic_data = NULL;
	}

	/*
	 * Keep 'pod_count'! It's required when more frames will be
	 * received, and does not harm when kept after acquisition.
	 */
}

SR_PRIV int rs_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_channel *ch;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const struct scope_state *state;
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

	/*
	 * Send "frame begin" packet upon reception of data for the
	 * first enabled channel.
	 */
	if (devc->current_channel == devc->enabled_channels) {
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);
	}

	/*
	 * Pass on the received data of the channel(s).
	 */
	switch (ch->type) {
	case SR_CHANNEL_ANALOG:
	case SR_CHANNEL_FFT:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);
			return TRUE;
		}

		packet.type = SR_DF_ANALOG;

		analog.data = data->data;
		analog.num_samples = data->len / sizeof(float);
		/* Truncate acquisition if a smaller number of samples has been requested. */
		if (devc->samples_limit > 0 && analog.num_samples > devc->samples_limit)
			analog.num_samples = devc->samples_limit;
		analog.encoding = &encoding;
		analog.meaning = &meaning;
		analog.spec = &spec;

		encoding.unitsize = sizeof(float);
		encoding.is_signed = TRUE;
		encoding.is_float = TRUE;
#ifdef WORDS_BIGENDIAN
		encoding.is_bigendian = TRUE;
#else
		encoding.is_bigendian = FALSE;
#endif
		/* TODO: Use proper 'digits' value for this device (and its modes). */
		encoding.digits = 2;
		encoding.is_digits_decimal = FALSE;
		encoding.scale.p = 1;
		encoding.scale.q = 1;
		encoding.offset.p = 0;
		encoding.offset.q = 1;
		if (ch->type == SR_CHANNEL_ANALOG) {
			if (state) {
				if (state->analog_channels[ch->index].probe_unit == 'V') {
					meaning.mq = SR_MQ_VOLTAGE;
					meaning.unit = SR_UNIT_VOLT;
				} else {
					meaning.mq = SR_MQ_CURRENT;
					meaning.unit = SR_UNIT_AMPERE;
				}
			}
		} else if (ch->type == SR_CHANNEL_FFT) {
			meaning.mq = SR_MQ_POWER;
			meaning.unit = SR_UNIT_DECIBEL_MW;
		}
		meaning.mqflags = 0;
		meaning.channels = g_slist_append(NULL, ch);
		/* TODO: Use proper 'digits' value for this device (and its modes). */
		spec.spec_digits = 2;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);
		devc->num_samples = data->len / sizeof(float);
		g_slist_free(meaning.channels);
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	case SR_CHANNEL_LOGIC:
		if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
			if (data)
				g_byte_array_free(data, TRUE);
			return TRUE;
		}

		/*
		 * If only data from the first pod is involved in the
		 * acquisition, then the raw input bytes can get passed
		 * forward for performance reasons. When the second pod
		 * is involved (either alone, or in combination with the
		 * first pod), then the received bytes need to be put
		 * into memory in such a layout that all channel groups
		 * get combined, and a unitsize larger than a single byte
		 * applies. The "queue" logic transparently copes with
		 * any such configuration. This works around the lack
		 * of support for "meaning" to logic data, which is used
		 * above for analog data.
		 */
		if (devc->pod_count == 1) {
			packet.type = SR_DF_LOGIC;
			logic.data = data->data;
			logic.length = data->len;
			/* Truncate acquisition if a smaller number of samples has been requested. */
			if (devc->samples_limit > 0 && logic.length > devc->samples_limit * BYTES_PER_POD)
				logic.length = devc->samples_limit * BYTES_PER_POD;
			logic.unitsize = BYTES_PER_POD;
			packet.payload = &logic;
			sr_session_send(sdi, &packet);
		} else {
			rs_queue_logic_data(devc, data);
		}

		devc->num_samples = data->len / (devc->pod_count * BYTES_PER_POD);
		g_byte_array_free(data, TRUE);
		data = NULL;
		break;
	default:
		sr_err("Invalid channel type.");
		break;
	}

	/*
	 * Advance to the next enabled channel. When data for all enabled
	 * channels was received, then flush potentially queued logic data,
	 * and send the "frame end" packet.
	 */
	if (devc->current_channel->next) {
		devc->current_channel = devc->current_channel->next;
		rs_request_data(sdi);
		return TRUE;
	}
	rs_send_logic_packet(sdi, devc);

	/*
	 * Release the logic data storage after each frame. This copes
	 * with sample counts that differ in length per frame. -- Is
	 * this a real constraint when acquiring multiple frames with
	 * identical device settings?
	 */
	rs_cleanup_logic_data(devc);

	packet.type = SR_DF_FRAME_END;
	sr_session_send(sdi, &packet);

	/*
	 * End of frame was reached. Stop acquisition after the specified
	 * number of frames or after the specified number of samples, or
	 * continue reception by starting over at the first enabled channel.
	 */
	if (++devc->num_frames >= devc->frame_limit || devc->num_samples >= devc->samples_limit) {
		sr_dev_acquisition_stop(sdi);
		rs_cleanup_logic_data(devc);
	} else {
		devc->current_channel = devc->enabled_channels;
		rs_request_data(sdi);
	}

	return TRUE;
}
