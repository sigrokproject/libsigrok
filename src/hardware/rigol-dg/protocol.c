/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Timo Kokkonen <tjko@iki.fi>
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
#include "scpi.h"
#include "protocol.h"

SR_PRIV const char *rigol_dg_waveform_to_string(enum waveform_type type)
{
	switch (type) {
	case WF_DC:
		return "DC";
	case WF_SINE:
		return "Sine";
	case WF_SQUARE:
		return "Square";
	case WF_RAMP:
		return "Ramp";
	case WF_PULSE:
		return "Pulse";
	case WF_NOISE:
		return "Noise";
	case WF_ARB:
		return "Arb";
	}

	return "Unknown";
}

SR_PRIV const struct waveform_spec *rigol_dg_get_waveform_spec(
	const struct channel_spec *ch, enum waveform_type wf)
{
	const struct waveform_spec *spec;
	unsigned int i;

	spec = NULL;
	for (i = 0; i < ch->num_waveforms; i++) {
		if (ch->waveforms[i].waveform == wf) {
			spec = &ch->waveforms[i];
			break;
		}
	}

	return spec;
}

SR_PRIV int rigol_dg_get_channel_state(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *ch;
	struct channel_status *ch_status;
	const char *command;
	GVariant *data;
	gchar *response, **params;
	const gchar *s;
	enum waveform_type wf;
	double freq, ampl, offset, phase;
	int ret;

	devc = sdi->priv;
	scpi = sdi->conn;
	data = NULL;
	params = NULL;
	response = NULL;
	ret = SR_ERR_NA;

	if (!sdi || !cg)
		return SR_ERR_BUG;

	ch = cg->channels->data;
	ch_status = &devc->ch_status[ch->index];

	command = sr_scpi_cmd_get(devc->cmdset, PSG_CMD_GET_SOURCE);
	if (command && *command) {
		sr_scpi_get_opc(scpi);
		ret = sr_scpi_cmd_resp(sdi, devc->cmdset,
			PSG_CMD_SELECT_CHANNEL, cg->name, &data,
			G_VARIANT_TYPE_STRING, PSG_CMD_GET_SOURCE, cg->name);
		if (ret != SR_OK)
			goto done;
		response = g_variant_dup_string(data, NULL);
		g_strstrip(response);
		s = sr_scpi_unquote_string(response);
		sr_spew("Channel state: '%s'", s);
		params = g_strsplit(s, ",", 0);
		if (!params[0])
			goto done;

		/* First parameter is the waveform type */
		if (!(s = params[0]))
			goto done;
		if (g_ascii_strncasecmp(s, "SIN", strlen("SIN")) == 0)
			wf = WF_SINE;
		else if (g_ascii_strncasecmp(s, "SQU", strlen("SQU")) == 0)
			wf = WF_SQUARE;
		else if (g_ascii_strncasecmp(s, "RAMP", strlen("RAMP")) == 0)
			wf = WF_RAMP;
		else if (g_ascii_strncasecmp(s, "PULSE", strlen("PULSE")) == 0)
			wf = WF_PULSE;
		else if (g_ascii_strncasecmp(s, "NOISE", strlen("NOISE")) == 0)
			wf = WF_NOISE;
		else if (g_ascii_strncasecmp(s, "USER", strlen("USER")) == 0)
			wf = WF_ARB;
		else if (g_ascii_strncasecmp(s, "DC", strlen("DC")) == 0)
			wf = WF_DC;
		else
			goto done;
		ch_status->wf = wf;
		ch_status->wf_spec = rigol_dg_get_waveform_spec(
				&devc->device->channels[ch->index], wf);

		/* Second parameter if the frequency (or "DEF" if not applicable) */
		if (!(s = params[1]))
			goto done;
		freq = g_ascii_strtod(s, NULL);
		ch_status->freq = freq;

		/* Third parameter if the amplitude (or "DEF" if not applicable) */
		if (!(s = params[2]))
			goto done;
		ampl = g_ascii_strtod(s, NULL);
		ch_status->ampl = ampl;

		/* Fourth parameter if the offset (or "DEF" if not applicable) */
		if (!(s = params[3]))
			goto done;
		offset = g_ascii_strtod(s, NULL);
		ch_status->offset = offset;

		/* Fifth parameter if the phase (or "DEF" if not applicable) */
		if (!(s = params[4]))
			goto done;
		phase = g_ascii_strtod(s, NULL);
		ch_status->phase = phase;

		ret = SR_OK;
	}

done:
	g_variant_unref(data);
	g_free(response);
	g_strfreev(params);
	return ret;
}

static void rigol_dg_send_channel_value(const struct sr_dev_inst *sdi,
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

SR_PRIV int rigol_dg_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	const char *cmd, *s;
	char *response, **params;
	double meas[5];
	GSList *l;
	int i, start_idx, ret;

	(void)fd;
	(void)revents;
	response = NULL;
	params = NULL;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	scpi = sdi->conn;
	devc = sdi->priv;
	if (!scpi || !devc)
		return TRUE;

	cmd = sr_scpi_cmd_get(devc->cmdset, PSG_CMD_COUNTER_MEASURE);
	if (!cmd || !*cmd)
		return TRUE;

	sr_scpi_get_opc(scpi);
	ret = sr_scpi_get_string(scpi, cmd, &response);
	if (ret != SR_OK) {
		sr_info("Error getting measurement from counter: %d", ret);
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}
	g_strstrip(response);

	/*
	 * Parse measurement string:
	 *  frequency, period, duty cycle, width+, width-
	 */
	params = g_strsplit(response, ",", 0);
	for (i = 0; i < 5; i++) {
		if (!(s = params[i]))
			goto done;
		meas[i] = g_ascii_strtod(s, NULL);
	}
	sr_spew("%s: freq=%.10E, period=%.10E, duty=%.10E, width+=%.10E,"
		"width-=%.10E", __func__,
		meas[0], meas[1], meas[2], meas[3], meas[4]);

	std_session_send_df_frame_begin(sdi);
	start_idx = devc->device->num_channels;

	/* Frequency */
	l = g_slist_nth(sdi->channels, start_idx++);
	rigol_dg_send_channel_value(sdi, l->data, meas[0], SR_MQ_FREQUENCY,
		SR_UNIT_HERTZ, 10);

	/* Period */
	l = g_slist_nth(sdi->channels, start_idx++);
	rigol_dg_send_channel_value(sdi, l->data, meas[1], SR_MQ_TIME,
		SR_UNIT_SECOND, 10);

	/* Duty Cycle */
	l = g_slist_nth(sdi->channels, start_idx++);
	rigol_dg_send_channel_value(sdi, l->data, meas[2], SR_MQ_DUTY_CYCLE,
		SR_UNIT_PERCENTAGE, 3);

	/* Pulse Width */
	l = g_slist_nth(sdi->channels, start_idx++);
	rigol_dg_send_channel_value(sdi, l->data, meas[3], SR_MQ_PULSE_WIDTH,
		SR_UNIT_SECOND, 10);

	std_session_send_df_frame_end(sdi);
	sr_sw_limits_update_samples_read(&devc->limits, 1);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

done:
	g_free(response);
	g_strfreev(params);
	return TRUE;
}
