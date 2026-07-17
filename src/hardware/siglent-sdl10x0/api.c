/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Timo Boettcher <timo@timoboettcher.name>
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
#include "scpi.h"
#include "protocol.h"

static const char *manufacturers[] = {
	"Siglent Technologies",
};

static const char *models[] = {
	"SDL1020X-E",
	"SDL1020X",
	"SDL1030X-E",
	"SDL1030X",
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
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
	SR_CONF_RESISTANCE | SR_CONF_GET,
	SR_CONF_RESISTANCE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_POWER_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_POWER_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_POWER_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
};

static const char *regulation[] = {
	"CURRENT",
	"VOLTAGE",
	"POWER",
	"RESISTANCE"
};

static struct sr_dev_driver siglent_sdl10x0_driver_info;

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		goto fail;
	}

	if (std_str_idx_s(hw_info->manufacturer, ARRAY_AND_SIZE(manufacturers)) < 0)
		goto fail;

	if (std_str_idx_s(hw_info->model, ARRAY_AND_SIZE(models)) < 0)
		goto fail;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->driver = &siglent_sdl10x0_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;
	sdi->channels = NULL;
	sdi->channel_groups = NULL;

	cg = sr_channel_group_new(sdi, "1", NULL);

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "1");
	cg->channels = g_slist_append(cg->channels, ch);

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));
	sdi->priv = devc;

	/*
	 * modelname 'SDL1020X-E':
	 * 6th character indicates wattage:
	 * 2 => 200
	 * 3 => 300
	 */
	devc->maxpower = 200.0;
	if (g_ascii_strncasecmp(sdi->model, "SDL1030", strlen("SDL1030")) == 0) {
		devc->maxpower = 300.0;
	}

	return sdi;

fail:
	sr_scpi_hw_info_free(hw_info);
	sr_dev_inst_free(sdi);
	g_free(devc);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return sr_scpi_open(sdi->conn);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	float ival;
	gboolean bval;
	char *mode;

	(void)cg;
	struct dev_context *devc;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		ret = sr_scpi_get_bool(sdi->conn, ":INPUT?", &bval);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_boolean(bval);
		break;
	case SR_CONF_REGULATION:
		ret = sr_scpi_get_string(sdi->conn, ":FUNC?", &mode);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_string(mode);
		break;
	case SR_CONF_VOLTAGE:
		ret = sr_scpi_get_float(sdi->conn, "MEAS:VOLTage?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ret = sr_scpi_get_float(sdi->conn, ":VOLTage:LEVel?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_CURRENT:
		ret = sr_scpi_get_float(sdi->conn, "MEAS:CURRent?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = sr_scpi_get_float(sdi->conn, ":CURRENT:LEVel?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_POWER:
		ret = sr_scpi_get_float(sdi->conn, "MEAS:POWer?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_POWER_TARGET:
		ret = sr_scpi_get_float(sdi->conn, ":POWer:LEVel?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_RESISTANCE:
		ret = sr_scpi_get_float(sdi->conn, "MEAS:RESistance?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_RESISTANCE_TARGET:
		ret = sr_scpi_get_float(sdi->conn, ":RESistance:LEVel?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_OVER_POWER_PROTECTION_ENABLED:
		/* Always true */
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_POWER_PROTECTION_ACTIVE:
		ret = sr_scpi_get_bool(sdi->conn, ":POWer:PROTection:STATe?", &bval);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_boolean(bval);
		break;
	case SR_CONF_OVER_POWER_PROTECTION_THRESHOLD:
		ret = sr_scpi_get_float(sdi->conn, ":POWer:PROTection:LEVel?", &ival);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		/* Always true */
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		ret = sr_scpi_get_bool(sdi->conn, ":CURRent:PROTection:STATe?", &bval);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_boolean(bval);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ret = sr_scpi_get_bool(sdi->conn, ":CURRent:PROTection:LEVel?", &bval);
		if (ret != SR_OK)
			return SR_ERR;
		*data = g_variant_new_double((double)ival);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	double ivalue;
	gboolean ena;
	char command[64];
	const char *mode_str;
	enum siglent_sdl10x0_modes mode;

	(void)cg;
	struct dev_context *devc;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		ena = g_variant_get_boolean(data);
		g_snprintf(command, sizeof(command), ":INPUT %s", ena ? "ON" : "OFF");
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_REGULATION:
		mode_str = g_variant_get_string(data, NULL);
		if (siglent_sdl10x0_string_to_mode(mode_str, &mode) != SR_OK) {
			ret = SR_ERR_ARG;
			break;
		}
		g_snprintf(command, sizeof(command), ":FUNC %s", siglent_sdl10x0_mode_to_longstring(mode));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ivalue = g_variant_get_double(data);
		g_snprintf(command, sizeof(command), ":VOLT:LEV:IMM %.3f", (ivalue));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_CURRENT_LIMIT:
		ivalue = g_variant_get_double(data);
		g_snprintf(command, sizeof(command), ":CURR:LEV:IMM %.3f", (ivalue));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_POWER_TARGET:
		ivalue = g_variant_get_double(data);
		g_snprintf(command, sizeof(command), ":POW:LEV:IMM %.3f", (ivalue));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_RESISTANCE_TARGET:
		ivalue = g_variant_get_double(data);
		g_snprintf(command, sizeof(command), ":RES:LEV:IMM %.3f", (ivalue));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_OVER_POWER_PROTECTION_THRESHOLD:
		ivalue = g_variant_get_double(data);
		g_snprintf(command, sizeof(command), ":POW:PROT:LEV %.3f", (ivalue));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ivalue = g_variant_get_double(data);
		g_snprintf(command, sizeof(command), ":CURR:PROT:LEV %.3f", (ivalue));
		sr_spew("Sending '%s'.", command);
		ret = sr_scpi_send(sdi->conn, command);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	struct dev_context *devc;

	devc = sdi ? sdi->priv : NULL;

	if (!cg) {
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_REGULATION:
			*data = std_gvar_array_str(ARRAY_AND_SIZE(regulation));
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = std_gvar_min_max_step(0.0, 150.0, 0.001);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = std_gvar_min_max_step(0.0, 30.0, 0.001);
			break;
		case SR_CONF_POWER_TARGET:
			if (!devc) {
				*data = std_gvar_min_max_step(0.0, 200.0, 0.001);
			} else {
				*data = std_gvar_min_max_step(0.0, devc->maxpower, 0.001);
			}
			break;
		case SR_CONF_RESISTANCE_TARGET:
			*data = std_gvar_min_max_step(0.03, 10000.0, 0.01);
			break;
		case SR_CONF_OVER_POWER_PROTECTION_THRESHOLD:
			if (!devc) {
				*data = std_gvar_min_max_step(0.0, 200.0, 0.001);
			} else {
				*data = std_gvar_min_max_step(0.0, devc->maxpower, 0.001);
			}
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			*data = std_gvar_min_max_step(0.0, 30.0, 0.001);
			break;
		default:
			return SR_ERR_NA;
		}
	}
	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	scpi = sdi->conn;
	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	/*
	 * Start acquisition. The receive routine will continue
	 * driving the acquisition.
	 */
	sr_scpi_send(scpi, "MEAS:VOLT?");
	devc->acq_state = ACQ_REQUESTED_VOLTAGE;
	return sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 100, siglent_sdl10x0_handle_events, (void *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;

	sr_scpi_source_remove(sdi->session, scpi);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver siglent_sdl10x0_driver_info = {
	.name = "siglent-sdl10x0",
	.longname = "SIGLENT SDL10x0",
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
SR_REGISTER_DEV_DRIVER(siglent_sdl10x0_driver_info);
