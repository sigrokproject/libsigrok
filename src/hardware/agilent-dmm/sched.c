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
#include <string.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "agilent-dmm.h"

static void dispatch(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const struct agdmm_job *jobs;
	int64_t now;
	int i;

	devc = sdi->priv;
	jobs = devc->profile->jobs;
	now = g_get_monotonic_time() / 1000;
	for (i = 0; (&jobs[i])->interval; i++) {
		if (now - devc->jobqueue[i] > (&jobs[i])->interval) {
			sr_spew("Running job %d.", i);
			(&jobs[i])->send(sdi);
			devc->jobqueue[i] = now;
		}
	}
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
		recv->recv(sdi, match);
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

static int agdmm_send(const struct sr_dev_inst *sdi, const char *cmd)
{
	struct sr_serial_dev_inst *serial;
	char buf[32];

	serial = sdi->conn;

	sr_spew("Sending '%s'.", cmd);
	strncpy(buf, cmd, 28);
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
		devc->cur_mqflags |= SR_MQFLAG_MAX | SR_MQFLAG_MIN;
	else
		devc->cur_mqflags &= ~(SR_MQFLAG_MAX | SR_MQFLAG_MIN);

	if (s[1] == '1')
		devc->cur_mqflags |= SR_MQFLAG_RELATIVE;
	else
		devc->cur_mqflags &= ~SR_MQFLAG_RELATIVE;

	/* Triggered or auto hold modes. */
	if (s[2] == '1' || s[3] == '1')
		devc->cur_mqflags |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags &= ~SR_MQFLAG_HOLD;

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

	return SR_OK;
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
		devc->cur_mqflags |= SR_MQFLAG_MAX | SR_MQFLAG_MIN;
	else
		devc->cur_mqflags &= ~(SR_MQFLAG_MAX | SR_MQFLAG_MIN);

	if (s[1] == '1')
		devc->cur_mqflags |= SR_MQFLAG_RELATIVE;
	else
		devc->cur_mqflags &= ~SR_MQFLAG_RELATIVE;

	/* Hold mode. */
	if (s[7] == '1')
		devc->cur_mqflags |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags &= ~SR_MQFLAG_HOLD;

	g_free(s);

	return SR_OK;
}

static int recv_stat_u125x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *s;

	devc = sdi->priv;
	s = g_match_info_fetch(match, 1);
	sr_spew("STAT response '%s'.", s);

	/* Peak hold mode. */
	if (s[4] == '1')
		devc->cur_mqflags |= SR_MQFLAG_MAX;
	else
		devc->cur_mqflags &= ~SR_MQFLAG_MAX;

	/* Triggered hold mode. */
	if (s[7] == '1')
		devc->cur_mqflags |= SR_MQFLAG_HOLD;
	else
		devc->cur_mqflags &= ~SR_MQFLAG_HOLD;

	g_free(s);

	return SR_OK;
}

static int send_fetc(const struct sr_dev_inst *sdi)
{
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

	sr_spew("FETC reply '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;

	if (devc->cur_mq == -1)
		/* Haven't seen configuration yet, so can't know what
		 * the fetched float means. Not really an error, we'll
		 * get metadata soon enough. */
		return SR_OK;

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
		if (devc->cur_exponent != 0)
			fvalue *= powf(10, devc->cur_exponent);
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	analog.meaning->mq = devc->cur_mq;
	analog.meaning->unit = devc->cur_unit;
	analog.meaning->mqflags = devc->cur_mqflags;
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &fvalue;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	sr_sw_limits_update_samples_read(&devc->limits, 1);

	return SR_OK;
}

static int send_conf(const struct sr_dev_inst *sdi)
{
	return agdmm_send(sdi, "CONF?");
}

static int recv_conf_u123x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *mstr;

	sr_spew("CONF? response '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;
	mstr = g_match_info_fetch(match, 1);
	if (!strcmp(mstr, "V")) {
		devc->cur_mq = SR_MQ_VOLTAGE;
		devc->cur_unit = SR_UNIT_VOLT;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "MV")) {
		if (devc->mode_tempaux) {
			devc->cur_mq = SR_MQ_TEMPERATURE;
			/* No way to detect whether Fahrenheit or Celsius
			 * is used, so we'll just default to Celsius. */
			devc->cur_unit = SR_UNIT_CELSIUS;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
		} else {
			devc->cur_mq = SR_MQ_VOLTAGE;
			devc->cur_unit = SR_UNIT_VOLT;
			devc->cur_mqflags = 0;
			devc->cur_exponent = -3;
		}
	} else if (!strcmp(mstr, "A")) {
		devc->cur_mq = SR_MQ_CURRENT;
		devc->cur_unit = SR_UNIT_AMPERE;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "UA")) {
		devc->cur_mq = SR_MQ_CURRENT;
		devc->cur_unit = SR_UNIT_AMPERE;
		devc->cur_mqflags = 0;
		devc->cur_exponent = -6;
	} else if (!strcmp(mstr, "FREQ")) {
		devc->cur_mq = SR_MQ_FREQUENCY;
		devc->cur_unit = SR_UNIT_HERTZ;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "RES")) {
		if (devc->mode_continuity) {
			devc->cur_mq = SR_MQ_CONTINUITY;
			devc->cur_unit = SR_UNIT_BOOLEAN;
		} else {
			devc->cur_mq = SR_MQ_RESISTANCE;
			devc->cur_unit = SR_UNIT_OHM;
		}
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "CAP")) {
		devc->cur_mq = SR_MQ_CAPACITANCE;
		devc->cur_unit = SR_UNIT_FARAD;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else
		sr_dbg("Unknown first argument.");
	g_free(mstr);

	if (g_match_info_get_match_count(match) == 4) {
		mstr = g_match_info_fetch(match, 3);
		/* Third value, if present, is always AC or DC. */
		if (!strcmp(mstr, "AC")) {
			devc->cur_mqflags |= SR_MQFLAG_AC;
			if (devc->cur_mq == SR_MQ_VOLTAGE)
				devc->cur_mqflags |= SR_MQFLAG_RMS;
		} else if (!strcmp(mstr, "DC")) {
			devc->cur_mqflags |= SR_MQFLAG_DC;
		} else {
		sr_dbg("Unknown first argument '%s'.", mstr);
		}
		g_free(mstr);
	} else
		devc->cur_mqflags &= ~(SR_MQFLAG_AC | SR_MQFLAG_DC);

	return SR_OK;
}

static int recv_conf_u124x_5x(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *mstr, *m2;

	sr_spew("CONF? response '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;
	mstr = g_match_info_fetch(match, 1);
	if (!strncmp(mstr, "VOLT", 4)) {
		devc->cur_mq = SR_MQ_VOLTAGE;
		devc->cur_unit = SR_UNIT_VOLT;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
		if (mstr[4] == ':') {
			if (!strncmp(mstr + 5, "AC", 2)) {
				devc->cur_mqflags |= SR_MQFLAG_AC | SR_MQFLAG_RMS;
			} else if (!strncmp(mstr + 5, "DC", 2)) {
				devc->cur_mqflags |= SR_MQFLAG_DC;
			} else if (!strncmp(mstr + 5, "ACDC", 4)) {
				/* AC + DC offset */
				devc->cur_mqflags |= SR_MQFLAG_AC | SR_MQFLAG_DC | SR_MQFLAG_RMS;
			} else {
				devc->cur_mqflags &= ~(SR_MQFLAG_AC | SR_MQFLAG_DC);
			}
		} else
			devc->cur_mqflags &= ~(SR_MQFLAG_AC | SR_MQFLAG_DC);
	} else if (!strcmp(mstr, "CURR")) {
		devc->cur_mq = SR_MQ_CURRENT;
		devc->cur_unit = SR_UNIT_AMPERE;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "RES")) {
		devc->cur_mq = SR_MQ_RESISTANCE;
		devc->cur_unit = SR_UNIT_OHM;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "CAP")) {
		devc->cur_mq = SR_MQ_CAPACITANCE;
		devc->cur_unit = SR_UNIT_FARAD;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "FREQ")) {
		devc->cur_mq = SR_MQ_FREQUENCY;
		devc->cur_unit = SR_UNIT_HERTZ;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "CONT")) {
		devc->cur_mq = SR_MQ_CONTINUITY;
		devc->cur_unit = SR_UNIT_BOOLEAN;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strncmp(mstr, "T1", 2) || !strncmp(mstr, "T2", 2)) {
		devc->cur_mq = SR_MQ_TEMPERATURE;
		m2 = g_match_info_fetch(match, 2);
		if (!strcmp(m2, "FAR"))
			devc->cur_unit = SR_UNIT_FAHRENHEIT;
		else
			devc->cur_unit = SR_UNIT_CELSIUS;
		g_free(m2);
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else if (!strcmp(mstr, "SCOU")) {
		/*
		 * Switch counter, not supported. Not sure what values
		 * come from FETC in this mode, or how they would map
		 * into libsigrok.
		 */
	} else if (!strncmp(mstr, "CPER:", 5)) {
		devc->cur_mq = SR_MQ_CURRENT;
		devc->cur_unit = SR_UNIT_PERCENTAGE;
		devc->cur_mqflags = 0;
		devc->cur_exponent = 0;
	} else {
		sr_dbg("Unknown first argument '%s'.", mstr);
	}
	g_free(mstr);

	return SR_OK;
}

static int recv_conf(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *mstr;

	sr_spew("CONF? response '%s'.", g_match_info_get_string(match));
	devc = sdi->priv;
	mstr = g_match_info_fetch(match, 1);
	if (!strcmp(mstr, "DIOD")) {
		devc->cur_mq = SR_MQ_VOLTAGE;
		devc->cur_unit = SR_UNIT_VOLT;
		devc->cur_mqflags = SR_MQFLAG_DIODE;
		devc->cur_exponent = 0;
	} else
		sr_dbg("Unknown single argument.");
	g_free(mstr);

	return SR_OK;
}

/* This comes in whenever the rotary switch is changed to a new position.
 * We could use it to determine the major measurement mode, but we already
 * have the output of CONF? for that, which is more detailed. However
 * we do need to catch this here, or it'll show up in some other output. */
static int recv_switch(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	(void)sdi;

	sr_spew("Switch '%s'.", g_match_info_get_string(match));

	return SR_OK;
}

/* Poll keys/switches and values at 7Hz, mode at 1Hz. */
SR_PRIV const struct agdmm_job agdmm_jobs_u12xx[] = {
	{ 143, send_stat },
	{ 1000, send_conf },
	{ 143, send_fetc },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u123x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u123x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", recv_fetc },
	{ "^\"(V|MV|A|UA|FREQ),(\\d),(AC|DC)\"$", recv_conf_u123x },
	{ "^\"(RES|CAP),(\\d)\"$", recv_conf_u123x},
	{ "^\"(DIOD)\"$", recv_conf },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u124x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u124x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", recv_fetc },
	{ "^\"(VOLT|CURR|RES|CAP|FREQ) ([-+][0-9\\.E\\-+]+),([-+][0-9\\.E\\-+]+)\"$", recv_conf_u124x_5x },
	{ "^\"(VOLT:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9\\.E\\-+]+)\"$", recv_conf_u124x_5x },
	{ "^\"(CPER:[40]-20mA) ([-+][0-9\\.E\\-+]+),([-+][0-9\\.E\\-+]+)\"$", recv_conf_u124x_5x },
	{ "^\"(T[0-9]:[A-Z]+) ([A-Z]+)\"$", recv_conf_u124x_5x },
	{ "^\"(DIOD)\"$", recv_conf },
	ALL_ZERO
};

SR_PRIV const struct agdmm_recv agdmm_recvs_u125x[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", recv_stat_u125x },
	{ "^\\*([0-9])$", recv_switch },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", recv_fetc },
	{ "^\"(VOLT|CURR|RES|CAP|FREQ) ([-+][0-9\\.E\\-+]+),([-+][0-9\\.E\\-+]+)\"$", recv_conf_u124x_5x },
	{ "^\"(VOLT:[ACD]+) ([-+][0-9\\.E\\-+]+),([-+][0-9\\.E\\-+]+)\"$", recv_conf_u124x_5x },
	{ "^\"(CPER:[40]-20mA) ([-+][0-9\\.E\\-+]+),([-+][0-9\\.E\\-+]+)\"$", recv_conf_u124x_5x },
	{ "^\"(T[0-9]:[A-Z]+) ([A-Z]+)\"$", recv_conf_u124x_5x },
	{ "^\"(DIOD)\"$", recv_conf },
	ALL_ZERO
};
