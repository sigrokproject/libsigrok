/*
 * This file is part of the sigrok project.
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

#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "config.h"
#include "agilent-dmm.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>


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
			sr_spew("agilent-dmm: running job %d", i);
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
	sr_spew("agilent-dmm: received '%s'", devc->buf);

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
	}

	/* Done with this. */
	devc->buflen = 0;

}

SR_PRIV int agdmm_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int len;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		len = AGDMM_BUFSIZE - devc->buflen - 1;
		if (len > 0) {
			len = serial_read(fd, devc->buf + devc->buflen, len);
			if (len > 0) {
				devc->buflen += len;
				*(devc->buf + devc->buflen) = '\0';
				if (devc->buflen > 0 && *(devc->buf + devc->buflen - 1) == '\n')
					/* End of line */
					receive_line(sdi);
			}
		}
	}

	dispatch(sdi);

	if (devc->num_samples >= devc->limit_samples)
		sdi->driver->dev_acquisition_stop(sdi, cb_data);

	return TRUE;
}

static int agdmm_send(const struct sr_dev_inst *sdi, const char *cmd)
{
	struct dev_context *devc;
	char buf[32];

	devc = sdi->priv;
	sr_spew("agilent-dmm: sending '%s'", cmd);
	strncpy(buf, cmd, 28);
	if (!strncmp(buf, "*IDN?", 5))
		strncat(buf, "\r\n", 32);
	else
		strncat(buf, "\n\r\n", 32);
	if (serial_write(devc->serial->fd, buf, strlen(buf)) == -1) {
		sr_err("agilent-dmm: failed to send: %s", strerror(errno));
		return SR_ERR;
	}
	
	return SR_OK;
}

static int agdmm_ident_send(const struct sr_dev_inst *sdi)
{

	return agdmm_send(sdi, "*IDN?");
}

static int agdmm_ident_recv(const struct sr_dev_inst *sdi, GMatchInfo *match)
{

	(void)sdi;

	sr_spew("got ident '%s'", g_match_info_get_string(match));

	return SR_OK;
}

static int agdmm_stat_send(const struct sr_dev_inst *sdi)
{

	return agdmm_send(sdi, "STAT?");
}

static int agdmm_stat_recv(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *s;

	devc = sdi->priv;
	s = g_match_info_fetch(match, 1);
	sr_spew("got stat '%s'", s);

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

	/*  Continuity mode. */
	if (s[16] == '1')
		devc->mode_continuity = TRUE;
	else
		devc->mode_continuity = FALSE;

	g_free(s);

	return SR_OK;
}

SR_PRIV int agdmm_fetc_send(const struct sr_dev_inst *sdi)
{

	return agdmm_send(sdi, "FETC?");
}

SR_PRIV int agdmm_fetc_recv(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	float fvalue;
	char *mstr, *eptr;

	sr_spew("agilent-dmm: FETC reply '%s'", g_match_info_get_string(match));
	devc = sdi->priv;

	if (devc->cur_mq == -1)
		/* Haven't seen configuration yet, so can't know what
		 * the fetched float means. Not really an error, we'll
		 * get metadata soon enough. */
		return SR_OK;

	if (!strcmp(g_match_info_get_string(match), "+9.90000000E+37")) {
		/* An invalid measurement shows up on the display as "O.L, but
		 * comes through like this. Since comparing 38-digit floats
		 * is rather problematic, we'll cut through this here. */
		fvalue = NAN;
	} else {
		mstr = g_match_info_fetch(match, 1);
		fvalue = strtof(mstr, &eptr);
		g_free(mstr);
		if (fvalue == 0.0 && eptr == mstr) {
			sr_err("agilent-dmm: invalid float");
			return SR_ERR;
		}
		if (devc->cur_divider > 0)
			fvalue /= devc->cur_divider;
	}

	memset(&analog, 0, sizeof(struct sr_datafeed_analog));
	analog.mq = devc->cur_mq;
	analog.unit = devc->cur_unit;
	analog.mqflags = devc->cur_mqflags;
	analog.num_samples = 1;
	analog.data = &fvalue;
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->cb_data, &packet);

	devc->num_samples++;

	return SR_OK;
}

SR_PRIV int agdmm_conf_send(const struct sr_dev_inst *sdi)
{

	return agdmm_send(sdi, "CONF?");
}

SR_PRIV int agdmm_conf_recv(const struct sr_dev_inst *sdi, GMatchInfo *match)
{
	struct dev_context *devc;
	char *mstr;

	sr_spew("got conf '%s'",  g_match_info_get_string(match));
	devc = sdi->priv;
	mstr = g_match_info_fetch(match, 1);
	if (!strcmp(mstr, "V")) {
		devc->cur_mq = SR_MQ_VOLTAGE;
		devc->cur_unit = SR_UNIT_VOLT;
		devc->cur_mqflags = 0;
		devc->cur_divider = 0;
	} else if(!strcmp(mstr, "MV")) {
		if (devc->mode_tempaux) {
			devc->cur_mq = SR_MQ_TEMPERATURE;
			/* No way to detect whether Fahrenheit or Celcius
			 * is used, so we'll just default to Celcius. */
			devc->cur_unit = SR_UNIT_CELSIUS;
		devc->cur_mqflags = 0;
		devc->cur_divider = 0;
		} else {
			devc->cur_mq = SR_MQ_VOLTAGE;
			devc->cur_unit = SR_UNIT_VOLT;
			devc->cur_mqflags = 0;
			devc->cur_divider = 1000;
		}
	} else if(!strcmp(mstr, "A")) {
		devc->cur_mq = SR_MQ_CURRENT;
		devc->cur_unit = SR_UNIT_AMPERE;
		devc->cur_mqflags = 0;
		devc->cur_divider = 0;
	} else if(!strcmp(mstr, "UA")) {
		devc->cur_mq = SR_MQ_CURRENT;
		devc->cur_unit = SR_UNIT_AMPERE;
		devc->cur_mqflags = 0;
		devc->cur_divider = 1000000;
	} else if(!strcmp(mstr, "FREQ")) {
		devc->cur_mq = SR_MQ_FREQUENCY;
		devc->cur_unit = SR_UNIT_HERTZ;
		devc->cur_mqflags = 0;
		devc->cur_divider = 0;
	} else if(!strcmp(mstr, "RES")) {
		if (devc->mode_continuity) {
			devc->cur_mq = SR_MQ_CONTINUITY;
			devc->cur_unit = SR_UNIT_BOOLEAN;
		} else {
			devc->cur_mq = SR_MQ_RESISTANCE;
			devc->cur_unit = SR_UNIT_OHM;
		}
		devc->cur_mqflags = 0;
		devc->cur_divider = 0;
	} else if(!strcmp(mstr, "CAP")) {
		devc->cur_mq = SR_MQ_CAPACITANCE;
		devc->cur_unit = SR_UNIT_FARAD;
		devc->cur_mqflags = 0;
		devc->cur_divider = 0;
	} else if(!strcmp(mstr, "DIOD")) {
		devc->cur_mq = SR_MQ_VOLTAGE;
		devc->cur_unit = SR_UNIT_VOLT;
		devc->cur_mqflags = SR_MQFLAG_DIODE;
		devc->cur_divider = 0;
	} else
		sr_dbg("agilent-dmm: unknown first argument");
	g_free(mstr);

	if (g_match_info_get_match_count(match) == 3) {
		mstr = g_match_info_fetch(match, 1);
		/* Third value, if present, is always AC or DC. */
		if (!strcmp(mstr, "AC"))
			devc->cur_mqflags |= SR_MQFLAG_AC;
		else if (!strcmp(mstr, "DC"))
			devc->cur_mqflags |= SR_MQFLAG_DC;
		else
			sr_dbg("agilent-dmm: unknown third argument");
		g_free(mstr);
	} else
		devc->cur_mqflags &= ~(SR_MQFLAG_AC | SR_MQFLAG_DC);

	return SR_OK;
}

SR_PRIV int agdmm_switch_recv(const struct sr_dev_inst *sdi, GMatchInfo *match)
{

	(void)sdi;

	sr_spew("got switch '%s'",  g_match_info_get_string(match));

	return SR_OK;
}


SR_PRIV const struct agdmm_job u123x_jobs[] = {
	{ 1000, agdmm_ident_send },
	{ 143, agdmm_stat_send },
	{ 1000, agdmm_conf_send },
	{ 143, agdmm_fetc_send },
	{ 0, NULL }
};

SR_PRIV const struct agdmm_recv u123x_recvs[] = {
	{ "^\"(\\d\\d.{18}\\d)\"$", agdmm_stat_recv },
	{ "^\\*([0-9])$", agdmm_switch_recv },
	{ "^([-+][0-9]\\.[0-9]{8}E[-+][0-9]{2})$", agdmm_fetc_recv },
	{ "^\"(V|MV|A|UA|FREQ),(\\d),(AC|DC)\"$", agdmm_conf_recv },
	{ "^\"(RES|CAP),(\\d)\"$", agdmm_conf_recv },
	{ "^\"(DIOD)\"$", agdmm_conf_recv },
	{ NULL, NULL }
};


