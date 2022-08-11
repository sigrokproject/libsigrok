/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Sven Schnelle <svens@stackframe.org>
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

struct lecroy_wavedesc_2_x {
	uint16_t comm_type;
	uint16_t comm_order; /* 1 - little endian */
	uint32_t wave_descriptor_length;
	uint32_t user_text_len;
	uint32_t res_desc1;
	uint32_t trigtime_array_length;
	uint32_t ris_time1_array_length;
	uint32_t res_array1;
	uint32_t wave_array1_length;
	uint32_t wave_array2_length;
	uint32_t wave_array3_length;
	uint32_t wave_array4_length;
	char instrument_name[16];
	uint32_t instrument_number;
	char trace_label[16];
	uint32_t reserved;
	uint32_t wave_array_count;
	uint32_t points_per_screen;
	uint32_t first_valid_point;
	uint32_t last_valid_point;
	uint32_t first_point;
	uint32_t sparsing_factor;
	uint32_t segment_index;
	uint32_t subarray_count;
	uint32_t sweeps_per_acq;
	uint16_t points_per_pair;
	uint16_t pair_offset;
	float vertical_gain;
	float vertical_offset;
	float max_value;
	float min_value;
	uint16_t nominal_bits;
	uint16_t nom_subarray_count;
	float horiz_interval;
	double horiz_offset;
	double pixel_offset;
	char vertunit[48];
	char horunit[48];
	uint32_t reserved1;
	double trigger_time;
} __attribute__((packed));

struct lecroy_wavedesc {
	char descriptor_name[16];
	char template_name[16];
	union {
		struct lecroy_wavedesc_2_x version_2_x;
	};
} __attribute__((packed));

static const char *coupling_options[] = {
	"A1M", ///< AC with 1 MOhm termination
	"D50", ///< DC with 50 Ohm termination
	"D1M", ///< DC with 1 MOhm termination
	"GND",
	"OVL",
};

static const char *scope_trigger_slopes[] = {
	"POS", "NEG",
};

static const char *trigger_sources[] = {
	"C1", "C2", "C3", "C4", "LINE", "EXT",
};

static const uint64_t timebases[][2] = {
	/* picoseconds */
	{ 20, UINT64_C(1000000000000) },
	{ 50, UINT64_C(1000000000000) },
	{ 100, UINT64_C(1000000000000) },
	{ 200, UINT64_C(1000000000000) },
	{ 500, UINT64_C(1000000000000) },
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
	{ 1000, 1 },
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
};

static const char *scope_analog_channel_names[] = {
	"CH1", "CH2", "CH3", "CH4",
};

static const struct scope_config scope_models[] = {
	{
		 /* Default config */
		.name = {NULL},

		.analog_channels = 4,
		.analog_names = &scope_analog_channel_names,

		.coupling_options = &coupling_options,
		.num_coupling_options = ARRAY_SIZE(coupling_options),

		.trigger_sources = &trigger_sources,
		.num_trigger_sources = ARRAY_SIZE(trigger_sources),

		.trigger_slopes = &scope_trigger_slopes,
		.num_trigger_slopes = ARRAY_SIZE(scope_trigger_slopes),

		.timebases = &timebases,
		.num_timebases = ARRAY_SIZE(timebases),

		.vdivs = &vdivs,
		.num_vdivs = ARRAY_SIZE(vdivs),

		.num_xdivs = 10,
		.num_ydivs = 8,
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
		sr_info("State of analog channel %d -> %s : %s (coupling) %s (vdiv) %2.2e (offset)",
				i + 1, state->analog_channels[i].state ? "On" : "Off",
				(*config->coupling_options)[state->analog_channels[i].coupling],
				tmp, state->analog_channels[i].vertical_offset);
	}

	tmp = sr_period_string((*config->timebases)[state->timebase][0],
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

static int scope_state_get_array_option(const char *resp,
		const char *(*array)[], unsigned int n, int *result)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (!g_strcmp0(resp, (*array)[i])) {
			*result = i;
			return SR_OK;
		}
	}

	return SR_ERR;
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
		const struct scope_config *config, struct scope_state *state)
{
	unsigned int i, j;
	char command[MAX_COMMAND_SIZE];
	char *tmp_str;

	for (i = 0; i < config->analog_channels; i++) {
		g_snprintf(command, sizeof(command), "C%d:TRACE?", i + 1);

		if (sr_scpi_get_bool(scpi, command,
				&state->analog_channels[i].state) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command), "C%d:VDIV?", i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;

		if (array_float_get(tmp_str, ARRAY_AND_SIZE(vdivs), &j) != SR_OK) {
			g_free(tmp_str);
			sr_err("Could not determine array index for vertical div scale.");
			return SR_ERR;
		}

		g_free(tmp_str);
		state->analog_channels[i].vdiv = j;

		g_snprintf(command, sizeof(command), "C%d:OFFSET?", i + 1);

		if (sr_scpi_get_float(scpi, command, &state->analog_channels[i].vertical_offset) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command), "C%d:COUPLING?", i + 1);

		if (sr_scpi_get_string(scpi, command, &tmp_str) != SR_OK)
			return SR_ERR;


		if (scope_state_get_array_option(tmp_str, config->coupling_options,
				 config->num_coupling_options,
				 &state->analog_channels[i].coupling) != SR_OK)
			return SR_ERR;

		g_free(tmp_str);
	}

	return SR_OK;
}

SR_PRIV int lecroy_xstream_channel_state_set(const struct sr_dev_inst *sdi,
		const int ch_index, gboolean ch_state)
{
	GSList *l;
	struct sr_channel *ch;
	struct dev_context *devc = NULL;
	struct scope_state *state;
	char command[MAX_COMMAND_SIZE];
	gboolean chan_found;
	int result;

	result = SR_OK;

	devc = sdi->priv;
	state = devc->model_state;
	chan_found = FALSE;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;

		switch (ch->type) {
		case SR_CHANNEL_ANALOG:
			if (ch->index == ch_index) {
				g_snprintf(command, sizeof(command), "C%d:TRACE %s", ch_index + 1,
						(ch_state ? "ON" : "OFF"));
				if ((sr_scpi_send(sdi->conn, command) != SR_OK ||
						sr_scpi_get_opc(sdi->conn) != SR_OK)) {
					result = SR_ERR;
					break;
				}

				ch->enabled = ch_state;
				state->analog_channels[ch->index].state = ch_state;
				chan_found = TRUE;
				break;
			}
			break;
		default:
			result = SR_ERR_NA;
		}
	}

	if ((result == SR_OK) && !chan_found)
		result = SR_ERR_BUG;

	return result;
}

SR_PRIV int lecroy_xstream_update_sample_rate(const struct sr_dev_inst *sdi,
		int num_of_samples)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	double time_div;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	if (sr_scpi_get_double(sdi->conn, "TIME_DIV?", &time_div) != SR_OK)
		return SR_ERR;

	state->sample_rate = num_of_samples / (time_div * config->num_xdivs);

	return SR_OK;
}

SR_PRIV int lecroy_xstream_state_get(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct scope_state *state;
	const struct scope_config *config;
	unsigned int i;
	char *tmp_str, *tmp_str2, *tmpp, *p, *key;
	char command[MAX_COMMAND_SIZE];
	char *trig_source = NULL;

	devc = sdi->priv;
	config = devc->model_config;
	state = devc->model_state;

	sr_info("Fetching scope state");

	if (analog_channel_state_get(sdi->conn, config, state) != SR_OK)
		return SR_ERR;

	if (sr_scpi_get_string(sdi->conn, "TIME_DIV?", &tmp_str) != SR_OK)
		return SR_ERR;

	if (array_float_get(tmp_str, ARRAY_AND_SIZE(timebases), &i) != SR_OK) {
		g_free(tmp_str);
		sr_err("Could not determine array index for timbase scale.");
		return SR_ERR;
	}
	g_free(tmp_str);
	state->timebase = i;

	if (sr_scpi_get_string(sdi->conn, "TRIG_SELECT?", &tmp_str) != SR_OK)
		return SR_ERR;

	key = tmpp = NULL;
	tmp_str2 = tmp_str;
	i = 0;
	while ((p = strtok_r(tmp_str2, ",", &tmpp))) {
		tmp_str2 = NULL;
		if (i == 0) {
			/* trigger type */
		} else if (i & 1) {
			key = p;
			/* key */
		} else if (!(i & 1)) {
			if (!strcmp(key, "SR"))
				trig_source = p;
		}
		i++;
	}
	g_free(tmp_str);

	if (!trig_source || scope_state_get_array_option(trig_source,
			config->trigger_sources, config->num_trigger_sources,
			&state->trigger_source) != SR_OK)
		return SR_ERR;

	g_snprintf(command, sizeof(command), "%s:TRIG_SLOPE?", trig_source);
	if (sr_scpi_get_string(sdi->conn, command, &tmp_str) != SR_OK)
		return SR_ERR;

	if (scope_state_get_array_option(tmp_str, config->trigger_slopes,
			config->num_trigger_slopes, &state->trigger_slope) != SR_OK)
		return SR_ERR;
	g_free(tmp_str);

	if (sr_scpi_get_float(sdi->conn, "TRIG_DELAY?", &state->horiz_triggerpos) != SR_OK)
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
	return state;
}

SR_PRIV void lecroy_xstream_state_free(struct scope_state *state)
{
	g_free(state->analog_channels);
	g_free(state);
}

SR_PRIV int lecroy_xstream_init_device(struct sr_dev_inst *sdi)
{
	char command[MAX_COMMAND_SIZE];
	int model_index;
	unsigned int i, j;
	struct sr_channel *ch;
	struct dev_context *devc;
	gboolean channel_enabled;

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
		sr_dbg("Unknown LeCroy device, using default config.");
		for (i = 0; i < ARRAY_SIZE(scope_models); i++)
			if (scope_models[i].name[0] == NULL)
				model_index = i;
	}

	/* Set the desired response and format modes. */
	sr_scpi_send(sdi->conn, "COMM_HEADER OFF");
	sr_scpi_send(sdi->conn, "COMM_FORMAT DEF9,WORD,BIN");

	devc->analog_groups = g_malloc0(sizeof(struct sr_channel_group*) *
				scope_models[model_index].analog_channels);

	/* Add analog channels. */
	for (i = 0; i < scope_models[model_index].analog_channels; i++) {
		g_snprintf(command, sizeof(command), "C%d:TRACE?", i + 1);

		if (sr_scpi_get_bool(sdi->conn, command, &channel_enabled) != SR_OK)
			return SR_ERR;

		g_snprintf(command, sizeof(command), "C%d:VDIV?", i + 1);

		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, channel_enabled,
			(*scope_models[model_index].analog_names)[i]);

		devc->analog_groups[i] = sr_channel_group_new(sdi,
			(*scope_models[model_index].analog_names)[i], NULL);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);
	}

	devc->model_config = &scope_models[model_index];
	devc->frame_limit = 0;
	devc->model_state = scope_state_new(devc->model_config);

	return SR_OK;
}

static int lecroy_waveform_2_x_to_analog(GByteArray *data,
		struct lecroy_wavedesc *desc, struct sr_datafeed_analog *analog)
{
	struct sr_analog_encoding *encoding = analog->encoding;
	struct sr_analog_meaning *meaning = analog->meaning;
	struct sr_analog_spec *spec = analog->spec;
	float *data_float;
	int16_t *waveform_data;
	unsigned int i, num_samples;

	data_float = g_malloc(desc->version_2_x.wave_array_count * sizeof(float));
	num_samples = desc->version_2_x.wave_array_count;

	waveform_data = (int16_t*)(data->data +
		+ desc->version_2_x.wave_descriptor_length
		+ desc->version_2_x.user_text_len);

	for (i = 0; i < num_samples; i++)
		data_float[i] = (float)waveform_data[i]
			* desc->version_2_x.vertical_gain
			+ desc->version_2_x.vertical_offset;

	analog->data = data_float;
	analog->num_samples = num_samples;

	encoding->unitsize = sizeof(float);
	encoding->is_signed = TRUE;
	encoding->is_float = TRUE;
	encoding->is_bigendian = FALSE;
	encoding->scale.p = 1;
	encoding->scale.q = 1;
	encoding->offset.p = 0;
	encoding->offset.q = 1;

	encoding->digits = 6;
	encoding->is_digits_decimal = FALSE;

	if (strcmp(desc->version_2_x.vertunit, "A")) {
		meaning->mq = SR_MQ_CURRENT;
		meaning->unit = SR_UNIT_AMPERE;
	} else {
		/* Default to voltage. */
		meaning->mq = SR_MQ_VOLTAGE;
		meaning->unit = SR_UNIT_VOLT;
	}

	meaning->mqflags = 0;
	spec->spec_digits = 3;

	return SR_OK;
}

static int lecroy_waveform_to_analog(GByteArray *data,
		struct sr_datafeed_analog *analog)
{
	struct lecroy_wavedesc *desc;

	if (data->len < sizeof(struct lecroy_wavedesc))
		return SR_ERR;

	desc = (struct lecroy_wavedesc*)data->data;

	if (!strncmp(desc->template_name, "LECROY_2_2", 16) ||
	    !strncmp(desc->template_name, "LECROY_2_3", 16)) {
		return lecroy_waveform_2_x_to_analog(data, desc, analog);
	}

	sr_err("Waveformat template '%.16s' not supported.", desc->template_name);
	return SR_ERR;
}

SR_PRIV int lecroy_xstream_receive_data(int fd, int revents, void *cb_data)
{
	char command[MAX_COMMAND_SIZE];
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

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	ch = devc->current_channel->data;
	state = devc->model_state;

	if (ch->type != SR_CHANNEL_ANALOG)
		return SR_ERR;

	data = NULL;
	if (sr_scpi_get_block(sdi->conn, NULL, &data) != SR_OK) {
		if (data)
			g_byte_array_free(data, TRUE);
		return TRUE;
	}

	analog.encoding = &encoding;
	analog.meaning = &meaning;
	analog.spec = &spec;

	if (lecroy_waveform_to_analog(data, &analog) != SR_OK)
		return SR_ERR;

	if (analog.num_samples == 0) {
		g_free(analog.data);
		g_byte_array_free(data, TRUE);

		/* No data available, we have to acquire data first. */
		g_snprintf(command, sizeof(command), "ARM;WAIT;*OPC;C%d:WAVEFORM?", ch->index + 1);
		sr_scpi_send(sdi->conn, command);

		state->sample_rate = 0;
		return TRUE;
	} else {
		/* Update sample rate if needed. */
		if (state->sample_rate == 0)
			if (lecroy_xstream_update_sample_rate(sdi, analog.num_samples) != SR_OK) {
				g_free(analog.data);
				g_byte_array_free(data, TRUE);
				return SR_ERR;
			}
	}

	/*
	 * Send "frame begin" packet upon reception of data for the
	 * first enabled channel.
	 */
	if (devc->current_channel == devc->enabled_channels)
		std_session_send_df_frame_begin(sdi);

	meaning.channels = g_slist_append(NULL, ch);
	packet.payload = &analog;
	packet.type = SR_DF_ANALOG;
	sr_session_send(sdi, &packet);

	g_byte_array_free(data, TRUE);
	data = NULL;

	g_slist_free(meaning.channels);
	g_free(analog.data);

	/*
	 * Advance to the next enabled channel. When data for all enabled
	 * channels was received, then flush potentially queued logic data,
	 * and send the "frame end" packet.
	 */
	if (devc->current_channel->next) {
		devc->current_channel = devc->current_channel->next;
		lecroy_xstream_request_data(sdi);
		return TRUE;
	}

	std_session_send_df_frame_end(sdi);

	/*
	 * End of frame was reached. Stop acquisition after the specified
	 * number of frames, or continue reception by starting over at
	 * the first enabled channel.
	 */
	devc->num_frames++;
	if (devc->frame_limit && (devc->num_frames == devc->frame_limit)) {
		sr_dev_acquisition_stop(sdi);
	} else {
		devc->current_channel = devc->enabled_channels;

		/* Wait for trigger, then begin fetching data. */
		g_snprintf(command, sizeof(command), "ARM;WAIT;*OPC");
		sr_scpi_send(sdi->conn, command);

		lecroy_xstream_request_data(sdi);
	}

	return TRUE;
}
