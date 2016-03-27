/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

/* Length of expected payload for each token. */
static const int token_payloads[][2] = {
	{ TOKEN_WEIGHT_TIME_FAST, 0 },
	{ TOKEN_WEIGHT_TIME_SLOW, 0 },
	{ TOKEN_HOLD_MAX, 0 },
	{ TOKEN_HOLD_MIN, 0 },
	{ TOKEN_TIME, 3 },
	{ TOKEN_MEAS_RANGE_OVER, 0 },
	{ TOKEN_MEAS_RANGE_UNDER, 0 },
	{ TOKEN_STORE_FULL, 0 },
	{ TOKEN_RECORDING_ON, 0 },
	{ TOKEN_MEAS_WAS_READOUT, 1 },
	{ TOKEN_MEAS_WAS_BARGRAPH, 0 },
	{ TOKEN_MEASUREMENT, 2 },
	{ TOKEN_HOLD_NONE, 0 },
	{ TOKEN_BATTERY_LOW, 0 },
	{ TOKEN_MEAS_RANGE_OK, 0 },
	{ TOKEN_STORE_OK, 0 },
	{ TOKEN_RECORDING_OFF, 0 },
	{ TOKEN_WEIGHT_FREQ_A, 1 },
	{ TOKEN_WEIGHT_FREQ_C, 1 },
	{ TOKEN_BATTERY_OK, 0 },
	{ TOKEN_MEAS_RANGE_30_80, 0 },
	{ TOKEN_MEAS_RANGE_30_130, 0 },
	{ TOKEN_MEAS_RANGE_50_100, 0 },
	{ TOKEN_MEAS_RANGE_80_130, 0 },
};

static int find_token_payload_len(unsigned char c)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(token_payloads); i++) {
		if (token_payloads[i][0] == c)
			return token_payloads[i][1];
	}

	return -1;
}

/* Process measurement or setting (0xa5 command). */
static void process_mset(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog_old analog;
	GString *dbg;
	float fvalue;
	int i;

	devc = sdi->priv;
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		dbg = g_string_sized_new(128);
		g_string_printf(dbg, "got command 0x%.2x token 0x%.2x",
				devc->cmd, devc->token);
		if (devc->buf_len) {
			g_string_append_printf(dbg, " payload");
			for (i = 0; i < devc->buf_len; i++)
				g_string_append_printf(dbg, " %.2x", devc->buf[i]);
		}
		sr_spew("%s", dbg->str);
		g_string_free(dbg, TRUE);
	}

	switch (devc->token) {
	case TOKEN_WEIGHT_TIME_FAST:
		devc->cur_mqflags |= SR_MQFLAG_SPL_TIME_WEIGHT_F;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_TIME_WEIGHT_S;
		break;
	case TOKEN_WEIGHT_TIME_SLOW:
		devc->cur_mqflags |= SR_MQFLAG_SPL_TIME_WEIGHT_S;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_TIME_WEIGHT_F;
		break;
	case TOKEN_WEIGHT_FREQ_A:
		devc->cur_mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_FREQ_WEIGHT_C;
		break;
	case TOKEN_WEIGHT_FREQ_C:
		devc->cur_mqflags |= SR_MQFLAG_SPL_FREQ_WEIGHT_C;
		devc->cur_mqflags &= ~SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		break;
	case TOKEN_HOLD_MAX:
		devc->cur_mqflags |= SR_MQFLAG_HOLD | SR_MQFLAG_MAX;
		devc->cur_mqflags &= ~SR_MQFLAG_MIN;
		break;
	case TOKEN_HOLD_MIN:
		devc->cur_mqflags |= SR_MQFLAG_HOLD | SR_MQFLAG_MIN;
		devc->cur_mqflags &= ~SR_MQFLAG_MAX;
		break;
	case TOKEN_HOLD_NONE:
		devc->cur_mqflags &= ~(SR_MQFLAG_MAX | SR_MQFLAG_MIN | SR_MQFLAG_HOLD);
		break;
	case TOKEN_MEASUREMENT:
		fvalue = ((devc->buf[0] & 0xf0) >> 4) * 100;
		fvalue += (devc->buf[0] & 0x0f) * 10;
		fvalue += ((devc->buf[1] & 0xf0) >> 4);
		fvalue += (devc->buf[1] & 0x0f) / 10.0;
		devc->last_spl = fvalue;
		break;
	case TOKEN_MEAS_WAS_READOUT:
	case TOKEN_MEAS_WAS_BARGRAPH:
		if (devc->cur_mqflags & (SR_MQFLAG_MAX | SR_MQFLAG_MIN)) {
			if (devc->token == TOKEN_MEAS_WAS_BARGRAPH) {
				/* The device still sends bargraph measurements even
				 * when in max/min hold mode. Suppress them here, unless
				 * they're readout values. This duplicates the behavior
				 * of the device display exactly. */
				break;
			}
		}
		memset(&analog, 0, sizeof(struct sr_datafeed_analog_old));
		analog.mq = SR_MQ_SOUND_PRESSURE_LEVEL;
		analog.mqflags = devc->cur_mqflags;
		analog.unit = SR_UNIT_DECIBEL_SPL;
		analog.channels = sdi->channels;
		analog.num_samples = 1;
		analog.data = &devc->last_spl;
		packet.type = SR_DF_ANALOG_OLD;
		packet.payload = &analog;
		sr_session_send(sdi, &packet);

		devc->num_samples++;
		if (devc->limit_samples && devc->num_samples >= devc->limit_samples)
			sdi->driver->dev_acquisition_stop((struct sr_dev_inst *)sdi);
		break;
	case TOKEN_RECORDING_ON:
		devc->recording = TRUE;
		break;
	case TOKEN_RECORDING_OFF:
		devc->recording = FALSE;
		break;
	case TOKEN_MEAS_RANGE_30_80:
	case TOKEN_MEAS_RANGE_30_130:
	case TOKEN_MEAS_RANGE_50_100:
	case TOKEN_MEAS_RANGE_80_130:
		devc->cur_meas_range = devc->token;
		break;
	case TOKEN_TIME:
	case TOKEN_STORE_OK:
	case TOKEN_STORE_FULL:
	case TOKEN_BATTERY_OK:
	case TOKEN_BATTERY_LOW:
	case TOKEN_MEAS_RANGE_OK:
	case TOKEN_MEAS_RANGE_OVER:
	case TOKEN_MEAS_RANGE_UNDER:
		/* Not useful, or not expressible in sigrok. */
		break;
	}

}

static void send_data(const struct sr_dev_inst *sdi, unsigned char *data,
		uint64_t num_samples)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog_old analog;
	float fbuf[SAMPLES_PER_PACKET];
	unsigned int i;

	devc = sdi->priv;

	for (i = 0; i < num_samples; i++) {
		fbuf[i] = ((data[i * 2] & 0xf0) >> 4) * 100;
		fbuf[i] += (data[i * 2] & 0x0f) * 10;
		fbuf[i] += ((data[i * 2 + 1] & 0xf0) >> 4);
		fbuf[i] += (data[i * 2 + 1] & 0x0f) / 10.0;
	}
	memset(&analog, 0, sizeof(struct sr_datafeed_analog_old));
	analog.mq = SR_MQ_SOUND_PRESSURE_LEVEL;
	analog.mqflags = devc->cur_mqflags;
	analog.unit = SR_UNIT_DECIBEL_SPL;
	analog.channels = sdi->channels;
	analog.num_samples = num_samples;
	analog.data = fbuf;
	packet.type = SR_DF_ANALOG_OLD;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	devc->num_samples += analog.num_samples;
	if (devc->limit_samples && devc->num_samples >= devc->limit_samples)
		sdi->driver->dev_acquisition_stop((struct sr_dev_inst *)sdi);

	return;
}

static void process_byte(const struct sr_dev_inst *sdi, const unsigned char c,
		int handle_packets)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	gint64 cur_time;
	int len;

	if (!(devc = sdi->priv))
		return;

	if (c == 0xff) {
		/* Device is in hold mode */
		devc->cur_mqflags |= SR_MQFLAG_HOLD;

		if (devc->hold_last_sent == 0) {
			/* First hold notification. */
			devc->hold_last_sent = g_get_monotonic_time();
			/* When the device leaves hold mode, it starts from scratch. */
			devc->state = ST_INIT;
		} else {
			cur_time = g_get_monotonic_time();
			if (cur_time - devc->hold_last_sent > HOLD_REPEAT_INTERVAL) {
				/* Force the last measurement out again. */
				devc->cmd = 0xa5;
				devc->token = TOKEN_MEAS_WAS_READOUT;
				if (handle_packets)
					process_mset(sdi);
				devc->hold_last_sent = cur_time;
			}
		}

		return;
	}
	devc->cur_mqflags &= ~SR_MQFLAG_HOLD;
	devc->hold_last_sent = 0;

	if (devc->state == ST_INIT) {
		if (c == 0xa5) {
			devc->cmd = c;
			devc->token = 0x00;
			devc->state = ST_GET_TOKEN;
		} else if (c == 0xbb) {
			devc->cmd = c;
			devc->buf_len = 0;
			devc->state = ST_GET_LOG_HEADER;
			sr_dbg("got command 0xbb");
		}
	} else if (devc->state == ST_GET_TOKEN) {
		devc->token = c;
		devc->buf_len = 0;
		len = find_token_payload_len(devc->token);
		if (len == -1 || len > 0) {
			devc->buf_len = 0;
			devc->state = ST_GET_DATA;
		} else {
			if (handle_packets)
				process_mset(sdi);
			devc->state = ST_INIT;
		}
	} else if (devc->state == ST_GET_DATA) {
		len = find_token_payload_len(devc->token);
		if (len == -1) {
			/* We don't know this token. */
			sr_dbg("Unknown 0xa5 token 0x%.2x", devc->token);
			if (c == 0xa5 || c == 0xbb) {
				/* Looks like a new command however. */
				if (handle_packets)
					process_mset(sdi);
				devc->state = ST_INIT;
			} else {
				devc->buf[devc->buf_len++] = c;
				if (devc->buf_len > BUF_SIZE) {
					/* Shouldn't happen, ignore. */
					devc->state = ST_INIT;
				}
			}
		} else {
			devc->buf[devc->buf_len++] = c;
			if (devc->buf_len == len) {
				if (handle_packets)
					process_mset(sdi);
				devc->state = ST_INIT;
			} else if (devc->buf_len > BUF_SIZE) {
				/* Shouldn't happen, ignore. */
				devc->state = ST_INIT;
			}
		}
	} else if (devc->state == ST_GET_LOG_HEADER) {
		sr_dbg("log header: 0x%.2x", c);
		if (devc->buf_len < 2)
			devc->buf[devc->buf_len++] = c;
		if (devc->buf_len == 2) {
			sr_dbg("Device says it has %d bytes stored.",
					((devc->buf[0] << 8) + devc->buf[1]) - 100);
			devc->buf_len = 0;
			devc->state = ST_GET_LOG_RECORD_META;
		}
	} else if (devc->state == ST_GET_LOG_RECORD_META) {
		sr_dbg("log meta: 0x%.2x", c);
		if (c == RECORD_END) {
			devc->state = ST_INIT;
			/* Stop acquisition after transferring all stored
			 * records. Otherwise the frontend would have no
			 * way to tell where stored data ends and live
			 * measurements begin. */
			sdi->driver->dev_acquisition_stop((struct sr_dev_inst *)sdi);
		} else if (c == RECORD_DATA) {
			devc->buf_len = 0;
			devc->state = ST_GET_LOG_RECORD_DATA;
		} else {
			/* RECORD_DBA/RECORD_DBC + 7 bytes of metadata */
			devc->buf[devc->buf_len++] = c;
			if (devc->buf_len < 8)
				/* Keep filling up the record header. */
				return;
			if (devc->buf[0] == RECORD_DBA)
				devc->cur_mqflags = SR_MQFLAG_SPL_FREQ_WEIGHT_A;
			else if (devc->buf[0] == RECORD_DBC)
				devc->cur_mqflags = SR_MQFLAG_SPL_FREQ_WEIGHT_C;
			else {
				/* Shouldn't happen. */
				sr_dbg("Unknown record token 0x%.2x", c);
				return;
			}
			packet.type = SR_DF_META;
			packet.payload = &meta;
			src = sr_config_new(SR_CONF_SAMPLE_INTERVAL,
					g_variant_new_uint64(devc->buf[7] * 1000));
			meta.config = g_slist_append(NULL, src);
			sr_session_send(sdi, &packet);
			g_free(src);
			devc->buf_len = 0;
		}
	} else if (devc->state == ST_GET_LOG_RECORD_DATA) {
		sr_dbg("log data: 0x%.2x", c);
		if (c == RECORD_DBA || c == RECORD_DBC || c == RECORD_DATA || c == RECORD_END) {
			/* Work around off-by-one bug in device firmware. This
			 * happens only on the last record, i.e. before RECORD_END */
			if (devc->buf_len & 1)
				devc->buf_len--;
			/* Done with this set of samples */
			send_data(sdi, devc->buf, devc->buf_len / 2);
			devc->buf_len = 0;

			/* Process this meta marker in the right state. */
			devc->state = ST_GET_LOG_RECORD_META;
			process_byte(sdi, c, handle_packets);
		} else {
			devc->buf[devc->buf_len++] = c;
			if (devc->buf_len == SAMPLES_PER_PACKET * 2) {
				send_data(sdi, devc->buf, devc->buf_len / 2);
				devc->buf_len = 0;
			}
		}
	}

}

SR_PRIV int cem_dt_885x_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	unsigned char c, cmd;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	devc = sdi->priv;
	serial = sdi->conn;
	if (revents == G_IO_IN) {
		if (serial_read_nonblocking(serial, &c, 1) != 1)
			return TRUE;
		process_byte(sdi, c, TRUE);

		if (devc->enable_data_source_memory) {
			if (devc->state == ST_GET_LOG_HEADER) {
				/* Memory transfer started. */
				devc->enable_data_source_memory = FALSE;
			} else {
				/* Tell device to start transferring from memory. */
				cmd = CMD_TRANSFER_MEMORY;
				serial_write_nonblocking(serial, &cmd, 1);
			}
		}
	}

	return TRUE;
}

static int wait_for_token(const struct sr_dev_inst *sdi, int8_t *tokens, int timeout)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	gint64 start_time;
	int i;
	unsigned char c;

	serial = sdi->conn;
	devc = sdi->priv;
	devc->state = ST_INIT;
	start_time = g_get_monotonic_time() / 1000;
	while (TRUE) {
		if (serial_read_nonblocking(serial, &c, 1) != 1)
			/* Device might have gone away. */
			return SR_ERR;
		process_byte(sdi, c, FALSE);
		if (devc->state != ST_INIT)
			/* Wait for a whole packet to get processed. */
			continue;
		for (i = 0; tokens[i] != -1; i++) {
			if (devc->token == tokens[i]) {
				sr_spew("wait_for_token: got token 0x%.2x", devc->token);
				return SR_OK;
			}
		}
		if (timeout && g_get_monotonic_time() / 1000 - start_time > timeout)
			return SR_ERR_TIMEOUT;
	}

	return SR_OK;
}

/* cmd is the command to send, tokens are the tokens that denote the state
 * which the command affects. The first token is the desired state. */
static int cem_dt_885x_toggle(const struct sr_dev_inst *sdi, uint8_t cmd,
		int8_t *tokens, int timeout)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	devc = sdi->priv;

	/* The device doesn't respond to commands very well. The
	 * only thing to do is wait for the token that will confirm
	 * whether the command worked or not, and resend if needed. */
	while (TRUE) {
		if (serial_write_nonblocking(serial, (const void *)&cmd, 1) != 1)
			return SR_ERR;
		if (wait_for_token(sdi, tokens, timeout) == SR_ERR)
			return SR_ERR;
		if (devc->token == tokens[0])
			/* It worked. */
			break;
	}

	return SR_OK;
}

SR_PRIV gboolean cem_dt_885x_recording_get(const struct sr_dev_inst *sdi,
		int *state)
{
	struct dev_context *devc;
	int8_t tokens[5];

	devc = sdi->priv;
	if (devc->recording == -1) {
		/* Didn't pick up device state yet. */
		tokens[0] = TOKEN_RECORDING_ON;
		tokens[1] = TOKEN_RECORDING_OFF;
		tokens[2] = -1;
		if (wait_for_token(sdi, tokens, 510) != SR_OK)
			return SR_ERR;
	}
	*state = devc->token == TOKEN_RECORDING_ON;

	return SR_OK;
}

SR_PRIV int cem_dt_885x_recording_set(const struct sr_dev_inst *sdi,
		gboolean state)
{
	struct dev_context *devc;
	int ret;
	int8_t tokens[5];

	devc = sdi->priv;

	/* The toggle below needs the desired state in first position. */
	if (state) {
		tokens[0] = TOKEN_RECORDING_ON;
		tokens[1] = TOKEN_RECORDING_OFF;
	} else {
		tokens[0] = TOKEN_RECORDING_OFF;
		tokens[1] = TOKEN_RECORDING_ON;
	}
	tokens[2] = -1;

	if (devc->recording == -1) {
		/* Didn't pick up device state yet. */
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == tokens[0])
			/* Nothing to do. */
			return SR_OK;
	} else if (devc->recording == state)
		/* Nothing to do. */
		return SR_OK;

	/* Recording state notifications are sent at 2Hz, so allow just over
	 * that, 510ms, for the state to come in. */
	ret = cem_dt_885x_toggle(sdi, CMD_TOGGLE_RECORDING, tokens, 510);

	return ret;
}

SR_PRIV int cem_dt_885x_weight_freq_get(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int cur_setting;
	int8_t tokens[5];

	devc = sdi->priv;

	cur_setting = devc->cur_mqflags & (SR_MQFLAG_SPL_FREQ_WEIGHT_A | SR_MQFLAG_SPL_FREQ_WEIGHT_C);
	if (cur_setting == 0) {
		/* Didn't pick up device state yet. */
		tokens[0] = TOKEN_WEIGHT_FREQ_A;
		tokens[1] = TOKEN_WEIGHT_FREQ_C;
		tokens[2] = -1;
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == TOKEN_WEIGHT_FREQ_A)
			return SR_MQFLAG_SPL_FREQ_WEIGHT_A;
		else
			return SR_MQFLAG_SPL_FREQ_WEIGHT_C;
	} else
		return cur_setting;
}

SR_PRIV int cem_dt_885x_weight_freq_set(const struct sr_dev_inst *sdi, int freqw)
{
	struct dev_context *devc;
	int cur_setting, ret;
	int8_t tokens[5];

	devc = sdi->priv;

	cur_setting = devc->cur_mqflags & (SR_MQFLAG_SPL_FREQ_WEIGHT_A | SR_MQFLAG_SPL_FREQ_WEIGHT_C);
	if (cur_setting == freqw)
		/* Already set to this frequency weighting. */
		return SR_OK;

	/* The toggle below needs the desired state in first position. */
	if (freqw == SR_MQFLAG_SPL_FREQ_WEIGHT_A) {
		tokens[0] = TOKEN_WEIGHT_FREQ_A;
		tokens[1] = TOKEN_WEIGHT_FREQ_C;
	} else {
		tokens[0] = TOKEN_WEIGHT_FREQ_C;
		tokens[1] = TOKEN_WEIGHT_FREQ_A;
	}
	tokens[2] = -1;

	if (cur_setting == 0) {
		/* Didn't pick up device state yet. */
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == tokens[0])
			/* Nothing to do. */
			return SR_OK;
	}

	/* 10ms timeout seems to work best for this. */
	ret = cem_dt_885x_toggle(sdi, CMD_TOGGLE_WEIGHT_FREQ, tokens, 10);

	return ret;
}

SR_PRIV int cem_dt_885x_weight_time_get(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int cur_setting;
	int8_t tokens[5];

	devc = sdi->priv;

	cur_setting = devc->cur_mqflags & (SR_MQFLAG_SPL_TIME_WEIGHT_F | SR_MQFLAG_SPL_TIME_WEIGHT_S);
	if (cur_setting == 0) {
		/* Didn't pick up device state yet. */
		tokens[0] = TOKEN_WEIGHT_TIME_FAST;
		tokens[1] = TOKEN_WEIGHT_TIME_SLOW;
		tokens[2] = -1;
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == TOKEN_WEIGHT_TIME_FAST)
			return SR_MQFLAG_SPL_TIME_WEIGHT_F;
		else
			return SR_MQFLAG_SPL_TIME_WEIGHT_S;
	} else
		return cur_setting;
}

SR_PRIV int cem_dt_885x_weight_time_set(const struct sr_dev_inst *sdi, int timew)
{
	struct dev_context *devc;
	int cur_setting, ret;
	int8_t tokens[5];

	devc = sdi->priv;

	cur_setting = devc->cur_mqflags & (SR_MQFLAG_SPL_TIME_WEIGHT_F | SR_MQFLAG_SPL_TIME_WEIGHT_S);
	if (cur_setting == timew)
		/* Already set to this time weighting. */
		return SR_OK;

	/* The toggle below needs the desired state in first position. */
	if (timew == SR_MQFLAG_SPL_TIME_WEIGHT_F) {
		tokens[0] = TOKEN_WEIGHT_TIME_FAST;
		tokens[1] = TOKEN_WEIGHT_TIME_SLOW;
	} else {
		tokens[0] = TOKEN_WEIGHT_TIME_SLOW;
		tokens[1] = TOKEN_WEIGHT_TIME_FAST;
	}
	tokens[2] = -1;

	if (cur_setting == 0) {
		/* Didn't pick up device state yet. */
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == tokens[0])
			/* Nothing to do. */
			return SR_OK;
	}

	/* 51ms timeout seems to work best for this. */
	ret = cem_dt_885x_toggle(sdi, CMD_TOGGLE_WEIGHT_TIME, tokens, 51);

	return ret;
}

SR_PRIV int cem_dt_885x_holdmode_get(const struct sr_dev_inst *sdi,
		gboolean *holdmode)
{
	struct dev_context *devc;
	int8_t tokens[5];

	devc = sdi->priv;

	if (devc->cur_mqflags == 0) {
		tokens[0] = TOKEN_HOLD_MAX;
		tokens[1] = TOKEN_HOLD_MIN;
		tokens[2] = TOKEN_HOLD_NONE;
		tokens[3] = -1;
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == TOKEN_HOLD_MAX)
			devc->cur_mqflags = SR_MQFLAG_MAX;
		else if (devc->token == TOKEN_HOLD_MIN)
			devc->cur_mqflags = SR_MQFLAG_MIN;
	}
	*holdmode = devc->cur_mqflags & (SR_MQFLAG_MAX | SR_MQFLAG_MIN);

	return SR_OK;
}

SR_PRIV int cem_dt_885x_holdmode_set(const struct sr_dev_inst *sdi, int holdmode)
{
	struct dev_context *devc;
	int cur_setting, ret;
	int8_t tokens[5];

	devc = sdi->priv;

	/* The toggle below needs the desired state in first position. */
	if (holdmode == SR_MQFLAG_MAX) {
		tokens[0] = TOKEN_HOLD_MAX;
		tokens[1] = TOKEN_HOLD_MIN;
		tokens[2] = TOKEN_HOLD_NONE;
	} else if (holdmode == SR_MQFLAG_MIN) {
		tokens[0] = TOKEN_HOLD_MIN;
		tokens[1] = TOKEN_HOLD_MAX;
		tokens[2] = TOKEN_HOLD_NONE;
	} else {
		tokens[0] = TOKEN_HOLD_NONE;
		tokens[1] = TOKEN_HOLD_MAX;
		tokens[2] = TOKEN_HOLD_MIN;
	}
	tokens[3] = -1;

	if (devc->cur_mqflags == 0) {
		/* Didn't pick up device state yet. */
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		if (devc->token == tokens[0])
			/* Nothing to do. */
			return SR_OK;
	} else {
		cur_setting = devc->cur_mqflags & (SR_MQFLAG_MAX | SR_MQFLAG_MIN);
		if (cur_setting == holdmode)
			/* Already set correctly. */
			return SR_OK;
	}

	/* 51ms timeout seems to work best for this. */
	ret = cem_dt_885x_toggle(sdi, CMD_TOGGLE_HOLD_MAX_MIN, tokens, 51);

	return ret;
}

SR_PRIV int cem_dt_885x_meas_range_get(const struct sr_dev_inst *sdi,
		uint64_t *low, uint64_t *high)
{
	struct dev_context *devc;
	int8_t tokens[5];

	devc = sdi->priv;
	if (devc->cur_meas_range == 0) {
		tokens[0] = TOKEN_MEAS_RANGE_30_130;
		tokens[1] = TOKEN_MEAS_RANGE_30_80;
		tokens[2] = TOKEN_MEAS_RANGE_50_100;
		tokens[3] = TOKEN_MEAS_RANGE_80_130;
		tokens[4] = -1;
		if (wait_for_token(sdi, tokens, 0) != SR_OK)
			return SR_ERR;
		devc->cur_meas_range = devc->token;
	}

	switch (devc->cur_meas_range) {
	case TOKEN_MEAS_RANGE_30_130:
		*low = 30;
		*high = 130;
		break;
	case TOKEN_MEAS_RANGE_30_80:
		*low = 30;
		*high = 80;
		break;
	case TOKEN_MEAS_RANGE_50_100:
		*low = 50;
		*high = 100;
		break;
	case TOKEN_MEAS_RANGE_80_130:
		*low = 80;
		*high = 130;
		break;
	default:
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int cem_dt_885x_meas_range_set(const struct sr_dev_inst *sdi,
		uint64_t low, uint64_t high)
{
	struct dev_context *devc;
	int ret;
	int8_t token, tokens[6];

	devc = sdi->priv;
	if (low == 30 && high == 130)
		token = TOKEN_MEAS_RANGE_30_130;
	else if (low == 30 &&  high == 80)
		token = TOKEN_MEAS_RANGE_30_80;
	else if (low == 50 &&  high == 100)
		token = TOKEN_MEAS_RANGE_50_100;
	else if (low == 80 &&  high == 130)
		token = TOKEN_MEAS_RANGE_80_130;
	else
		return SR_ERR;

	sr_dbg("want 0x%.2x", token);
	/* The toggle below needs the desired state in first position. */
	tokens[0] = token;
	tokens[1] = TOKEN_MEAS_RANGE_30_130;
	tokens[2] = TOKEN_MEAS_RANGE_30_80;
	tokens[3] = TOKEN_MEAS_RANGE_50_100;
	tokens[4] = TOKEN_MEAS_RANGE_80_130;
	tokens[5] = -1;

	if (devc->cur_meas_range == 0) {
		/* 110ms should be enough for two of these announcements */
		if (wait_for_token(sdi, tokens, 110) != SR_OK)
			return SR_ERR;
		devc->cur_meas_range = devc->token;
	}

	if (devc->cur_meas_range == token)
		/* Already set to this range. */
		return SR_OK;

	/* For measurement range, it works best to ignore announcements of the
	 * current setting and keep resending the toggle quickly. */
	tokens[1] = -1;
	ret = cem_dt_885x_toggle(sdi, CMD_TOGGLE_MEAS_RANGE, tokens, 11);

	return ret;
}

SR_PRIV int cem_dt_885x_power_off(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	char c, cmd;

	serial = sdi->conn;

	cmd = CMD_TOGGLE_POWER_OFF;
	while (TRUE) {
		serial_flush(serial);
		if (serial_write_nonblocking(serial, (const void *)&cmd, 1) != 1)
			return SR_ERR;
		/* It never takes more than 23ms for the next token to arrive. */
		g_usleep(25 * 1000);
		if (serial_read_nonblocking(serial, &c, 1) != 1)
			/* Device is no longer responding. Good! */
			break;
	}

	/* In case the user manually turns on the device again, reset
	 * the port back to blocking. */
	serial_close(serial);
	serial_open(serial, SERIAL_RDWR);

	return SR_OK;
}
