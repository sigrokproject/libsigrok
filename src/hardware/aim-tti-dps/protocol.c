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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

SR_PRIV int aim_tti_dps_set_value(struct sr_scpi_dev_inst *scpi,
					struct dev_context *devc, int param, size_t channel)
{
	int ret;
	g_mutex_lock(&devc->rw_mutex);

	switch (param) {
	default:
	case AIM_TTI_CURRENT:
	case AIM_TTI_VOLTAGE:
	case AIM_TTI_STATUS:
		sr_err("Read only parameter %d.", param);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	case AIM_TTI_CURRENT_LIMIT:
		ret = sr_scpi_send(scpi, "I%1zu %01.2f", channel+1,
					devc->config[channel].current_limit);
		break;
	case AIM_TTI_VOLTAGE_TARGET:
		ret = sr_scpi_send(scpi, "V%1zu %01.2f", channel+1,
					devc->config[channel].voltage_target);
		break;
	case AIM_TTI_OUTPUT_ENABLE:
		ret = sr_scpi_send(scpi, "OP%1zu %d", channel+1,
					(devc->config[channel].output_enabled) ? 1 : 0);
		break;
	case AIM_TTI_OCP_THRESHOLD:
		ret = sr_scpi_send(scpi, "OCP%1zu %01.2f", channel+1,
					devc->config[channel].over_current_protection_threshold);
		break;
	case AIM_TTI_OVP_THRESHOLD:
		ret = sr_scpi_send(scpi, "OVP%1zu %01.2f", channel+1,
					devc->config[channel].over_voltage_protection_threshold);
		break;
	case AIM_TTI_OUTPUT_ENABLE_DUAL:
		ret = sr_scpi_send(scpi, devc->config[0].output_enabled ?
					"OPALL 1" : "OPALL 0");
		break;
	case AIM_TTI_TRACKING_ENABLE:
		ret = sr_scpi_send(scpi, devc->tracking_enabled ?
					"CONFIG 0" : "CONFIG 2");
		break;
	};
	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

SR_PRIV int aim_tti_dps_get_value(struct sr_scpi_dev_inst *scpi,
					struct dev_context *devc, int param, size_t channel)
{
	int ret;
	char *response;
	float val;
	int status_byte;
	int new_mode;
	gboolean bval;
	g_mutex_lock(&devc->rw_mutex);

	switch (param) {
	case AIM_TTI_VOLTAGE:
		ret = sr_scpi_send(scpi, "V%1zuO?", channel+1);
		break;
	case AIM_TTI_CURRENT:
		ret = sr_scpi_send(scpi, "I%1zuO?", channel+1);
		break;
	case AIM_TTI_VOLTAGE_TARGET:
		ret = sr_scpi_send(scpi, "V%1zu?", channel+1);
		break;
	case AIM_TTI_CURRENT_LIMIT:
		ret = sr_scpi_send(scpi, "I%1zu?", channel+1);
		break;
	case AIM_TTI_OUTPUT_ENABLE:
		ret = sr_scpi_send(scpi, "OP%1zu?", channel+1);
		break;
	case AIM_TTI_OCP_THRESHOLD:
		ret = sr_scpi_send(scpi, "OCP%1zu?", channel+1);
		break;
	case AIM_TTI_OVP_THRESHOLD:
		ret = sr_scpi_send(scpi, "OVP%1zu?", channel+1);
		break;
	case AIM_TTI_STATUS:
		ret = sr_scpi_send(scpi, "LSR%1zu?", channel+1);
		break;
	case AIM_TTI_TRACKING_ENABLE:
		ret = sr_scpi_send(scpi, "CONFIG?");
		break;
	default:
		sr_err("Don't know how to query %d.", param);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	}
	if (ret != SR_OK) {
		g_mutex_unlock(&devc->rw_mutex);
		return ret;
	}

	ret = sr_scpi_get_string(scpi, NULL, &response);
	if (ret != SR_OK || !response) {
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	}


	switch (param) {
	case AIM_TTI_VOLTAGE:
		val = atof(&(response[0]));
		devc->config[channel].actual_voltage = val;
		break;
	case AIM_TTI_CURRENT:
		val = atof(&(response[0]));
		devc->config[channel].actual_current = val;
		break;
	case AIM_TTI_VOLTAGE_TARGET:
		val = atof(&(response[3]));
		devc->config[channel].voltage_target = val;
		break;
	case AIM_TTI_CURRENT_LIMIT:
		val = atof(&(response[3]));
		devc->config[channel].current_limit = val;
		break;
	case AIM_TTI_OUTPUT_ENABLE:
		devc->config[channel].output_enabled =
			(response[0] == '1');
		break;
	case AIM_TTI_OCP_THRESHOLD:
		val = atof(&(response[4]));
		devc->config[channel].over_current_protection_threshold = val;
		break;
	case AIM_TTI_OVP_THRESHOLD:
		val = atof(&(response[4]));
		devc->config[channel].over_voltage_protection_threshold = val;
		break;
	case AIM_TTI_STATUS:
		status_byte = atoi(&(response[0]));

		new_mode = AIM_TTI_CV;
		if (status_byte & 0x02)
			new_mode = AIM_TTI_CC;
		else if (status_byte & 0x10)
			new_mode = AIM_TTI_UR;
		if (devc->config[channel].mode != new_mode)
			devc->config[channel].mode_changed = TRUE;
		devc->config[channel].mode = new_mode;

		bval = ((status_byte & 0x04) != 0);
		if (devc->config[channel].ovp_active != bval)
			devc->config[channel].ovp_active_changed = TRUE;
		devc->config[channel].ovp_active = bval;

		bval = ((status_byte & 0x08) != 0);
		if (devc->config[channel].ocp_active != bval)
			devc->config[channel].ocp_active_changed = TRUE;
		devc->config[channel].ocp_active = bval;
		break;
	case AIM_TTI_TRACKING_ENABLE:
		devc->tracking_enabled = response[0] == '0';
		break;
	default:
		sr_err("Don't know how to query %d.", param);
		g_mutex_unlock(&devc->rw_mutex);
		return SR_ERR;
	};

	g_mutex_unlock(&devc->rw_mutex);

	return ret;
}

SR_PRIV int aim_tti_dps_sync_state(struct sr_scpi_dev_inst *scpi,
									struct dev_context *devc)
{
	int ret;
	ret = SR_OK;

	for (int channel = 0 ; channel < devc->model_config->channels ; ++channel) {
		for (int param = AIM_TTI_VOLTAGE;
			param < AIM_TTI_LAST_CHANNEL_PARAM && ret >= SR_OK;
			++param) {
			ret = aim_tti_dps_get_value(scpi, devc, param, channel);
		}
		devc->config[channel].mode_changed = TRUE;
		devc->config[channel].ocp_active_changed = TRUE;
		devc->config[channel].ovp_active_changed = TRUE;
	}

	if (ret == SR_OK)
		ret = aim_tti_dps_get_value(scpi, devc, AIM_TTI_TRACKING_ENABLE, 0);

	devc->acquisition_param = AIM_TTI_VOLTAGE;
	devc->acquisition_channel = 0;

	return ret;
}

SR_PRIV void aim_tti_dps_next_acqusition(struct dev_context *devc)
{
	if (devc->acquisition_param == AIM_TTI_VOLTAGE)
		devc->acquisition_param = AIM_TTI_CURRENT;
	else if (devc->acquisition_param == AIM_TTI_CURRENT)
		devc->acquisition_param = AIM_TTI_STATUS;
	else if (devc->acquisition_param == AIM_TTI_STATUS) {
		devc->acquisition_param = AIM_TTI_VOLTAGE;
		if (devc->acquisition_channel < 0)
			devc->acquisition_channel = 0;
		else {
			devc->acquisition_channel++;
			if (devc->acquisition_channel >= devc->model_config->channels)
				devc->acquisition_channel = 0;
		}
	} else {
		devc->acquisition_param = AIM_TTI_VOLTAGE;
		devc->acquisition_channel = 0;
	}
}

SR_PRIV int aim_tti_dps_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	GSList *l;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv) || !(scpi = sdi->conn))
		return TRUE;


	aim_tti_dps_get_value(scpi, devc, devc->acquisition_param,
						devc->acquisition_channel);

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = 1;

	int channel = devc->acquisition_channel;
	l = g_slist_copy(sdi->channels);
	if (devc->acquisition_param == AIM_TTI_VOLTAGE) {
		analog.meaning->channels = g_slist_nth(l, 2*channel);
		l = g_slist_remove_link(l, analog.meaning->channels);
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.meaning->mqflags = SR_MQFLAG_DC;
		analog.encoding->digits = 2;
		analog.spec->spec_digits = 2;
		analog.data = &devc->config[channel].actual_voltage;
		sr_session_send(sdi, &packet);
	} else if (devc->acquisition_param == AIM_TTI_CURRENT) {
		analog.meaning->channels = g_slist_nth(l, 2*channel+1);
		l = g_slist_remove_link(l, analog.meaning->channels);
		analog.meaning->mq = SR_MQ_CURRENT;
		analog.meaning->unit = SR_UNIT_AMPERE;
		analog.meaning->mqflags = SR_MQFLAG_DC;
		analog.encoding->digits = 3;
		analog.spec->spec_digits = 3;
		analog.data = &devc->config[channel].actual_current;
		sr_session_send(sdi, &packet);
		if (devc->acquisition_channel + 1 == devc->model_config->channels)
			sr_sw_limits_update_samples_read(&devc->limits, 1);
	} else if (devc->acquisition_param == AIM_TTI_STATUS) {
		if (devc->config[channel].mode_changed) {
			sr_session_send_meta(sdi, SR_CONF_REGULATION,
				g_variant_new_string(
					(devc->config[channel].output_enabled == FALSE) ?
						"" :
							((devc->config[channel].mode == AIM_TTI_CC) ?
								"CC" :
								((devc->config[channel].mode == AIM_TTI_CV) ?
									"CV" :
									"UR"))));
			devc->config[channel].mode_changed = FALSE;
		}
		if (devc->config[channel].ocp_active_changed) {
			sr_session_send_meta(sdi, SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,
				g_variant_new_boolean(devc->config[channel].ocp_active));
			devc->config[channel].ocp_active_changed = FALSE;
		}
		if (devc->config[channel].ovp_active_changed) {
			sr_session_send_meta(sdi, SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE,
				g_variant_new_boolean(devc->config[channel].ovp_active));
			devc->config[channel].ovp_active_changed = FALSE;
		}
	}
	g_slist_free(l);

	aim_tti_dps_next_acqusition(devc);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

