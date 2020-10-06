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
#include "protocol.h"

#define MIN_SAMPLE_RATE SR_HZ(1)
#define MAX_SAMPLE_RATE SR_HZ(60)
#define DEFAULT_SAMPLE_RATE SR_HZ(10)

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
	SR_CONF_REGULATION | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_POWER | SR_CONF_GET,
	SR_CONF_POWER_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RESISTANCE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
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

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_HZ(2),
	SR_HZ(5),
	SR_HZ(10),
	SR_HZ(15),
	SR_HZ(20),
	SR_HZ(30),
	SR_HZ(40),
	SR_HZ(50),
	SR_HZ(60),
};

static const char *default_serial_parameters[] = {
	"9600/8n1", /* Factory default. */
	"38400/8n1",
	"19200/8n1",
	"4800/8n1",
	NULL,
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
	const char *custom_serial_parameters[2];
	const char **serial_parameters;
	const char *conn, *serialcomm;
	GSList *l;
	struct itech_it8500_cmd_packet *cmd, *response;
	uint8_t fw_major, fw_minor;
	const uint8_t *p;
	char *unit_model, *unit_serial, *unit_barcode;
	double max_i, max_v, min_v, max_p, max_r, min_r;
	uint64_t max_samplerate;

	size_t u, i;
	int ret;

	cmd = g_malloc0(sizeof(*cmd));
	devc = g_malloc0(sizeof(*devc));
	sdi = g_malloc0(sizeof(*sdi));
	if (!cmd || !devc || !sdi)
		return NULL;

	serial = NULL;
	response = NULL;
	unit_model = NULL;
	unit_serial = NULL;

	/*
	 * Use a list of typical parameters for serial communication by
	 * default. Prefer user specified parameters when available.
	 * Lack of a user specified serial port is fatal.
	 */
	conn = NULL;
	serialcomm = NULL;
	serial_parameters = default_serial_parameters;
	for (l = options; l; l = l->next) {
		conf = l->data;
		switch (conf->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(conf->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(conf->data, NULL);
			custom_serial_parameters[0] = serialcomm;
			custom_serial_parameters[1] = NULL;
			serial_parameters = custom_serial_parameters;
			break;
		}
	}
	if (!conn)
		goto error;

	/*
	 * Try different serial parameters in the list
	 * until we get a response (or none at all).
	 */
	sr_info("Probing serial port: %s", conn);
	for (i = 0; (serialcomm = serial_parameters[i]); i++) {
		serial = sr_serial_dev_inst_new(conn, serialcomm);
		if (serial_open(serial, SERIAL_RDWR) != SR_OK)
			goto error;
		serial_flush(serial);

		cmd->address = 0xff; /* Use "broadcast" address. */
		cmd->command = CMD_GET_MODEL_INFO;
		if (itech_it8500_send_cmd(serial, cmd, &response) == SR_OK)
			break;

		serial_close(serial);
		sr_serial_dev_inst_free(serial);
		serial = NULL;
	}
	if (!serialcomm)
		goto error;

	/*
	 * The "dense" response string consists of several fields. Grab
	 * integer data before putting terminators in their place to
	 * grab text strings afterwards. Order is important here.
	 */
	devc->address = response->address;
	fw_major = response->data[6];
	fw_minor = response->data[5];
	response->data[5] = 0;
	unit_model = g_strdup((const char *)&response->data[0]);
	response->data[17] = 0;
	unit_serial = g_strdup((const char *)&response->data[7]);
	sr_info("Model name: %s (v%x.%02x)", unit_model, fw_major, fw_minor);
	sr_info("Address: %d", devc->address);
	sr_info("Serial number: %s", unit_serial);

	sdi->status = SR_ST_INACTIVE;
	sdi->conn = serial;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->driver = &itech_it8500_driver_info;
	sdi->priv = devc;
	g_mutex_init(&devc->mutex);

	/*
	 * Calculate maxium "safe" sample rate based on serial connection
	 * speed / bitrate.
	 */
	max_samplerate = serial->comm_params.bit_rate * 15 / 9600;
	if (max_samplerate < 15)
		max_samplerate = 10;
	if (max_samplerate > MAX_SAMPLE_RATE)
		max_samplerate = MAX_SAMPLE_RATE;
	devc->max_sample_rate_idx = 0;
	for (u = 0; u < ARRAY_SIZE(samplerates); u++) {
		if (samplerates[u] > max_samplerate)
			break;
		devc->max_sample_rate_idx = u;
	}
	devc->sample_rate = DEFAULT_SAMPLE_RATE;

	/*
	 * Get full serial number (barcode).
	 */
	cmd->address = devc->address;
	cmd->command = CMD_GET_BARCODE_INFO;
	if (itech_it8500_send_cmd(serial, cmd, &response) == SR_OK) {
		unit_barcode = g_malloc0(IT8500_DATA_LEN + 1);
		memcpy(unit_barcode, response->data, IT8500_DATA_LEN);
		sr_info("Barcode: %s", response->data);
		g_free(unit_barcode);
	}

	/*
	 * Query unit capabilities.
	 */
	cmd->command = CMD_GET_LOAD_LIMITS;
	if (itech_it8500_send_cmd(serial, cmd, &response) != SR_OK)
		goto error;
	p = response->data;
	max_i = read_u32le_inc(&p) / 10000.0;
	max_v = read_u32le_inc(&p) / 1000.0;
	min_v = read_u32le_inc(&p) / 1000.0;
	max_p = read_u32le_inc(&p) / 1000.0;
	max_r = read_u32le_inc(&p) / 1000.0;
	min_r = read_u16le_inc(&p) / 1000.0;
	sr_info("Max current: %.0f A", max_i);
	sr_info("Max power: %.0f W", max_p);
	sr_info("Voltage range: %.1f - %.1f V", min_v, max_v);
	sr_info("Resistance range: %.2f - %.2f Ohm", min_r, max_r);

	/*
	 * Get current status of the unit.
	 */
	if ((ret = itech_it8500_get_status(sdi)) != SR_OK) {
		sr_err("Failed to get unit status: %d", ret);
		goto error;
	}
	sr_info("Mode: %s", itech_it8500_mode_to_string(devc->mode));
	sr_info("State: %s", devc->load_on ? "ON" : "OFF");
	sr_info("Default sample rate: %" PRIu64 " Hz", devc->sample_rate);
	sr_info("Maximum sample rate: %" PRIu64 " Hz",
		samplerates[devc->max_sample_rate_idx]);

	/*
	 * Populate data structures.
	 */

	devc->fw_ver_major = fw_major;
	devc->fw_ver_minor = fw_minor;
	snprintf(devc->model, sizeof(devc->model), "%s", unit_model);
	devc->max_current = max_i;
	devc->min_voltage = min_v;
	devc->max_voltage = max_v;
	devc->max_power = max_p;
	devc->min_resistance = min_r;
	devc->max_resistance = max_r;

	sdi->vendor = g_strdup("ITECH");
	sdi->model = unit_model;
	sdi->version = g_strdup_printf("%x.%02x", fw_major, fw_minor);
	sdi->serial_num = unit_serial;

	cg = g_malloc0(sizeof(*cg));
	cg->name = g_strdup("1");
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V1");
	cg->channels = g_slist_append(cg->channels, ch);
	ch = sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I1");
	cg->channels = g_slist_append(cg->channels, ch);
	ch = sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, TRUE, "P1");
	cg->channels = g_slist_append(cg->channels, ch);

	g_free(cmd);
	g_free(response);
	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

error:
	g_free(cmd);
	g_free(devc);
	g_free(sdi);
	g_free(response);
	g_free(unit_model);
	g_free(unit_serial);
	if (serial) {
		serial_close(serial);
		sr_serial_dev_inst_free(serial);
	}

	return NULL;
}

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct sr_key_info *kinfo;
	const char *mode;
	int ret, ival;
	gboolean bval;

	(void)cg;

	if (!data || !sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);
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
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_boolean(devc->load_on);
		break;
	case SR_CONF_REGULATION:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		mode = itech_it8500_mode_to_string(devc->mode);
		*data = g_variant_new_string(mode);
		break;
	case SR_CONF_VOLTAGE:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double(devc->voltage);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ret = itech_it8500_get_int(sdi, CMD_GET_CV_VOLTAGE, &ival);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double((double)ival / 1000.0);
		break;
	case SR_CONF_CURRENT:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double(devc->current);
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = itech_it8500_get_int(sdi, CMD_GET_CC_CURRENT, &ival);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double((double)ival / 10000.0);
		break;
	case SR_CONF_POWER:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double(devc->power);
		break;
	case SR_CONF_POWER_TARGET:
		ret = itech_it8500_get_int(sdi, CMD_GET_CW_POWER, &ival);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double((double)ival / 1000.0);
		break;
	case SR_CONF_RESISTANCE_TARGET:
		ret = itech_it8500_get_int(sdi, CMD_GET_CR_RESISTANCE, &ival);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double((double)ival / 1000.0);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		bval = devc->demand_state & DS_OV_FLAG;
		*data = g_variant_new_boolean(bval);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		ret = itech_it8500_get_int(sdi, CMD_GET_MAX_VOLTAGE, &ival);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double((double)ival / 1000.0);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		bval = devc->demand_state & DS_OC_FLAG;
		*data = g_variant_new_boolean(bval);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ret = itech_it8500_get_int(sdi, CMD_GET_MAX_CURRENT, &ival);
		if (ret != SR_OK)
			break;
		*data = g_variant_new_double((double)ival / 10000.0);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		ret = itech_it8500_get_status(sdi);
		if (ret != SR_OK)
			break;
		bval = devc->demand_state & DS_OT_FLAG;
		*data = g_variant_new_boolean(bval);
		break;
	/* Hardware doesn't support under voltage reporting. */
	case SR_CONF_UNDER_VOLTAGE_CONDITION:
	case SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE:
		*data = g_variant_new_boolean(FALSE);
		break;
	case SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD:
		*data = g_variant_new_double(0.0);
		break;
	default:
		sr_dbg("%s: Unsupported key: %u (%s)", __func__, key,
			kinfo ? kinfo->name : "unknown");
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct itech_it8500_cmd_packet *cmd, *response;
	const struct sr_key_info *kinfo;
	enum itech_it8500_modes mode;
	int ret, ivalue;
	uint64_t new_sr;
	const char *s;

	(void)cg;

	if (!data || !sdi)
		return SR_ERR_ARG;

	cmd = g_malloc0(sizeof(*cmd));
	if (!cmd)
		return SR_ERR_MALLOC;

	devc = sdi->priv;
	response = NULL;
	ret = SR_OK;

	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		ret = sr_sw_limits_config_set(&devc->limits, key, data);
		break;
	case SR_CONF_SAMPLERATE:
		new_sr = g_variant_get_uint64(data);
		if (new_sr < MIN_SAMPLE_RATE) {
			ret = SR_ERR_SAMPLERATE;
			break;
		}
		if (new_sr > samplerates[devc->max_sample_rate_idx]) {
			ret = SR_ERR_SAMPLERATE;
			break;
		}
		devc->sample_rate = new_sr;
		break;
	case SR_CONF_ENABLED:
		cmd->command = CMD_LOAD_ON_OFF;
		cmd->data[0] = g_variant_get_boolean(data);
		break;
	case SR_CONF_REGULATION:
		s = g_variant_get_string(data, NULL);
		if (itech_it8500_string_to_mode(s, &mode) != SR_OK) {
			ret = SR_ERR_ARG;
			break;
		}
		cmd->command = CMD_SET_MODE;
		cmd->data[0] = mode;
		break;
	case SR_CONF_VOLTAGE_TARGET:
		cmd->command = CMD_SET_CV_VOLTAGE;
		ivalue = g_variant_get_double(data) * 1000.0;
		WL32(&cmd->data[0], ivalue);
		break;
	case SR_CONF_CURRENT_LIMIT:
		cmd->command = CMD_SET_CC_CURRENT;
		ivalue = g_variant_get_double(data) * 10000.0;
		WL32(&cmd->data[0], ivalue);
		break;
	case SR_CONF_POWER_TARGET:
		cmd->command = CMD_SET_CW_POWER;
		ivalue = g_variant_get_double(data) * 1000.0;
		WL32(&cmd->data[0], ivalue);
		break;
	case SR_CONF_RESISTANCE_TARGET:
		cmd->command = CMD_SET_CR_RESISTANCE;
		ivalue = g_variant_get_double(data) * 1000.0;
		WL32(&cmd->data[0], ivalue);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		cmd->command = CMD_SET_MAX_VOLTAGE;
		ivalue = g_variant_get_double(data) * 1000.0;
		WL32(&cmd->data[0], ivalue);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		cmd->command = CMD_SET_MAX_CURRENT;
		ivalue = g_variant_get_double(data) * 10000.0;
		WL32(&cmd->data[0], ivalue);
		break;
	default:
		sr_dbg("%s: Unsupported key: %u (%s)", __func__, key,
			kinfo ? kinfo->name : "unknown");
		ret = SR_ERR_NA;
		break;
	}

	if (ret == SR_OK && cmd->command) {
		cmd->address = devc->address;
		ret = itech_it8500_cmd(sdi, cmd, &response);
	}

	g_free(cmd);
	g_free(response);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	const struct dev_context *devc;
	const struct sr_key_info *kinfo;
	GVariantBuilder *b;

	devc = sdi ? sdi->priv : NULL;
	if (!data)
		return SR_ERR_ARG;

	if (!cg)
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);

	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
		break;
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(samplerates,
				1 + devc->max_sample_rate_idx);
		break;
	case SR_CONF_REGULATION:
		b = g_variant_builder_new(G_VARIANT_TYPE("as"));
		g_variant_builder_add(b, "s", itech_it8500_mode_to_string(CC));
		g_variant_builder_add(b, "s", itech_it8500_mode_to_string(CV));
		g_variant_builder_add(b, "s", itech_it8500_mode_to_string(CW));
		g_variant_builder_add(b, "s", itech_it8500_mode_to_string(CR));
		*data = g_variant_new("as", b);
		g_variant_builder_unref(b);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(devc->min_voltage,
				devc->max_voltage, 0.01);
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(0.0, devc->max_current, 0.001);
		break;
	case SR_CONF_POWER_TARGET:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(0.0, devc->max_power, 0.01);
		break;
	case SR_CONF_RESISTANCE_TARGET:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(devc->min_resistance,
				devc->max_resistance, 0.01);
		break;

	default:
		sr_dbg("%s: Unsupported key: %u (%s)", __func__, key,
			kinfo ? kinfo->name : "unknown");
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	serial = sdi->conn;

	ret = serial_source_add(sdi->session, serial,
			G_IO_IN, (1000.0 / devc->sample_rate),
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

	if (!sdi)
		return SR_ERR_ARG;

	serial = sdi->conn;

	std_session_send_df_end(sdi);
	serial_source_remove(sdi->session, serial);

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct itech_it8500_cmd_packet *cmd, *response;
	int ret, res;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	ret = std_serial_dev_open(sdi);
	if (ret == SR_OK) {
		/* Request the unit to enter remote control mode. */
		response = NULL;
		cmd = g_malloc0(sizeof(*cmd));
		if (cmd) {
			cmd->address = devc->address;
			cmd->command = CMD_SET_REMOTE_MODE;
			cmd->data[0] = 1;
			res = itech_it8500_cmd(sdi, cmd, &response);
			if (res != SR_OK)
				sr_dbg("Failed to set unit to remote mode");
			g_free(cmd);
			g_free(response);
		}
	}

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct itech_it8500_cmd_packet *cmd, *response;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	response = NULL;
	cmd = g_malloc0(sizeof(*cmd));
	if (cmd) {
		/* Request the unit to enter local control mode. */
		cmd->address = devc->address;
		cmd->command = CMD_SET_REMOTE_MODE;
		cmd->data[0] = 0;
		ret = itech_it8500_cmd(sdi, cmd, &response);
		if (ret != SR_OK)
			sr_dbg("Failed to set unit back to local mode: %d",
				ret);
	}

	g_free(cmd);
	g_free(response);

	return std_serial_dev_close(sdi);
}

static void dev_clear_callback(void *priv)
{
	struct dev_context *devc;

	if (!priv)
		return;

	devc = priv;
	g_mutex_clear(&devc->mutex);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, dev_clear_callback);
}

static struct sr_dev_driver itech_it8500_driver_info = {
	.name = "itech-it8500",
	.longname = "ITECH IT8500 series",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(itech_it8500_driver_info);
