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
#include "scpi.h"
#include "protocol.h"

#define SERIALCOMM "9600/8n1"

static const char *manufacturer = "THURLBY THANDAR";

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts_wtracking[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_SET,
	SR_CONF_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_wotracking[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_ENABLED | SR_CONF_SET,
};

static const uint32_t cg_devopts[] = {
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET, /* "CV", "CC"*/
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET
			| SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET
			| SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET
};

static const char* tracking_config[] = {
	"Independent",
	"Track"
};

static struct sr_dev_driver aim_tti_dps_driver_info;

static const struct aim_tti_dps_model models[] = {
	{"CPX200DP", 2, 180.0, {0, 60.0, 0.01}, {0, 10.0, 0.001}},
	{"CPX400SP", 1, 420.0, {0, 60.0, 0.01}, {0, 20.0, 0.001}},
	{"CPX400DP", 2, 420.0, {0, 60.0, 0.01}, {0, 20.0, 0.001}},

	{"QPX1200",  1, 1200.0, {0, 60.0, 0.001}, {0, 50.0, 0.01}},
	{"QPX600DP", 2,  600.0, {0, 80.0, 0.001}, {0, 50.0, 0.01}},
	// QPX750SP has a different command set

	{"MX100TP", 3, 105.0, {0,  70.0, 0.001}, {0,  6.0, 0.0001}},
	{"MX180TP", 3, 125.0, {0, 120.0, 0.001}, {0, 20.0, 0.001}},
	{"MX100QP", 3, 105.0, {0,  70.0, 0.001}, {0,  6.0, 0.0001}},

	{"QL355P",  1, 105.0, {0, 35.0, 0.001}, {0, 5.0, 0.0001}},
	{"QL564P",  1, 105.0, {0, 56.0, 0.001}, {0, 4.0, 0.0001}},
	{"QL355TP", 3, 105.0, {0, 35.0, 0.001}, {0, 5.0, 0.0001}},
	{"QL564TP", 3, 105.0, {0, 56.0, 0.001}, {0, 4.0, 0.0001}},

	{"PLH120-P", 1, .0, {0, 120.0, 0.001}, {0, 0.75,  0.0001}},
	{"PLH250-P", 1, .0, {0, 250.0, 0.001}, {0, 0.375, 0.0001}},

	ALL_ZERO
};

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_scpi_hw_info *hw_info;
	int ret;
	size_t model_idx;
	int model_found;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	char buf[13];

	ret = sr_scpi_get_hw_id(scpi, &hw_info);
	if (ret != SR_OK) {
		sr_info("Could not get IDN response.");
		return NULL;
	}

	if (strcmp(hw_info->manufacturer, manufacturer) != 0) {
		sr_info("not an THURLBY THANDAR device.");
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	model_idx = 0;
	model_found = 0;
	for (model_idx = 0; model_idx < ARRAY_SIZE(models); model_idx++) {
		if (strcmp(models[model_idx].name, hw_info->model) == 0) {
			model_found = 1;
			break;
		}
	}
	if (!model_found) {
		sr_err("Unknown/unsupported type: %s", hw_info->model);
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->conn = scpi;
	sdi->driver = &aim_tti_dps_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	ret = sr_scpi_connection_id(scpi, &sdi->connection_id);
	if (ret != SR_OK) {
		g_free(sdi->connection_id);
		sdi->connection_id = NULL;
	}
	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	for (int chidx = 0 ; chidx < models[model_idx].channels; ++chidx) {
		cg = g_malloc0(sizeof(struct sr_channel_group));
		snprintf(buf, ARRAY_SIZE(buf), "CH%d", chidx+1);
		cg->name = g_strdup(buf);
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

		snprintf(buf, ARRAY_SIZE(buf), "V%d", chidx+1);
		ch = sr_channel_new(sdi, 2*chidx, SR_CHANNEL_ANALOG, TRUE, buf);
		cg->channels = g_slist_append(cg->channels, ch);

		snprintf(buf, ARRAY_SIZE(buf), "I%d", chidx+1);
		ch = sr_channel_new(sdi, 2*chidx + 1, SR_CHANNEL_ANALOG, TRUE, buf);
		cg->channels = g_slist_append(cg->channels, ch);
		cg->priv = malloc(sizeof(int));
		*(int*)(cg->priv) = chidx;
	}

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&(devc->limits));
	devc->model_config = &models[model_idx];
	g_mutex_init(&devc->rw_mutex);
	devc->config = g_malloc0(sizeof(struct per_channel_dev_context) *
									models[model_idx].channels);
	sdi->priv = devc;

	if (aim_tti_dps_sync_state(scpi, devc) < 0) {
		sr_scpi_close(sdi->conn);
		sr_dev_inst_free(sdi);
		g_mutex_clear(&devc->rw_mutex);
		g_free(devc);
		sr_dbg("Scan failed.");
		return NULL;
	}
	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	int ret;

	scpi = sdi->conn;
	ret = sr_scpi_open(scpi);
	if (ret < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;
	if (!sdi || !scpi)
		return SR_ERR_BUG;

	sr_dbg("DIAG: sdi->status %d.", sdi->status - SR_ST_NOT_FOUND);
	if (sdi->status <= SR_ST_INACTIVE)
		return SR_OK;

	/* enable local interface again */
	sr_scpi_send(scpi, "LOCAL");

	devc = sdi->priv;
	if (devc)
		g_mutex_clear(&devc->rw_mutex);

	return sr_scpi_close(sdi->conn);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int channel;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
		case SR_CONF_LIMIT_MSEC:
			return sr_sw_limits_config_get(&devc->limits, key, data);
		case SR_CONF_CONN:
			*data = g_variant_new_string(sdi->connection_id);
			break;
		case SR_CONF_CHANNEL_CONFIG:
			*data = g_variant_new_string((devc->tracking_enabled) ?
											"Track" :
											"Independent");
			break;
		case SR_CONF_ENABLED:
			/* no readback possible */
		default:
			return SR_ERR_NA;
		}
	} else {
		channel = *(int*)(cg->priv);
		switch (key) {
		case SR_CONF_VOLTAGE:
			*data = g_variant_new_double(devc->config[channel].actual_voltage);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = g_variant_new_double(devc->config[channel].voltage_target);
			break;
		case SR_CONF_CURRENT:
			*data = g_variant_new_double(devc->config[channel].actual_current);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = g_variant_new_double(devc->config[channel].current_limit);
			break;
		case SR_CONF_ENABLED:
			*data = g_variant_new_boolean(devc->config[channel].output_enabled);
			break;
		case SR_CONF_REGULATION:
			*data = g_variant_new_string(
					(devc->config[channel].output_enabled == FALSE) ?
						"" :
							((devc->config[channel].mode == AIM_TTI_CC) ?
								"CC" :
								((devc->config[channel].mode == AIM_TTI_CV) ?
									"CV" :
									"UR")));
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
			*data = g_variant_new_double(
					devc->config[channel].over_voltage_protection_threshold);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			*data = g_variant_new_double(
					devc->config[channel].over_current_protection_threshold);
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
			*data = g_variant_new_boolean(devc->config[channel].ovp_active);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
			*data = g_variant_new_boolean(devc->config[channel].ocp_active);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	double dval;
	gboolean bval;
	int channel, chidx;
	const char *ch_config;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_MSEC:
		case SR_CONF_LIMIT_SAMPLES:
			return sr_sw_limits_config_set(&devc->limits, key, data);
		case SR_CONF_ENABLED:
			bval = g_variant_get_boolean(data);
			for (chidx = 0 ; chidx < devc->model_config->channels ; ++chidx) {
				devc->config[chidx].output_enabled = bval;
				devc->config[chidx].mode_changed = TRUE;
			}
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_OUTPUT_ENABLE_DUAL, 0) < 0)
				return SR_ERR;
			break;
		case SR_CONF_CHANNEL_CONFIG:
			ch_config = g_variant_get_string(data, NULL);
			devc->tracking_enabled = (strcmp(ch_config, "Track") == 0);
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_TRACKING_ENABLE, 0) < 0)
				return SR_ERR;
			break;
		default:
			return SR_ERR_NA;
		}
	}
	else {
		channel = *(int*)(cg->priv);
		switch (key) {
		case SR_CONF_VOLTAGE_TARGET:
			dval = g_variant_get_double(data);
			if (dval < devc->model_config->voltage[0] ||
					dval > devc->model_config->voltage[1])
				return SR_ERR_ARG;
			if (devc->config[channel].voltage_target == dval)
				break;
			devc->config[channel].voltage_target = dval;
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_VOLTAGE_TARGET, channel) < 0)
				return SR_ERR;
			break;
		case SR_CONF_CURRENT_LIMIT:
			dval = g_variant_get_double(data);
			if (dval < devc->model_config->current[0] ||
					dval > devc->model_config->current[1])
				return SR_ERR_ARG;
			if (devc->config[channel].current_limit == dval)
				break;
			devc->config[channel].current_limit = dval;
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_CURRENT_LIMIT, channel) < 0)
				return SR_ERR;
			break;
		case SR_CONF_ENABLED:
			bval = g_variant_get_boolean(data);
			if (devc->config[channel].output_enabled != bval)
				devc->config[channel].mode_changed = TRUE;
			devc->config[channel].output_enabled = bval;
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_OUTPUT_ENABLE, channel) < 0)
				return SR_ERR;
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
			dval = g_variant_get_double(data);
			if (devc->config[channel].over_voltage_protection_threshold == dval)
				break;
			devc->config[channel].over_voltage_protection_threshold = dval;
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_OVP_THRESHOLD, channel) < 0)
				return SR_ERR;
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			dval = g_variant_get_double(data);
			if (devc->config[channel].over_current_protection_threshold == dval)
				break;
			devc->config[channel].over_current_protection_threshold = dval;
			if (aim_tti_dps_set_value(sdi->conn, devc,
					AIM_TTI_OCP_THRESHOLD, channel) < 0)
				return SR_ERR;
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	devc = (sdi) ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
					devopts_wotracking);
		case SR_CONF_DEVICE_OPTIONS:
			if (!devc || !devc->model_config)
				return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
						devopts_wotracking);
			else if (devc->model_config->channels == 1)
				return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
						devopts_wotracking);
			else
				return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
						devopts_wtracking);
		case SR_CONF_CHANNEL_CONFIG:
			if (!devc || !devc->model_config)
				return SR_ERR_ARG;
			if (devc->model_config->channels == 1)
				return SR_ERR_ARG;
			*data = std_gvar_array_str(tracking_config, 2);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(cg_devopts));
			break;
		case SR_CONF_VOLTAGE_TARGET:
		case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
			if (!devc || !devc->model_config)
				return SR_ERR_ARG;
			*data = std_gvar_min_max_step_array(devc->model_config->voltage);
			break;
		case SR_CONF_CURRENT_LIMIT:
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			if (!devc || !devc->model_config)
				return SR_ERR_ARG;
			*data = std_gvar_min_max_step_array(devc->model_config->current);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;

	if (!(devc = sdi->priv) || !(scpi = sdi->conn))
		return SR_ERR_BUG;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 100,
			aim_tti_dps_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	std_session_send_df_end(sdi);
	sr_scpi_source_remove(sdi->session, sdi->conn);
	return SR_OK;

	return std_serial_dev_acquisition_stop(sdi);
}

static struct sr_dev_driver aim_tti_dps_driver_info = {
	.name = "aim-tti-dps",
	.longname = "Aim-TTi DC Power Supplies",
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

SR_REGISTER_DEV_DRIVER(aim_tti_dps_driver_info);

