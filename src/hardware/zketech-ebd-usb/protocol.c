/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Sven Bursch-Osewold <sb_git@bursch.com>
 * Copyright (C) 2019 King Kévin <kingkevin@cuvoodoo.info>
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

	sr_dbg("%s: %s [%zu bytes]", message, buffer, count);
}

/* Send a command to the device. */
static int send_cmd(struct sr_serial_dev_inst *serial, uint8_t buf[], size_t count)
{
	int ret;

	log_buf("Sending", buf, count);
	for (size_t byte = 0; byte < count; byte++) {
		ret = serial_write_blocking(serial, &buf[byte], 1, 0);
		if (ret < 0) {
			sr_err("Error sending command: %d.", ret);
			return ret;
		}
		g_usleep(10000); // wait between bytes to prevent data loss at the receiving side
	}

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
	encode_value(devc->voltage_limit, &send[4], &send[5], 100.0);

	send[8] = send[1] ^ send[2] ^ send[3] ^ send[4] ^ send[5] ^ send[6] ^ send[7];

	return send_cmd(serial, send, 10);
}

/* Send the init/connect sequence; drive starts sending voltage and current. */
SR_PRIV int ebd_init(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t init[] = { 0xfa, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xf8 };

	(void)devc;

	int ret = send_cmd(serial, init, 10);

	return ret;
}

/* Start the load functionality. */
SR_PRIV int ebd_loadstart(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t start[] = { 0xfa, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf8 };
	int ret;

	encode_value(devc->current_limit, &start[2], &start[3], 1000.0);
	encode_value(devc->voltage_limit, &start[4], &start[5], 100.0);

	start[8] = start[1] ^ start[2] ^ start[3] ^ start[4] ^ start[5] ^ start[6] ^ start[7];

	sr_info("Activating load");
	ret = send_cmd(serial, start, 10);
	if (ret)
		return ret;

	sr_dbg("current limit: %.03f", devc->current_limit);
	sr_dbg("under-voltage threshold: %.02f", devc->voltage_limit);
	if (ebd_current_is0(devc))
		return SR_OK;

	return ret;
}

/* Toggle the load functionality. */
SR_PRIV int ebd_loadtoggle(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	int ret;
	uint8_t toggle[] = { 0xfa, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xF8 };

	(void) devc;

	sr_info("Toggling load");
	ret = send_cmd(serial, toggle, 10);

	return ret;
}

/* Stop the drive. */
SR_PRIV int ebd_stop(struct sr_serial_dev_inst *serial, struct dev_context *devc)
{
	uint8_t stop[] = { 0xfa, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xF8 };
	int ret;

	(void) devc;

	ret = send_cmd(serial, stop, 10);

	return ret;
}

/** Receive a complete message
 *  @param[in] serial Serial port from which to read the packet
 *  @param[in] length Buffer length
 *  @param[out] buf Buffer to write packet to
 *  @return packet length (0 = timeout, -1 = error)
 */
SR_PRIV int ebd_read_message(struct sr_serial_dev_inst *serial, int length, uint8_t *buf)
{
	// check parameters
	if (NULL == serial) {
		sr_err("Serial device to receive packet missing.");
		return -1;
	}
	if (length < 3) {
		sr_err("Packet buffer not large enough.");
		return -1;
	}
	if (NULL == buf) {
		sr_err("Packet buffer missing.");
		return -1;
	}

	int ret;
	gboolean message_complete = FALSE;
	int message_length = 0;
	unsigned int turn = 0;
	const unsigned int TURNS = 200;
	buf[message_length] = 0;
	// try to read data
	while (!message_complete && turn < TURNS) {
		// wait for header byte
		message_length = 0;
		while (MSG_FRAME_BEGIN != buf[0] && turn < TURNS) {
			ret = serial_read_blocking(serial, &buf[0], 1,  serial_timeout(serial, 1));
			if (ret < 0) {
				sr_err("Error %d reading byte.", ret);
				return ret;
			} else if (1 == ret) {
				if (MSG_FRAME_BEGIN != buf[message_length]) {
					sr_warn("Not frame begin byte %02x received", buf[message_length]);
				} else {
					sr_dbg("Message header received: %02x", buf[message_length]);
					message_length += ret;
				}
			}
			turn++;
		}
		// read until end byte
		while (MSG_FRAME_END !=  buf[message_length - 1] && message_length < length && turn < TURNS) {
			ret = serial_read_blocking(serial, &buf[message_length], 1,  serial_timeout(serial, 1));
			if (ret < 0) {
				sr_err("Error %d reading byte.", ret);
				return ret;
			} else if (1 == ret) {
				if (MSG_FRAME_BEGIN == buf[message_length]) {
					sr_warn("Frame begin before end received");
					message_length = 1;
				} else {
					sr_dbg("Message data received: %02x", buf[message_length]);
					message_length += ret;
				}
			}
			turn++;
		}
		// verify frame
		if (turn < TURNS) {
			if (MSG_FRAME_END == buf[message_length - 1]) {
				message_complete = TRUE;
				sr_dbg("Message end received");
			} else {
				sr_warn("Frame end not received");
			}
		} else {
			sr_warn("Invalid data and timeout");
		}
	}

	if (message_complete && message_length > 2) {
		ret = message_length;
	} else if (turn >= TURNS) {
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

static void ebd_send_value(const struct sr_dev_inst *sdi, struct sr_channel *ch,
		float value, enum sr_mq mq, enum sr_unit unit, int digits)
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
	float voltage, voltage_dp, voltage_dm, current, current_limit, voltage_limit;

	(void)revents;
	(void)fd;

	if (!(sdi = cb_data))
		return FALSE;

	if (!(devc = sdi->priv))
		return FALSE;

	serial = sdi->conn;
	current_limit = devc->current_limit;
	voltage_limit = devc->voltage_limit;

	uint8_t reply[MSG_MAX_LEN];
	int ret = ebd_read_message(serial, MSG_MAX_LEN, reply);

	/* Tests for correct message. */
	if (-1 == ret) {
		sr_err("Can't receive messages");
		return SR_ERR;
	} else if (0 == ret) {
		sr_err("No messages received");
		devc->running = FALSE;
		return 0;
	} else if (ret != 19 || (reply[1] != 0x00 && reply[1] != 0x0a && reply[1] != 0x64 && reply[1] != 0x6e)) {
		sr_info("Not measurement message received");
		return ret;
	}

	/* Verify checksum */
	uint8_t checksum = 0;
	for (int i = 1; i < ret - 1; i++) {
		checksum ^= reply[i];
	}
	if (0 != checksum) {
		sr_warn("Invalid checksum");
		return ret; /* Don't exit on wrong checksum, the device can recover */
	}

	devc->running = TRUE;
	if (0x00 == reply[1] || 0x64 == reply[1]) {
		devc->load_activated = FALSE;
	} else if (0x0a == reply[1] || 0x6e == reply[1]) {
		devc->load_activated = TRUE;
	}

	/* Calculate values. */
	current = decode_value(reply[2], reply[3], 10000.0);
	voltage = decode_value(reply[4], reply[5], 1000.0);
	voltage_dp = decode_value(reply[6], reply[7], 1000.0);
	voltage_dm = decode_value(reply[8], reply[9], 1000.0);
	if (0x0a == reply[1]) {
		current_limit = decode_value(reply[10], reply[11], 1000.0);
		voltage_limit = decode_value(reply[12], reply[13], 100.0);
	}

	sr_dbg("VBUS current %.04f A", current);
	sr_dbg("VBUS voltage %.03f V", voltage);
	sr_dbg("D+ voltage %.03f V", voltage_dp);
	sr_dbg("D- voltage %.03f V", voltage_dm);
	if (0x0a == reply[1]) {
		sr_dbg("Current limit %.03f A", current_limit);
		sr_dbg("Voltage limit %.03f A", voltage_limit);
	}

	// update load state
	if (devc->load_activated && ebd_current_is0(devc)) {
		ebd_loadtoggle(serial, devc);
	} else if (!devc->load_activated && !ebd_current_is0(devc)) {
		ebd_loadstart(serial, devc);
	} else if (devc->load_activated && (current_limit != devc->current_limit || voltage_limit != devc->voltage_limit)) {
		sr_dbg("Adjusting limit from %.03f A %.03f V to %.03f A %.03f V", current_limit, voltage_limit, devc->current_limit, devc->voltage_limit);
		send_cfg(serial, devc);
	}

	/* Begin frame. */
	std_session_send_df_frame_begin(sdi);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 4);

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = 1;

	/* Values */
	ebd_send_value(sdi, sdi->channels->data, current,
		SR_MQ_VOLTAGE, SR_UNIT_VOLT, 3);
	ebd_send_value(sdi, sdi->channels->next->data, voltage,
		SR_MQ_CURRENT, SR_UNIT_AMPERE, 4);
	ebd_send_value(sdi, sdi->channels->next->next->data, voltage_dp,
		SR_MQ_VOLTAGE, SR_UNIT_VOLT, 3);
	ebd_send_value(sdi, sdi->channels->next->next->next->data, voltage_dm,
		SR_MQ_VOLTAGE, SR_UNIT_VOLT, 3);

	/* End frame. */
	std_session_send_df_frame_end(sdi);

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
			ret = ebd_loadtoggle(sdi->conn, devc);
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

SR_PRIV int ebd_get_voltage_limit(const struct sr_dev_inst *sdi, float *voltage)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv))
		return SR_ERR;

	g_mutex_lock(&devc->rw_mutex);
	*voltage = devc->voltage_limit;
	g_mutex_unlock(&devc->rw_mutex);

	return SR_OK;
}

SR_PRIV int ebd_set_voltage_limit(const struct sr_dev_inst *sdi, float voltage)
{
	struct dev_context *devc;
	int ret;

	if (!(devc = sdi->priv))
		return SR_ERR;

	g_mutex_lock(&devc->rw_mutex);
	devc->voltage_limit = voltage;

	if (!devc->running) {
		sr_dbg("Setting voltage limit later.");
		g_mutex_unlock(&devc->rw_mutex);
		return SR_OK;
	}

	sr_dbg("Setting voltage limit to %fV.", voltage);

	if (devc->load_activated) {
		if (ebd_current_is0(devc)) {
			/* Stop load. */
			ret = ebd_loadtoggle(sdi->conn, devc);
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
