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

	if (sr_scpi_send(sdi->conn, "SAMPles %ld", sample_rate) != SR_OK) {
		sr_spew("Sent \"SAMPLes %llu\", ERROR on response\n", sample_rate);
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

	if (sr_scpi_get_int(sdi->conn, "SAMPles?", &return_buf) != SR_OK) {
		sr_spew("Sent \"SAMPLes?\", ERROR on response\n");
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

	for (j = 0; j < channel_count; j++) {
		if ( tlf_channel_state_set(sdi, j, TRUE) != SR_OK ) {
			return SR_ERR;
		}
		sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE,
			       devc->chan_names[j]);
	}

	return SR_OK;

}

SR_PRIV int tlf_trigger_list(const struct sr_dev_inst *sdi) // gets trigger options
{
	char *buf;
	char command[25];
	char *token;
	int32_t trigger_option_count;
	struct dev_context *devc;

	devc = sdi->priv;

	sprintf(command, "TRIGger:OPTions?"); // get trigger options
	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
		return SR_ERR;
	}
	sr_spew("send: %s, TRIGGER options: %s", command, buf);

	// parse the trigger options string (CSV format)
	trigger_option_count=0;
	token = strtok(buf, ","); // initialize the pointer location to beginning of the buffer

	while (token!=NULL) {
		// set the trigger_matches to the token's trigger type
		if        ( !g_ascii_strcasecmp(token, "0") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_ZERO;
			sr_spew("Trigger token: %s, Accept ZERO trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "1") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_ONE;
			sr_spew("Trigger token: %s, Accept ONE trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "R") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_RISING;
			sr_spew("Trigger token: %s, Accept RISING trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "F") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_FALLING;
			sr_spew("Trigger token: %s, Accept FALLING trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "E") ) {
			devc->trigger_matches[trigger_option_count]=SR_TRIGGER_EDGE;
			sr_spew("Trigger token: %s, Accept EDGE trigger", token);
		} else if ( !g_ascii_strcasecmp(token, "X") ) {
			// ignore 'X' that means OFF
		} else {
			sr_spew("Error on token: %s", token);
			return SR_ERR;
		}
		trigger_option_count += 1; // increment the number of trigger options

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
	char print_buffer[2048];
	char tmp_buffer[500];

	(void) revents;

	(void)fd;

	sr_spew("---> Entering tlf_receive_data");
	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	// if (revents == G_IO_IN) {
	// 	/* TODO */
	// }

	/* Are we waiting for a response from the device? */
	if (!devc->data_pending)
		return TRUE;

	/* Check if a new query response is coming our way. */
	if (!data) {
		if (sr_scpi_read_begin(sdi->conn) == SR_OK) {
			/* The 16 here accounts for the header and EOL. */
			data = g_array_sized_new(FALSE, FALSE, sizeof(uint8_t),
					32);
					//16 + model_state->samples_per_frame);
		sr_spew("read_begin");
		}
		else
			return TRUE;
	}

	/* Store incoming data. */
	chunk_len = sr_scpi_read_data(sdi->conn, devc->receive_buffer,
			RECEIVE_BUFFER_SIZE);
	if (chunk_len < 0) {
		sr_dbg("Finished reading data, chunk_len: %d", chunk_len);
		goto fail;
	}

	sr_spew("appending data, chunk_len: %d", chunk_len);
	g_array_append_vals(data, devc->receive_buffer, chunk_len);

	print_buffer[0] = '\0';
	for (int i=0; i < chunk_len; i=i+4) { // for 32 bit uint timestamp
		// uint32_t timestamp = ((char) devc->receive_buffer[i+1] << 8) | ((char) devc->receive_buffer[i]);
		// uint16_t value = ((char) devc->receive_buffer[i+3] << 8) | ((char) devc->receive_buffer[i+2]);

		// uint32_t timestamp = ((uint16_t) devc->receive_buffer[i+1] << 8) | devc->receive_buffer[i];

		uint32_t timestamp = (((uint8_t) devc->receive_buffer[i+1]) << 8) | ((uint8_t) devc->receive_buffer[i]);
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

		uint16_t value = (((uint8_t) devc->receive_buffer[i+3]) << 8) | ((uint8_t) devc->receive_buffer[i+2]);
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
	sr_spew("Data: %s", print_buffer);

	return TRUE;

	// /* Read the entire query response before processing. */
	// if (!sr_scpi_read_complete(sdi->conn)){
	// 	sr_spew("read is incomplete, loop back");
	// 	return TRUE;
	// }

	sr_spew("read is complete");

	sr_spew("Data: %d, %d, %d, %d", devc->receive_buffer[0], devc->receive_buffer[1], devc->receive_buffer[2], devc->receive_buffer[3]);

	/* We finished reading and are no longer waiting for data. */
	devc->data_pending = FALSE;

	sr_spew("freeing data");
	g_array_free(data, TRUE);
	data = NULL;
	return TRUE;

	fail:
	if (data) {
		g_array_free(data, TRUE);
		data = NULL;
	}

	return FALSE;

}


