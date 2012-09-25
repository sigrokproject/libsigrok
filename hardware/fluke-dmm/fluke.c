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
#include "fluke-dmm.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>


static struct sr_datafeed_analog *handle_qm_v2(const struct sr_dev_inst *sdi,
		char **tokens)
{
	struct sr_datafeed_analog *analog;
	float fvalue;
	char *eptr;

	(void)sdi;
	fvalue = strtof(tokens[0], &eptr);
	if (fvalue == 0.0 && eptr == tokens[0]) {
		sr_err("fluke-dmm: invalid float");
		return NULL;
	}

	analog = g_try_malloc0(sizeof(struct sr_datafeed_analog));
	analog->num_samples = 1;
	analog->data = g_try_malloc(sizeof(float));
	*analog->data = fvalue;
	analog->mq = -1;

	if (!strcmp(tokens[1], "VAC") || !strcmp(tokens[1], "VDC")) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
		if (!strcmp(tokens[2], "NORMAL")) {
			if (tokens[1][1] == 'A') {
				analog->mqflags |= SR_MQFLAG_AC;
				analog->mqflags |= SR_MQFLAG_RMS;
			} else
				analog->mqflags |= SR_MQFLAG_DC;
		} else if (!strcmp(tokens[2], "OL") || !strcmp(tokens[2], "OL_MINUS")) {
			*analog->data = NAN;
		} else
			analog->mq = -1;
	} if (!strcmp(tokens[1], "CEL") || !strcmp(tokens[1], "FAR")) {
		if (!strcmp(tokens[2], "NORMAL")) {
			analog->mq = SR_MQ_TEMPERATURE;
			if (tokens[1][0] == 'C')
			   analog->unit = SR_UNIT_CELSIUS;
			else
				analog->unit = SR_UNIT_FAHRENHEIT;
		}
	} if (!strcmp(tokens[1], "OHM")) {
		if (!strcmp(tokens[3], "NONE")) {
			analog->mq = SR_MQ_RESISTANCE;
			analog->unit = SR_UNIT_OHM;
			if (!strcmp(tokens[2], "OL") || !strcmp(tokens[2], "OL_MINUS")) {
				*analog->data = INFINITY;
			} else if (strcmp(tokens[2], "NORMAL"))
				analog->mq = -1;
		} else if (!strcmp(tokens[3], "OPEN_CIRCUIT")) {
			analog->mq = SR_MQ_CONTINUITY;
			analog->unit = SR_UNIT_BOOLEAN;
			*analog->data = 0.0;
		} else if (!strcmp(tokens[3], "SHORT_CIRCUIT")) {
			analog->mq = SR_MQ_CONTINUITY;
			analog->unit = SR_UNIT_BOOLEAN;
			*analog->data = 1.0;
		}
	} if (!strcmp(tokens[1], "F")
			&& !strcmp(tokens[2], "NORMAL")
			&& !strcmp(tokens[3], "NONE")) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	} else if (!strcmp(tokens[1], "AAC") || !strcmp(tokens[1], "ADC")) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
		analog->mqflags = SR_MQFLAG_RMS;
		if (!strcmp(tokens[2], "NORMAL")) {
			if (tokens[1][1] == 'A') {
				analog->mqflags |= SR_MQFLAG_AC;
				analog->mqflags |= SR_MQFLAG_RMS;
			} else
				analog->mqflags |= SR_MQFLAG_DC;
		} else if (!strcmp(tokens[2], "OL") || !strcmp(tokens[2], "OL_MINUS")) {
			*analog->data = NAN;
		} else
			analog->mq = -1;
	} if (!strcmp(tokens[1], "Hz") && !strcmp(tokens[2], "NORMAL")) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	} if (!strcmp(tokens[1], "PCT") && !strcmp(tokens[2], "NORMAL")) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	} if (!strcmp(tokens[1], "S") && !strcmp(tokens[2], "NORMAL")) {
		analog->mq = SR_MQ_PULSE_WIDTH;
		analog->unit = SR_UNIT_SECOND;
	} if (!strcmp(tokens[1], "SIE") && !strcmp(tokens[2], "NORMAL")) {
		analog->mq = SR_MQ_CONDUCTANCE;
		analog->unit = SR_UNIT_SIEMENS;
	}


	if (analog->mq == -1) {
		/* Not a valid measurement. */
		g_free(analog->data);
		g_free(analog);
		analog = NULL;
	}

	return analog;
}

static void handle_line(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog *analog;
	char **tokens;

	devc = sdi->priv;
	sr_spew("fluke-dmm: received line '%s' (%d)", devc->buf, devc->buflen);

	if (devc->buflen == 1) {
		if (devc->buf[0] != '0') {
			/* Not just a CMD_ACK from the query command. */
			sr_dbg("fluke-dmm: got CMD_ACK '%c'", devc->buf[0]);
			devc->expect_response = FALSE;
		}
		devc->buflen = 0;
		return;
	}

	analog = NULL;
	tokens = g_strsplit(devc->buf, ",", 0);
	if (devc->profile->model == FLUKE_187) {
		if (!strcmp(tokens[0], "QM")) {
			/* Yes, this is the response to the query. */
			devc->expect_response = FALSE;
			sr_spew("187 line");
			/* TODO */
			devc->buflen = 0;
		}
	} else if (devc->profile->model == FLUKE_287) {
		devc->expect_response = FALSE;
		analog = handle_qm_v2(sdi, tokens);
	}
	g_strfreev(tokens);
	devc->buflen = 0;

	if (analog) {
		/* Got a measurement. */
		sr_dbg("val %f", *analog->data);
		packet.type = SR_DF_ANALOG;
		packet.payload = analog;
		sr_session_send(devc->cb_data, &packet);
		devc->num_samples++;
		g_free(analog->data);
		g_free(analog);
	}

}

SR_PRIV int fluke_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int len;
	int64_t now, elapsed;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* Serial data arrived. */
		while(FLUKEDMM_BUFSIZE - devc->buflen - 1 > 0) {
			len = serial_read(fd, devc->buf + devc->buflen, 1);
			if (len < 1)
				break;
			devc->buflen++;
			*(devc->buf + devc->buflen) = '\0';
			if (*(devc->buf + devc->buflen - 1) == '\r') {
				*(devc->buf + --devc->buflen) = '\0';
				handle_line(sdi);
				break;
			}
		}
	}

	if (devc->num_samples >= devc->limit_samples) {
		sdi->driver->dev_acquisition_stop(sdi, cb_data);
		return TRUE;
	}

	now = g_get_monotonic_time() / 1000;
	elapsed = now - devc->cmd_sent_at;
	/* Send query command at poll_period interval, or after 1 second
	 * has elapsed. This will make it recover from any out-of-sync
	 * or temporary disconnect issues. */
	if ((devc->expect_response == FALSE && elapsed > devc->profile->poll_period)
			|| elapsed > 1000) {
		sr_spew("fluke-dmm: sending QM");
		if (serial_write(devc->serial->fd, "QM\r", 3) == -1)
			sr_err("fluke-dmm: unable to send QM: %s", strerror(errno));
		devc->cmd_sent_at = now;
		devc->expect_response = TRUE;
	}

	return TRUE;
}


