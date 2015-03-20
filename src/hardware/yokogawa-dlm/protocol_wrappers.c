/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 abraxa (Soeren Apel) <soeren@apelpie.net>
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

#include "protocol_wrappers.h"

#define MAX_COMMAND_SIZE 64

/*
 * DLM2000 comm spec:
 * https://www.yokogawa.com/pdf/provide/E/GW/IM/0000022842/0/IM710105-17E.pdf
 */

int dlm_timebase_get(struct sr_scpi_dev_inst *scpi,
		gchar **response)
{
	return sr_scpi_get_string(scpi, ":TIMEBASE:TDIV?", response);
}

int dlm_timebase_set(struct sr_scpi_dev_inst *scpi,
		const gchar *value)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":TIMEBASE:TDIV %s", value);
	return sr_scpi_send(scpi, cmd);
}

int dlm_horiz_trigger_pos_get(struct sr_scpi_dev_inst *scpi,
		float *response)
{
	return sr_scpi_get_float(scpi, ":TRIGGER:DELAY:TIME?", response);
}

int dlm_horiz_trigger_pos_set(struct sr_scpi_dev_inst *scpi,
		const gchar *value)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":TRIGGER:DELAY:TIME %s", value);
	return sr_scpi_send(scpi, cmd);
}

int dlm_trigger_source_get(struct sr_scpi_dev_inst *scpi,
		gchar **response)
{
	return sr_scpi_get_string(scpi, ":TRIGGER:ATRIGGER:SIMPLE:SOURCE?", response);
}

int dlm_trigger_source_set(struct sr_scpi_dev_inst *scpi,
		const gchar *value)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":TRIGGER:ATRIGGER:SIMPLE:SOURCE %s", value);
	return sr_scpi_send(scpi, cmd);
}

int dlm_trigger_slope_get(struct sr_scpi_dev_inst *scpi,
		int *response)
{
	gchar *resp;
	int result;

	result = SR_ERR;

	if (sr_scpi_get_string(scpi, ":TRIGGER:ATRIGGER:SIMPLE:SLOPE?", &resp) != SR_OK) {
		g_free(resp);
		return SR_ERR;
	}

	if (strcmp("RISE", resp) == 0) {
		*response = SLOPE_POSITIVE;
		result = SR_OK;
	}

	if (strcmp("FALL", resp) == 0) {
		*response = SLOPE_NEGATIVE;
		result = SR_OK;
	}

	g_free(resp);
	return result;
}

int dlm_trigger_slope_set(struct sr_scpi_dev_inst *scpi,
		const int value)
{
	if (value == SLOPE_POSITIVE)
		return sr_scpi_send(scpi, ":TRIGGER:ATRIGGER:SIMPLE:SLOPE RISE");

	if (value == SLOPE_NEGATIVE)
		return sr_scpi_send(scpi, ":TRIGGER:ATRIGGER:SIMPLE:SLOPE FALL");

	return SR_ERR_ARG;
}

int dlm_analog_chan_state_get(struct sr_scpi_dev_inst *scpi, int channel,
		gboolean *response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:DISPLAY?", channel);
	return sr_scpi_get_bool(scpi, cmd, response);
}

int dlm_analog_chan_state_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gboolean value)
{
	gchar cmd[MAX_COMMAND_SIZE];

	if (value)
		g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:DISPLAY ON", channel);
	else
		g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:DISPLAY OFF", channel);

	return sr_scpi_send(scpi, cmd);
}

int dlm_analog_chan_vdiv_get(struct sr_scpi_dev_inst *scpi, int channel,
		gchar **response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:VDIV?", channel);
	return sr_scpi_get_string(scpi, cmd, response);
}

int dlm_analog_chan_vdiv_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gchar *value)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:VDIV %s", channel, value);
	return sr_scpi_send(scpi, cmd);
}

int dlm_analog_chan_voffs_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:POSITION?", channel);
	return sr_scpi_get_float(scpi, cmd, response);
}

int dlm_analog_chan_srate_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:TRACE %d", channel);

	if (sr_scpi_send(scpi, cmd) != SR_OK)
		return SR_ERR;

	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:RECORD 0");
	if (sr_scpi_send(scpi, cmd) != SR_OK)
		return SR_ERR;

	return sr_scpi_get_float(scpi, ":WAVEFORM:SRATE?", response);
}

int dlm_analog_chan_coupl_get(struct sr_scpi_dev_inst *scpi, int channel,
		gchar **response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:COUPLING?", channel);
	return sr_scpi_get_string(scpi, cmd, response);
}

int dlm_analog_chan_coupl_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gchar *value)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":CHANNEL%d:COUPLING %s", channel, value);
	return sr_scpi_send(scpi, cmd);
}

int dlm_analog_chan_wrange_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	int result;

	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:TRACE %d", channel);
	result  = sr_scpi_send(scpi, cmd);
	result &= sr_scpi_get_float(scpi, ":WAVEFORM:RANGE?", response);
	return result;
}

int dlm_analog_chan_woffs_get(struct sr_scpi_dev_inst *scpi, int channel,
		float *response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	int result;

	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:TRACE %d", channel);
	result  = sr_scpi_send(scpi, cmd);
	result &= sr_scpi_get_float(scpi, ":WAVEFORM:OFFSET?", response);
	return result;
}

int dlm_digital_chan_state_get(struct sr_scpi_dev_inst *scpi, int channel,
		gboolean *response)
{
	gchar cmd[MAX_COMMAND_SIZE];
	g_snprintf(cmd, sizeof(cmd), ":LOGIC:PODA:BIT%d:DISPLAY?", channel);
	return sr_scpi_get_bool(scpi, cmd, response);
}

int dlm_digital_chan_state_set(struct sr_scpi_dev_inst *scpi, int channel,
		const gboolean value)
{
	gchar cmd[MAX_COMMAND_SIZE];

	if (value)
		g_snprintf(cmd, sizeof(cmd), ":LOGIC:PODA:BIT%d:DISPLAY ON", channel);
	else
		g_snprintf(cmd, sizeof(cmd), ":LOGIC:PODA:BIT%d:DISPLAY OFF", channel);

	return sr_scpi_send(scpi, cmd);
}

int dlm_digital_pod_state_get(struct sr_scpi_dev_inst *scpi, int pod,
		gboolean *response)
{
	gchar cmd[MAX_COMMAND_SIZE];

	/* TODO: pod currently ignored as DLM2000 only has pod A. */
	(void)pod;

	g_snprintf(cmd, sizeof(cmd), ":LOGIC:MODE?");
	return sr_scpi_get_bool(scpi, cmd, response);
}

int dlm_digital_pod_state_set(struct sr_scpi_dev_inst *scpi, int pod,
		const gboolean value)
{
	/* TODO: pod currently ignored as DLM2000 only has pod A. */
	(void)pod;

	if (value)
		return sr_scpi_send(scpi, ":LOGIC:MODE ON");
	else
		return sr_scpi_send(scpi, ":LOGIC:MODE OFF");
}

int dlm_response_headers_set(struct sr_scpi_dev_inst *scpi,
		const gboolean value)
{
	if (value)
		return sr_scpi_send(scpi, ":COMMUNICATE:HEADER ON");
	else
		return sr_scpi_send(scpi, ":COMMUNICATE:HEADER OFF");
}

int dlm_acquisition_stop(struct sr_scpi_dev_inst *scpi)
{
	return sr_scpi_send(scpi, ":STOP");
}

int dlm_acq_length_get(struct sr_scpi_dev_inst *scpi,
		uint32_t *response)
{
	int ret;
	char *s;
	long tmp;

	if (sr_scpi_get_string(scpi, ":WAVEFORM:LENGTH?", &s) != SR_OK)
		if (!s)
			return SR_ERR;

	if (sr_atol(s, &tmp) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(s);
	*response = tmp;

	return ret;
}

int dlm_chunks_per_acq_get(struct sr_scpi_dev_inst *scpi, int *response)
{
	int result, acq_len;

	/* Data retrieval queries such as :WAVEFORM:SEND? will only return
	 * up to 12500 samples at a time. If the oscilloscope operates in a
	 * mode where more than 12500 samples fit on screen (i.e. in one
	 * acquisition), data needs to be retrieved multiple times.
	 */

	result = sr_scpi_get_int(scpi, ":WAVEFORM:LENGTH?", &acq_len);
	*response = MAX(acq_len / DLM_MAX_FRAME_LENGTH, 1);

	return result;
}

int dlm_start_frame_set(struct sr_scpi_dev_inst *scpi, int value)
{
	gchar cmd[MAX_COMMAND_SIZE];

	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:START %d",
			value * DLM_MAX_FRAME_LENGTH);

	return sr_scpi_send(scpi, cmd);
}

int dlm_data_get(struct sr_scpi_dev_inst *scpi, int acquisition_num)
{
	gchar cmd[MAX_COMMAND_SIZE];

	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:ALL:SEND? %d", acquisition_num);
	return sr_scpi_send(scpi, cmd);
}

int dlm_analog_data_get(struct sr_scpi_dev_inst *scpi, int channel)
{
	gchar cmd[MAX_COMMAND_SIZE];
	int result;

	result = sr_scpi_send(scpi, ":WAVEFORM:FORMAT BYTE");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:RECORD 0");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:START 0");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:END 124999999");

	g_snprintf(cmd, sizeof(cmd), ":WAVEFORM:TRACE %d", channel);
	if (result == SR_OK) result = sr_scpi_send(scpi, cmd);

	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:SEND? 1");

	return result;
}

int dlm_digital_data_get(struct sr_scpi_dev_inst *scpi)
{
	int result;

	result = sr_scpi_send(scpi, ":WAVEFORM:FORMAT BYTE");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:RECORD 0");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:START 0");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:END 124999999");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:TRACE LOGIC");
	if (result == SR_OK) result = sr_scpi_send(scpi, ":WAVEFORM:SEND? 1");

	return result;
}
