/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "scpi.h"
#include "protocol.h"

SR_PRIV int scpi_pps_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct sr_dev_inst *sdi;
	int channel_group_cmd;
	const char *channel_group_name;
	struct pps_channel *pch;
	const struct channel_spec *ch_spec;
	int ret;
	float f;
	GVariant *gvdata;
	const GVariantType *gvtype;
	int cmd;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	pch = devc->cur_acquisition_channel->priv;

	channel_group_cmd = 0;
	channel_group_name = NULL;
	if (g_slist_length(sdi->channel_groups) > 1) {
		channel_group_cmd = SCPI_CMD_SELECT_CHANNEL;
		channel_group_name = pch->hwname;
	}

	if (pch->mq == SR_MQ_VOLTAGE) {
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_VOLTAGE;
	} else if (pch->mq == SR_MQ_FREQUENCY) {
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_FREQUENCY;
	} else if (pch->mq == SR_MQ_CURRENT) {
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_CURRENT;
	} else if (pch->mq == SR_MQ_POWER) {
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_POWER;
	} else {
		return SR_ERR;
	}

	ret = sr_scpi_cmd_resp(sdi, devc->device->commands,
		channel_group_cmd, channel_group_name, &gvdata, gvtype, cmd);

	if (ret != SR_OK)
		return ret;

	ch_spec = &devc->device->channels[pch->hw_output_idx];
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	/* Note: digits/spec_digits will be overridden later. */
	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	analog.meaning->channels = g_slist_append(NULL, devc->cur_acquisition_channel);
	analog.num_samples = 1;
	analog.meaning->mq = pch->mq;
	if (pch->mq == SR_MQ_VOLTAGE) {
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.encoding->digits = ch_spec->voltage[4];
		analog.spec->spec_digits = ch_spec->voltage[3];
	} else if (pch->mq == SR_MQ_CURRENT) {
		analog.meaning->unit = SR_UNIT_AMPERE;
		analog.encoding->digits = ch_spec->current[4];
		analog.spec->spec_digits = ch_spec->current[3];
	} else if (pch->mq == SR_MQ_POWER) {
		analog.meaning->unit = SR_UNIT_WATT;
		analog.encoding->digits = ch_spec->power[4];
		analog.spec->spec_digits = ch_spec->power[3];
	}
	analog.meaning->mqflags = SR_MQFLAG_DC;
	f = (float)g_variant_get_double(gvdata);
	g_variant_unref(gvdata);
	analog.data = &f;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);

	/* Next channel. */
	if (g_slist_length(sdi->channels) > 1) {
		devc->cur_acquisition_channel =
			sr_next_enabled_channel(sdi, devc->cur_acquisition_channel);
	}

	if (devc->cur_acquisition_channel == sr_next_enabled_channel(sdi, NULL))
		/* First enabled channel, so each channel has been sampled */
		sr_sw_limits_update_samples_read(&devc->limits, 1);

	/* Stop if limits have been hit. */
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}
