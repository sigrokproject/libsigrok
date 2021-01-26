/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Peter Skarpetis <peters@skarpetis.com>
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

SR_PRIV const char *mhs5200a_waveform_to_string(enum waveform_type wtype)
{
	switch (wtype) {
	case WAVEFORM_SINE:
		return "sine";
	case WAVEFORM_SQUARE:
		return "square";
	case WAVEFORM_TRIANGLE:
		return "triangle";
	case WAVEFORM_RISING_SAWTOOTH:
		return "rising sawtooth";
	case WAVEFORM_FALLING_SAWTOOTH:
		return "falling sawtooth";
	case WAVEFORM_UNKNOWN:
		return "unknown";
	}
	return "unknown";
}

SR_PRIV enum waveform_type mhs5200a_string_to_waveform(const char *wtype)
{
	if (!wtype)
		return WAVEFORM_UNKNOWN;
		
	if (g_ascii_strcasecmp(wtype, "sine") == 0) {
		return WAVEFORM_SINE;
	} else if (g_ascii_strcasecmp(wtype, "square") == 0) {
		return WAVEFORM_SQUARE;
	} else if (g_ascii_strcasecmp(wtype, "triangle") == 0) {
		return WAVEFORM_TRIANGLE;
	} else if (g_ascii_strcasecmp(wtype, "rising sawtooth") == 0) {
		return WAVEFORM_RISING_SAWTOOTH;
	} else if (g_ascii_strcasecmp(wtype, "falling sawtooth") == 0) {
		return WAVEFORM_FALLING_SAWTOOTH;
	} else {
		return WAVEFORM_UNKNOWN;
	}
}

/**
 * Read message into buf until "OK" received.
 *
 * @retval SR_OK Msg received; buf and buflen contain result, if any except OK.
 * @retval SR_ERR Error, including timeout.
*/
static int mhs5200a_read_reply(struct sr_serial_dev_inst *serial, char **buf, int *buflen)
{
	int retc;
	
	*buf[0] = '\0';

	retc = serial_read_blocking(serial, *buf, *buflen, SERIAL_READ_TIMEOUT_MS);
	if (retc <= 0)
		return SR_ERR;
	while ( ((*buf)[retc - 1] == '\n') || ((*buf)[retc - 1] == '\r') ) {
		(*buf)[retc - 1] = '\0';
		--retc;
	}
	*buflen = retc;
	if (!strcmp(*buf, "ok")) { /* We got an OK! */
		*buf[0] = '\0';
		*buflen = 0;
		return SR_OK;
	}
	return SR_OK;
}

/** Send command to device with va_list. */
static int mhs5200a_send_va(struct sr_serial_dev_inst *serial, const char *fmt, va_list args)
{
	int retc;
	char auxfmt[PROTOCOL_LEN_MAX];
	char buf[PROTOCOL_LEN_MAX];

	snprintf(auxfmt, sizeof(auxfmt), "%s\n", fmt); // all command require a \n at the end
	vsnprintf(buf, sizeof(buf), auxfmt, args);

	retc = serial_write_blocking(serial, buf, strlen(buf), SERIAL_WRITE_TIMEOUT_MS);
	if (retc < 0)
		return SR_ERR;

	return SR_OK;
}

/** Send command and consume simple OK reply. */
static int mhs5200a_cmd_ok(struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;
	char buf[PROTOCOL_LEN_MAX];
	char *bufptr;
	int buflen;

	/* Send command */
	va_start(args, fmt);
	retc = mhs5200a_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = mhs5200a_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen == 0))
		return SR_OK;

	return SR_ERR;
}

/**
 * Send command and read reply string.
 * @param reply Pointer to buffer of size PROTOCOL_LEN_MAX. Will be NUL-terminated.
 */
static int mhs5200a_cmd_reply(char *reply, struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;
	char buf[PROTOCOL_LEN_MAX];
	char *bufptr;
	int buflen;

	reply[0] = '\0';

	/* Send command */
	va_start(args, fmt);
	retc = mhs5200a_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = mhs5200a_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen > 0)) {
		strcpy(reply, buf);
		return SR_OK;
	}

	return SR_ERR;
}

SR_PRIV int mhs5200a_get_model(struct sr_serial_dev_inst *serial, char *model, int modelsize)
{
	char buf[PROTOCOL_LEN_MAX];

	if (modelsize < 10) {
		return SR_ERR;
	}
	/* Query and verify model string. */
	if ( (mhs5200a_cmd_reply(buf, serial, ":r0c") != SR_OK) || strlen(buf) < 10) {
		return SR_ERR;
	}
	if (strncmp(buf, ":r0c52", 6) != 0) {
		return SR_ERR;
	}
	memcpy(model, "MHS-", 4);
	memcpy(model + 4, buf + 4, 5);
	model[9] = 0;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_waveform(const struct sr_dev_inst *sdi, int ch, long *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dw", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atol(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int mhs5200a_get_attenuation(const struct sr_dev_inst *sdi, int ch, long *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dy", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atol(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int mhs5200a_get_onoff(const struct sr_dev_inst *sdi, long *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r1b");
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atol(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int mhs5200a_get_frequency(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%df", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val /= 100.0;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_amplitude(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	long attenuation;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_get_attenuation(sdi, ch, &attenuation);
	if (retc < 0)
		return SR_ERR;
	
	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%da", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val /= 100.0;
	if (attenuation == ATTENUATION_MINUS_20DB) {
		*val /= 10.0;
	}
	return SR_OK;
}

SR_PRIV int mhs5200a_get_duty_cycle(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dd", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val /= 10.0;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_offset(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	double amplitude;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_get_amplitude(sdi, ch, &amplitude);
	if (retc < 0)
		return SR_ERR;

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%do", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val -= 120.0; // offset is returned as a percentage of amplitude
	*val = amplitude * *val / 100.0;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_phase(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dp", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int mhs5200a_set_frequency(const struct sr_dev_inst *sdi, int ch, double val)
{
	struct dev_context *devc;
	long wtype;
	double freq_min;
	double freq_max;
	
	if (mhs5200a_get_waveform(sdi, ch, &wtype) < 0) {
		return SR_ERR;
	}
	devc = sdi->priv;
	if (mhs5200a_frequency_limits(wtype, &freq_min, &freq_max) < 0)
		return SR_ERR;
	
	if (val > devc->max_frequency || val < freq_min || val > freq_max) {
		sr_err("Invalid frequency %.2fHz for %s wave. Valid values are between %.2fHZ and %.2fHz",
		       val, mhs5200a_waveform_to_string(wtype),
		       freq_min, freq_max);
		return SR_ERR;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%df%d", ch, (int)(val * 100.0 + 0.5));
}

SR_PRIV int mhs5200a_set_waveform(const struct sr_dev_inst *sdi, int ch, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s%dw%d", ch, val);
}

SR_PRIV int mhs5200a_set_waveform_string(const struct sr_dev_inst *sdi, int ch, const char *val)
{
	enum waveform_type wtype;
	
	wtype = mhs5200a_string_to_waveform(val);
	if (wtype == WAVEFORM_UNKNOWN) {
		sr_err("Unknown waveform %s", val);
		return SR_ERR;
	}
	return mhs5200a_set_waveform(sdi, ch, wtype);
}

SR_PRIV int mhs5200a_set_amplitude(const struct sr_dev_inst *sdi, int ch, double val)
{
	long attenuation;

	if (val < 0.0 || val > 20.0) {
		sr_err("Invalid amplitude %.2fV. Supported values are between 0V and 20V", val);
		return SR_ERR;
	}
	if (mhs5200a_get_attenuation(sdi, ch, &attenuation) < 0)
		return SR_ERR;
	
	if (attenuation == ATTENUATION_MINUS_20DB) {
		val *= 1000.0;
	}  else {
		val *= 100.0;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%da%d", ch, (int)(val + 0.5));
}

SR_PRIV int mhs5200a_set_duty_cycle(const struct sr_dev_inst *sdi, int ch, double val)
{
	if (val < 0.0 || val > 100.0) {
		sr_err("Invalid duty cycle %.2f%%. Supported values are between 0%% and 100%%", val);
		return SR_ERR;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%dd%d", ch, (int)(val * 10.0 + 0.5));
}

SR_PRIV int mhs5200a_set_offset(const struct sr_dev_inst *sdi, int ch, double val)
{
	double amplitude;

	if (mhs5200a_get_amplitude(sdi, ch, &amplitude) < 0)
		return SR_ERR;
	// offset is set as a percentage and encoded with an offset of 120 for a range of -120 to 120
	val = val / amplitude * 100.0;
	if (val > 120.0 || val < -120.0) {
		sr_err("Invalid offset %.2f%%. Supported values are between -120%% and 120%% of the amplitude value", val);
		return SR_ERR;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%do%d", ch, (int)(val + 0.5 + 120.0));
}

SR_PRIV int mhs5200a_set_phase(const struct sr_dev_inst *sdi, int ch, double val)
{
	while (val >= 360.0)
		val -= 360.0;
	while (val < 0.0)
		val += 360.0;
			
	return mhs5200a_cmd_ok(sdi->conn, ":s%dp%d", ch, (int)(val + 0.5));
}

SR_PRIV int mhs5200a_set_attenuation(const struct sr_dev_inst *sdi, int ch, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s%dy%d", ch, val);
}

SR_PRIV int mhs5200a_set_onoff(const struct sr_dev_inst *sdi, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s1b%d", val ? 1 : 0);
}

SR_PRIV int mhs5200a_set_counter_onoff(const struct sr_dev_inst *sdi, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s6b%d", val);
}

SR_PRIV  int mhs5200a_set_counter_function(const struct sr_dev_inst *sdi, enum counter_function val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s%dm", val);
}

SR_PRIV int mhs5200a_set_counter_gate_time(const struct sr_dev_inst *sdi, enum gate_time val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s1g%d", val);
}

SR_PRIV int mhs5200a_get_counter_value(const struct sr_dev_inst *sdi, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r0e");
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_frequency(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val /= 10.0;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_period(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val *= 1.0e-9;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_pulse_width(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val *= 1.0e-9;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_duty_cycle(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val /= 10.0;
	return SR_OK;
}

static void mhs5200a_send_channel_value(const struct sr_dev_inst *sdi,
					struct sr_channel *ch, double value, enum sr_mq mq,
					enum sr_unit unit, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	double val;

	val = value;
	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = 1;
	analog.data = &val;
	analog.encoding->unitsize = sizeof(val);
	analog.encoding->is_float = TRUE;
	analog.encoding->digits = digits;
	analog.meaning->mq = mq;
	analog.meaning->unit = unit;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

SR_PRIV int mhs5200a_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GSList *l;
	int start_idx;
	int ret;
	double val;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	
	std_session_send_df_frame_begin(sdi);
	start_idx = 2;

	// frequency
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_FREQUENCY);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_frequency(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_FREQUENCY,
				    SR_UNIT_HERTZ, 10);

	// period
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_PERIOD);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_period(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_TIME,
				    SR_UNIT_SECOND, 10);
	
	// duty cycle
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_DUTY_CYCLE);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_duty_cycle(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_DUTY_CYCLE,
				    SR_UNIT_PERCENTAGE, 3);
	
	// pulse width
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_PULSE_WIDTH);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_pulse_width(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_PULSE_WIDTH,
				    SR_UNIT_SECOND, 10);
	

	std_session_send_df_frame_end(sdi);
	sr_sw_limits_update_samples_read(&devc->limits, 1);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
