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

SR_PRIV int tlf_collect_channels(const struct sr_dev_inst *sdi) // gets channel names from device
{

	char *buf;
	char command[25];
	int samples;
	int chan_count;
	// // request the SYSTem:VERSion?
	// if (sr_scpi_get_string(sdi->conn, "SYSTem:VERSion?", &buf) != SR_OK) {
	// 	sr_spew("Sent \"SYSTem:VERSion?\", ERROR on response\n");
	// 	//return SR_ERR;
	// }

	// sr_spew("Sent \"SYSTem:VERSion?\", received: %s\n", buf);


	if (sr_scpi_get_string(sdi->conn, "*TST?", &buf) != SR_OK) {
		sr_spew("Sent \"TEST:TEXT trial\", ERROR on response\n");
		//return SR_ERR;
	}

	sprintf(command, "TRIGger:OPTions?"); // get trigger options
	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
		return SR_ERR;
	}
	sr_spew("send: %s, TRIGGER options: %s", command, buf);


	// // request the CHANnel count
	if (sr_scpi_get_int(sdi->conn, "CHANnel:COUNT?", &chan_count) != SR_OK) {
		sr_spew("Sent \"CHANnel:COUNT?\", ERROR on response\n");
		//return SR_ERR;
	}

	sr_spew("chan_count = %d", chan_count);

	// for (int i=0; i < chan_count; i++) {

	// 	sprintf(command, "CHANnel%d:NAME?", i+1); // define channel to get name
	// 	if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
	// 		return SR_ERR;
	// 	}
	// 	sr_spew("send: %s, chan #: %d, channel name: %s", command, i+1, buf);
	// }

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

	// **** TRIGGER test

	for (int i=0; i < chan_count; i++) {

		sprintf(command, "CHANnel%d:TRIGger?", i+1); // define channel to get name
		if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
			return SR_ERR;
		}
		sr_spew("send: %s, chan #: %d, TRIGGER: %s", command, i+1, buf);
	}

	// Set trigger
	for (int i=0; i < chan_count; i++) {

		sprintf(command, "CHANnel%d:TRIGger R", i+1); // define channel to get name
		if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
			return SR_ERR;
		}
		sr_spew("send: %s, chan #: %d, TRIGGER: %s", command, i+1, buf);
	}


	for (int i=0; i < chan_count; i++) {

		sprintf(command, "CHANnel%d:TRIGger?", i+1); // define channel to get name
		if (sr_scpi_get_string(sdi->conn, command, &buf) != SR_OK) {
			return SR_ERR;
		}
		sr_spew("send: %s, chan #: %d, TRIGGER: %s", command, i+1, buf);
	}


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


	if (sr_scpi_get_string(sdi->conn, "LUVU", &buf) != SR_OK) {
		sr_spew("Sent \"LUVU\", ERROR on response\n");
		//return SR_ERR;
	}

	sr_spew("Sent \"LUVU\", received: %s\n", buf);

	return SR_OK;

// // parse the returned string and load sdi->channels using
// 	for (size_t i = 0; i < ARRAY_SIZE(channel_list); i++) {

// 	}
// 	sr_dev_inst_channel_add(sdi, index, SR_)


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


