/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Vlad Ivanov <vlad.ivanov@lab-systems.ru>
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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
#include <scpi.h>
#include <string.h>

#include "protocol.h"

static struct sr_dev_driver rohde_schwarz_sme_0x_driver_info;

static const char *manufacturer = "Rohde&Schwarz";

static const struct rs_device_model_config model_sme0x =
{
	.freq_step = 0.1,
	.power_step = 0.1,
	.commands = commands_sme0x,
	.responses = responses_sme0x,
};

static const struct rs_device_model_config model_smx100 =
{
	.freq_step = 0.001,
	.power_step = 0.01,
	.commands = commands_smx100,
	.responses = responses_smx100,
};

static const struct rs_device_model device_models[] = {
	{
		.model_str = "SME02",
		.model_config = &model_sme0x,
	},
	{
		.model_str = "SME03E",
		.model_config = &model_sme0x,
	},
	{
		.model_str = "SME03A",
		.model_config = &model_sme0x,
	},
	{
		.model_str = "SME03",
		.model_config = &model_sme0x,
	},
	{
		.model_str = "SME06",
		.model_config = &model_sme0x,
	},
	{
		.model_str = "SMB100A",
		.model_config = &model_smx100,
	},
	{
		.model_str = "SMBV100A",
		.model_config = &model_smx100,
	},
	{
		.model_str = "SMC100A",
		.model_config = &model_smx100,
	},
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t devopts[] = {
	SR_CONF_OUTPUT_FREQUENCY      | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE             | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED               | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_EXTERNAL_CLOCK_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *clock_sources[] = {
	"Internal",
	"External",
};

static int rs_init_device(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t model_found;
	double min_val;
	double max_val;

	devc = sdi->priv;
	model_found = 0;

	for (size_t i = 0; i < ARRAY_SIZE(device_models); i++) {
		if (!strcmp(device_models[i].model_str, sdi->model)) {
			model_found = 1;
			devc->model_config = device_models[i].model_config;
			break;
		}
	}

	if (!model_found) {
		sr_dbg("Device %s %s is not supported by this driver.",
			manufacturer, sdi->model);
		return SR_ERR_NA;
	}

	if (rs_sme0x_init(sdi) != SR_OK)
		return SR_ERR;

	if (rs_sme0x_get_minmax_freq(sdi, &min_val, &max_val) != SR_OK)
		return SR_ERR;
	devc->freq_min = min_val;
	devc->freq_max = max_val;

	if (rs_sme0x_get_minmax_power(sdi, &min_val, &max_val) != SR_OK)
		return SR_ERR;
	devc->power_min = min_val;
	devc->power_max = max_val;

	if (rs_sme0x_sync(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK)
		goto fail;

	if (strcmp(hw_info->manufacturer, manufacturer) != 0)
		goto fail;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->driver = &rohde_schwarz_sme_0x_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->conn = scpi;

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	devc = g_malloc0(sizeof(struct dev_context));
	sdi->priv = devc;

	if (rs_init_device(sdi) != SR_OK)
		goto fail;

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
	int ret;
	if ((ret = sr_scpi_open(sdi->conn)) != SR_OK)
		return ret;

	ret = rs_sme0x_mode_remote(sdi);
	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	rs_sme0x_mode_local(sdi);
	return sr_scpi_close(sdi->conn);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	double value_f;
	int idx;
	gboolean bval;
	struct dev_context *devc;

	(void) cg;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_ENABLED:
		rs_sme0x_get_enable(sdi, &bval);
		*data = g_variant_new_boolean(bval);
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		rs_sme0x_get_freq(sdi, &value_f);
		*data = g_variant_new_double(value_f);
		break;
	case SR_CONF_AMPLITUDE:
		rs_sme0x_get_power(sdi, &value_f);
		*data = g_variant_new_double(value_f);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		rs_sme0x_get_clk_src_idx(sdi, &idx);
		*data = g_variant_new_string(clock_sources[idx]);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)cg;
	int ret;
	size_t i;
	const char *clksrc_str;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_ENABLED:
		ret = rs_sme0x_set_enable(sdi, g_variant_get_boolean(data));
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		ret = rs_sme0x_set_freq(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_AMPLITUDE:
		ret = rs_sme0x_set_power(sdi, g_variant_get_double(data));
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		clksrc_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(clock_sources); i++)
			if (g_strcmp0(clock_sources[i], clksrc_str) == 0) {
				ret = rs_sme0x_set_clk_src(sdi, i);
				break;
			}
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
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
	case SR_CONF_AMPLITUDE:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(devc->power_min,
									  devc->power_max,
									  devc->model_config->power_step);
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		if (!devc)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step(devc->freq_min,
									devc->freq_max,
									devc->model_config->freq_step);
		break;
	case SR_CONF_EXTERNAL_CLOCK_SOURCE:
		*data = std_gvar_array_str(ARRAY_AND_SIZE(clock_sources));
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static struct sr_dev_driver rohde_schwarz_sme_0x_driver_info = {
	.name = "rohde-schwarz-sme-0x",
	.longname = "Rohde&Schwarz SME-0x",
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
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(rohde_schwarz_sme_0x_driver_info);

