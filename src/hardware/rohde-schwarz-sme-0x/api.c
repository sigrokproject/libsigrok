/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Vlad Ivanov <vlad.ivanov@lab-systems.ru>
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
#include <libserialport.h>
#include <scpi.h>
#include <string.h>

#include "protocol.h"

SR_PRIV struct sr_dev_driver rohde_schwarz_sme_0x_driver_info;

static const char *manufacturer = "Rohde&Schwarz";

static const struct rs_device_model device_models[] = {
	{
		.model_str = "SME02",
		.freq_max = SR_GHZ(1.5),
		.freq_min = SR_KHZ(5),
		.power_max = 16,
		.power_min = -144,
	},
	{
		.model_str = "SME03E",
		.freq_max = SR_GHZ(2.2),
		.freq_min = SR_KHZ(5),
		.power_max = 16,
		.power_min = -144,
	},
	{
		.model_str = "SME03A",
		.freq_max = SR_GHZ(3),
		.freq_min = SR_KHZ(5),
		.power_max = 16,
		.power_min = -144,
	},
	{
		.model_str = "SME03",
		.freq_max = SR_GHZ(3),
		.freq_min = SR_KHZ(5),
		.power_max = 16,
		.power_min = -144,
	},
	{
		.model_str = "SME06",
		.freq_max = SR_GHZ(1.5),
		.freq_min = SR_KHZ(5),
		.power_max = 16,
		.power_min = -144,
	}
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
        SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t devopts[] = {
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static int rs_init_device(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t model_found;

	devc = sdi->priv;
	model_found = 0;

	for (size_t i = 0; i < ARRAY_SIZE(device_models); i++) {
		if (!strcmp(device_models[i].model_str, sdi->model)) {
			model_found = 1;
			devc->model_config = &device_models[i];
			break;
		}
	}

	if (!model_found) {
		sr_dbg("Device %s %s is not supported by this driver.",
			manufacturer, sdi->model);
		return SR_ERR_NA;
	}

	return SR_OK;
}

static struct sr_dev_inst *rs_probe_serial_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	rs_sme0x_mode_remote(scpi);

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
	return sr_scpi_scan(di->context, options, rs_probe_serial_device);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, NULL);
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
	double value_f;

	(void) cg;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		rs_sme0x_get_freq(sdi, &value_f);
		*data = g_variant_new_double(value_f);
		break;
	case SR_CONF_AMPLITUDE:
		rs_sme0x_get_power(sdi, &value_f);
		*data = g_variant_new_double(value_f);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	double value_f;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		value_f = g_variant_get_double(data);
		rs_sme0x_set_freq(sdi, value_f);
		break;
	case SR_CONF_AMPLITUDE:
		value_f = g_variant_get_double(data);
		rs_sme0x_set_power(sdi, value_f);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	/* Return drvopts without sdi (and devopts with sdi, see below). */
	if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	(void)sdi;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver rohde_schwarz_sme_0x_driver_info = {
	.name = "rohde-schwarz-sme-0x",
	.longname = "Rohde&Schwarz SME-0x",
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
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(rohde_schwarz_sme_0x_driver_info);
