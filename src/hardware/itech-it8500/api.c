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
#include "protocol.h"

#define DEFAULT_SERIALCOMM "9600/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_UNDER_VOLTAGE_CONDITION | SR_CONF_GET,
	SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE | SR_CONF_GET,
	SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET,
};


#define MIN_SAMPLE_RATE SR_HZ(1)
#define MAX_SAMPLE_RATE SR_HZ(100)

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_HZ(2),
	SR_HZ(5),
	SR_HZ(10),
	SR_HZ(50),
	SR_HZ(100),
};

static struct sr_dev_driver itech_it8500_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_config *conf;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	struct dev_context *devc;
	const char *conn, *serialcomm;
	GSList *l;
	struct itech_it8500_cmd_packet *cmd, *response;
	int fw_major, fw_minor;
	char *unit_model, *unit_serial;
	double max_i, max_v, min_v, max_p, max_r, min_r;
	int ret;


	sr_dbg("%s(%p,%p): called", __func__, di, options);

	cmd = g_malloc0(sizeof(*cmd));
	devc = g_malloc0(sizeof(*devc));
	if (!cmd || !devc)
		return NULL;

	conn = NULL;
	serialcomm = NULL;
	response = NULL;
	unit_model = NULL;
	unit_serial = NULL;

	for (l = options; l; l = l->next) {
		conf = l->data;
		switch (conf->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(conf->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(conf->data, NULL);
			break;
		}
	}

	if (!conn)
		goto error;
	if (!serialcomm)
		serialcomm = DEFAULT_SERIALCOMM;

	/*
	 * open serial port
	 */
	sr_info("Probing serial port: %s", conn);
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		goto error;
	serial_flush(serial);

	/*
	 * get load model information
	 */
	cmd->command = CMD_GET_MODEL_INFO;
	if (itech_it8500_send_cmd(serial, cmd, &response) != SR_OK)
		goto error;
	fw_major = response->data[6];
	fw_minor = response->data[5];
	response->data[5]=0;
	unit_model = g_strdup(&response->data[0]);
	response->data[17]=0;
	unit_serial = g_strdup(&response->data[7]);
	sr_info("Model name: %s (v%x.%02x)", unit_model, fw_major, fw_minor);
	sr_info("Serial number: %s", unit_serial);
	sr_info("Address: %d", response->address);

	/*
	 * query unit capabilities
	 */
	cmd->command = CMD_GET_LOAD_LIMITS;
	if (itech_it8500_send_cmd(serial, cmd, &response) != SR_OK)
		goto error;
	max_i = itech_it8500_decode_int(&response->data[0],4) / 10000.0;
	max_v = itech_it8500_decode_int(&response->data[4],4) / 1000.0;
	min_v = itech_it8500_decode_int(&response->data[8],4) / 1000.0;
	max_p = itech_it8500_decode_int(&response->data[12],4) / 1000.0;
	max_r = itech_it8500_decode_int(&response->data[16],4) / 1000.0;
	min_r = itech_it8500_decode_int(&response->data[20],2) / 1000.0;
	sr_info("Max current: %.0f A", max_i);
	sr_info("Max power: %.0f W", max_p);
	sr_info("Voltage range: %.1f - %.1f V", min_v, max_v);
	sr_info("Resistance range: %.2f - %.2f Ohm", min_r, max_r);

	/*
	 * get current status of the unit
	 */
	if ((ret = itech_it8500_get_status(serial, devc)) != SR_OK) {
		sr_err("Failed to get unit status: %d",  ret);
		goto error;
	}
	sr_info("Mode: %s", itech_it8500_mode_to_string(devc->mode));
	sr_info("State: %s", (devc->load_on ? "ON" : "OFF"));


	/*
	 * populate data structures
	 */

	devc->fw_ver_major = fw_major;
	devc->fw_ver_minor = fw_minor;
	strncpy(devc->model, unit_model, sizeof(devc->model) - 1);
	devc->address = response->address;
	devc->max_current = max_i;
	devc->min_voltage = min_v;
	devc->max_voltage = max_v;
	devc->max_power = max_p;
	devc->min_resistance = min_r;
	devc->max_resistance = max_r;
	devc->sample_rate = SR_HZ(10);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("ITECH");
	sdi->model = unit_model;
	sdi->version = g_strdup_printf("%x.%02x", fw_major, fw_minor);
	sdi->serial_num = unit_serial;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->driver = &itech_it8500_driver_info;
	sdi->priv = devc;

	cg = g_malloc0(sizeof(*cg));
	cg->name = g_strdup("1");
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V1");
	cg->channels = g_slist_append(cg->channels, ch);
	ch = sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I1");
	cg->channels = g_slist_append(cg->channels, ch);
	ch = sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, TRUE, "P1");
	cg->channels = g_slist_append(cg->channels, ch);


	g_free(response);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

error:
	g_free(cmd);
	g_free(devc);
	if (response)
		g_free(response);
	if (unit_model)
		g_free(unit_model);
	if (unit_serial)
		g_free(unit_serial);
	return NULL;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret, ivalue;


	sr_dbg("%s(%u,%p,%p,%p): called", __func__, key, data, sdi, cg);

	if (!data || !sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	serial = sdi->conn;
	ret = SR_OK;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		ret = sr_sw_limits_config_get(&devc->limits, key, data);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
		break;
	case SR_CONF_ENABLED:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_boolean(devc->load_on);
		}
		break;
	case SR_CONF_REGULATION:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_string(itech_it8500_mode_to_string(
							     devc->mode));
		}
		break;
	case SR_CONF_VOLTAGE:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_double(devc->voltage);
		}
		break;
	case SR_CONF_VOLTAGE_TARGET:
		if ((ret = itech_it8500_get_int(serial,
						CMD_GET_CV_VOLTAGE, &ivalue)) == SR_OK) {
			*data = g_variant_new_double((double)ivalue / 1000.0);
		}
		break;
	case SR_CONF_CURRENT:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_double(devc->current);
		}
		break;
	case SR_CONF_CURRENT_LIMIT:
		if ((ret = itech_it8500_get_int(serial,
						CMD_GET_CC_CURRENT, &ivalue)) == SR_OK) {
			*data = g_variant_new_double((double)ivalue / 10000.0);
		}
		break;

#if 0
        /* commented out for now as libsigrok doesnt yet have CW / CR mode support... */
	case SR_CONF_POWER:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_double(devc->power);
		}
		break;
	case SR_CONF_POWER_LIMIT:
		if ((ret = itech_it8500_get_int(serial,
						CMD_GET_CW_POWER, &ivalue)) == SR_OK) {
			*data = g_variant_new_double((double)ivalue / 1000.0);
		}
		break;
	case SR_CONF_RESISTANCE_TARGET:
		if ((ret = itech_it8500_get_int(serial,
						CMD_GET_CR_RESISTANCE,
						&ivalue)) == SR_OK) {
			*data = g_variant_new_double((double)ivalue / 1000.0);
		}
		break;
#endif

	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_boolean(devc->demand_state & 0x0002);
		}
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		if ((ret = itech_it8500_get_int(serial,
						CMD_GET_MAX_VOLTAGE,
						&ivalue)) == SR_OK) {
			*data = g_variant_new_double((double)ivalue / 1000.0);
		}
		break;

	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_boolean(devc->demand_state & 0x0004);
		}
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		if ((ret = itech_it8500_get_int(serial,
						CMD_GET_MAX_CURRENT,
						&ivalue)) == SR_OK) {
			*data = g_variant_new_double((double)ivalue / 10000.0);
		}
		break;

	case SR_CONF_OVER_TEMPERATURE_PROTECTION:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		if ((ret = itech_it8500_get_status(serial, devc)) == SR_OK) {
			*data = g_variant_new_boolean(devc->demand_state & 0x0010);
		}
		break;

	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct itech_it8500_cmd_packet *cmd, *response;
	int ret, ivalue;
	unsigned int new_sr;

	sr_dbg("%s(%u,%p,%p,%p): called", __func__, key, data, sdi, cg);

	if (!data || !sdi)
		return SR_ERR_ARG;

	cmd = g_malloc0(sizeof(*cmd));
	if (!cmd)
		return SR_ERR_MALLOC;

	devc = sdi->priv;
	serial = sdi->conn;
	response = NULL;

	ret = SR_OK;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		ret = sr_sw_limits_config_set(&devc->limits, key, data);
		break;
	case SR_CONF_SAMPLERATE:
		new_sr = g_variant_get_uint64(data);
		if (new_sr < MIN_SAMPLE_RATE || new_sr > MAX_SAMPLE_RATE) {
			ret = SR_ERR_SAMPLERATE;
			break;
		}
		devc->sample_rate = new_sr;
		break;
	case SR_CONF_ENABLED:
		cmd->command = CMD_LOAD_ON_OFF;
		cmd->data[0] = g_variant_get_boolean(data);
		ret = itech_it8500_send_cmd(serial, cmd, &response);
		break;
	case SR_CONF_REGULATION:
		cmd->command = CMD_SET_MODE;
		if (!strncmp(g_variant_get_string(data, NULL),"CV",2)) {
			cmd->data[0]=1;
		} else if (!strncmp(g_variant_get_string(data, NULL),"CC",2)) {
			cmd->data[0]=0;
		} else if (!strncmp(g_variant_get_string(data, NULL),"CW",2)) {
			cmd->data[0]=2;
		} else if (!strncmp(g_variant_get_string(data, NULL),"CR",2)) {
			cmd->data[0]=3;
		} else {
			ret = SR_ERR_NA;
			break;
		}
		ret = itech_it8500_send_cmd(serial, cmd, &response);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		cmd->command = CMD_SET_CV_VOLTAGE;
		ivalue = g_variant_get_double(data) * 1000.0;
		itech_it8500_encode_int(&cmd->data[0], 4, ivalue);
		ret = itech_it8500_send_cmd(serial, cmd, &response);
		break;
	case SR_CONF_CURRENT_LIMIT:
		cmd->command = CMD_SET_CC_CURRENT;
		ivalue = g_variant_get_double(data) * 10000.0;
		itech_it8500_encode_int(&cmd->data[0], 4, ivalue);
		ret = itech_it8500_send_cmd(serial, cmd, &response);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		cmd->command = CMD_SET_MAX_VOLTAGE;
		ivalue = g_variant_get_double(data) * 1000.0;
		itech_it8500_encode_int(&cmd->data[0], 4, ivalue);
		ret = itech_it8500_send_cmd(serial, cmd, &response);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		cmd->command = CMD_SET_MAX_CURRENT;
		ivalue = g_variant_get_double(data) * 10000.0;
		itech_it8500_encode_int(&cmd->data[0], 4, ivalue);
		ret = itech_it8500_send_cmd(serial, cmd, &response);
		break;

	default:
		ret = SR_ERR_NA;
	}


	g_free(cmd);
	if (response)
		g_free(response);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	const struct dev_context *devc;


	sr_dbg("%s(%u,%p,%p,%p): called", __func__, key, data, sdi, cg);

	devc = (sdi ? sdi->priv : NULL);
	if (!data)
		return SR_ERR_ARG;

	if (!cg) {
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts,
				       drvopts, devopts);
	}

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
		break;
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_VOLTAGE_TARGET:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(devc->min_voltage, devc->max_voltage, 0.01);
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(0.0, devc->max_current, 0.001);
		break;

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;


	sr_dbg("%s(%p): called", __func__, sdi);

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	serial = sdi->conn;

	ret = serial_source_add(sdi->session, serial, G_IO_IN,
				(1000.0/devc->sample_rate),
				itech_it8500_receive_data, (void *)sdi);
	if (ret == SR_OK) {
		sr_sw_limits_acquisition_start(&devc->limits);
		std_session_send_df_header(sdi);
	}

	return ret;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;


	sr_dbg("%s(%p): called", __func__, sdi);

	if (!sdi)
		return SR_ERR_ARG;

	serial = sdi->conn;

	std_session_send_df_end(sdi);
	serial_source_remove(sdi->session, serial);

	return SR_OK;
}

static struct sr_dev_driver itech_it8500_driver_info = {
	.name = "itech-it8500",
	.longname = "ITECH IT8500 series",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(itech_it8500_driver_info);
