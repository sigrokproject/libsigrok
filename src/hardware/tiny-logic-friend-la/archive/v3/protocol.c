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

int32_t channel_count = 0; // initialize to 0

SR_PRIV int tlf_collect_samplerates(struct sr_dev_inst *sdi) // gets sample rates from device
{

	struct device_context devc;
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
	devc->samplerates[0] = (uint64_t) sample_rate_min;
	devc->samplerates[1] = (uint64_t) sample_rate_max;
	devc->samplerates[2] = (uint64_t) sample_rate_step;

	sr_spew("Sample rate MIN: %d Hz, MAX: %d Hz, STEP: %d Hz\n",
				sample_rate_min, sample_rate_max, sample_rate_step);

	return SR_OK;
}

SR_PRIV int tlf_set_samplerate(const struct sr_dev_inst *sdi, uint64_t sample_rate)
{
	struct device_context devc;
	devc = sdi->priv;

	if (sr_scpi_send(sdi->conn, "SAMPles %ld", sample_rate) != SR_OK) {
		sr_spew("Sent \"SAMPLes %llu\", ERROR on response\n", sample_rate);
		return SR_ERR;
	}

	devc->cur_samplerate = sample_rate;

	return SR_OK;
}

SR_PRIV int tlf_get_samplerate(const struct sr_dev_inst *sdi, uint64_t *sample_rate)
{
	struct device_context devc;
	devc = sdi->priv;

	if (sr_scpi_get_int(sdi->conn, "SAMPles?", sample_rate) != SR_OK) {
		sr_spew("Sent \"SAMPLes?\", ERROR on response\n");
		return SR_ERR;
	}
	devc->cur_samplerate = (uint64_t) return_buf; // update private device context
	*sample_rate = (uint64_t) return_buf;         // send back the sample_rate value

	return SR_OK;
}

SR_PRIV int tlf_set_samples(const struct sr_dev_inst *sdi, int32_t samples) // set samples count
{
	struct device_context devc;
	devc = sdi->priv;

	if (sr_scpi_send(sdi->conn, "SAMPles %ld", samples) != SR_OK) {
		sr_dbg("tlf_set_samples Sent \"SAMPLes %ld\", ERROR on response\n", samples);
		return SR_ERR;
	}
	sr_spew("tlf_set_samples sent \"SAMPLes %ld\"", samples);

	devc->cur_samples = samples;

	return SR_OK;
}

SR_PRIV int tlf_get_samples(const struct sr_dev_inst *sdi, int32_t *samples) // get samples count
{
	struct device_context devc;
	devc = sdi->priv;

	if (sr_scpi_get_int(sdi->conn, "SAMPles?", samples) != SR_OK) {
		sr_dbg("tlf_get_samples Sent \"SAMPLes?\", ERROR on response\n");
		return SR_ERR;
	}
	sr_spew("tlf_get_samples Samples = %ld", *samples);

	devc->cur_samples = *samples;

	return SR_OK;
}

SR_PRIV int tlf_set_channel_state(const struct sr_dev_inst *sdi, int32_t channel_index, gboolean enabled) // gets channel status
{
	char command[64];

	// channel count and names should be collected before setting any channels
	if ( (channel_index < 0) ||
		 (channel_index >= channel_count) ) {
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

	sr_spew("tlf_set_channel_state Channel: %d set ON", channel_index+1);
	return SR_OK;
}

SR_PRIV int tlf_collect_channels(struct sr_dev_inst *sdi) // gets channel names from device
{

	char *buf;
	char command[25];
	int32_t samples;
	int32_t int_buffer;
	int32_t j;

	// // request the SYSTem:VERSion?
	// if (sr_scpi_get_string(sdi->conn, "SYSTem:VERSion?", &buf) != SR_OK) {
	// 	sr_spew("Sent \"SYSTem:VERSion?\", ERROR on response\n");
	// 	//return SR_ERR;
	// }

	// sr_spew("Sent \"SYSTem:VERSion?\", received: %s\n", buf);


	// if (sr_scpi_get_string(sdi->conn, "*TST?", &buf) != SR_OK) {
	// 	sr_spew("Sent \"TEST:TEXT trial\", ERROR on response\n");
	// 	//return SR_ERR;
	// }


	if (sr_scpi_get_int(sdi->conn, "RATE?", &int_buffer) != SR_OK) {
		sr_spew("Sent \"RATE?\", ERROR on response\n");
		//return SR_ERR;
	}

	if (sr_scpi_send(sdi->conn, "RATE 2e6") != SR_OK) {
		sr_spew("Sent \"RATE 2e6\", ERROR on response\n");
		//return SR_ERR;
	}

	if (sr_scpi_get_int(sdi->conn, "RATE?", &int_buffer) != SR_OK) {
		sr_spew("Sent \"RATE?\", ERROR on response\n");
		//return SR_ERR;
	}


	// ****** TRIGGER options

	sprintf(command, "TRIGger:OPTions?"); // get trigger options
	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
		return SR_ERR;
	}
	sr_spew("send: %s, TRIGGER options: %s", command, buf);


	// // request the CHANnel count
	if (sr_scpi_get_int(sdi->conn, "CHANnel:COUNT?", &channel_count) != SR_OK) {
		sr_spew("Sent \"CHANnel:COUNT?\", ERROR on response\n");
		//return SR_ERR;
	}

	sr_spew("channel_count = %d", channel_count);


	for (int i=0; i < channel_count; i++) {
		sr_spew("chan name: %s", chan_names[i]);
	}

	for (int i=0; i < channel_count; i++) {

		sprintf(command, "CHANnel%d:NAME?", i+1); // define channel to get name
		if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
			return SR_ERR;
		}
		sr_spew("send: %s, chan #: %d, channel name: %s", command, i+1, buf);
		if ( strlen(buf) > channel_char_max ) {
			buf[channel_char_max] = '\0';
		}
		strcpy(chan_names[i], buf);

	}
	// set remaining channel names to NULL
	if (channel_count < channel_count_max) {
		for (int i=channel_count; i < channel_count_max; i++) {
			strcpy(chan_names[i], "");
		}
	}

	for (int i=0; i < channel_count; i++) {
		sr_spew("chan name: %s", chan_names[i]);
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


	// ***  SAMPLE test *********************

	// // request the sample count
	if (sr_scpi_get_int(sdi->conn, "SAMPles?", &samples) != SR_OK) {
		sr_spew("Sent \"SAMPLes?\", ERROR on response\n");
		//return SR_ERR;
	}
	sr_spew("Samples = %d", samples);

	// set the sample count
	if (sr_scpi_send(sdi->conn, "SAMPles 50e3") != SR_OK) {
		sr_spew("Sent \"SAMPLes 50000\", ERROR on response\n");
		//return SR_ERR;
	}

	// request the sample count
	if (sr_scpi_get_int(sdi->conn, "SAMPles?", &samples) != SR_OK) {
		sr_spew("Sent \"SAMPLes?\", ERROR on response\n");
		//return SR_ERR;
	}
	sr_spew("Samples = %d", samples);

	// if (sr_scpi_get_string(sdi->conn, "LUVU", &buf) != SR_OK) {
	// 	sr_spew("Sent \"LUVU\", ERROR on response\n");
	// 	//return SR_ERR;
	// }

	// sr_spew("Sent \"LUVU\", received: %s\n", buf);

	sr_dbg("Setting all channels on, configuring channels");

	for (j = 0; j < channel_count; j++) {
		if ( tlf_set_channel_state(sdi, j, TRUE) != SR_OK ) {
			return SR_ERR;
		}
		sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE,
			       chan_names[j]);
	}

	return SR_OK;

}


SR_PRIV int tiny_logic_friend_la_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}


