/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Sven Bursch-Osewold <sb_git@bursch.com>
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

/* Log a byte-array as hex values. */
static void log_buf(const char *message, uint8_t buf[], size_t count)
{
	char buffer[count * 2 + 1];

	for (size_t j = 0; j < count; j++)
		sprintf(&buffer[2 * j], "%02X", buf[j]);

	buffer[count * 2] = 0;

	sr_dbg("%s: %s [%lu bytes]", message, buffer, count);
}

/* Send a command to the device. */
static int send_cmd(struct sr_serial_dev_inst *serial, uint8_t buf[], size_t count)
{
	int ret;

	log_buf("Sending", buf, count);
	ret = serial_write_blocking(serial, buf, count, 0);
	if (ret < 0) {
		sr_err("Error sending command: %d.", ret);
		return ret;
	}
	sr_dbg("Sent %d bytes.", ret);

	return (ret == (int)count) ? SR_OK : SR_ERR;
}

/* Decode high byte and low byte into a float. */
static float decode_value(uint8_t hi, uint8_t lo, float divisor)
{
	return ((float)hi * 240.0 + (float)lo) / divisor;
}

/* Encode a float into high byte and low byte. */
static void encode_value(float current, uint8_t *hi, uint8_t *lo, float divisor)
{
	int value;

	value = (int)(current * divisor);
	sr_dbg("Value %d %d %d", value, value / 240, value % 240);
	*hi = value / 240;
	*lo = value % 240;
}

/* Send updated configuration values to the load. */
static int send_cfg(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t send[] = { 0xfa, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8 };

	encode_value(devc->current_limit, &send[2], &send[3], 1000.0);

	send[8] = send[1] ^ send[2] ^ send[3] ^ send[4] ^ send[5] ^ \
			send[6] ^ send[7];

	return send_cmd(serial, send, 10);
}

/* Send the init/connect sequence; drive starts sending voltage and current. */
SR_PRIV int ebd_init(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t init[] = { 0xfa, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xf8 };

	(void)devc;

	int ret = send_cmd(serial, init, 10);
	if (ret == SR_OK)
		devc->running = TRUE;

	return ret;
}

/* Start the load functionality. */
SR_PRIV int ebd_loadstart(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t start[] = { 0xfa, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf8 };
	int ret;

	ret = send_cmd(serial, start, 10);
	sr_dbg("Current limit: %f.", devc->current_limit);
	if (ebd_current_is0(devc))
		return SR_OK;

	ret = send_cfg(serial, devc);
	if (ret == SR_OK) {
		sr_dbg("Load activated.");
		devc->load_activated = TRUE;
	}

	return ret;
}

/* Stop the load functionality. */
SR_PRIV int ebd_loadstop(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	int ret;
	uint8_t stop[] = { 0xfa, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xF8 };

	ret = send_cmd(serial, stop, 10);
	if (ret == SR_OK)
		devc->load_activated = FALSE;

	return ret;
}

/* Stop the drive. */
SR_PRIV int ebd_stop(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t stop[] = { 0xfa, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xF8 };
	int ret;

	(void) devc;

	ret = send_cmd(serial, stop, 10);
	if (ret == SR_OK) {
		devc->load_activated = FALSE;
		devc->running= FALSE;
	}

	return ret;
}

/** Read count bytes from the serial connection. */
SR_PRIV int ebd_read_chars(struct sr_serial_dev_inst *serial, int count, uint8_t *buf)
{
	int ret, received, turns;

	received = 0;
	turns = 0;

	do {
		ret = serial_read_blocking(serial, buf + received,
			count - received, serial_timeout(serial, count));
		if (ret < 0) {
			sr_err("Error %d reading %d bytes.", ret, count);
			return ret;
		}
		received += ret;
		turns++;
	} while ((received < count) && (turns < 100));

	log_buf("Received", buf, received);

	return received;
}

SR_PRIV int ebd_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float voltage, current, current_limit;
	GSList *l;

	(void)revents;
	(void)fd;

	if (!(sdi = cb_data))
		return FALSE;

	if (!(devc = sdi->priv))
		return FALSE;

	serial = sdi->conn;

	uint8_t reply[MSG_LEN];
	int ret = ebd_read_chars(serial, MSG_LEN, reply);

	/* Tests for correct message. */
	if (ret != MSG_LEN) {
		sr_err("Message invalid [Len].");
		return (ret < 0) ? ret : SR_ERR;
	}

	uint8_t xor = reply[1] ^ reply[2] ^ reply[3] ^ reply[4] ^ \
		      reply[5] ^ reply[6] ^ reply[7] ^ reply[8] ^ \
		      reply[9] ^ reply[10] ^ reply[11] ^ reply[12] ^ \
		      reply[13] ^ reply[14] ^ reply[15] ^ reply[16];

	if (reply[MSG_FRAME_BEGIN_POS] != MSG_FRAME_BEGIN || \
			reply[MSG_FRAME_END_POS] != MSG_FRAME_END || \
			xor != reply[MSG_CHECKSUM_POS]) {
		sr_err("Message invalid [XOR, BEGIN/END].");
		return SR_ERR;
	}

	/* Calculate values. */
	sr_dbg("V: %02X %02X A: %02X %02X -- Limit %02X %02X", reply[4],
		reply[5], reply[2], reply[3], reply[10], reply[11]);

	voltage = decode_value(reply[4], reply[5], 1000.0);
	current = decode_value(reply[2], reply[3], 10000.0);
	current_limit = decode_value(reply[10], reply[11], 1000.0);

	sr_dbg("Voltage %f", voltage);
	sr_dbg("Current %f", current);
	sr_dbg("Current limit %f", current_limit);

	/* Begin frame. */
	packet.type = SR_DF_FRAME_BEGIN;
	packet.payload = NULL;
	sr_session_send(sdi, &packet);

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
	analog.data = &voltage;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	/* Current */
	l = g_slist_copy(sdi->channels);
	l = g_slist_remove_link(l, g_slist_nth(l, 0));
	meaning.channels = l;
	meaning.mq = SR_MQ_CURRENT;
	meaning.mqflags = SR_MQFLAG_DC;
	meaning.unit = SR_UNIT_AMPERE;
	analog.data = &current;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	/* End frame. */
	packet.type = SR_DF_FRAME_END;
	packet.payload = NULL;
	sr_session_send(sdi, &packet);

	sr_sw_limits_update_samples_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

SR_PRIV int ebd_get_current_limit(const struct sr_dev_inst *sdi, float *current)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;

	g_mutex_lock(&devc->rw_mutex);
	*current = devc->current_limit;
	g_mutex_unlock(&devc->rw_mutex);

	return SR_OK;
}

SR_PRIV int ebd_set_current_limit(const struct sr_dev_inst *sdi, float current)
{
	struct dev_context *devc;
	int ret;

	if (!(devc = sdi->priv))
		return SR_ERR;

	g_mutex_lock(&devc->rw_mutex);
	devc->current_limit = current;

	if (!devc->running) {
		sr_dbg("Setting current limit later.");
		g_mutex_unlock(&devc->rw_mutex);
		return SR_OK;
	}

	sr_dbg("Setting current limit to %fV.", current);

	if (devc->load_activated) {
		if (ebd_current_is0(devc)) {
			/* Stop load. */
			ret = ebd_loadstop(sdi->conn, devc);
		} else {
			/* Send new current. */
			ret = send_cfg(sdi->conn, devc);
		}
	} else {
		if (ebd_current_is0(devc)) {
			/* Nothing to do. */
			ret = SR_OK;
		} else {
			/* Start load. */
			ret = ebd_loadstart(sdi->conn, devc);
		}
	}

	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

SR_PRIV gboolean ebd_current_is0(struct dev_context *devc)
{
	return devc->current_limit < 0.001;
}
