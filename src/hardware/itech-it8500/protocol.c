/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Timo Kokkonen <tjko@iki.fi>
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
#include <string.h>
#include "protocol.h"


SR_PRIV char itech_it8500_checksum(struct itech_it8500_cmd_packet *packet)
{
	unsigned char *p;
	unsigned char checksum;
	unsigned int i;

	if (!packet)
		return 0xff;

	checksum = 0;
	p = (unsigned char*) packet;

	for (i = 0; i < sizeof(*packet) - 1; i++) {
		checksum += *p++;
	}

	return checksum;
}

SR_PRIV const char* itech_it8500_mode_to_string(enum itech_it8500_modes mode)
{
	switch (mode) {
	case CC:
		return "CC";
	case CV:
		return "CV";
	case CW:
		return "CW";
	case CR:
		return "CR";
	}
	return "Unknown";
}

SR_PRIV int itech_it8500_send_cmd(struct sr_serial_dev_inst *serial,
				  struct itech_it8500_cmd_packet *cmd,
				  struct itech_it8500_cmd_packet **response)
{
	int ret;
	unsigned char checksum;
	struct itech_it8500_cmd_packet *resp;
	const int packet_size = sizeof(struct itech_it8500_cmd_packet);

	if (!serial || !cmd || !response)
		return SR_ERR_ARG;

	resp = g_malloc0(packet_size);
	if (!resp)
		return SR_ERR_MALLOC;

	cmd->preamble = 0xaa;
	checksum = itech_it8500_checksum(cmd);
	cmd->checksum = checksum;

	sr_spew("%s: sending command: %02x", __func__, cmd->command);
	ret = serial_write_blocking(serial, cmd, packet_size,
				    serial_timeout(serial, packet_size));
	if (ret < 0) {
		sr_err("%s: error sending command: %d", __func__, ret);
		goto error;
	}

	ret = serial_read_blocking(serial, resp, packet_size, 100);
	if (ret < packet_size) {
		sr_err("%s: timeout waiting response to command: %d",
		       __func__, ret);
		goto error;
	}
	sr_spew("%s: response packet received: %02x", __func__, resp->command);

	if (resp->preamble != 0xaa) {
		sr_err("%s: invalid packet received (first byte: %02x)",
		       __func__, resp->preamble);
		goto error;
	}

	checksum = itech_it8500_checksum(resp);
	if (resp->checksum != checksum) {
		sr_err("%s: invalid packet received: checksum mismatch",
		       __func__);
		goto error;
	}

	if (resp->command == RESPONSE) {
		if (resp->data[0] != 0x80) {
			sr_err("%s: command (%02x) failed: status=%02x",
			       __func__, cmd->command, resp->data[0]);
			goto error;
		}
	} else {
		if (resp->command != cmd->command) {
			sr_err("%s: invalid response received: %02x"
			       " (expected: %02x)",
			       __func__, resp->command, cmd->command);
			goto error;
		}
	}


	if (*response)
		g_free(*response);
	*response = resp;
	return SR_OK;

error:
	if (resp)
		g_free(resp);
	return SR_ERR;
}

SR_PRIV int itech_it8500_get_status(struct sr_serial_dev_inst *serial,
				    struct dev_context *devc)
{
	struct itech_it8500_cmd_packet *cmd;
	struct itech_it8500_cmd_packet *resp;
	double voltage, current, power;
	unsigned char operation_state;
	unsigned int demand_state;
	enum itech_it8500_modes mode;
	gboolean load_on;
	int ret;

	if (!serial)
		return SR_ERR_ARG;

	cmd = g_malloc0(sizeof(*cmd));
	if (!cmd)
		return SR_ERR_MALLOC;
	cmd->command = CMD_GET_STATE;
	resp = NULL;

	ret = itech_it8500_send_cmd(serial, cmd, &resp);
	if (ret == SR_OK) {
		voltage = RL32(&resp->data[0]) / 1000.0;
		current = RL32(&resp->data[4]) / 10000.0;
		power = RL32(&resp->data[8]) / 1000.0;
		operation_state = resp->data[12];
		demand_state = RL16(&resp->data[13]);
		if (demand_state & 0x0040)
			mode = CC;
		else if (demand_state & 0x0080)
			mode = CV;
		else if (demand_state & 0x0100)
			mode = CW;
		else if (demand_state & 0x0200)
			mode = CR;
		else
			mode = CC;
		load_on = (operation_state & 0x08 ? TRUE : FALSE);

		sr_dbg("Load status: V=%.4f, I=%.4f, P=%.3f, State=%s, Mode=%s"
		       " (op=0x%02x, demand=0x%04x)",
		       voltage, current, power,
		       (load_on ? "ON": "OFF"),
		       itech_it8500_mode_to_string(mode),
		       operation_state, demand_state);

		if (devc) {
			devc->voltage = voltage;
			devc->current = current;
			devc->power = power;
			devc->operation_state = operation_state;
			devc->demand_state = demand_state;
			devc->mode = mode;
			devc->load_on = load_on;
		}
	}

	g_free(cmd);
	if (resp)
		g_free(resp);

	return ret;
}

SR_PRIV int itech_it8500_get_int(struct sr_serial_dev_inst *serial,
				 enum itech_it8500_command command,
				 int *result)
{
	struct itech_it8500_cmd_packet *cmd;
	struct itech_it8500_cmd_packet *resp;
	int ret;

	cmd = g_malloc0(sizeof(*cmd));
	if (!cmd)
		return SR_ERR_MALLOC;
	cmd->command = command;
	resp = NULL;

	ret = itech_it8500_send_cmd(serial, cmd, &resp);
	if (ret == SR_OK) {
		*result = RL32(&resp->data[0]);
	}

	g_free(cmd);
	if (resp)
		g_free(resp);

	return ret;
}

SR_PRIV void itech_it8500_channel_send_value(const struct sr_dev_inst *sdi,
					     struct sr_channel *ch,
					     float value,
					     enum sr_mq mq,
					     enum sr_unit unit,
					     int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = 1;
	analog.data = &value;
	analog.meaning->mq = mq;
	analog.meaning->unit = unit;
	analog.meaning->mqflags = SR_MQFLAG_DC;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

SR_PRIV int itech_it8500_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	GSList *l;
	int ret;

	sr_spew("%s(%d,%d,%p): called (%d)", __func__, fd, revents,
		cb_data, G_IO_IN);

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	ret = itech_it8500_get_status(serial, devc);
	if (ret == SR_OK) {
		std_session_send_df_frame_begin(sdi);

		l = g_slist_nth(sdi->channels, 0);
		itech_it8500_channel_send_value(sdi, l->data,
						(float) devc->voltage,
						SR_MQ_VOLTAGE,
						SR_UNIT_VOLT, 5);

		l = g_slist_nth(sdi->channels, 1);
		itech_it8500_channel_send_value(sdi, l->data,
						(float) devc->current,
						SR_MQ_CURRENT,
						SR_UNIT_AMPERE, 5);

		l = g_slist_nth(sdi->channels, 2);
		itech_it8500_channel_send_value(sdi, l->data,
						(float) devc->power,
						SR_MQ_POWER,
						SR_UNIT_WATT, 5);

		std_session_send_df_frame_end(sdi);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
