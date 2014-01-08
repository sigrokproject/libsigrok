/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

#include <string.h>
#include <errno.h>
#include "protocol.h"

static void dump_packet(char *msg, uint8_t *packet)
{
	int i;
	char str[128];

	str[0] = 0;
	for (i = 0; i < PACKET_SIZE; i++)
		sprintf(str + strlen(str), "%.2x ", packet[i]);
	sr_dbg("%s: %s", msg, str);

}

static void handle_packet(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	float value, data[MAX_CHANNELS];
	int offset, i;

	devc = sdi->priv;
	dump_packet("received", devc->packet);
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.probes = sdi->probes;
	analog.num_samples = 1;

	analog.mq = SR_MQ_VOLTAGE;
	analog.unit = SR_UNIT_VOLT;
	analog.mqflags = SR_MQFLAG_DC;
	analog.data = data;
	for (i = 0; i < devc->model->num_channels; i++) {
		offset = 2 + i * 4;
		value = ((devc->packet[offset] << 8) + devc->packet[offset + 1]) / 100.0;
		analog.data[i] = value;
		devc->config[i].output_voltage_last = value;
	}
	sr_session_send(sdi, &packet);

	analog.mq = SR_MQ_CURRENT;
	analog.unit = SR_UNIT_AMPERE;
	analog.mqflags = 0;
	analog.data = data;
	for (i = 0; i < devc->model->num_channels; i++) {
		offset = 4 + i * 4;
		value = ((devc->packet[offset] << 8) + devc->packet[offset + 1]) / 1000.0;
		analog.data[i] = value;
		devc->config[i].output_current_last = value;
	}
	sr_session_send(sdi, &packet);

	for (i = 0; i < devc->model->num_channels; i++)
		devc->config[i].output_enabled = (devc->packet[15] & (1 << i)) ? TRUE : FALSE;

	devc->over_current_protection = devc->packet[18] ? TRUE : FALSE;
	if (devc->packet[19] < 3)
		devc->channel_mode = devc->packet[19];

}

SR_PRIV void send_packet(const struct sr_dev_inst *sdi, uint8_t *packet)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial_write(serial, packet, PACKET_SIZE) == -1)
		sr_dbg("Failed to send packet: %s", strerror(errno));
	dump_packet("sent", packet);
}

SR_PRIV void send_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t packet[PACKET_SIZE];
	int value, offset, i;

	devc = sdi->priv;
	memset(packet, 0, PACKET_SIZE);
	packet[0] = 0xaa;
	packet[1] = 0x20;
	packet[14] = 0x01;
	packet[16] = 0x01;
	for (i = 0; i < devc->model->num_channels; i++) {
		offset = 2 + i * 4;
		value = devc->config[i].output_voltage_max * 100;
		packet[offset] = (value >> 8) & 0xff;
		packet[offset + 1] = value & 0xff;
		value = devc->config[i].output_current_max * 1000;
		packet[offset + 2] = (value >> 8) & 0xff;
		packet[offset + 3] = value & 0xff;
		if (devc->config[i].output_enabled)
			packet[15] |= 1 << i;
	}
	packet[18] = devc->over_current_protection ? 1 : 0;
	packet[19] = devc->channel_mode;
	/* Checksum. */
	value = 0;
	for (i = 0; i < PACKET_SIZE - 1; i++)
		value += packet[i];
	packet[i] = value & 0xff;
	send_packet(sdi, packet);

}

SR_PRIV int atten_pps3xxx_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	const struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	unsigned char c;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;
	if (revents == G_IO_IN) {
		if (serial_read_nonblocking(serial, &c, 1) < 0)
			return TRUE;
		devc->packet[devc->packet_size++] = c;
		if (devc->packet_size == PACKET_SIZE) {
			handle_packet(sdi);
			devc->packet_size = 0;
			if (devc->acquisition_running)
				send_config(sdi);
			else {
				serial_source_remove(serial);
				packet.type = SR_DF_END;
				sr_session_send(sdi, &packet);
			}
		}
	}

	return TRUE;
}

