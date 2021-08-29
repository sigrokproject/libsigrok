/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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

static void rohde_schwarz_nrpxsn_send_packet(
	const struct sr_dev_inst *sdi, double value, int digits)
{
	struct dev_context *devc = sdi->priv;

	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	if (!devc)
		return;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->mq = SR_MQ_POWER;
	analog.meaning->unit = SR_UNIT_DECIBEL_MW;
	analog.meaning->channels = sdi->channels;
	analog.num_samples = 1;
	analog.data = &value;
	analog.encoding->unitsize = sizeof(value);
	analog.encoding->is_float = TRUE;
	analog.encoding->digits = 3;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
}

SR_PRIV int rohde_schwarz_nrpxsn_receive_data(int fd, int revents,
		void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	int ret;
	double meas_value;
	int buf_count;

	meas_value = 0.0;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	scpi = sdi->conn;
	devc = sdi->priv;
	if (!scpi || !devc)
		return TRUE;

	if (devc->measurement_state == IDLE) {
		if (devc->trigger_source_changed) {
			ret = rohde_schwarz_nrpxsn_update_trigger_source(scpi, devc);
		}
		else if(devc->curr_freq_changed) {
			ret = rohde_schwarz_nrpxsn_update_curr_freq(scpi, devc);
		}
		else {
			ret = sr_scpi_send(scpi, "BUFF:CLE");
			if (ret == SR_OK) {
				ret = sr_scpi_send(scpi, "INITiate");
				devc->measurement_state = WAITING_MEASUREMENT;
			}
		}
	}
	else {
		buf_count = 0;
		ret = sr_scpi_get_int(scpi, "BUFF:COUN?", &buf_count);
		if (ret == SR_OK && buf_count > 0) {
			ret = sr_scpi_get_double(scpi, "FETCh?", &meas_value);
			if (ret == SR_OK) {
				devc->measurement_state = IDLE;
				rohde_schwarz_nrpxsn_send_packet(sdi, meas_value, 16);
				sr_sw_limits_update_samples_read(&devc->limits, 1);
			}
		}
	}

	if (ret != SR_OK || sr_sw_limits_check(&devc->limits))
		/* also stop acquisition upon communication or data errors. */
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

SR_PRIV int rohde_schwarz_nrpxsn_init(
	struct sr_scpi_dev_inst *scpi, struct dev_context *devc)
{
	devc->measurement_state = IDLE;

	int ret = sr_scpi_send(scpi, "*RST");
	if(ret != SR_OK)
		return ret;

	ret = rohde_schwarz_nrpxsn_update_trigger_source(scpi, devc);
	if(ret != SR_OK)
		return ret;

	ret = rohde_schwarz_nrpxsn_update_curr_freq(scpi, devc);
	if(ret != SR_OK)
		return ret;

	return sr_scpi_send(scpi, "UNIT:POW DBM");
}

SR_PRIV int rohde_schwarz_nrpxsn_update_trigger_source(
	struct sr_scpi_dev_inst *scpi, struct dev_context *devc)
{
	char *cmd;
	if (!scpi || !devc)
		return SR_ERR;

	cmd = (devc->trigger_source == 0) ? "TRIG:SOUR IMM" : "TRIG:SOUR EXT2";

	if (sr_scpi_send(scpi, cmd) == SR_OK) {
		devc->trigger_source_changed = 0;
		return SR_OK;
	}
	return SR_ERR;
}

SR_PRIV int rohde_schwarz_nrpxsn_update_curr_freq(
	struct sr_scpi_dev_inst *scpi, struct dev_context *devc)
{
	char cmd[32];
	if (!scpi || !devc)
		return SR_ERR;

	sprintf(cmd, "SENSeq:FREQ %ld", devc->curr_freq);
	if (sr_scpi_send(scpi, cmd) == SR_OK) {
		devc->curr_freq_changed = 0;
		return SR_OK;
	}
	return SR_ERR;
}
