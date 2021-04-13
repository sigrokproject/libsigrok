/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 James Churchill <pelrun@gmail.com>
 * Copyright (C) 2019 Frank Stettner <frank-stettner@gmx.net>
 * Copyright (C) 2021 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <math.h>
#include <string.h>

#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	SR_CONF_MODBUSADDR,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
};

/* Model ID, model name, max current/voltage/power, current/voltage digits. */
static const struct rdtech_dps_model supported_models[] = {
	{ 3005, "DPS3005",  5, 30,  160, 3, 2 },
	{ 5005, "DPS5005",  5, 50,  250, 3, 2 },
	{ 5205, "DPH5005",  5, 50,  250, 3, 2 },
	{ 5015, "DPS5015", 15, 50,  750, 2, 2 },
	{ 5020, "DPS5020", 20, 50, 1000, 2, 2 },
	{ 8005, "DPS8005",  5, 80,  408, 3, 2 },
};

static struct sr_dev_driver rdtech_dps_driver_info;

static struct sr_dev_inst *probe_device(struct sr_modbus_dev_inst *modbus)
{
	uint16_t id, version;
	int ret;
	const struct rdtech_dps_model *model;
	size_t i;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	ret = rdtech_dps_get_model_version(modbus, &id, &version);
	if (ret != SR_OK)
		return NULL;
	model = NULL;
	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (id == supported_models[i].id) {
			model = &supported_models[i];
			break;
		}
	}
	if (!model) {
		sr_err("Unknown model: %u.", id);
		return NULL;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("RDTech");
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup_printf("v%u", version);
	sdi->conn = modbus;
	sdi->driver = &rdtech_dps_driver_info;
	sdi->inst_type = SR_INST_MODBUS;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");
	sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, TRUE, "P");

	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);
	devc->model = model;
	devc->current_multiplier = pow(10.0, model->current_digits);
	devc->voltage_multiplier = pow(10.0, model->voltage_digits);

	sdi->priv = devc;

	return sdi;
}

static int config_compare(gconstpointer a, gconstpointer b)
{
	const struct sr_config *ac = a, *bc = b;
	return ac->key != bc->key;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config default_serialcomm = {
		.key = SR_CONF_SERIALCOMM,
		.data = g_variant_new_string("9600/8n1"),
	};
	struct sr_config default_modbusaddr = {
		.key = SR_CONF_MODBUSADDR,
		.data = g_variant_new_uint64(1),
	};
	GSList *opts, *devices;

	opts = options;
	if (!g_slist_find_custom(options, &default_serialcomm, config_compare))
		opts = g_slist_prepend(opts, &default_serialcomm);
	if (!g_slist_find_custom(options, &default_modbusaddr, config_compare))
		opts = g_slist_prepend(opts, &default_modbusaddr);

	devices = sr_modbus_scan(di->context, opts, probe_device);

	while (opts != options)
		opts = g_slist_delete_link(opts, opts);
	g_variant_unref(default_serialcomm.data);
	g_variant_unref(default_modbusaddr.data);

	return devices;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;
	struct rdtech_dps_state state;
	int ret;

	modbus = sdi->conn;
	if (sr_modbus_open(modbus) < 0)
		return SR_ERR;

	memset(&state, 0, sizeof(state));
	state.lock = TRUE;
	state.mask |= STATE_LOCK;
	ret = rdtech_dps_set_state(sdi, &state);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;
	struct rdtech_dps_state state;

	modbus = sdi->conn;
	if (!modbus)
		return SR_ERR_BUG;

	memset(&state, 0, sizeof(state));
	state.lock = FALSE;
	state.mask |= STATE_LOCK;
	(void)rdtech_dps_set_state(sdi, &state);

	return sr_modbus_close(modbus);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct rdtech_dps_state state;
	int ret;
	const char *cc_text;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		ret = sr_sw_limits_config_get(&devc->limits, key, data);
		break;
	case SR_CONF_ENABLED:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_OUTPUT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.output_enabled);
		break;
	case SR_CONF_REGULATION:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_REGULATION_CC))
			return SR_ERR_DATA;
		cc_text = state.regulation_cc ? "CC" : "CV";
		*data = g_variant_new_string(cc_text);
		break;
	case SR_CONF_VOLTAGE:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_VOLTAGE))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.voltage);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_VOLTAGE_TARGET))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.voltage_target);
		break;
	case SR_CONF_CURRENT:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_CURRENT))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.current);
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_CURRENT_LIMIT))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.current_limit);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_enabled);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_OVP))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_ovp);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_OUTPUT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.ovp_threshold);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_enabled);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_OCP))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_ocp);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ret = rdtech_dps_get_state(sdi, &state);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_OCP_THRESHOLD))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.ocp_threshold);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct rdtech_dps_state state;

	(void)cg;

	devc = sdi->priv;
	memset(&state, 0, sizeof(state));

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		state.output_enabled = g_variant_get_boolean(data);
		state.mask |= STATE_OUTPUT_ENABLED;
		return rdtech_dps_set_state(sdi, &state);
	case SR_CONF_VOLTAGE_TARGET:
		state.voltage_target = g_variant_get_double(data);
		state.mask |= STATE_VOLTAGE_TARGET;
		return rdtech_dps_set_state(sdi, &state);
	case SR_CONF_CURRENT_LIMIT:
		state.current_limit = g_variant_get_double(data);
		state.mask |= STATE_CURRENT_LIMIT;
		return rdtech_dps_set_state(sdi, &state);
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		state.ovp_threshold = g_variant_get_double(data);
		state.mask |= STATE_OVP_THRESHOLD;
		return rdtech_dps_set_state(sdi, &state);
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		state.ocp_threshold = g_variant_get_double(data);
		state.mask |= STATE_OCP_THRESHOLD;
		return rdtech_dps_set_state(sdi, &state);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_VOLTAGE_TARGET:
		*data = std_gvar_min_max_step(0.0, devc->model->max_voltage,
			1 / devc->voltage_multiplier);
		break;
	case SR_CONF_CURRENT_LIMIT:
		*data = std_gvar_min_max_step(0.0, devc->model->max_current,
			1 / devc->current_multiplier);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	int ret;

	modbus = sdi->conn;
	devc = sdi->priv;

	/* Seed internal state from current data. */
	ret = rdtech_dps_seed_receive(sdi);
	if (ret != SR_OK)
		return ret;

	/* Register the periodic data reception callback. */
	ret = sr_modbus_source_add(sdi->session, modbus, G_IO_IN, 10,
			rdtech_dps_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;

	std_session_send_df_end(sdi);

	modbus = sdi->conn;
	sr_modbus_source_remove(sdi->session, modbus);

	return SR_OK;
}

static struct sr_dev_driver rdtech_dps_driver_info = {
	.name = "rdtech-dps",
	.longname = "RDTech DPS/DPH series power supply",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(rdtech_dps_driver_info);
