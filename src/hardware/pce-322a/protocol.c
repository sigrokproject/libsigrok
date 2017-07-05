/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 George Hopkins <george-hopkins@null.net>
 * Copyright (C) 2016 Matthieu Guillaumin <matthieu@guillaum.in>
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

static int send_long_command(const struct sr_dev_inst *sdi, uint32_t command)
{
	struct sr_serial_dev_inst *serial;
	uint8_t buffer[4];

	buffer[0] = command >> 24;
	buffer[1] = command >> 16;
	buffer[2] = command >> 8;
	buffer[3] = command;

	if (!(serial = sdi->conn))
		return SR_ERR;

	if (serial_write_nonblocking(serial, (const void *)buffer, 4) != 4)
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
	/* Limiting number of samples is only supported for live data. */
	if (devc->cur_data_source == DATA_SOURCE_LIVE && devc->limit_samples && devc->num_samples >= devc->limit_samples)
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
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

static void process_memory_measurement(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint16_t value;

	devc = sdi->priv;
	value = devc->buffer[devc->buffer_len - 1] << 8;
	value |= devc->buffer[devc->buffer_len - 2];

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
	}

	if (devc->buffer_len == BUFFER_SIZE && devc->buffer[0] == 0x7f
			&& devc->buffer[BUFFER_SIZE - 1] == 0x00) {
		process_measurement(sdi);
		devc->buffer_len = 0;
	}
}

static void process_usage_byte(const struct sr_dev_inst *sdi, uint8_t c)
{
	struct dev_context *devc;
	unsigned int i;

	devc = sdi->priv;

	if (devc->buffer_len < MEM_USAGE_BUFFER_SIZE) {
		devc->buffer[devc->buffer_len++] = c;
	} else {
		for (i = 1; i < MEM_USAGE_BUFFER_SIZE; i++)
			devc->buffer[i - 1] = devc->buffer[i];
		devc->buffer[MEM_USAGE_BUFFER_SIZE - 1] = c;
	}

	if (devc->buffer_len == MEM_USAGE_BUFFER_SIZE && devc->buffer[0] == 0xd1
			&& devc->buffer[1] == 0x05 && devc->buffer[2] == 0x00
			&& devc->buffer[3] == 0x01 && devc->buffer[4] == 0xd2
			&& devc->buffer[MEM_USAGE_BUFFER_SIZE - 1] == 0x20) {
		devc->memory_block_usage = devc->buffer[5] << 8 | devc->buffer[6];
		devc->memory_last_block_usage = devc->buffer[7];
		sr_warn("Memory usage: %d blocks of 256 bytes, 1 block of %d bytes",
			devc->memory_block_usage - 1, devc->memory_last_block_usage);
		devc->buffer_len = 0;
		devc->buffer_skip = 1;
		devc->memory_state = MEM_STATE_REQUEST_MEMORY_BLOCK;
		devc->memory_block_cursor = 0;
		devc->memory_block_counter = 0;
	}
}

static void process_memory_byte(const struct sr_dev_inst *sdi, uint8_t c)
{
	struct dev_context *devc;
	unsigned int i;

	devc = sdi->priv;

	if (devc->buffer_len < MEM_DATA_BUFFER_SIZE) {
		devc->buffer[devc->buffer_len++] = c;
	} else {
		for (i = 1; i < MEM_DATA_BUFFER_SIZE; i++)
			devc->buffer[i - 1] = devc->buffer[i];
		devc->buffer[MEM_DATA_BUFFER_SIZE - 1] = c;
	}

	if (devc->buffer_skip == 0 \
			&& (devc->buffer[devc->buffer_len-2] & 0x7f) == 0x7f
			&& (devc->buffer[devc->buffer_len-1] & 0xf7) == 0xf7) {
		/* Recording session header bytes found, load next 7 bytes. */
		devc->buffer_skip = MEM_DATA_BUFFER_SIZE - 2;
	}

	if (devc->buffer_skip == 0 && devc->buffer_len == MEM_DATA_BUFFER_SIZE
			&& (devc->buffer[0] & 0x7f) == 0x7f && (devc->buffer[1] & 0xf7) == 0xf7
			&& devc->buffer[2] == 0x01 && devc->buffer[3] == 0x00) {
		/* Print information about recording. */
		sr_err("Recording dB(%X) %02x/%02x/%02x %02x:%02x:%02x ",
			devc->buffer[4], devc->buffer[5], devc->buffer[6], devc->buffer[7],
			devc->buffer[8] & 0x3f, devc->buffer[9], devc->buffer[10]);
		/* Set dBA/dBC flag for recording. */
		if (devc->buffer[4] == 0x0c) {
			devc->cur_mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_C;
			devc->cur_mqflags &= ~SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		} else {
			devc->cur_mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_A;
			devc->cur_mqflags &= ~SR_MQFLAG_SPL_FREQ_WEIGHT_C;
		}
		send_data(sdi, -1.0); /* Signal switch of recording. */
		devc->buffer_skip = 2;
	}

	if (devc->buffer_skip == 0) {
		process_memory_measurement(sdi);
		devc->buffer_skip = 1;
	} else {
		devc->buffer_skip -= 1;
	}

	devc->memory_block_cursor++; /* uint8_t goes back to 0 after 255. */
	if (devc->memory_block_cursor == 0) {
		/* Current block is completed. */
		devc->memory_block_counter++;
		devc->memory_state = MEM_STATE_REQUEST_MEMORY_BLOCK;
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

	if (devc->cur_data_source == DATA_SOURCE_MEMORY) {
		switch (devc->memory_state) {
		case MEM_STATE_REQUEST_MEMORY_USAGE:
			/* At init, disconnect and request the memory status. */
			sr_warn("Requesting memory usage.");
			pce_322a_disconnect(sdi);
			devc->memory_state = MEM_STATE_GET_MEMORY_USAGE;
			devc->memory_block_usage = 0;
			devc->memory_last_block_usage = 0;
			devc->memory_block_counter = 0;
			devc->memory_block_cursor = 0;
			pce_322a_memory_status(sdi);
			break;
		case MEM_STATE_GET_MEMORY_USAGE:
			/* Listen for memory usage answer. */
			if (revents == G_IO_IN) {
				if (serial_read_nonblocking(serial, &c, 1) != 1)
					return TRUE;
				process_usage_byte(sdi, c);
			}
			break;
		case MEM_STATE_REQUEST_MEMORY_BLOCK:
			/* When cursor is 0, request next memory block. */
			if (devc->memory_block_counter <= devc->memory_block_usage) {
				sr_warn("Requesting memory block %d.", devc->memory_block_counter);
				pce_322a_memory_block(sdi, devc->memory_block_counter);
				devc->memory_state = MEM_STATE_GET_MEMORY_BLOCK;
			} else {
				sr_warn("Exhausted memory blocks.");
				return FALSE;
			}
			break;
		case MEM_STATE_GET_MEMORY_BLOCK:
			/* Stop after reading last byte of last block. */
			if (devc->memory_block_counter >= devc->memory_block_usage
					&& devc->memory_block_cursor >= devc->memory_last_block_usage) {
				sr_warn("Done reading memory (%d bytes).",
					256 * (devc->memory_block_counter - 1)
					+ devc->memory_block_cursor);
				return FALSE;
			}
			/* Listen for memory data. */
			if (revents == G_IO_IN) {
				if (serial_read_nonblocking(serial, &c, 1) != 1)
					return TRUE;
				process_memory_byte(sdi, c);
			}
			break;
		}
	} else {
		/* Listen for live data. */
		if (revents == G_IO_IN) {
			if (serial_read_nonblocking(serial, &c, 1) != 1)
				return TRUE;
			process_byte(sdi, c);
		}
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

SR_PRIV int pce_322a_memory_status(const struct sr_dev_inst *sdi)
{
	return send_command(sdi, CMD_MEMORY_STATUS);
}

SR_PRIV int pce_322a_memory_clear(const struct sr_dev_inst *sdi)
{
	return send_command(sdi, CMD_MEMORY_CLEAR);
}

SR_PRIV int pce_322a_memory_block(const struct sr_dev_inst *sdi, uint16_t memblk)
{
	uint8_t buf0 = memblk;
	uint8_t buf1 = memblk >> 8;
	uint32_t command = CMD_MEMORY_TRANSFER << 16 | buf0 << 8 | buf1;
	return send_long_command(sdi, command);
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
