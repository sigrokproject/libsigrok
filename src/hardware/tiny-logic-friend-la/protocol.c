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

#include "protocol.h"
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"


size_t samples_sent = 0; // DEBUG

// uint64_t samplerates[3]; // sample rate storage: min, max, step size (all in Hz)
// const int channel_count_max = 16; // maximum number of channels
// const int channel_char_max=6;
// char chan_names[16][7] = { // channel names, start with default
// 	"000001", "000002", "000003", "000004", "000005", "000006", "000007", "000008",
// 	"000009", "000010", "000011", "000012", "000013", "000014", "000015", "000016",
// };

// int32_t channel_count = 0; // initialize to 0

SR_PRIV int tlf_samplerates_list(const struct sr_dev_inst *sdi) // gets sample rates from device
{

	struct dev_context *devc;
	devc = sdi->priv;

	int32_t sample_rate_min, sample_rate_max, sample_rate_step;

	if (sr_scpi_get_int(sdi->conn, "RATE:MIN?", &sample_rate_min) != SR_OK) {
		sr_spew("Sent \"RATE:MIN?\", ERROR on response\n");
		return SR_ERR;
	}

	if (sr_scpi_get_int(sdi->conn, "RATE:MAX?", &sample_rate_max) != SR_OK) {
		sr_spew("Sent \"RATE:MAX?\", ERROR on response\n");
		return SR_ERR;
	}

	if (sr_scpi_get_int(sdi->conn, "RATE:STEP?", &sample_rate_step) != SR_OK) {
		sr_spew("Sent \"RATE:STEP?\", ERROR on response\n");
		return SR_ERR;
	}

	// store the sample rate range and step size
	devc->samplerate_range[0] = (uint64_t) sample_rate_min;
	devc->samplerate_range[1] = (uint64_t) sample_rate_max;
	devc->samplerate_range[2] = (uint64_t) sample_rate_step;

	sr_spew("Sample rate MIN: %d Hz, MAX: %d Hz, STEP: %d Hz\n",
				sample_rate_min, sample_rate_max, sample_rate_step);

	return SR_OK;
}

SR_PRIV int tlf_samplerate_set(const struct sr_dev_inst *sdi, uint64_t sample_rate)
{
	struct dev_context *devc;
	devc = sdi->priv;

	if (sr_scpi_send(sdi->conn, "RATE %ld", sample_rate) != SR_OK) {
		sr_spew("Sent \"RATE %llu\", ERROR on response\n", sample_rate);
		return SR_ERR;
	}

	devc->cur_samplerate = sample_rate;

	return SR_OK;
}

SR_PRIV int tlf_samplerate_get(const struct sr_dev_inst *sdi, uint64_t *sample_rate)
{
	struct dev_context *devc;
	int return_buf;

	devc = sdi->priv;

	if (sr_scpi_get_int(sdi->conn, "RATE?", &return_buf) != SR_OK) {
		sr_spew("Sent \"RATE?\", ERROR on response\n");
		return SR_ERR;
	}
	devc->cur_samplerate = (uint64_t) return_buf; // update private device context
	*sample_rate = (uint64_t) return_buf;         // send back the sample_rate value

	return SR_OK;
}

SR_PRIV int tlf_samples_set(const struct sr_dev_inst *sdi, int32_t samples) // set samples count
{
	struct dev_context *devc;
	devc = sdi->priv;

	if (sr_scpi_send(sdi->conn, "SAMPles %ld", samples) != SR_OK) {
		sr_dbg("tlf_samples_set Sent \"SAMPLes %d\", ERROR on response\n", samples);
		return SR_ERR;
	}
	sr_spew("tlf_samples_set sent \"SAMPLes %d\"", samples);

	devc->cur_samples = samples;

	return SR_OK;
}

SR_PRIV int tlf_maxsamples_get(const struct sr_dev_inst *sdi) // get samples count
{
	struct dev_context *devc;
	uint32_t max_samples_buf;
	devc = sdi->priv;

	if (sr_scpi_get_int(sdi->conn, "SAMPles:MAX?", &max_samples_buf) != SR_OK) {
		sr_dbg("tlf_samples_get Sent \"SAMPLes?\", ERROR on response\n");
		return SR_ERR;
	}
	sr_spew("tlf_samples_get Samples = %d", &max_samples_buf);

	devc->max_samples = max_samples_buf;

	return SR_OK;
}

SR_PRIV int tlf_samples_get(const struct sr_dev_inst *sdi, int32_t *samples) // get samples count
{
	struct dev_context *devc;
	devc = sdi->priv;

	if (sr_scpi_get_int(sdi->conn, "SAMPles?", samples) != SR_OK) {
		sr_dbg("tlf_samples_get Sent \"SAMPLes?\", ERROR on response\n");
		return SR_ERR;
	}
	sr_spew("tlf_samples_get Samples = %d", *samples);

	devc->cur_samples = *samples;

	return SR_OK;
}

SR_PRIV int tlf_channel_state_set(const struct sr_dev_inst *sdi, int32_t channel_index, gboolean enabled) // gets channel status
{
	char command[64];
	struct dev_context *devc;

	devc = sdi->priv;

	// channel count and names should be collected before setting any channels
	if ( (channel_index < 0) ||
		 (channel_index >= devc->channels) ) {
		return SR_ERR;
	}

	if (enabled == TRUE) {
		sprintf(command, "CHANnel%d:STATus %s", channel_index+1, "ON"); // define channel to get name
	} else if (enabled == FALSE) {
		sprintf(command, "CHANnel%d:STATus %s", channel_index+1, "OFF"); // define channel to get name
	} else {
		return SR_ERR;
	}

	if (sr_scpi_send(sdi->conn, command) != SR_OK) {
		return SR_ERR;
	}

	devc->channel_state[channel_index] = enabled; // sets to gboolean value of enabled

	sr_spew("tlf_channel_state_set Channel: %d set ON", channel_index+1);
	return SR_OK;
}

SR_PRIV int tlf_channel_state_get(const struct sr_dev_inst *sdi, int32_t channel_index, gboolean *enabled) // sets channel status
{
	struct dev_context *devc;

	devc = sdi->priv;

	// channel count and names should be collected before setting any channels
	if ( (channel_index < 0) ||
		 (channel_index >= devc->channels) ) {
		return SR_ERR;
	}

	*enabled = devc->channel_state[channel_index];

	return SR_OK;
}

SR_PRIV int tlf_channels_list(struct sr_dev_inst *sdi) // gets channel names from device
{
	sr_spew("tlf_channels_list 0");

	char *buf;
	char command[25];
	int32_t j;
	int32_t channel_count;
	struct dev_context *devc;
	struct sr_channel_group *cg;
	struct sr_channel *ch;

	devc = sdi->priv;

	sr_spew("tlf_channels_list 1");

	// request the CHANnel count
	if (sr_scpi_get_int(sdi->conn, "CHANnel:COUNT?", &channel_count) != SR_OK) {
		sr_dbg("Sent \"CHANnel:COUNT?\", ERROR on response\n");
		return SR_ERR;
	}

	sr_spew("tlf_channels_list 2");

	if ( (channel_count < 0) ||
		 (channel_count > TLF_CHANNEL_COUNT_MAX) ) {
		sr_spew("Sent \"CHANnel:COUNT?\", received %d", channel_count);
		sr_dbg("ERROR: Out of channel range \
			   between 0 and %d (TLF_CHANNEL_COUNT_MAX)", TLF_CHANNEL_COUNT_MAX);
		return SR_ERR;
	}

	sr_spew("channel_count = %d", channel_count);
	devc->channels = channel_count; // update the device context channels value

	sr_spew("tlf_channels_list 3");

	for (int i=0; i < channel_count; i++) {
		sprintf(command, "CHANnel%d:NAME?", i+1); // define channel to get name
		if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
			sr_dbg("Sent \"%s\", ERROR on response\n", command);
			return SR_ERR;
		}
		sr_spew("send: %s, chan #: %d, channel name: %s", command, i+1, buf);
		if ( strlen(buf) > TLF_CHANNEL_CHAR_MAX ) { // ensure string is shorter than max length
			buf[TLF_CHANNEL_CHAR_MAX] = '\0'; // if so, put a null at max length
		}
		strcpy(devc->chan_names[i], buf); // copy into the device context's channel names
	}

	// set remaining channel names to NULL
	if (channel_count < TLF_CHANNEL_COUNT_MAX) {
		for (int i=channel_count; i < TLF_CHANNEL_COUNT_MAX; i++) {
			strcpy(devc->chan_names[i], "");
		}
	}

	for (int i=0; i < TLF_CHANNEL_COUNT_MAX; i++) {
		sr_spew("Channel index: %d Channel name: %s", i, devc->chan_names[i]);
	}

	// **** CHANNEL test ************************

	// for (int i=0; i < chan_count; i++) {

	// 	sprintf(command, "CHANnel%d:STATus?", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, status: %s", command, i+1, buf);
	// }

	// for (int i=0; i < chan_count; i++) {

	// 	sprintf(command, "CHANnel%d:STATus ON", i+1); // define channel to get name
	// 	sr_spew("About to send: %s, count: %ld", command, strlen(command));
	// 	if (sr_scpi_send(sdi->conn, command) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s", command);
	// }

	// for (int i=0; i < chan_count; i++) {

	// 	sprintf(command, "CHANnel%d:STATus?", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, status: %s", command, i+1, buf);
	// }

	// 	for (int i=0; i < chan_count; i++) {

	// 	sprintf(command, "CHANnel%d:STATus OFF", i+1); // define channel to get name
	// 	sr_spew("About to send: %s, count: %ld", command, strlen(command));
	// 	if (sr_scpi_send(sdi->conn, command) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s", command);
	// }

	// for (int i=0; i < chan_count; i++) {

	// 	sprintf(command, "CHANnel%d:STATus?", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, status: %s", command, i+1, buf);
	// }

	// ***** end Channel setting test

	// // **** TRIGGER test

	// for (int32_t i=0; i < channel_count; i++) {

	// 	sprintf(command, "CHANnel%d:TRIGger?", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, TRIGGER: %s", command, i+1, buf);
	// }

	// // Set trigger
	// for (int32_t i=0; i < channel_count; i++) {

	// 	sprintf(command, "CHANnel%d:TRIGger R", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, TRIGGER: %s", command, i+1, buf);
	// }


	// for (int32_t i=0; i < channel_count; i++) {

	// 	sprintf(command, "CHANnel%d:TRIGger?", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, TRIGGER: %s", command, i+1, buf);
	// }


	sr_dbg("Setting all channels on, configuring channels");

	/* Logic channels, all in one channel group. */
	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("Logic");

	for (j = 0; j < channel_count; j++) {
		if ( tlf_channel_state_set(sdi, j, TRUE) != SR_OK ) {
			return SR_ERR;
		}
		sr_spew("Adding channel %d: %s", j, devc->chan_names[j]);
		ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE,
			       devc->chan_names[j]);
		cg->channels = g_slist_append(cg->channels, ch);
	}

	sdi->channel_groups = g_slist_append(NULL, cg);

	return SR_OK;

}

SR_PRIV int tlf_trigger_list(const struct sr_dev_inst *sdi) // gets trigger options
{
	char *buf;
	char command[25];
	char *token;
	int32_t trigger_option_count, array_count;
	struct dev_context *devc;

	devc = sdi->priv;

	sprintf(command, "TRIGger:OPTions?"); // get trigger options
	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
		return SR_ERR;
	}
	sr_spew("send: %s, TRIGGER options: %s", command, buf);

	// parse the trigger options string (CSV format)
	trigger_option_count = 0;
	array_count = 0;
	token = strtok(buf, ","); // initialize the pointer location to beginning of the buffer

	for(size_t i=0;i < devc->trigger_matches_count; i++){
		devc->trigger_matches[i] = NULL;
	}


	while (token!=NULL) {
		// set the trigger_matches to the token's trigger type
		if  ( !g_ascii_strcasecmp(token, "0") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_ZERO;
			trigger_option_count++;
			sr_spew("Trigger token: %s, Accept ZERO trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "1") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_ONE;
			trigger_option_count++;
			sr_spew("Trigger token: %s, Accept ONE trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "R") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_RISING;
			trigger_option_count++;
			sr_spew("Trigger token: %s, Accept RISING trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "F") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_FALLING;
			trigger_option_count++;
			sr_spew("Trigger token: %s, Accept FALLING trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "E") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_EDGE;
			trigger_option_count++;
			sr_spew("Trigger token: %s, Accept EDGE trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "X") ) {
			// ignore 'X' that means OFF
		} else {
			sr_spew("Error on token: %s", token);
			return SR_ERR;
		}

		token = strtok(NULL, ","); // set to the next token
	}

	return SR_OK;
}

SR_PRIV int tlf_exec_run(const struct sr_dev_inst *sdi) // start measurement
{
	return sr_scpi_send(sdi->conn, "RUN");

}

SR_PRIV int tlf_exec_stop(const struct sr_dev_inst *sdi) // stop measurement
{
	return sr_scpi_send(sdi->conn, "STOP");

}


// // from Yokogawa-dlm
// /**
//  * Attempts to query sample data from the oscilloscope in order to send it
//  * to the session bus for further processing.
//  *
//  * @param fd The file descriptor used as the event source.
//  * @param revents The received events.
//  * @param cb_data Callback data, in this case our device instance.
//  *
//  * @return TRUE in case of success or a recoverable error,
//  *         FALSE when a fatal error was encountered.
//  */
// SR_PRIV int dlm_data_receive(int fd, int revents, void *cb_data)
// {
// 	struct sr_dev_inst *sdi;
// 	struct scope_state *model_state;
// 	struct dev_context *devc;
// 	struct sr_channel *ch;
// 	int chunk_len, num_bytes;
// 	static GArray *data = NULL;

// 	(void)fd;
// 	(void)revents;

// 	if (!(sdi = cb_data))
// 		return FALSE;

// 	if (!(devc = sdi->priv))
// 		return FALSE;

// 	if (!(model_state = (struct scope_state*)devc->model_state))
// 		return FALSE;

// 	/* Are we waiting for a response from the device? */
// 	if (!devc->data_pending)
// 		return TRUE;

// 	/* Check if a new query response is coming our way. */
// 	if (!data) {
// 		if (sr_scpi_read_begin(sdi->conn) == SR_OK)
// 			/* The 16 here accounts for the header and EOL. */
// 			data = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t),
// 					16 + model_state->samples_per_frame);
// 		else
// 			return TRUE;
// 	}

// 	/* Store incoming data. */
// 	chunk_len = sr_scpi_read_data(sdi->conn, devc->receive_buffer,
// 			RECEIVE_BUFFER_SIZE);
// 	if (chunk_len < 0) {
// 		sr_err("Error while reading data: %d", chunk_len);
// 		goto fail;
// 	}
// 	g_array_append_vals(data, devc->receive_buffer, chunk_len);

// 	/* Read the entire query response before processing. */
// 	if (!sr_scpi_read_complete(sdi->conn))
// 		return TRUE;

// 	/* We finished reading and are no longer waiting for data. */
// 	devc->data_pending = FALSE;

// 	 Signal the beginning of a new frame if this is the first channel.
// 	if (devc->current_channel == devc->enabled_channels)
// 		std_session_send_df_frame_begin(sdi);

// 	if (dlm_block_data_header_process(data, &num_bytes) != SR_OK) {
// 		sr_err("Encountered malformed block data header.");
// 		goto fail;
// 	}

// 	if (num_bytes == 0) {
// 		sr_warn("Zero-length waveform data packet received. " \
// 				"Live mode not supported yet, stopping " \
// 				"acquisition and retrying.");
// 		/* Don't care about return value here. */
// 		dlm_acquisition_stop(sdi->conn);
// 		g_array_free(data, TRUE);
// 		dlm_channel_data_request(sdi);
// 		return TRUE;
// 	}

// 	ch = devc->current_channel->data;
// 	switch (ch->type) {
// 	case SR_CHANNEL_ANALOG:
// 		if (dlm_analog_samples_send(data,
// 				&model_state->analog_states[ch->index],
// 				sdi) != SR_OK)
// 			goto fail;
// 		break;
// 	case SR_CHANNEL_LOGIC:
// 		if (dlm_digital_samples_send(data, sdi) != SR_OK)
// 			goto fail;
// 		break;
// 	default:
// 		sr_err("Invalid channel type encountered.");
// 		break;
// 	}

// 	g_array_free(data, TRUE);
// 	data = NULL;

// 	/*
// 	 * Signal the end of this frame if this was the last enabled channel
// 	 * and set the next enabled channel. Then, request its data.
// 	 */
// 	if (!devc->current_channel->next) {
// 		std_session_send_df_frame_end(sdi);
// 		devc->current_channel = devc->enabled_channels;

// 		/*
// 		 * As of now we only support importing the current acquisition
// 		 * data so we're going to stop at this point.
// 		 */
// 		sr_dev_acquisition_stop(sdi);
// 		return TRUE;
// 	} else
// 		devc->current_channel = devc->current_channel->next;

// 	if (dlm_channel_data_request(sdi) != SR_OK) {
// 		sr_err("Failed to request acquisition data.");
// 		goto fail;
// 	}

// 	return TRUE;

// fail:
// 	if (data) {
// 		g_array_free(data, TRUE);
// 		data = NULL;
// 	}

// 	return FALSE;
// }

SR_PRIV int tlf_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int chunk_len;
	static GArray *data = NULL;
	char print_buffer[6000];
	char tmp_buffer[6000];

	// int ret;

	(void) revents;

	(void) fd;

	sr_spew("---> Entering tlf_receive_data");
	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	 //Are we waiting for a response from the device?
	if (!devc->data_pending)
		return TRUE;

	if ( sr_scpi_send(sdi->conn, "DATA?") != SR_OK ) {
			goto close;
		}

	// sr_spew("sent DATA?");


	// ret = sr_scpi_get_string(sdi->conn, "DATA?", tmp_buffer);
	// //ret = sr_scpi_get_data(sdi->conn, "DATA?", devc->receive_buffer);

	// sr_spew("get data, ret= %d, %s", ret, tmp_buffer);

	// /* Check if a new query response is coming our way. */
	// if (!data) { // initial query

	// 	ret = sr_scpi_read_begin(sdi->conn);
	// 	sr_spew("read_begin = %d", ret);

	// 	// if (sr_scpi_read_begin(sdi->conn) == SR_OK) {
	// 	// 	// /* The 16 here accounts for the header and EOL. */
	// 	// 	data = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t),
	// 	// 			32);
	// 	// 			//16 + model_state->samples_per_frame);
	// 	// 	sr_spew("created new data array");
	// 	// }
	// 	// else
	// 	// 	sr_spew("else?");
	// 	// 	return TRUE;
	// }

	//std_session_send_df_frame_end(sdi);

	if (!data) {
		if (sr_scpi_read_begin(sdi->conn) == SR_OK) {
			// /* The 16 here accounts for the header and EOL. */
			data = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t),
					32);
					//16 + model_state->samples_per_frame);
			sr_spew("read_begin");
		}
		else
			return TRUE;
	} else {
		sr_scpi_read_begin(sdi->conn);
	}

	/* Store incoming data. */

	// request data

	sr_spew("get a chunk");

	// read data

	chunk_len = sr_scpi_read_data(sdi->conn, devc->receive_buffer,
			RECEIVE_BUFFER_SIZE);
	if (chunk_len < 0) {
		sr_dbg("Finished reading data, chunk_len: %d", chunk_len);
		goto close;
	}

	sr_spew("Received data, chunk_len: %d", chunk_len);
	// g_array_append_vals(data, devc->receive_buffer, chunk_len);

	uint32_t timestamp;
	uint16_t value;

	print_buffer[0] = '\0';
	for (int i=0; i < chunk_len; i=i+4) { // for 32 bit uint timestamp
		// uint32_t timestamp = ((char) devc->receive_buffer[i+1] << 8) | ((char) devc->receive_buffer[i]);
		// uint16_t value = ((char) devc->receive_buffer[i+3] << 8) | ((char) devc->receive_buffer[i+2]);

		// uint32_t timestamp = ((uint16_t) devc->receive_buffer[i+1] << 8) | devc->receive_buffer[i];

		timestamp = (((uint8_t) devc->receive_buffer[i+1]) << 8) | ((uint8_t) devc->receive_buffer[i]);
		// uint32_t timestamp = (uint8_t) devc->receive_buffer[i+1];
		// sprintf(tmp_buffer, "[1: %u]", timestamp); //32 bit timestamp
		// strcat(print_buffer, tmp_buffer);
		// timestamp = timestamp << 8;
		// sprintf(tmp_buffer, "[2: %u]", timestamp); //32 bit timestamp
		// strcat(print_buffer, tmp_buffer);
		// timestamp = timestamp | (uint8_t) devc->receive_buffer[i];
		// sprintf(tmp_buffer, "[3: %u]", timestamp); //32 bit timestamp
		// strcat(print_buffer, tmp_buffer);

		// uint16_t value = ((uint16_t) devc->receive_buffer[i+3] << 8) | devc->receive_buffer[i+2];

		value = (((uint8_t) devc->receive_buffer[i+3]) << 8) | ((uint8_t) devc->receive_buffer[i+2]);
		// sprintf(tmp_buffer, "[1v: %u]", value); //32 bit timestamp
		// strcat(print_buffer, tmp_buffer);
		// value = value << 8;
		// // sprintf(tmp_buffer, "[2v: %u]", value); //32 bit timestamp
		// // strcat(print_buffer, tmp_buffer);
		// value = value | (uint8_t) devc->receive_buffer[i+2];
		// // sprintf(tmp_buffer, "[3v: %u]", value); //32 bit timestamp
		// // strcat(print_buffer, tmp_buffer);


		sprintf(tmp_buffer, "[%u %u]", timestamp, value); //32 bit timestamp
		strcat(print_buffer, tmp_buffer);

		//sprintf(tmp_buffer, "[old %u %hu] ", (uint32_t) devc->receive_buffer[i], (uint16_t) devc->receive_buffer[i+4]); //32 bit timestamp
	// for (int i=0; i < chunk_len; i=i+4) {
	// 	sprintf(tmp_buffer, "[%hu %hu]:", devc->receive_buffer[i], devc->receive_buffer[i+2]); //16 bit timestamp
		//strcat(print_buffer, tmp_buffer);
		// sprintf(tmp_buffer, "[%d %d %d %d]--", devc->receive_buffer[i], devc->receive_buffer[i+1], devc->receive_buffer[i+2], devc->receive_buffer[i+3]);

		// sprintf(tmp_buffer, "[%d %d %d %d]--", (uint8_t) devc->receive_buffer[i], (uint8_t) devc->receive_buffer[i+1], (uint8_t) devc->receive_buffer[i+2], (uint8_t) devc->receive_buffer[i+3]);
		// strcat(print_buffer, tmp_buffer);
	}
	sr_warn("Data: %s", print_buffer);

	//return TRUE;

	// Perform Run-Length Encoded extraction into samples at each tick
	//

	// should initialize prior to starting first read
	//		- devc->last_sample
	//		- devc->last_timestamp
	//		- devc->num_samples

	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	unsigned int buffer_size = 12;


	if (devc->measured_samples == 0) { // this is the first time reading, so allocate the buffer
							  // todo *** after the last read, free the malloc'ed buffer.
		devc->raw_sample_buf = g_try_malloc(buffer_size * 2);
		if (!devc->raw_sample_buf) {
			sr_err("Sample buffer malloc failed.");
			return FALSE;
		}

		// Do we need to initialize the last_timestamp, last_sample for the first sample?
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = 2;
	logic.data = devc->raw_sample_buf;


	for (int i=0; i < chunk_len; i=i+4) {
		timestamp = (((uint8_t) devc->receive_buffer[i+1]) << 8) | ((uint8_t) devc->receive_buffer[i]);
		value = (((uint8_t) devc->receive_buffer[i+3]) << 8) | ((uint8_t) devc->receive_buffer[i+2]);
		samples_sent++;

		sr_spew("devc->measured_samples: %zu", devc->measured_samples);

		// todo * can we do math with int32_t and uint16_t unsigned??  WARNING
		for (int32_t tick=devc->last_timestamp; tick < (int32_t) timestamp; tick++) {
			// run from the previous time_stamp to the current one, fill with last_sample data
			((uint16_t*) devc->raw_sample_buf)[devc->pending_samples] = devc->last_sample;
			//sr_spew("measured_samples: %ld, storing: %u, last: [%u %u], data: [%u %u]", devc->measured_samples, devc->last_sample, devc->last_timestamp, devc->last_sample, timestamp, value);
			//sr_spew("measured_samples: %ld, data: [%u %u]", devc->measured_samples, timestamp, value);
			devc->measured_samples++;
			devc->pending_samples++; // number of samples currently stored in the buffer

			if (devc->pending_samples == buffer_size) { //buffer_size) {
				logic.length = devc->pending_samples * 2;
				if (logic.data == NULL) {
					sr_spew("tlf_receive_data: payload is NULL");
				}
				sr_session_send(sdi, &packet);


				// DEBUG


				//sr_spew("Packet sent, proceeding.");
				devc->pending_samples = 0;
			}
		}

		// finished processing the previous sample and timestamp, save next sample
		devc->last_sample = value;
		if (timestamp == 65535) {
			devc->last_timestamp = -1; // wrapped around the 16 bit counter,
									   // so reset to -1 for next sample
		} else {
			devc->last_timestamp = timestamp;
		}
	}

	sr_spew("About to flush...");
	// flush any remaining data in the buffer wit sr_session_send
	if (devc->pending_samples > 0) {
		logic.length = devc->pending_samples * 2;
		sr_session_send(sdi, &packet);
	}


	sr_warn("Data: %s", print_buffer);

	sr_spew("Finished flush.");

	sr_warn("Sent samples %zu", samples_sent);

	if (samples_sent >= devc->cur_samples) {
		goto close;
	}


	// std_session_send_df_frame_end(sdi);
	// std_session_send_df_frame_begin(sdi);

	sr_spew("<- returning from tlf_receive_data");

	// Need to determine when samples are completed.

	return TRUE;

	// /* Read the entire query response before processing. */
	// if (!sr_scpi_read_complete(sdi->conn)){
	// 	sr_spew("read is incomplete, loop back");
	// 	return TRUE;
	// }



	//sr_dev_acquisition_stop(sdi); //   ***** this is just a trial

	/* We finished reading and are no longer waiting for data. */


	sr_spew("freeing data");
	g_array_free(data, TRUE);
	data = NULL;
	return TRUE;

	close:
	if (data) {
		std_session_send_df_frame_end(sdi);
		std_session_send_df_end(sdi);
		sr_dbg("read is complete");
		devc->data_pending = FALSE;

		g_array_free(data, TRUE);
		data = NULL;
		sr_dev_acquisition_stop(sdi);
	}

	return FALSE;

}


