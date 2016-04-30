/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 George Hopkins <george-hopkins@null.net>
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

static int send_command(const struct sr_dev_inst *sdi, uint16_t command)
{
	struct sr_serial_dev_inst *serial;
	uint8_t buffer[2];

	buffer[0] = command >> 8;
	buffer[1] = command;

	if (!(serial = sdi->conn))
		return SR_ERR;

	if (serial_write_nonblocking(serial, (const void *)buffer, 2) != 2)
		return SR_ERR;

	return SR_OK;
}

static void send_data(const struct sr_dev_inst *sdi, float sample)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	devc = sdi->priv;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 1);
	meaning.mq = SR_MQ_SOUND_PRESSURE_LEVEL;
	meaning.mqflags = devc->cur_mqflags;
	meaning.unit = SR_UNIT_DECIBEL_SPL;
	meaning.channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &sample;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	devc->num_samples++;
	if (devc->limit_samples && devc->num_samples >= devc->limit_samples)
		sdi->driver->dev_acquisition_stop((struct sr_dev_inst *)sdi);
}

static void process_measurement(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	unsigned short value;

	devc = sdi->priv;

	if (devc->buffer[3] & (1 << 0)) {
		devc->cur_mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_C;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_FREQ_WEIGHT_A;
	} else {
		devc->cur_mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_FREQ_WEIGHT_C;
	}

	if (devc->buffer[3] & (1 << 1)) {
		devc->cur_mqflags |= SR_MQFLAG_SPL_TIME_WEIGHT_S;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_TIME_WEIGHT_F;
	} else {
		devc->cur_mqflags |= SR_MQFLAG_SPL_TIME_WEIGHT_F;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_TIME_WEIGHT_S;
	}

	devc->cur_meas_range = devc->buffer[4] & 3;

	if (devc->buffer[4] & (1 << 2)) {
		devc->cur_mqflags |= SR_MQFLAG_MAX;
		devc->cur_mqflags &= ~SR_MQFLAG_MIN;
	} else if (devc->buffer[4] & (1 << 3)) {
		devc->cur_mqflags |= SR_MQFLAG_MIN;
		devc->cur_mqflags &= ~SR_MQFLAG_MAX;
	} else {
		devc->cur_mqflags &= ~SR_MQFLAG_MIN;
		devc->cur_mqflags &= ~SR_MQFLAG_MAX;
	}

	value = devc->buffer[1] << 8 | devc->buffer[2];
	send_data(sdi, value / 10.0);
}

static void process_byte(const struct sr_dev_inst *sdi, const unsigned char c)
{
	struct dev_context *devc;
	unsigned int i;

	devc = sdi->priv;

	if (devc->buffer_len < BUFFER_SIZE) {
		devc->buffer[devc->buffer_len++] = c;
	} else {
		for (i = 1; i < BUFFER_SIZE; i++)
			devc->buffer[i - 1] = devc->buffer[i];
		devc->buffer[BUFFER_SIZE - 1] = c;
		if (devc->buffer[0] == 0x7f && devc->buffer[BUFFER_SIZE - 1] == 0x00) {
			process_measurement(sdi);
			devc->buffer_len = 0;
		}
	}
}

SR_PRIV int pce_322a_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	unsigned char c;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (!(serial = sdi->conn))
		return TRUE;

	if (revents == G_IO_IN) {
		if (serial_read_nonblocking(serial, &c, 1) != 1)
			return TRUE;
		process_byte(sdi, c);
	}

	return TRUE;
}

SR_PRIV int pce_322a_connect(const struct sr_dev_inst *sdi)
{
	return send_command(sdi, CMD_CONNECT);
}

SR_PRIV int pce_322a_disconnect(const struct sr_dev_inst *sdi)
{
	return send_command(sdi, CMD_DISCONNECT);
}

SR_PRIV uint64_t pce_322a_weight_freq_get(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	return devc->cur_mqflags & (SR_MQFLAG_SPL_FREQ_WEIGHT_A | SR_MQFLAG_SPL_FREQ_WEIGHT_C);
}

SR_PRIV int pce_322a_weight_freq_set(const struct sr_dev_inst *sdi, uint64_t freqw)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->cur_mqflags & freqw)
		return SR_OK;

	return send_command(sdi, CMD_TOGGLE_WEIGHT_FREQ);
}

SR_PRIV uint64_t pce_322a_weight_time_get(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	return devc->cur_mqflags & (SR_MQFLAG_SPL_TIME_WEIGHT_F | SR_MQFLAG_SPL_TIME_WEIGHT_S);
}

SR_PRIV int pce_322a_weight_time_set(const struct sr_dev_inst *sdi, uint64_t timew)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->cur_mqflags & timew)
		return SR_OK;

	return send_command(sdi, CMD_TOGGLE_WEIGHT_TIME);
}

SR_PRIV int pce_322a_meas_range_get(const struct sr_dev_inst *sdi,
		uint64_t *low, uint64_t *high)
{
	struct dev_context *devc;

	devc = sdi->priv;

	switch (devc->cur_meas_range) {
	case MEAS_RANGE_30_130:
		*low = 30;
		*high = 130;
		break;
	case MEAS_RANGE_30_80:
		*low = 30;
		*high = 80;
		break;
	case MEAS_RANGE_50_100:
		*low = 50;
		*high = 100;
		break;
	case MEAS_RANGE_80_130:
		*low = 80;
		*high = 130;
		break;
	default:
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int pce_322a_meas_range_set(const struct sr_dev_inst *sdi,
		uint64_t low, uint64_t high)
{
	struct dev_context *devc;
	uint8_t range;
	int ret = SR_OK;

	devc = sdi->priv;

	if (low == 30 && high == 130)
		range = MEAS_RANGE_30_130;
	else if (low == 30 && high == 80)
		range = MEAS_RANGE_30_80;
	else if (low == 50 && high == 100)
		range = MEAS_RANGE_50_100;
	else if (low == 80 && high == 130)
		range = MEAS_RANGE_80_130;
	else
		return SR_ERR;

	while (range != devc->cur_meas_range) {
		ret = send_command(sdi, CMD_TOGGLE_MEAS_RANGE);
		if (ret != SR_OK)
			break;
		range = (range - 1) & 3;
	}

	return ret;
}

SR_PRIV int pce_322a_power_off(const struct sr_dev_inst *sdi)
{
	return send_command(sdi, CMD_POWER_OFF);
}
