/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Aleksandr Orlov <orlovaleksandr7922@gmail.com>
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

void my_osc_osc_set_samplerate(struct sr_serial_dev_inst *serial, uint64_t val){
	char buf[9];
	buf[0] = CMD_SET_SAMPLE_RATE;
	WL64(buf + 1, val);
	if (serial_write_blocking(serial, buf, 9, serial_timeout(serial, 1)) != 9)
		return SR_ERR;
}

void my_osc_set_limit_samples(struct sr_serial_dev_inst *serial, uint64_t val){
	char buf[9];
	buf[0] = CMD_SET_LIMIT_SAMPLES;
	WL64(buf + 1, val);
	if (serial_write_blocking(serial, buf, 9, serial_timeout(serial, 1)) != 9)
		return SR_ERR;
}

SR_PRIV int my_osc_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	int len;
	struct sr_serial_dev_inst *serial;
	char *buf;

	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	char **tokens;
	GSList *l;

	struct sr_channel *ch;

	sdi = cb_data;
	devc = sdi->priv;
	serial = sdi->conn;

	if (revents != G_IO_IN) {
		return TRUE;
	}
	len = BUFSIZE - devc->buflen;
	buf = devc->buf;
	if (serial_readline(serial, &buf, &len, 100) != SR_OK) {
		return TRUE;
	}
	if (len <= 0)
	{
		return TRUE;
	}

	devc->buflen += len;
	if (!g_str_has_prefix((const char *)devc->buf, "meas ")) {
		sr_dbg("Unknown packet: '%s'.", devc->buf);
		return TRUE;
	}
	tokens = g_strsplit((const char *)devc->buf, " ", 3);
		devc->voltage = strtod(tokens[2], NULL) / 1000;
		devc->current = strtod(tokens[1], NULL) / 1000;
		g_strfreev(tokens);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 4);

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = 1;
	
	/* Voltage */
	l = g_slist_copy(sdi->channels);
	l = g_slist_remove_link(l, g_slist_nth(l, 1));
	meaning.channels = l;
	meaning.mq = SR_MQ_VOLTAGE;
	meaning.mqflags = SR_MQFLAG_DC;
	meaning.unit = SR_UNIT_VOLT;
	encoding.digits = 3;
	analog.data = &devc->voltage;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	/* Current */
	l = g_slist_copy(sdi->channels);
	l = g_slist_remove_link(l, g_slist_nth(l, 0));
	meaning.channels = l;
	meaning.mq = SR_MQ_VOLTAGE;
	meaning.mqflags = SR_MQFLAG_DC;
	meaning.unit = SR_UNIT_VOLT;
	encoding.digits = 3;
	analog.data = &devc->current;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	sr_sw_limits_update_samples_read(&devc->limits, 1);
	memset(devc->buf, 0, BUFSIZE);
	devc->buflen = 0;

	if (sr_sw_limits_check(&devc->limits)){
		sr_dev_acquisition_stop(sdi);
	}/*
	else{
		std_session_send_df_frame_begin(sdi);
	}

	if (devc->limits.limit_frames && devc->limits.samples_read >= SAMPLES_PER_FRAME) {
		std_session_send_df_frame_end(sdi);
		devc->limits.samples_read = 0;
		sr_sw_limits_update_frames_read(&devc->limits, 1);
		if (sr_sw_limits_check(&devc->limits)){
			sr_info("Requested number of frames reached.");
			sr_dev_acquisition_stop(sdi);
		}
	}*/

	len = devc->cur_samplerate;
	buf = devc->buf;
	if (serial_read_blocking(serial, buf, len, 250) != len) {
		return TRUE;
	}

	ch = devc->channel_entry->data;
	sr_analog_init(&analog, &encoding, &meaning, &spec, 4);
	

	if (devc->channel_entry->next) {
		/* We got the frame for this channel, now get the next channel. */
		devc->channel_entry = devc->channel_entry->next;
		rigol_ds_channel_start(sdi);
	} else {
		/* Done with this frame. */
		std_session_send_df_frame_end(sdi);

	return TRUE;
}
