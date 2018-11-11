/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <string.h>
#include "protocol.h"

static struct sr_dev_driver scpi_dmm_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const struct scpi_command cmdset_agilent[] = {
	{ DMM_CMD_SETUP_REMOTE, "\n", },
	{ DMM_CMD_SETUP_FUNC, "CONF:%s", },
	{ DMM_CMD_QUERY_FUNC, "CONF?", },
	{ DMM_CMD_START_ACQ, "MEAS", },
	{ DMM_CMD_STOP_ACQ, "ABORT", },
	{ DMM_CMD_QUERY_VALUE, "READ?", },
	{ DMM_CMD_QUERY_PREC, "CONF?", },
	ALL_ZERO,
};

static const struct mqopt_item mqopts_agilent_5digit[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "VOLT ", NO_DFLT_PREC, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "VOLT:AC ", NO_DFLT_PREC, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "CURR ", NO_DFLT_PREC, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "CURR:AC ", NO_DFLT_PREC, },
	{ SR_MQ_RESISTANCE, 0, "RES", "RES ", NO_DFLT_PREC, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "FRES ", NO_DFLT_PREC, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "CONT", -1, },
	{ SR_MQ_CAPACITANCE, 0, "CAP", "CAP ", NO_DFLT_PREC, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "DIOD", -4, },
	{ SR_MQ_TEMPERATURE, 0, "TEMP", "TEMP ", NO_DFLT_PREC, },
	{ SR_MQ_FREQUENCY, 0, "FREQ", "FREQ ", NO_DFLT_PREC, },
};

SR_PRIV const struct scpi_dmm_model models[] = {
	{
		"Agilent", "34405A",
		1, 5, cmdset_agilent, ARRAY_AND_SIZE(mqopts_agilent_5digit),
		scpi_dmm_get_meas_agilent,
	},
};

static const struct scpi_dmm_model *is_compatible(const char *vendor, const char *model)
{
	size_t i;
	const struct scpi_dmm_model *entry;

	for (i = 0; i < ARRAY_SIZE(models); i++) {
		entry = &models[i];
		if (!entry->vendor || !entry->model)
			continue;
		if (strcmp(vendor, entry->vendor) != 0)
			continue;
		if (strcmp(model, entry->model) != 0)
			continue;
		return entry;
	}

	return NULL;
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_scpi_hw_info *hw_info;
	int ret;
	const char *vendor;
	const struct scpi_dmm_model *model;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t i;
	gchar *channel_name;

	ret = sr_scpi_get_hw_id(scpi, &hw_info);
	scpi_dmm_cmd_delay(scpi);
	if (ret != SR_OK) {
		sr_info("Could not get IDN response.");
		return NULL;
	}
	vendor = sr_vendor_alias(hw_info->manufacturer);
	model = is_compatible(vendor, hw_info->model);
	if (!model) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->conn = scpi;
	sdi->driver = &scpi_dmm_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sr_scpi_hw_info_free(hw_info);

	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	devc->num_channels = model->num_channels;
	devc->cmdset = model->cmdset;
	devc->model = model;

	for (i = 0; i < devc->num_channels; i++) {
		channel_name = g_strdup_printf("P%zu", i + 1);
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, channel_name);
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
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;
	if (!scpi)
		return SR_ERR_BUG;

	sr_dbg("DIAG: sdi->status %d.", sdi->status - SR_ST_NOT_FOUND);
	if (sdi->status <= SR_ST_INACTIVE)
		return SR_OK;

	return sr_scpi_close(scpi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	enum sr_mq mq;
	enum sr_mqflag mqflag;
	GVariant *arr[2];
	int ret;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_MEASURED_QUANTITY:
		ret = scpi_dmm_get_mq(sdi, &mq, &mqflag, NULL);
		if (ret != SR_OK)
			return ret;
		arr[0] = g_variant_new_uint32(mq);
		arr[1] = g_variant_new_uint64(mqflag);
		*data = g_variant_new_tuple(arr, ARRAY_SIZE(arr));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	enum sr_mq mq;
	enum sr_mqflag mqflag;
	GVariant *tuple_child;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_MEASURED_QUANTITY:
		tuple_child = g_variant_get_child_value(data, 0);
		mq = g_variant_get_uint32(tuple_child);
		tuple_child = g_variant_get_child_value(data, 1);
		mqflag = g_variant_get_uint64(tuple_child);
		g_variant_unref(tuple_child);
		return scpi_dmm_set_mq(sdi, mq, mqflag);
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *gvar, *arr[2];
	GVariantBuilder gvb;
	size_t i;

	(void)cg;

	devc = sdi ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_MEASURED_QUANTITY:
		/* TODO Use std_gvar_measured_quantities() when available. */
		if (!devc)
			return SR_ERR_ARG;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < devc->model->mqopt_size; i++) {
			arr[0] = g_variant_new_uint32(devc->model->mqopts[i].mq);
			arr[1] = g_variant_new_uint64(devc->model->mqopts[i].mqflag);
			gvar = g_variant_new_tuple(arr, ARRAY_SIZE(arr));
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		return SR_OK;
	default:
		(void)devc;
		return SR_ERR_NA;
	}
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	int ret;
	const char *command;

	scpi = sdi->conn;
	devc = sdi->priv;

	ret = scpi_dmm_get_mq(sdi, &devc->start_acq_mq.curr_mq,
		&devc->start_acq_mq.curr_mqflag, NULL);
	if (ret != SR_OK)
		return ret;

	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_START_ACQ);
	if (command && *command) {
		ret = sr_scpi_send(scpi, command);
		scpi_dmm_cmd_delay(scpi);
		if (ret != SR_OK)
			return ret;
	}

	sr_sw_limits_acquisition_start(&devc->limits);
	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK)
		return ret;

	ret = sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 10,
		scpi_dmm_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	const char *command;

	scpi = sdi->conn;
	devc = sdi->priv;

	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_STOP_ACQ);
	if (command && *command) {
		(void)sr_scpi_send(scpi, command);
		scpi_dmm_cmd_delay(scpi);
	}
	sr_scpi_source_remove(sdi->session, scpi);

	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver scpi_dmm_driver_info = {
	.name = "scpi-dmm",
	.longname = "SCPI DMM",
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
SR_REGISTER_DEV_DRIVER(scpi_dmm_driver_info);
