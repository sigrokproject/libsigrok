/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <glib.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define JOB_TIMEOUT 300

#define INFINITE_INTERVAL   INT_MAX
#define SAMPLERATE_INTERVAL -1

static const struct agdmm_job *job_current(const struct dev_context *devc)
{
	return &devc->profile->jobs[devc->current_job];
}

static void job_done(struct dev_context *devc)
{
	devc->job_running = FALSE;
}

static void job_again(struct dev_context *devc)
{
	devc->job_again = TRUE;
}

static gboolean job_is_running(const struct dev_context *devc)
{
	return devc->job_running;
}

static gboolean job_in_interval(const struct dev_context *devc)
{
	int64_t job_start = devc->jobs_start[devc->current_job];
	int64_t now = g_get_monotonic_time() / 1000;
	int interval = job_current(devc)->interval;
	if (interval == SAMPLERATE_INTERVAL)
		interval = 1000 / devc->cur_samplerate;
	return (now - job_start) < interval || interval == INFINITE_INTERVAL;
}

static gboolean job_has_timeout(const struct dev_context *devc)
{
	int64_t job_start = devc->jobs_start[devc->current_job];
	int64_t now = g_get_monotonic_time() / 1000;
	return job_is_running(devc) && (now - job_start) > JOB_TIMEOUT;
}

static const struct agdmm_job *job_next(struct dev_context *devc)
{
	int current_job = devc->current_job;
	do {
		devc->current_job++;
		if (!job_current(devc)->send)
			devc->current_job = 0;
	} while(job_in_interval(devc) && devc->current_job != current_job);
	return job_current(devc);
}

static void job_run_again(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	devc->job_again = FALSE;
	devc->job_running = TRUE;
	if (job_current(devc)->send(sdi) == SR_ERR_NA)
		job_done(devc);
}

static void job_run(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int64_t now = g_get_monotonic_time() / 1000;
	devc->jobs_start[devc->current_job] = now;
	job_run_again(sdi);
}

static void dispatch(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->job_again) {
		job_run_again(sdi);
		return;
	}

	if (!job_is_running(devc))
		job_next(devc);
	else if (job_has_timeout(devc))
		job_done(devc);

	if (!job_is_running(devc) && !job_in_interval(devc))
		job_run(sdi);
}

static void receive_line(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct agdmm_recv *recvs, *recv;
	GRegex *reg;
	GMatchInfo *match;
	int i;

	devc = sdi->priv;

	/* Strip CRLF */
	while (devc->buflen) {
		if (*(devc->buf + devc->buflen - 1) == '\r'
				|| *(devc->buf + devc->buflen - 1) == '\n')
			*(devc->buf + --devc->buflen) = '\0';
		else
			break;
	}
	sr_spew("Received '%s'.", devc->buf);

	recv = NULL;
	recvs = devc->profile->recvs;
	for (i = 0; (&recvs[i])->recv_regex; i++) {
		reg = g_regex_new((&recvs[i])->recv_regex, 0, 0, NULL);
		if (g_regex_match(reg, (char *)devc->buf, 0, &match)) {
			recv = &recvs[i];
			break;
		}
		g_match_info_unref(match);
		g_regex_unref(reg);
	}
	if (recv) {
		enum job_type type = recv->recv(sdi, match);
		if (type == job_current(devc)->type)
			job_done(devc);
		else if (type == JOB_AGAIN)
			job_again(devc);
		g_match_info_unref(match);
		g_regex_unref(reg);
	} else
		sr_dbg("Unknown line '%s'.", devc->buf);

	/* Done with this. */
	devc->buflen = 0;
}

SR_PRIV int agdmm_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;
	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		while (AGDMM_BUFSIZE - devc->buflen - 1 > 0) {
			len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;
			devc->buflen += len;
			*(devc->buf + devc->buflen) = '\0';
			if (*(devc->buf + devc->buflen - 1) == '\n') {
				/* End of line */
				receive_line(sdi);
				break;
			}
		}
	}

	dispatch(sdi);

	if (sr_sw_limits_check(&devc->limits))
		sdi->driver->dev_acquisition_stop(sdi);

	return TRUE;
}

static int agdmm_send(const struct sr_dev_inst *sdi, const char *cmd, ...)
{
	struct sr_serial_dev_inst *serial;
	va_list args;
	char buf[32];

	serial = sdi->conn;

	va_start(args, cmd);
	vsnprintf(buf, sizeof(buf) - 3, cmd, args);
	va_end(args);
	sr_spew("Sending '%s'.", buf);
	if (!strncmp(buf, "*IDN?", 5))
		strcat(buf, "\r\n");
	else
		strcat(buf, "\n\r\n");
	if (serial_write_blocking(serial, buf, strlen(buf), SERIAL_WRITE_TIMEOUT_MS) < (int)strlen(buf)) {
		sr_err("Failed to send.");
		return SR_ERR;
	}

	return SR_OK;
}

static int send_stat(const struct sr_dev_inst *sdi)
{
	return agdmm_send(sdi, "STAT?");
}

static int recv_stat_u123x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *s;

	devc = sdi->priv;
	s = g_match_info_fetch(match, 1);
	sr_spew("STAT response '%s'.", s);

	/* Max, Min or Avg mode -- no way to tell which, so we'll
	 * set both flags to denote it's not a normal measurement. */
	if (s[0] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_MAX | SR_MQFLAG_MIN;
	else
		devc->cur_mqflags[0] &= ~(SR_MQFLAG_MAX | SR_MQFLAG_MIN);

	if (s[1] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_RELATIVE;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_RELATIVE;

	/* Triggered or auto hold modes. */
	if (s[2] == '1' || s[3] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_HOLD;

	/* Temp/aux mode. */
	if (s[7] == '1')
		devc->mode_tempaux = TRUE;
	else
		devc->mode_tempaux = FALSE;

	/* Continuity mode. */
	if (s[16] == '1')
		devc->mode_continuity = TRUE;
	else
		devc->mode_continuity = FALSE;

	g_free(s);

	return JOB_STAT;
}

static int recv_stat_u124x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *s;

	devc = sdi->priv;
	s = g_match_info_fetch(match, 1);
	sr_spew("STAT response '%s'.", s);

	/* Max, Min or Avg mode -- no way to tell which, so we'll
	 * set both flags to denote it's not a normal measurement. */
	if (s[0] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_MAX | SR_MQFLAG_MIN;
	else
		devc->cur_mqflags[0] &= ~(SR_MQFLAG_MAX | SR_MQFLAG_MIN);

	if (s[1] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_RELATIVE;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_RELATIVE;

	/* Hold mode. */
	if (s[7] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_HOLD;

	g_free(s);

	return JOB_STAT;
}

static int recv_stat_u125x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *s;

	devc = sdi->priv;
	s = g_match_info_fetch(match, 1);
	sr_spew("STAT response '%s'.", s);

	/* dBm/dBV modes. */
	if ((s[2] & ~0x20) == 'M')
		devc->mode_dbm_dbv = devc->cur_unit[0] = SR_UNIT_DECIBEL_MW;
	else if ((s[2] & ~0x20) == 'V')
		devc->mode_dbm_dbv = devc->cur_unit[0] = SR_UNIT_DECIBEL_VOLT;
	else
		devc->mode_dbm_dbv = 0;

	/* Peak hold mode. */
	if (s[4] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_MAX;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_MAX;

	/* Triggered hold mode. */
	if (s[7] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_HOLD;

	g_free(s);

	return JOB_STAT;
}

static int recv_stat_u128x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *s;

	devc = sdi->priv;
	s = g_match_info_fetch(match, 1);
	sr_spew("STAT response '%s'.", s);

	/* Max, Min or Avg mode -- no way to tell which, so we'll
	 * set both flags to denote it's not a normal measurement. */
	if (s[0] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_MAX | SR_MQFLAG_MIN | SR_MQFLAG_AVG;
	else
		devc->cur_mqflags[0] &= ~(SR_MQFLAG_MAX | SR_MQFLAG_MIN | SR_MQFLAG_AVG);

	/* dBm/dBV modes. */
	if ((s[2] & ~0x20) == 'M')
		devc->mode_dbm_dbv = devc->cur_unit[0] = SR_UNIT_DECIBEL_MW;
	else if ((s[2] & ~0x20) == 'V')
		devc->mode_dbm_dbv = devc->cur_unit[0] = SR_UNIT_DECIBEL_VOLT;
	else
		devc->mode_dbm_dbv = 0;

	/* Peak hold mode. */
	if (s[4] == '4')
		devc->cur_mqflags[0] |= SR_MQFLAG_MAX;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_MAX;

	/* Null function. */
	if (s[1] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_RELATIVE;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_RELATIVE;

	/* Triggered or auto hold modes. */
	if (s[7] == '1' || s[11] == '1')
		devc->cur_mqflags[0] |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags[0] &= ~SR_MQFLAG_HOLD;

	g_free(s);

	return JOB_STAT;
}

static int send_fetc(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->mode_squarewave)
		return SR_ERR_NA;

	if (devc->cur_channel->index > 0)
		return agdmm_send(sdi, "FETC? @%d", devc->cur_channel->index + 1);
	else
		return agdmm_send(sdi, "FETC?");
}

static int recv_fetc(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	float fvalue;
	const char *s;
	char *mstr;
	int i, exp;

	sr_spew("FETC reply '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;
	i = devc->cur_channel->index;

	if (devc->cur_mq[i] == -1)
		/* This detects when channel P2 is reporting TEMP as an identical
		 * copy of channel P3. In this case, we just skip P2. */
		goto skip_value;

	s = g_match_info_get_string(match);
	if (!strcmp(s, "-9.90000000E+37") || !strcmp(s, "+9.90000000E+37")) {
		/* An invalid measurement shows up on the display as "O.L", but
		 * comes through like this. Since comparing 38-digit floats
		 * is rather problematic, we'll cut through this here. */
		fvalue = NAN;
	} else {
		mstr = g_match_info_fetch(match, 1);
		if (sr_atof_ascii(mstr, &fvalue) != SR_OK) {
			g_free(mstr);
			sr_dbg("Invalid float.");
			return SR_ERR;
		}
		g_free(mstr);
		if (devc->cur_exponent[i] != 0)
			fvalue *= powf(10, devc->cur_exponent[i]);
	}

	if (devc->cur_unit[i] == SR_UNIT_DECIBEL_MW ||
	    devc->cur_unit[i] == SR_UNIT_DECIBEL_VOLT ||
	    devc->cur_unit[i] == SR_UNIT_PERCENTAGE) {
		mstr = g_match_info_fetch(match, 2);
		if (mstr && sr_atoi(mstr, &exp) == SR_OK) {
			devc->cur_digits[i] = MIN(4 - exp, devc->cur_digits[i]);
			devc->cur_encoding[i] = MIN(5 - exp, devc->cur_encoding[i]);
		}
		g_free(mstr);
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec,
	               devc->cur_digits[i] - devc->cur_exponent[i]);
	analog.meaning->mq = devc->cur_mq[i];
	analog.meaning->unit = devc->cur_unit[i];
	analog.meaning->mqflags = devc->cur_mqflags[i];
	analog.meaning->channels = g_slist_append(NULL, devc->cur_channel);
	analog.num_samples = 1;
	analog.data = &fvalue;
	encoding.digits = devc->cur_encoding[i] - devc->cur_exponent[i];
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);

	sr_sw_limits_update_samples_read(&devc->limits, 1);

skip_value:;
	struct sr_channel *prev_chan = devc->cur_channel;
	devc->cur_channel = sr_next_enabled_channel(sdi, devc->cur_channel);
	if (devc->cur_channel->index > prev_chan->index)
		return JOB_AGAIN;
	else
		return JOB_FETC;
}

static int send_conf(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	/* Do not try to send CONF? for internal temperature channel. */
	if (devc->cur_conf->index == MAX(devc->profile->nb_channels - 1, 1))
		return SR_ERR_NA;

	if (devc->cur_conf->index > 0)
		return agdmm_send(sdi, "CONF? @%d", devc->cur_conf->index + 1);
	else
		return agdmm_send(sdi, "CONF?");
}

static int recv_conf_u123x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *mstr, *rstr;
	int i, resolution;

	sr_spew("CONF? response '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;
	i = devc->cur_conf->index;

	rstr = g_match_info_fetch(match, 2);
	if (rstr)
		sr_atoi(rstr, &resolution);
	g_free(rstr);

	mstr = g_match_info_fetch(match, 1);
	if (!strcmp(mstr, "V")) {
		devc->cur_mq[i] = SR_MQ_VOLTAGE;
		devc->cur_unit[i] = SR_UNIT_VOLT;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 4 - resolution;
	} else if (!strcmp(mstr, "MV")) {
		if (devc->mode_tempaux) {
			devc->cur_mq[i] = SR_MQ_TEMPERATURE;
			/* No way to detect whether Fahrenheit or Celsius
			 * is used, so we'll just default to Celsius. */
			devc->cur_unit[i] = SR_UNIT_CELSIUS;
			devc->cur_mqflags[i] = 0;
			devc->cur_exponent[i] = 0;
			devc->cur_digits[i] = 1;
		} else {
			devc->cur_mq[i] = SR_MQ_VOLTAGE;
			devc->cur_unit[i] = SR_UNIT_VOLT;
			devc->cur_mqflags[i] = 0;
			devc->cur_exponent[i] = -3;
			devc->cur_digits[i] = 5 - resolution;
		}
	} else if (!strcmp(mstr, "A")) {
		devc->cur_mq[i] = SR_MQ_CURRENT;
		devc->cur_unit[i] = SR_UNIT_AMPERE;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 3 - resolution;
	} else if (!strcmp(mstr, "UA")) {
		devc->cur_mq[i] = SR_MQ_CURRENT;
		devc->cur_unit[i] = SR_UNIT_AMPERE;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = -6;
		devc->cur_digits[i] = 8 - resolution;
	} else if (!strcmp(mstr, "FREQ")) {
		devc->cur_mq[i] = SR_MQ_FREQUENCY;
		devc->cur_unit[i] = SR_UNIT_HERTZ;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 2 - resolution;
	} else if (!strcmp(mstr, "RES")) {
		if (devc->mode_continuity) {
			devc->cur_mq[i] = SR_MQ_CONTINUITY;
			devc->cur_unit[i] = SR_UNIT_BOOLEAN;
		} else {
			devc->cur_mq[i] = SR_MQ_RESISTANCE;
			devc->cur_unit[i] = SR_UNIT_OHM;
		}
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 1 - resolution;
	} else if (!strcmp(mstr, "DIOD")) {
		devc->cur_mq[i] = SR_MQ_VOLTAGE;
		devc->cur_unit[i] = SR_UNIT_VOLT;
		devc->cur_mqflags[i] = SR_MQFLAG_DIODE;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 3;
	} else if (!strcmp(mstr, "CAP")) {
		devc->cur_mq[i] = SR_MQ_CAPACITANCE;
		devc->cur_unit[i] = SR_UNIT_FARAD;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 9 - resolution;
	} else
		sr_dbg("Unknown first argument.");
	g_free(mstr);

	/* This is based on guess, supposing similarity with other models. */
	devc->cur_encoding[i] = devc->cur_digits[i] + 1;

	if (g_match_info_get_match_count(match) == 4) {
		mstr = g_match_info_fetch(match, 3);
		/* Third value, if present, is always AC or DC. */
		if (!strcmp(mstr, "AC")) {
			devc->cur_mqflags[i] |= SR_MQFLAG_AC;
			if (devc->cur_mq[i] == SR_MQ_VOLTAGE)
				devc->cur_mqflags[i] |= SR_MQFLAG_RMS;
		} else if (!strcmp(mstr, "DC")) {
			devc->cur_mqflags[i] |= SR_MQFLAG_DC;
		} else {
		sr_dbg("Unknown first argument '%s'.", mstr);
		}
		g_free(mstr);
	} else
		devc->cur_mqflags[i] &= ~(SR_MQFLAG_AC | SR_MQFLAG_DC);

	return JOB_CONF;
}

static int recv_conf_u124x_5x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *mstr, *rstr, *m2;
	int i, resolution;

	sr_spew("CONF? response '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;
	i = devc->cur_conf->index;

	devc->mode_squarewave = 0;

	rstr = g_match_info_fetch(match, 4);
	if (rstr && sr_atoi(rstr, &resolution) == SR_OK) {
		devc->cur_digits[i] = -resolution;
		devc->cur_encoding[i] = -resolution + 1;
	}
	g_free(rstr);

	mstr = g_match_info_fetch(match, 1);
	if (!strncmp(mstr, "VOLT", 4)) {
		devc->cur_mq[i] = SR_MQ_VOLTAGE;
		devc->cur_unit[i] = SR_UNIT_VOLT;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		if (i == 0 && devc->mode_dbm_dbv) {
			devc->cur_unit[i] = devc->mode_dbm_dbv;
			devc->cur_digits[i] = 3;
			devc->cur_encoding[i] = 4;
		}
		if (mstr[4] == ':') {
			if (!strncmp(mstr + 5, "ACDC", 4)) {
				/* AC + DC offset */
				devc->cur_mqflags[i] |= SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS;
			} else if (!strncmp(mstr + 5, "AC", 2)) {
				devc->cur_mqflags[i] |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
			} else if (!strncmp(mstr + 5, "DC", 2)) {
				devc->cur_mqflags[i] |= SR_MQFLAG_DC;
			}
		} else
			devc->cur_mqflags[i] |= SR_MQFLAG_DC;
	} else if (!strncmp(mstr, "CURR", 4)) {
		devc->cur_mq[i] = SR_MQ_CURRENT;
		devc->cur_unit[i] = SR_UNIT_AMPERE;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		if (mstr[4] == ':') {
			if (!strncmp(mstr + 5, "ACDC", 4)) {
				/* AC + DC offset */
				devc->cur_mqflags[i] |= SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS;
			} else if (!strncmp(mstr + 5, "AC", 2)) {
				devc->cur_mqflags[i] |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
			} else if (!strncmp(mstr + 5, "DC", 2)) {
				devc->cur_mqflags[i] |= SR_MQFLAG_DC;
			}
		} else
			devc->cur_mqflags[i] |= SR_MQFLAG_DC;
	} else if (!strcmp(mstr, "RES")) {
		devc->cur_mq[i] = SR_MQ_RESISTANCE;
		devc->cur_unit[i] = SR_UNIT_OHM;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
	} else if (!strcmp(mstr, "COND")) {
		devc->cur_mq[i] = SR_MQ_CONDUCTANCE;
		devc->cur_unit[i] = SR_UNIT_SIEMENS;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
	} else if (!strcmp(mstr, "CAP")) {
		devc->cur_mq[i] = SR_MQ_CAPACITANCE;
		devc->cur_unit[i] = SR_UNIT_FARAD;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
	} else if (!strncmp(mstr, "FREQ", 4) || !strncmp(mstr, "FC1", 3)) {
		devc->cur_mq[i] = SR_MQ_FREQUENCY;
		devc->cur_unit[i] = SR_UNIT_HERTZ;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
	} else if (!strncmp(mstr, "PULS:PWID", 9)) {
		devc->cur_mq[i] = SR_MQ_PULSE_WIDTH;
		devc->cur_unit[i] = SR_UNIT_SECOND;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_encoding[i] = MIN(devc->cur_encoding[i], 6);
	} else if (!strncmp(mstr, "PULS:PDUT", 9)) {
		devc->cur_mq[i] = SR_MQ_DUTY_CYCLE;
		devc->cur_unit[i] = SR_UNIT_PERCENTAGE;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 3;
		devc->cur_encoding[i] = 4;
	} else if (!strcmp(mstr, "CONT")) {
		devc->cur_mq[i] = SR_MQ_CONTINUITY;
		devc->cur_unit[i] = SR_UNIT_OHM;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
	} else if (!strcmp(mstr, "DIOD")) {
		devc->cur_mq[i] = SR_MQ_VOLTAGE;
		devc->cur_unit[i] = SR_UNIT_VOLT;
		devc->cur_mqflags[i] = SR_MQFLAG_DIODE;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 4;
		devc->cur_encoding[i] = 5;
	} else if (!strncmp(mstr, "T1", 2) || !strncmp(mstr, "T2", 2) ||
		   !strncmp(mstr, "TEMP", 2)) {
		devc->cur_mq[i] = SR_MQ_TEMPERATURE;
		m2 = g_match_info_fetch(match, 2);
		if (!m2)
			/*
			 * TEMP without param is for secondary display (channel P2)
			 * and is identical to channel P3, so discard it.
			 */
			devc->cur_mq[i] = -1;
		else if (!strcmp(m2, "FAR"))
			devc->cur_unit[i] = SR_UNIT_FAHRENHEIT;
		else
			devc->cur_unit[i] = SR_UNIT_CELSIUS;
		g_free(m2);
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 1;
		devc->cur_encoding[i] = 2;
	} else if (!strcmp(mstr, "SCOU")) {
		/*
		 * Switch counter, not supported. Not sure what values
		 * come from FETC in this mode, or how they would map
		 * into libsigrok.
		 */
	} else if (!strncmp(mstr, "CPER:", 5)) {
		devc->cur_mq[i] = SR_MQ_CURRENT;
		devc->cur_unit[i] = SR_UNIT_PERCENTAGE;
		devc->cur_mqflags[i] = 0;
		devc->cur_exponent[i] = 0;
		devc->cur_digits[i] = 2;
		devc->cur_encoding[i] = 3;
	} else if (!strcmp(mstr, "SQU")) {
		/*
		 * Square wave output, not supported. FETC just return
		 * an error in this mode, so don't even call it.
		 */
		devc->mode_squarewave = 1;
	} else {
		sr_dbg("Unknown first argument '%s'.", mstr);
	}
	g_free(mstr);

	struct sr_channel *prev_conf = devc->cur_conf;
	devc->cur_conf = sr_next_enabled_channel(sdi, devc->cur_conf);
	if (devc->cur_conf->index == MAX(devc->profile->nb_channels - 1, 1))
		devc->cur_conf = sr_next_enabled_channel(sdi, devc->cur_conf);
	if (devc->cur_conf->index > prev_conf->index)
		return JOB_AGAIN;
	else
		return JOB_CONF;
}

/* This comes in whenever the rotary switch is changed to a new position.
 * We could use it to determine the major measurement mode, but we already
 * have the output of CONF? for that, which is more detailed. However
 * we do need to catch this here, or it'll show up in some other output. */
static int recv_switch(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc = sdi->priv;

	sr_spew("Switch '%s'.", g_match_info_get_string(match));

	devc->current_job = 0;
	devc->job_running = FALSE;
	memset(devc->jobs_start, 0, sizeof(devc->jobs_start));
	devc->cur_mq[0] = -1;
	if (devc->profile->nb_channels > 2)
		devc->cur_mq[1] = -1;

	return SR_OK;
}

/* Poll CONF/STAT at 1Hz and values at samplerate. */
SR_PRIV const struct agdmm_job agdmm_jobs_u12xx[] = {
	{ JOB_FETC, SAMPLERATE_INTERVAL, send_fetc },
	{ JOB_CONF,                1000, send_conf },
	{ JOB_STAT,                1000, send_stat },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u123x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u123x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", recv_fetc },
	{ "^\"(V|MV|A|UA|FREQ),(\\d),(AC|DC)\"$", recv_conf_u123x },
	{ "^\"(RES|CAP),(\\d)\"$", recv_conf_u123x},
	{ "^\"(DIOD)\"$", recv_conf_u123x },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u124x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u124x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", recv_fetc },
	{ "^\"(VOLT|CURR|RES|CAP|FREQ) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(VOLT:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(CURR:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(CPER:[40]-20mA) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(T[0-9]:[A-Z]+) ([A-Z]+)\"$", recv_conf_u124x_5x },
	{ "^\"(DIOD)\"$", recv_conf_u124x_5x },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u125x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u125x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", recv_fetc },
	{ "^\"(VOLT|CURR|RES|CAP|FREQ) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(VOLT:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(CURR:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(CPER:[40]-20mA) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(T[0-9]:[A-Z]+) ([A-Z]+)\"$", recv_conf_u124x_5x },
	{ "^\"(DIOD)\"$", recv_conf_u124x_5x },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u128x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u128x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))$", recv_fetc },
	{ "^\"(VOLT|CURR|RES|CONT|COND|CAP|FREQ|FC1|FC100) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(VOLT:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(CURR:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(FREQ:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(CPER:[40]-20mA) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(PULS:PWID|PULS:PWID:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9]\\.[0-9]{8}E([-+][0-9]{2}))\"$", recv_conf_u124x_5x },
	{ "^\"(TEMP:[A-Z]+) ([A-Z]+)\"$", recv_conf_u124x_5x },
	{ "^\"(DIOD|SQU|PULS:PDUT|TEMP)\"$", recv_conf_u124x_5x },
	ALL_ZERO
};
