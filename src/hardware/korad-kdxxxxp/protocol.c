/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
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

#define REQ_TIMEOUT_MS 500
#define DEVICE_PROCESSING_TIME_MS 80

SR_PRIV int korad_kdxxxxp_send_cmd(struct sr_serial_dev_inst *serial,
				const char *cmd)
{
	int ret;

	sr_dbg("Sending '%s'.", cmd);
	if ((ret = serial_write_blocking(serial, cmd, strlen(cmd), 0)) < 0) {
		sr_err("Error sending command: %d.", ret);
		return ret;
	}

	return ret;
}

SR_PRIV int korad_kdxxxxp_read_chars(struct sr_serial_dev_inst *serial,
				int count, char *buf)
{
	int ret, received, turns;

	received = 0;
	turns = 0;

	do {
		if ((ret = serial_read_blocking(serial, buf + received,
				count - received,
				serial_timeout(serial, count))) < 0) {
			sr_err("Error %d reading %d bytes from device.",
			       ret, count);
			return ret;
		}
		received += ret;
		turns++;
	} while ((received < count) && (turns < 100));

	buf[count] = 0;

	sr_spew("Received: '%s'.", buf);

	return ret;
}

static void give_device_time_to_process(struct dev_context *devc)
{
	int64_t sleeping_time;

	sleeping_time = devc->req_sent_at + (DEVICE_PROCESSING_TIME_MS * 1000);
	sleeping_time -= g_get_monotonic_time();

	if (sleeping_time > 0) {
		g_usleep(sleeping_time);
		sr_spew("Sleeping for processing %" PRIi64 " usec", sleeping_time);
	}
}

SR_PRIV int korad_kdxxxxp_set_value(struct sr_serial_dev_inst *serial,
				struct dev_context *devc)
{
	char msg[21], *cmd;
	float value;
	int ret;

	give_device_time_to_process(devc);

	msg[20] = 0;
	switch(devc->target){
	case KDXXXXP_CURRENT:
	case KDXXXXP_VOLTAGE:
	case KDXXXXP_STATUS:
		sr_err("Can't set measurable parameter.");
		return SR_ERR;
	case KDXXXXP_CURRENT_MAX:
		cmd = "ISET1:%05.3f";
		value = devc->current_max;
		break;
	case KDXXXXP_VOLTAGE_MAX:
		cmd = "VSET1:%05.2f";
		value = devc->voltage_max;
		break;
	case KDXXXXP_OUTPUT:
		cmd = "OUT%01.0f";
		value = (devc->output_enabled) ? 1 : 0;
		break;
	case KDXXXXP_BEEP:
		cmd = "BEEP%01.0f";
		value = (devc->beep_enabled) ? 1 : 0;
		break;
	case KDXXXXP_SAVE:
		cmd = "SAV%01.0f";
		if (devc->program < 1 || devc->program > 5) {
			sr_err("Only programs 1-5 supported and %d isn't "
			       "between them.", devc->program);
			return SR_ERR;
		}
		value = devc->program;
		break;
	case KDXXXXP_RECALL:
		cmd = "RCL%01.0f";
		if (devc->program < 1 || devc->program > 5) {
			sr_err("Only programs 1-5 supported and %d isn't "
			       "between them.", devc->program);
			return SR_ERR;
		}
		value = devc->program;
		break;
	default:
		sr_err("Don't know how to set %d.", devc->target);
		return SR_ERR;
	}

	if (cmd)
		snprintf(msg, 20, cmd, value);

	ret = korad_kdxxxxp_send_cmd(serial, msg);
	devc->req_sent_at = g_get_monotonic_time();
	devc->reply_pending = FALSE;

	return ret;
}

SR_PRIV int korad_kdxxxxp_query_value(struct sr_serial_dev_inst *serial,
				struct dev_context *devc)
{
	int ret;

	give_device_time_to_process(devc);

	switch(devc->target){
	case KDXXXXP_CURRENT:
		/* Read current from device. */
		ret = korad_kdxxxxp_send_cmd(serial, "IOUT1?");
		break;
	case KDXXXXP_CURRENT_MAX:
		/* Read set current from device. */
		ret = korad_kdxxxxp_send_cmd(serial, "ISET1?");
		break;
	case KDXXXXP_VOLTAGE:
		/* Read voltage from device. */
		ret = korad_kdxxxxp_send_cmd(serial, "VOUT1?");
		break;
	case KDXXXXP_VOLTAGE_MAX:
		/* Read set voltage from device. */
		ret = korad_kdxxxxp_send_cmd(serial, "VSET1?");
		break;
	case KDXXXXP_STATUS:
	case KDXXXXP_OUTPUT:
		/* Read status from device. */
		ret = korad_kdxxxxp_send_cmd(serial, "STATUS?");
		break;
	default:
		sr_err("Don't know how to query %d.", devc->target);
		return SR_ERR;
	}

	devc->req_sent_at = g_get_monotonic_time();
	devc->reply_pending = TRUE;

	return ret;
}

SR_PRIV int korad_kdxxxxp_get_all_values(struct sr_serial_dev_inst *serial,
				struct dev_context *devc)
{
	int ret;

	for (devc->target = KDXXXXP_CURRENT;
			devc->target <= KDXXXXP_STATUS; devc->target++) {
		if ((ret = korad_kdxxxxp_query_value(serial, devc)) < 0)
			return ret;
		if ((ret = korad_kdxxxxp_get_reply(serial, devc)) < 0)
			return ret;
	}

	return ret;
}

SR_PRIV int korad_kdxxxxp_get_reply(struct sr_serial_dev_inst *serial,
				struct dev_context *devc)
{
	double value;
	int count, ret;
	float *target;
	char status_byte;

	target = NULL;
	count = 5;

	switch (devc->target) {
	case KDXXXXP_CURRENT:
		/* Read current from device. */
		target = &(devc->current);
		break;
	case KDXXXXP_CURRENT_MAX:
		/* Read set current from device. */
		target = &(devc->current_max);
		break;
	case KDXXXXP_VOLTAGE:
		/* Read voltage from device. */
		target = &(devc->voltage);
		break;
	case KDXXXXP_VOLTAGE_MAX:
		/* Read set voltage from device. */
		target = &(devc->voltage_max);
		break;
	case KDXXXXP_STATUS:
	case KDXXXXP_OUTPUT:
		/* Read status from device. */
		count = 1;
		break;
	default:
		sr_err("Don't know where to put repply %d.", devc->target);
	}

	if ((ret = korad_kdxxxxp_read_chars(serial, count, devc->reply)) < 0)
		return ret;

	devc->reply[count] = 0;

	if (target) {
		value = g_ascii_strtod(devc->reply, NULL);
		*target = (float)value;
		sr_dbg("value: %f",value);
	} else {
		/* We have status reply. */
		status_byte = devc->reply[0];
		/* Constant current */
		devc->cc_mode[0] = !(status_byte & (1 << 0)); /* Channel one */
		devc->cc_mode[1] = !(status_byte & (1 << 1)); /* Channel two */
		/*
		 * Tracking
		 * status_byte & ((1 << 2) | (1 << 3))
		 * 00 independent 01 series 11 parallel
		 */
		devc->beep_enabled = (1 << 4);
		/* status_byte & (1 << 5) Unlocked */

		devc->output_enabled = (status_byte & (1 << 6));
		sr_dbg("Status: 0x%02x", status_byte);
		sr_spew("Status: CH1: constant %s CH2: constant %s. Device is "
			"%s and %s. Buttons are %s. Output is %s ",
			(status_byte & (1 << 0)) ? "voltage" : "current",
			(status_byte & (1 << 1)) ? "voltage" : "current",
			(status_byte & (1 << 3)) ? "tracking" : "independent",
			(status_byte & (1 << 4)) ? "beeping" : "silent",
			(status_byte & (1 << 5)) ? "locked" : "unlocked",
			(status_byte & (1 << 6)) ? "enabled" : "disabled");
	}

	devc->reply_pending = FALSE;

	return ret;
}

static void next_measurement(struct dev_context *devc)
{
	switch (devc->target) {
	case KDXXXXP_CURRENT:
		devc->target = KDXXXXP_VOLTAGE;
		break;
	case KDXXXXP_CURRENT_MAX:
		devc->target = KDXXXXP_CURRENT;
		break;
	case KDXXXXP_VOLTAGE:
		devc->target = KDXXXXP_STATUS;
		break;
	case KDXXXXP_VOLTAGE_MAX:
		devc->target = KDXXXXP_CURRENT;
		break;
	case KDXXXXP_OUTPUT:
		devc->target = KDXXXXP_STATUS;
		break;
	case KDXXXXP_STATUS:
		devc->target = KDXXXXP_CURRENT;
		break;
	default:
		devc->target = KDXXXXP_CURRENT;
	}
}

SR_PRIV int korad_kdxxxxp_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog_old analog;
	int64_t t, elapsed_us;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) {
		/* Get the value. */
		korad_kdxxxxp_get_reply(serial, devc);

		/* Send the value forward. */
		packet.type = SR_DF_ANALOG_OLD;
		packet.payload = &analog;
		analog.channels = sdi->channels;
		analog.num_samples = 1;
		if (devc->target == KDXXXXP_CURRENT) {
			analog.mq = SR_MQ_CURRENT;
			analog.unit = SR_UNIT_AMPERE;
			analog.mqflags = 0;
			analog.data = &devc->current;
			sr_session_send(sdi, &packet);
		}
		if (devc->target == KDXXXXP_VOLTAGE) {
			analog.mq = SR_MQ_VOLTAGE;
			analog.unit = SR_UNIT_VOLT;
			analog.mqflags = SR_MQFLAG_DC;
			analog.data = &devc->voltage;
			sr_session_send(sdi, &packet);
			devc->num_samples++;
		}
		next_measurement(devc);
	} else {
		/* Time out */
		if (!devc->reply_pending) {
			if (korad_kdxxxxp_query_value(serial, devc) < 0)
				return TRUE;
			devc->req_sent_at = g_get_monotonic_time();
			devc->reply_pending = TRUE;
		}
	}

	if (devc->limit_samples && (devc->num_samples >= devc->limit_samples)) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	if (devc->limit_msec) {
		t = (g_get_monotonic_time() - devc->starttime) / 1000;
		if (t > (int64_t)devc->limit_msec) {
			sr_info("Requested time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi, cb_data);
			return TRUE;
		}
	}

	/* Request next packet, if required. */
	if (sdi->status == SR_ST_ACTIVE) {
		if (devc->reply_pending) {
			elapsed_us = g_get_monotonic_time() - devc->req_sent_at;
			if (elapsed_us > (REQ_TIMEOUT_MS * 1000))
				devc->reply_pending = FALSE;
			return TRUE;
		}

	}

	return TRUE;
}
