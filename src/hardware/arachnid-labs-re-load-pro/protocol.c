/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015-2016 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include "protocol.h"

#define READ_TIMEOUT_MS 500

static int send_cmd(const struct sr_dev_inst *sdi, const char *cmd,
		char *replybuf, int replybufsize)
{
	char *bufptr;
	int len, ret;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	serial = sdi->conn;

	/* Send the command (blocking, with timeout). */
	if ((ret = serial_write_blocking(serial, cmd,
			strlen(cmd), serial_timeout(serial,
			strlen(cmd)))) < (int)strlen(cmd)) {
		sr_err("Unable to send command.");
		return SR_ERR;
	}

	if (!devc->acquisition_running) {
		/* Read the reply (blocking, with timeout). */
		memset(replybuf, 0, replybufsize);
		bufptr = replybuf;
		len = replybufsize;
		ret = serial_readline(serial, &bufptr, &len, READ_TIMEOUT_MS);

		/* If we got 0 characters (possibly one \r or \n), retry once. */
		if (len == 0) {
			len = replybufsize;
			ret = serial_readline(serial, &bufptr, &len, READ_TIMEOUT_MS);
		}

		if (g_str_has_prefix((const char *)&bufptr, "err ")) {
			sr_err("Device replied with an error: '%s'.", bufptr);
			return SR_ERR;
		}
	}

	return ret;
}

SR_PRIV int reloadpro_set_current_limit(const struct sr_dev_inst *sdi,
					float current_limit)
{
	struct dev_context *devc;
	int ret, ma;
	char buf[100];
	char *cmd;

	devc = sdi->priv;

	if (current_limit < 0 || current_limit > 6) {
		sr_err("The current limit must be 0-6 A (was %f A).", current_limit);
		return SR_ERR_ARG;
	}

	/* Hardware expects current limit in mA, integer (0..6000). */
	ma = (int)round(current_limit * 1000);

	cmd = g_strdup_printf("set %d\n", ma);
	g_mutex_lock(&devc->acquisition_mutex);
	ret = send_cmd(sdi, cmd, (char *)&buf, sizeof(buf));
	g_mutex_unlock(&devc->acquisition_mutex);
	g_free(cmd);

	if (ret < 0) {
		sr_err("Error sending current limit command: %d.", ret);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int reloadpro_set_on_off(const struct sr_dev_inst *sdi, gboolean on)
{
	struct dev_context *devc;
	int ret;
	char buf[100];
	const char *cmd;

	devc = sdi->priv;

	cmd = (on) ? "on\n" : "off\n";
	g_mutex_lock(&devc->acquisition_mutex);
	ret = send_cmd(sdi, cmd, (char *)&buf, sizeof(buf));
	g_mutex_unlock(&devc->acquisition_mutex);

	if (ret < 0) {
		sr_err("Error sending on/off command: %d.", ret);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int reloadpro_set_under_voltage_threshold(const struct sr_dev_inst *sdi,
					float voltage)
{
	struct dev_context *devc;
	int ret, mv;
	char buf[100];
	char *cmd;

	devc = sdi->priv;

	if (voltage < 0 || voltage > 60) {
		sr_err("The under voltage threshold must be 0-60 V (was %f V).",
			voltage);
		return SR_ERR_ARG;
	}

	/* Hardware expects voltage in mV, integer (0..60000). */
	mv = (int)round(voltage * 1000);

	sr_spew("Setting under voltage threshold to %f V (%d mV).", voltage, mv);

	cmd = g_strdup_printf("uvlo %d\n", mv);
	g_mutex_lock(&devc->acquisition_mutex);
	ret = send_cmd(sdi, cmd, (char *)&buf, sizeof(buf));
	g_mutex_unlock(&devc->acquisition_mutex);
	g_free(cmd);

	if (ret < 0) {
		sr_err("Error sending under voltage threshold command: %d.", ret);
		return SR_ERR;
	}
	return SR_OK;
}

SR_PRIV int reloadpro_get_current_limit(const struct sr_dev_inst *sdi,
					float *current_limit)
{
	struct dev_context *devc;
	int ret;
	char buf[100];
	gint64 end_time;

	devc = sdi->priv;

	g_mutex_lock(&devc->acquisition_mutex);
	if ((ret = send_cmd(sdi, "set\n", (char *)&buf, sizeof(buf))) < 0) {
		sr_err("Error sending current limit query: %d.", ret);
		return SR_ERR;
	}

	if (devc->acquisition_running) {
		end_time = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
		if (!g_cond_wait_until(&devc->current_limit_cond,
				&devc->acquisition_mutex, end_time)) {
			// timeout has passed.
			g_mutex_unlock(&devc->acquisition_mutex);
			return SR_ERR;
		}
	} else {
		/* Hardware sends current limit in mA, integer (0..6000). */
		devc->current_limit = g_ascii_strtod(buf + 4, NULL) / 1000;
	}
	g_mutex_unlock(&devc->acquisition_mutex);

	if (current_limit)
		*current_limit = devc->current_limit;

	return SR_OK;
}

SR_PRIV int reloadpro_get_under_voltage_threshold(const struct sr_dev_inst *sdi,
					float *uvc_threshold)
{
	struct dev_context *devc;
	int ret;
	char buf[100];
	gint64 end_time;

	devc = sdi->priv;

	g_mutex_lock(&devc->acquisition_mutex);
	if ((ret = send_cmd(sdi, "uvlo\n", (char *)&buf, sizeof(buf))) < 0) {
		sr_err("Error sending under voltage threshold query: %d.", ret);
		return SR_ERR;
	}

	if (devc->acquisition_running) {
		end_time = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
		if (!g_cond_wait_until(&devc->uvc_threshold_cond,
				&devc->acquisition_mutex, end_time)) {
			// timeout has passed.
			g_mutex_unlock(&devc->acquisition_mutex);
			return SR_ERR;
		}
	} else {
		/* Hardware sends voltage in mV, integer (0..60000). */
		devc->uvc_threshold = g_ascii_strtod(buf + 5, NULL) / 1000;
	}
	g_mutex_unlock(&devc->acquisition_mutex);

	if (uvc_threshold)
		*uvc_threshold = devc->uvc_threshold;

	return SR_OK;
}

SR_PRIV int reloadpro_get_voltage_current(const struct sr_dev_inst *sdi,
		float *voltage, float *current)
{
	struct dev_context *devc;
	int ret;
	char buf[100];
	char **tokens;
	gint64 end_time;

	devc = sdi->priv;

	g_mutex_lock(&devc->acquisition_mutex);
	if ((ret = send_cmd(sdi, "read\n", (char *)&buf, sizeof(buf))) < 0) {
		sr_err("Error sending voltage/current query: %d.", ret);
		return SR_ERR;
	}

	if (devc->acquisition_running) {
		end_time = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
		if (!g_cond_wait_until(&devc->voltage_cond,
				&devc->acquisition_mutex, end_time)) {
			// timeout has passed.
			g_mutex_unlock(&devc->acquisition_mutex);
			return SR_ERR;
		}
	} else {
		/* Reply: "read <current> <voltage>". */
		tokens = g_strsplit((const char *)&buf, " ", 3);
		devc->voltage = g_ascii_strtod(tokens[2], NULL) / 1000;
		devc->current = g_ascii_strtod(tokens[1], NULL) / 1000;
		g_strfreev(tokens);
	}
	g_mutex_unlock(&devc->acquisition_mutex);

	if (voltage)
		*voltage = devc->voltage;
	if (current)
		*current = devc->current;

	return SR_OK;
}

static int send_config_update_key(const struct sr_dev_inst *sdi,
		uint32_t key, GVariant *var)
{
	struct sr_config *cfg;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	int ret;

	cfg = sr_config_new(key, var);
	if (!cfg)
		return SR_ERR;

	memset(&meta, 0, sizeof(meta));

	packet.type = SR_DF_META;
	packet.payload = &meta;

	meta.config = g_slist_append(meta.config, cfg);

	ret = sr_session_send(sdi, &packet);
	sr_config_free(cfg);

	return ret;
}

static void handle_packet(const struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;
	char **tokens;
	GSList *l;

	devc = sdi->priv;

	if (g_str_has_prefix((const char *)devc->buf, "overtemp")) {
		sr_warn("Overtemperature condition!");
		devc->otp_active = TRUE;
		send_config_update_key(sdi, SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE,
			g_variant_new_boolean(TRUE));
		return;
	}

	if (g_str_has_prefix((const char *)devc->buf, "undervolt")) {
		sr_warn("Undervoltage condition!");
		devc->uvc_active = TRUE;
		send_config_update_key(sdi, SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE,
			g_variant_new_boolean(TRUE));
		return;
	}

	if (g_str_has_prefix((const char *)devc->buf, "err ")) {
		sr_err("Device replied with an error: '%s'.", devc->buf);
		return;
	}

	if (g_str_has_prefix((const char *)devc->buf, "set ")) {
		tokens = g_strsplit((const char *)devc->buf, " ", 2);
		devc->current_limit = g_ascii_strtod(tokens[1], NULL) / 1000;
		g_strfreev(tokens);
		g_cond_signal(&devc->current_limit_cond);
		send_config_update_key(sdi, SR_CONF_CURRENT_LIMIT,
			g_variant_new_double(devc->current_limit));
		return;
	}

	if (g_str_has_prefix((const char *)devc->buf, "uvlo ")) {
		tokens = g_strsplit((const char *)devc->buf, " ", 2);
		devc->uvc_threshold = g_ascii_strtod(tokens[1], NULL) / 1000;
		g_strfreev(tokens);
		g_cond_signal(&devc->uvc_threshold_cond);
		if (devc->uvc_threshold == .0) {
			send_config_update_key(sdi, SR_CONF_UNDER_VOLTAGE_CONDITION,
				g_variant_new_boolean(FALSE));
		} else {
			send_config_update_key(sdi, SR_CONF_UNDER_VOLTAGE_CONDITION,
				g_variant_new_boolean(TRUE));
			send_config_update_key(sdi,
				SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD,
				g_variant_new_double(devc->uvc_threshold));
		}
		return;
	}

	if (!g_str_has_prefix((const char *)devc->buf, "read ")) {
		sr_dbg("Unknown packet: '%s'.", devc->buf);
		return;
	}

	tokens = g_strsplit((const char *)devc->buf, " ", 3);
	devc->voltage = g_ascii_strtod(tokens[2], NULL) / 1000;
	devc->current = g_ascii_strtod(tokens[1], NULL) / 1000;
	g_strfreev(tokens);

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
	analog.data = &devc->voltage;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	/* Current */
	l = g_slist_copy(sdi->channels);
	l = g_slist_remove_link(l, g_slist_nth(l, 0));
	meaning.channels = l;
	meaning.mq = SR_MQ_CURRENT;
	meaning.mqflags = SR_MQFLAG_DC;
	meaning.unit = SR_UNIT_AMPERE;
	analog.data = &devc->current;
	sr_session_send(sdi, &packet);
	g_slist_free(l);

	/* End frame. */
	packet.type = SR_DF_FRAME_END;
	packet.payload = NULL;
	sr_session_send(sdi, &packet);

	sr_sw_limits_update_samples_read(&devc->limits, 1);
}

static void handle_new_data(const struct sr_dev_inst *sdi)
{
	int len;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	char *buf;

	devc = sdi->priv;
	serial = sdi->conn;

	len = RELOADPRO_BUFSIZE - devc->buflen;
	buf = devc->buf;
	g_mutex_lock(&devc->acquisition_mutex);
	if (serial_readline(serial, &buf, &len, 250) != SR_OK) {
		g_mutex_unlock(&devc->acquisition_mutex);
		return;
	}

	if (len == 0) {
		g_mutex_unlock(&devc->acquisition_mutex);
		return; /* No new bytes, nothing to do. */
	}
	if (len < 0) {
		sr_err("Serial port read error: %d.", len);
		g_mutex_unlock(&devc->acquisition_mutex);
		return;
	}
	devc->buflen += len;

	handle_packet(sdi);
	g_mutex_unlock(&devc->acquisition_mutex);
	memset(devc->buf, 0, RELOADPRO_BUFSIZE);
	devc->buflen = 0;
}

SR_PRIV int reloadpro_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	sdi = cb_data;
	devc = sdi->priv;

	if (revents != G_IO_IN)
		return TRUE;

	handle_new_data(sdi);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
