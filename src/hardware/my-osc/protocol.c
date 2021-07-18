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

void my_osc_set_limit_frames(struct sr_serial_dev_inst *serial, uint64_t val){
	char buf[9];
	buf[0] = CMD_SET_LIMIT_FRAMES;
	WL64(buf + 1, val);
	if (serial_write_blocking(serial, buf, 9, serial_timeout(serial, 1)) != 9)
		return SR_ERR;
}

SR_PRIV int my_osc_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	int len;
	uint8_t shift = 0;
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

	len = 4 * devc->cur_samplerate;;
	buf = devc->buf;
	//len = serial_read_nonblocking(serial, buf, len);
	len = serial_read_blocking(serial, buf, len, 100);
	if (len <= 0) {
		return TRUE;
	}

	while (devc->channel_entry)
	{
		for (uint64_t i = 0; i < devc->cur_samplerate; i++)
			devc->data[i] = *((int16_t *)buf + 2 * i + shift)/1000.0;

		ch = devc->channel_entry->data;
		sr_analog_init(&analog, &encoding, &meaning, &spec, 3);
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.num_samples = len/4;
		analog.data = devc->data;
		meaning.channels = g_slist_append(NULL, ch);
		meaning.mq = SR_MQ_VOLTAGE;
		meaning.unit = SR_UNIT_VOLT;
		sr_session_send(sdi, &packet);

		shift++;
		devc->channel_entry = devc->channel_entry->next;
	}
	
	/*if (devc->channel_entry->next) {
		devc->channel_entry = devc->channel_entry->next;
		rigol_ds_channel_start(sdi);
	} else {
		std_session_send_df_frame_end(sdi);
		if (sr_sw_limits_check(&devc->limits))
			sr_dev_acquisition_stop(sdi);
	}*/
	sr_sw_limits_update_frames_read(&devc->limits, 1);
	memset(devc->buf, 0, BUFSIZE);
	//std_session_send_df_frame_end(sdi);
	if (sr_sw_limits_check(&devc->limits)){
		sr_dev_acquisition_stop(sdi);
		sr_err("%d", len);
	}
	else{
		devc->channel_entry = devc->enabled_channels;
		//std_session_send_df_frame_begin(sdi);
		//sr_err("%d", len);
	}

	return TRUE;
}
