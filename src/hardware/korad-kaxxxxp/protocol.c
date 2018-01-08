/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
 * Copyright (C) 2018 Frank Stettner <frank-stettner@gmx.net>
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

SR_PRIV int korad_kaxxxxp_send_cmd(struct sr_serial_dev_inst *serial,
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

SR_PRIV int korad_kaxxxxp_read_chars(struct sr_serial_dev_inst *serial,
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

SR_PRIV int korad_kaxxxxp_set_value(struct sr_serial_dev_inst *serial,
				int target, struct dev_context *devc)
{
	char *msg;
	const char *cmd;
	float value;
	int ret;

	g_mutex_lock(&devc->rw_mutex);
	give_device_time_to_process(devc);

	switch (target) {
	case KAXXXXP_CURRENT:
	case KAXXXXP_VOLTAGE:
	case KAXXXXP_STATUS:
		sr_err("Can't set measurable parameter %d.", target);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	case KAXXXXP_CURRENT_MAX:
		cmd = "ISET1:%05.3f";
		value = devc->current_max;
		break;
	case KAXXXXP_VOLTAGE_MAX:
		cmd = "VSET1:%05.2f";
		value = devc->voltage_max;
		break;
	case KAXXXXP_OUTPUT:
		cmd = "OUT%01.0f";
		value = (devc->output_enabled) ? 1 : 0;
		break;
	case KAXXXXP_BEEP:
		cmd = "BEEP%01.0f";
		value = (devc->beep_enabled) ? 1 : 0;
		break;
	case KAXXXXP_OCP:
		cmd = "OCP%01.0f";
		value = (devc->ocp_enabled) ? 1 : 0;
		break;
	case KAXXXXP_OVP:
		cmd = "OVP%01.0f";
		value = (devc->ovp_enabled) ? 1 : 0;
		break;
	case KAXXXXP_SAVE:
		cmd = "SAV%01.0f";
		if (devc->program < 1 || devc->program > 5) {
			sr_err("Only programs 1-5 supported and %d isn't "
			       "between them.", devc->program);
			g_mutex_unlock(&devc->rw_mutex);
			return SR_ERR;
		}
		value = devc->program;
		break;
	case KAXXXXP_RECALL:
		cmd = "RCL%01.0f";
		if (devc->program < 1 || devc->program > 5) {
			sr_err("Only programs 1-5 supported and %d isn't "
			       "between them.", devc->program);
			g_mutex_unlock(&devc->rw_mutex);
			return SR_ERR;
		}
		value = devc->program;
		break;
	default:
		sr_err("Don't know how to set %d.", target);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	}

	msg = g_malloc0(20 + 1);
	if (cmd)
		sr_snprintf_ascii(msg, 20, cmd, value);

	ret = korad_kaxxxxp_send_cmd(serial, msg);
	devc->req_sent_at = g_get_monotonic_time();
	g_free(msg);

	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

SR_PRIV int korad_kaxxxxp_get_value(struct sr_serial_dev_inst *serial,
				int target, struct dev_context *devc)
{
	int ret, count;
	char reply[6];
	float *value;
	char status_byte;

	g_mutex_lock(&devc->rw_mutex);
	give_device_time_to_process(devc);

	value = NULL;
	count = 5;

	switch (target) {
	case KAXXXXP_CURRENT:
		/* Read current from device. */
		ret = korad_kaxxxxp_send_cmd(serial, "IOUT1?");
		value = &(devc->current);
		break;
	case KAXXXXP_CURRENT_MAX:
		/* Read set current from device. */
		ret = korad_kaxxxxp_send_cmd(serial, "ISET1?");
		value = &(devc->current_max);
		break;
	case KAXXXXP_VOLTAGE:
		/* Read voltage from device. */
		ret = korad_kaxxxxp_send_cmd(serial, "VOUT1?");
		value = &(devc->voltage);
		break;
	case KAXXXXP_VOLTAGE_MAX:
		/* Read set voltage from device. */
		ret = korad_kaxxxxp_send_cmd(serial, "VSET1?");
		value = &(devc->voltage_max);
		break;
	case KAXXXXP_STATUS:
	case KAXXXXP_OUTPUT:
	case KAXXXXP_OCP:
	case KAXXXXP_OVP:
		/* Read status from device. */
		ret = korad_kaxxxxp_send_cmd(serial, "STATUS?");
		count = 1;
		break;
	default:
		sr_err("Don't know how to query %d.", target);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	}

	devc->req_sent_at = g_get_monotonic_time();

	if ((ret = korad_kaxxxxp_read_chars(serial, count, reply)) < 0) {
		g_mutex_unlock(&devc->rw_mutex);
		return ret;
	}

	reply[count] = 0;

	if (value) {
		sr_atof_ascii((const char *)&reply, value);
		sr_dbg("value: %f", *value);
	} else {
		/* We have status reply. */
		status_byte = reply[0];
		/* Constant current */
		devc->cc_mode[0] = !(status_byte & (1 << 0)); /* Channel one */
		devc->cc_mode[1] = !(status_byte & (1 << 1)); /* Channel two */
		/*
		 * Tracking
		 * status_byte & ((1 << 2) | (1 << 3))
		 * 00 independent 01 series 11 parallel
		 */
		devc->beep_enabled = (1 << 4);
		devc->ocp_enabled = (status_byte & (1 << 5));
		devc->output_enabled = (status_byte & (1 << 6));
		/* Velleman LABPS3005 quirk */
		if (devc->output_enabled)
			devc->ovp_enabled = (status_byte & (1 << 7));
		sr_dbg("Status: 0x%02x", status_byte);
		sr_spew("Status: CH1: constant %s CH2: constant %s. "
			"Tracking would be %s. Device is "
			"%s and %s. Buttons are %s. Output is %s "
			"and extra byte is %s.",
			(status_byte & (1 << 0)) ? "voltage" : "current",
			(status_byte & (1 << 1)) ? "voltage" : "current",
			(status_byte & (1 << 2)) ? "parallel" : "series",
			(status_byte & (1 << 3)) ? "tracking" : "independent",
			(status_byte & (1 << 4)) ? "beeping" : "silent",
			(status_byte & (1 << 5)) ? "locked" : "unlocked",
			(status_byte & (1 << 6)) ? "enabled" : "disabled",
			(status_byte & (1 << 7)) ? "true" : "false");
	}

	/* Read the sixth byte from ISET? BUG workaround. */
	if (target == KAXXXXP_CURRENT_MAX)
		serial_read_blocking(serial, &status_byte, 1, 10);

	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

SR_PRIV int korad_kaxxxxp_get_all_values(struct sr_serial_dev_inst *serial,
				struct dev_context *devc)
{
	int ret, target;

	for (target = KAXXXXP_CURRENT;
			target <= KAXXXXP_STATUS; target++) {
		if ((ret = korad_kaxxxxp_get_value(serial, target, devc)) < 0)
			return ret;
	}

	return ret;
}

static void next_measurement(struct dev_context *devc)
{
	switch (devc->acquisition_target) {
	case KAXXXXP_CURRENT:
		devc->acquisition_target = KAXXXXP_VOLTAGE;
		break;
	case KAXXXXP_VOLTAGE:
		devc->acquisition_target = KAXXXXP_STATUS;
		break;
	case KAXXXXP_STATUS:
		devc->acquisition_target = KAXXXXP_CURRENT;
		break;
	default:
		devc->acquisition_target = KAXXXXP_CURRENT;
		sr_err("Invalid target for next acquisition.");
	}
}

SR_PRIV int korad_kaxxxxp_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	GSList *l;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	/* Get the value. */
	korad_kaxxxxp_get_value(serial, devc->acquisition_target, devc);

	/* Note: digits/spec_digits will be overridden later. */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	/* Send the value forward. */
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = 1;
	l = g_slist_copy(sdi->channels);
	if (devc->acquisition_target == KAXXXXP_CURRENT) {
		l = g_slist_remove_link(l, g_slist_nth(l, 0));
		analog.meaning->channels = l;
		analog.meaning->mq = SR_MQ_CURRENT;
		analog.meaning->unit = SR_UNIT_AMPERE;
		analog.meaning->mqflags = 0;
		analog.encoding->digits = 3;
		analog.spec->spec_digits = 3;
		analog.data = &devc->current;
		sr_session_send(sdi, &packet);
	} else if (devc->acquisition_target == KAXXXXP_VOLTAGE) {
		l = g_slist_remove_link(l, g_slist_nth(l, 1));
		analog.meaning->channels = l;
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.meaning->mqflags = SR_MQFLAG_DC;
		analog.encoding->digits = 2;
		analog.spec->spec_digits = 2;
		analog.data = &devc->voltage;
		sr_session_send(sdi, &packet);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}
	next_measurement(devc);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
