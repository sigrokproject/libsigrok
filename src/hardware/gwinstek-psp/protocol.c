/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 ettom <36895504+ettom@users.noreply.github.com>
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

#include "protocol.h"
#include <config.h>
#include <math.h>

#define CMD_ALL_QUERY "L\r\n"
#define RESPONSE_ALL_QUERY_LEN 37

static void give_device_time_to_process(struct dev_context *devc)
{
	int64_t sleeping_time;

	if (!devc->next_req_time)
		return;

	sleeping_time = devc->next_req_time - g_get_monotonic_time();
	if (sleeping_time > 0) {
		g_usleep(sleeping_time);
		sr_spew("Sleeping %" PRIi64 " us for processing", sleeping_time);
	}
}

SR_PRIV int gwinstek_psp_send_cmd(struct sr_serial_dev_inst *serial,
    struct dev_context *devc, const char *cmd, gboolean lock)
{
	int ret;

	if (lock)
		g_mutex_lock(&devc->rw_mutex);

	give_device_time_to_process(devc);

	sr_dbg("Sending '%s'.", cmd);
	if ((ret = serial_write_blocking(serial, cmd, strlen(cmd), 0)) < 0) {
		sr_err("Error sending command: %d.", ret);
	}

	devc->next_req_time =
		g_get_monotonic_time() + GWINSTEK_PSP_PROCESSING_TIME_MS * 1000;

	if (lock)
		g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

/*
 * Check for extra LF or CRLF (depends on whether device is in URPSP1 or URPSP2 mode.
 * Must be called right after calling gwinstek_psp_get_all_values.
 */
SR_PRIV int gwinstek_psp_check_terminator(struct sr_serial_dev_inst *serial,
    struct dev_context *devc)
{
	int bytes_left, ret;

	g_mutex_lock(&devc->rw_mutex);
	/* Sleep for a while to be extra sure the device has sent everything */
	g_usleep(20 * 1000);

	bytes_left = serial_has_receive_data(serial);
	sr_dbg("%d bytes left in buffer", bytes_left);

	if (bytes_left == 0) {
		/* 2, must already be set if we got here */
		sr_dbg("Device is in URPSP2 mode, terminator is CRLF");
	} else if (bytes_left == 1) {
		devc->msg_terminator_len = 3;
		sr_dbg("Device is in URPSP1 mode, terminator is CRCRLF");
	} else {
		sr_err("Don't know how to deal with %d bytes left", bytes_left);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	}

	ret = serial_flush(serial);

	g_mutex_unlock(&devc->rw_mutex);
	return ret;
}

/*
 * Can we trust that the reported voltage is the same as the voltage target? If
 * the output is off or the device is in CV mode, the answer is likely yes.
 * Only run this once during the initialization, since naively detecting CV
 * mode is not terribly reliable, especially when there is an ongoing
 * transition from CV to CC or vice-versa.
 */
SR_PRIV int gwinstek_psp_get_initial_voltage_target(struct dev_context *devc)
{
	if (!devc->output_enabled || (fabs(devc->current - devc->current_limit) >= 0.01)) {
		devc->voltage_target = devc->voltage;
		sr_dbg("Set initial voltage target to %.2f", devc->voltage_target);
	} else {
		/* Would it be more correct to fail the scan here? */
		sr_warn("Could not determine actual voltage target, falling back to 0");
		devc->voltage_target = 0;
	}

	return SR_OK;
}

SR_PRIV int gwinstek_psp_get_all_values(struct sr_serial_dev_inst *serial,
    struct dev_context *devc)
{
	int ret, bytes_to_read;
	char buf[50], *b;
	uint64_t now, delta;

	g_mutex_lock(&devc->rw_mutex);

	now = g_get_monotonic_time();
	delta = now - devc->last_status_query_time;
	if (delta <= GWINSTEK_PSP_STATUS_POLL_TIME_MS * 1000) {
		sr_spew("Last status query was only %" PRIu64 "us ago, returning", delta);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_OK;
	}

	ret = gwinstek_psp_send_cmd(serial, devc, CMD_ALL_QUERY, FALSE);
	devc->last_status_query_time = now;

	if (ret < 0) {
		g_mutex_unlock(&devc->rw_mutex);
		return ret;
	}

	b = buf;
	memset(buf, 0, sizeof(buf));
	bytes_to_read = RESPONSE_ALL_QUERY_LEN + devc->msg_terminator_len;
	ret = serial_read_blocking(serial, b, bytes_to_read, 1000);

	if (ret < 0) {
		sr_err("Error %d reading from device.", ret);
		g_mutex_unlock(&devc->rw_mutex);
		return ret;
	}

	sr_dbg("Received: '%s'", buf);

	if (ret != bytes_to_read || sscanf(buf, "V%fA%fW%fU%dI%f", &devc->voltage,
			&devc->current, &devc->power,
			&devc->voltage_limit, &devc->current_limit) != 5) {
		sr_err("Parsing status payload failed");
		while (serial_read_blocking(serial, b, 1, 1000) > 0);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	}

	devc->output_enabled = (buf[31] == '1');
	devc->otp_active = (buf[32] == '1');

	if (devc->output_enabled) {
		devc->voltage_or_0 = devc->voltage;
	} else {
		devc->voltage_target = devc->voltage;
		devc->voltage_target_updated = g_get_monotonic_time();
		devc->voltage_or_0 = 0;
	}

	sr_spew("Status: voltage_or_0=%.2f, voltage_target=%.2f, current=%.3f, power=%.1f, "
		"voltage_limit=%d, current_limit=%.2f",
		devc->voltage_or_0, devc->voltage_target, devc->current, devc->power,
		devc->voltage_limit, devc->current_limit);

	g_mutex_unlock(&devc->rw_mutex);
	return SR_OK;
}

SR_PRIV int gwinstek_psp_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	gboolean otp_active_prev;
	gboolean output_enabled_prev;
	GSList *l;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	serial = sdi->conn;

	otp_active_prev = devc->otp_active;
	output_enabled_prev = devc->output_enabled;

	gwinstek_psp_get_all_values(serial, devc);

	if (otp_active_prev != devc->otp_active) {
		sr_session_send_meta(sdi, SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE,
				     g_variant_new_boolean(devc->otp_active));
	}

	if (output_enabled_prev != devc->output_enabled) {
		sr_session_send_meta(sdi, SR_CONF_ENABLED,
				     g_variant_new_boolean(devc->output_enabled));
	}

	if (devc->set_voltage_target != devc->voltage_target &&
	    (devc->set_voltage_target_updated + 1000 * 1000) <
		    devc->voltage_target_updated) {
		/* The device reports a voltage target that is different from
		 * the one that was last set. Trust the device if the information
		 * is more recent. */
		sr_dbg("Updating session voltage target to %.2f",
		       devc->voltage_target);
		sr_session_send_meta(sdi, SR_CONF_VOLTAGE_TARGET,
				     g_variant_new_double(devc->voltage_target));
		devc->set_voltage_target = devc->voltage_target;
		devc->set_voltage_target_updated = g_get_monotonic_time();
	}

	/* Note: digits/spec_digits will be overridden later. */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	meaning.mqflags = SR_MQFLAG_DC;
	analog.num_samples = 1;

	/* Voltage */
	l = g_slist_copy(sdi->channels);
	l = g_slist_remove_link(l, g_slist_nth(l, 1));
	meaning.channels = l;
	meaning.mq = SR_MQ_VOLTAGE;
	meaning.mqflags = SR_MQFLAG_DC;
	meaning.unit = SR_UNIT_VOLT;
	encoding.digits = 2;
	spec.spec_digits = 2;
	analog.data = &devc->voltage_or_0;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	/* Current */
	l = g_slist_copy(sdi->channels);
	l = g_slist_remove_link(l, g_slist_nth(l, 0));
	meaning.channels = l;
	meaning.mq = SR_MQ_CURRENT;
	meaning.unit = SR_UNIT_AMPERE;
	encoding.digits = 3;
	spec.spec_digits = 3;
	analog.data = &devc->current;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	sr_sw_limits_update_samples_read(&devc->limits, 1);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
