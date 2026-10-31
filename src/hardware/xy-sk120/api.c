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

#include "config.h"

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
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_POWER | SR_CONF_GET,
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	
	/* TODO optional additional features
	
	SR_CONF_POWER_TARGET | SR_CONF_GET | SR_CONF_SET, // Limited usefulness since constant power mode is not properly supported. (SR_CONF_REGULATION has no mode for it)
    SR_CONF_OVER_POWER_PROTECTION_ENABLED, //(always on -> is it useful?)
	
	SR_CONF_OVER_POWER_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_POWER_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET, 
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET | SR_CONF_SET,
	
	SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE | SR_CONF_GET,
	SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_UNDER_VOLTAGE_CONDITION, //?
	
	SR_CONF_THERMOMETER, //Is this one supposed to be used for this purpose?
	*/

};

static const uint32_t devopts_w_range[] = {
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
	SR_CONF_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

/* Range name, max current/voltage/power, current/voltage digits. */
static const struct xy_sk120_range ranges_xy_sk120[] = {
	{"120W",
		6.1,
		36.5,  
		125, 
		3,
		2 }
};

/* Model ID, model name, model dependent ranges. */
static const struct xy_sk120_model supported_models[] = {
	{ MODEL_XY_SK, 22873, "XY-SK120", ARRAY_AND_SIZE(ranges_xy_sk120), },
};

static struct sr_dev_driver xy_sk120_driver_info;

static struct sr_dev_inst *probe_device(struct sr_modbus_dev_inst *modbus,
	enum xy_sk120_model_type model_type)
{
	static const char *type_prefix[] = {
		[MODEL_XY_SK] = "XY-SK",
	};

	uint16_t id, version;
	uint32_t serno;
	int ret;
	const struct xy_sk120_model *model, *supported;
	size_t i;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	ret = xy_sk120_get_model_version(modbus,
		model_type, &id, &version, &serno);
	sr_dbg("probe: ret %d, type %s, model %u, vers %u, snr %u.",
		ret, type_prefix[model_type], id, version, serno);
	if (ret != SR_OK)
		return NULL;
	model = NULL;
	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		supported = &supported_models[i];
		if (model_type != supported->model_type)
			continue;
		if (id != supported->id)
			continue;
		model = supported;
		break;
	}
	if (!model) {
		sr_err("Unknown model id for %s device: %u", type_prefix[model_type], id);
		return NULL;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("XY-SK");
	switch (model_type) {
	case MODEL_XY_SK:
		sdi->model = g_strdup(model->name);
		sdi->version = g_strdup_printf("v%u.%u.%u", version/100, version/10%10, version%10); //TODO do the decoding of the version number elsewhere in the code?
		sdi->driver = &xy_sk120_driver_info;
		break;
	default:
		sr_err("Programming error, unhandled XY-SK device type.");
		g_free(sdi->vendor);
		g_free(sdi);
		return NULL;
	}

	sdi->conn = modbus;
	sdi->inst_type = SR_INST_MODBUS;

	//TODO maybe subdivide channels into two groups (Output, Internal). sr_channel_group_new(sdi, "Output", NULL);
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");
	sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, TRUE, "P");
	sr_channel_new(sdi, 3, SR_CHANNEL_ANALOG, TRUE, "Q"); /* Electric Charge */
	sr_channel_new(sdi, 4, SR_CHANNEL_ANALOG, TRUE, "E"); /* Energy */
	sr_channel_new(sdi, 5, SR_CHANNEL_ANALOG, TRUE, "V_source"); /* Voltage which powers the device itself*/
	sr_channel_new(sdi, 6, SR_CHANNEL_ANALOG, TRUE, "T_internal"); /* Internal Temperature */


	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	sr_sw_limits_init(&devc->limits);
	devc->model = model;
	xy_sk120_update_multipliers(sdi);
	if (ret != SR_OK)
		return NULL;

	return sdi;
}

static struct sr_dev_inst *probe_device_xy_sk120(struct sr_modbus_dev_inst *modbus)
{
	return probe_device(modbus, MODEL_XY_SK);
}

static int config_compare(gconstpointer a, gconstpointer b)
{
	const struct sr_config *ac = a, *bc = b;
	return ac->key != bc->key;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options,
	enum xy_sk120_model_type model_type)
{
	static const char *default_serialcomm_xy = "115200/8n1";

	struct sr_config default_serialcomm = {
		.key = SR_CONF_SERIALCOMM,
		.data = NULL,
	};
	struct sr_config default_modbusaddr = {
		.key = SR_CONF_MODBUSADDR,
		.data = g_variant_new_uint64(1),
	};
	GSList *opts, *devices;
	const char *serialcomm;
	struct sr_dev_inst *(*probe_func)(struct sr_modbus_dev_inst *modbus);

	/* TODO See why di->context isn't available yet at this time. */
	serialcomm = NULL;
	probe_func = NULL;
	if (di->context == &xy_sk120_driver_info || model_type == MODEL_XY_SK) {
		serialcomm = default_serialcomm_xy;
		probe_func = probe_device_xy_sk120;
	}
	if (!probe_func)
		return NULL;
	if (serialcomm && *serialcomm)
		default_serialcomm.data = g_variant_new_string(serialcomm);

	opts = options;
	if (!g_slist_find_custom(options, &default_serialcomm, config_compare))
		opts = g_slist_prepend(opts, &default_serialcomm);
	if (!g_slist_find_custom(options, &default_modbusaddr, config_compare))
		opts = g_slist_prepend(opts, &default_modbusaddr);

	devices = sr_modbus_scan(di->context, opts, probe_func);

	while (opts != options)
		opts = g_slist_delete_link(opts, opts);
	g_variant_unref(default_serialcomm.data);
	g_variant_unref(default_modbusaddr.data);

	return devices;
}

static GSList *scan_xy(struct sr_dev_driver *di, GSList *options)
{
	return scan(di, options, MODEL_XY_SK);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;
	struct xy_sk120_state state;
	int ret;

	modbus = sdi->conn;
	if (sr_modbus_open(modbus) < 0)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;
	struct xy_sk120_state state;

	modbus = sdi->conn;
	if (!modbus)
		return SR_ERR_BUG;

	return sr_modbus_close(modbus);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct xy_sk120_state state;
	int ret;
	const char *cc_text, *range_text;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		ret = sr_sw_limits_config_get(&devc->limits, key, data);
		break;
	case SR_CONF_ENABLED:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_OUTPUT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.output_enabled);
		break;
	case SR_CONF_REGULATION:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_REGULATION_CC))
			return SR_ERR_DATA;
		cc_text = state.regulation_cc ? "CC" : "CV"; //TODO minor improvement: return "" if the output is off?
		*data = g_variant_new_string(cc_text);
		break;
	case SR_CONF_VOLTAGE:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_VOLTAGE))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.voltage);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_VOLTAGE_TARGET))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.voltage_target);
		break;
	case SR_CONF_CURRENT:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_CURRENT))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.current);
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_CURRENT_LIMIT))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.current_limit);
		break;
	case SR_CONF_POWER:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		/* TODO actually implement ?
		if (!(state.mask & STATE_POWER))
			return SR_ERR_DATA;
		*/
		*data = g_variant_new_double(state.power);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_enabled);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_OVP))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_ovp);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_OUTPUT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.ovp_threshold);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_ENABLED))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_enabled);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_PROTECT_OCP))
			return SR_ERR_DATA;
		*data = g_variant_new_boolean(state.protect_ocp);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_OCP_THRESHOLD))
			return SR_ERR_DATA;
		*data = g_variant_new_double(state.ocp_threshold);
		break;
	case SR_CONF_RANGE:
		ret = xy_sk120_get_state(sdi, &state, ST_CTX_CONFIG);
		if (ret != SR_OK)
			return ret;
		if (!(state.mask & STATE_RANGE))
			return SR_ERR_DATA;
		range_text = devc->model->ranges[state.range].range_str;
		*data = g_variant_new_string(range_text);
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
	struct xy_sk120_state state;
	const char *range_str;
	const struct xy_sk120_range *range;
	size_t i;

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
		return xy_sk120_set_state(sdi, &state);
	case SR_CONF_VOLTAGE_TARGET:
		state.voltage_target = g_variant_get_double(data);
		state.mask |= STATE_VOLTAGE_TARGET;
		return xy_sk120_set_state(sdi, &state);
	case SR_CONF_CURRENT_LIMIT:
		state.current_limit = g_variant_get_double(data);
		state.mask |= STATE_CURRENT_LIMIT;
		return xy_sk120_set_state(sdi, &state);
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		state.ovp_threshold = g_variant_get_double(data);
		state.mask |= STATE_OVP_THRESHOLD;
		return xy_sk120_set_state(sdi, &state);
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		state.ocp_threshold = g_variant_get_double(data);
		state.mask |= STATE_OCP_THRESHOLD;
		return xy_sk120_set_state(sdi, &state);
	case SR_CONF_RANGE:
		range_str = g_variant_get_string(data, NULL);
		for (i = 0; i < devc->model->n_ranges; i++) {
			range = &devc->model->ranges[i];
			if (g_strcmp0(range->range_str, range_str) != 0)
				continue;
			state.range = i;
			state.mask |= STATE_RANGE;
			return xy_sk120_set_state(sdi, &state);
		}
		return SR_ERR_NA;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct xy_sk120_range *range;
	GVariantBuilder gvb;
	size_t i;
	const char *s;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (devc && devc->model->n_ranges > 1) {
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts_w_range);
		} else {
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts);
		}
	case SR_CONF_VOLTAGE_TARGET:
		xy_sk120_update_range(sdi);
		range = &devc->model->ranges[devc->curr_range];
		*data = std_gvar_min_max_step(0.0, range->max_voltage,
			1 / devc->voltage_multiplier);
		break;
	case SR_CONF_CURRENT_LIMIT:
		xy_sk120_update_range(sdi);
		range = &devc->model->ranges[devc->curr_range];
		*data = std_gvar_min_max_step(0.0, range->max_current,
			1 / devc->current_multiplier);
		break;
	case SR_CONF_RANGE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < devc->model->n_ranges; i++) {
			s = devc->model->ranges[i].range_str;
			g_variant_builder_add(&gvb, "s", s);
		}
		*data = g_variant_builder_end(&gvb);
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

	devc->acquisition_started = TRUE;

	/* Seed internal state from current data. */
	ret = xy_sk120_seed_receive(sdi);
	if (ret != SR_OK) {
		devc->acquisition_started = FALSE;
		return ret;
	}

	/* Register the periodic data reception callback. */
	ret = sr_modbus_source_add(sdi->session, modbus, G_IO_IN, 10,
			xy_sk120_receive_data, (void *)sdi);
	if (ret != SR_OK) {
		devc->acquisition_started = FALSE;
		return ret;
	}

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;

	devc = sdi->priv;

	std_session_send_df_end(sdi);
	devc->acquisition_started = FALSE;

	modbus = sdi->conn;
	sr_modbus_source_remove(sdi->session, modbus);

	return SR_OK;
}

static struct sr_dev_driver xy_sk120_driver_info = {
	.name = "xy-sk120",
	.longname = "XY-SK120 power supply",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan_xy,
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
SR_REGISTER_DEV_DRIVER(xy_sk120_driver_info);

