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

static const uint32_t devopts_generic[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_generic_range[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const struct scpi_command cmdset_agilent[] = {
	{ DMM_CMD_SETUP_REMOTE, "\n", },
	{ DMM_CMD_SETUP_LOCAL, "SYST:LOC", },
	{ DMM_CMD_SETUP_FUNC, "CONF:%s", },
	{ DMM_CMD_QUERY_FUNC, "CONF?", },
	{ DMM_CMD_START_ACQ, "INIT", },
	{ DMM_CMD_STOP_ACQ, "ABORT", },
	{ DMM_CMD_QUERY_VALUE, "FETCH?", },
	{ DMM_CMD_QUERY_PREC, "CONF?", },
	{ DMM_CMD_QUERY_RANGE_AUTO, "%s:RANGE:AUTO?", },
	{ DMM_CMD_QUERY_RANGE, "%s:RANGE?", },
	{ DMM_CMD_SETUP_RANGE, "CONF:%s %s", },
	ALL_ZERO,
};

/*
 * cmdset_hp is used for the 34401A, which was added to this code after the
 * 34405A and 34465A. It differs in starting the measurement with INIT: using
 * MEAS without a trailing '?' (as used for the 34405A) is not valid for the
 * 34401A and gives an error.
 * I'm surprised the same instruction sequence doesn't work and INIT may
 * work for both, but I don't have the others to re-test.
 *
 * cmdset_hp also works well for the 34410A, using cmdset_agilent throws an
 * error on 'MEAS' without a '?'.
 *
 * On the 34401A,
 *  MEAS <optional parameters> ? configures, arms, triggers and waits
 *       for a reading
 *  CONF <parameters> configures
 *  INIT prepares for triggering (trigger mode is not set, assumed
 *       internal - external might time out)
 *  *OPC waits for completion, and
 *  READ? retrieves the result
 */
static const struct scpi_command cmdset_hp[] = {
	{ DMM_CMD_SETUP_REMOTE, "\n", },
	{ DMM_CMD_SETUP_FUNC, "CONF:%s", },
	{ DMM_CMD_QUERY_FUNC, "CONF?", },
	{ DMM_CMD_START_ACQ, "INIT", },
	{ DMM_CMD_STOP_ACQ, "ABORT", },
	{ DMM_CMD_QUERY_VALUE, "READ?", },
	{ DMM_CMD_QUERY_PREC, "CONF?", },
	ALL_ZERO,
};

static const struct scpi_command cmdset_gwinstek[] = {
	{ DMM_CMD_SETUP_REMOTE, "SYST:REM", },
	{ DMM_CMD_SETUP_LOCAL, "SYST:LOC", },
	{ DMM_CMD_SETUP_FUNC, "CONF:%s", },
	{ DMM_CMD_QUERY_FUNC, "CONF:STAT:FUNC?", },
	{ DMM_CMD_START_ACQ, "*CLS;SYST:REM", },
	{ DMM_CMD_QUERY_VALUE, "VAL1?", },
	{ DMM_CMD_QUERY_PREC, "SENS:DET:RATE?", },
	ALL_ZERO,
};

static const struct scpi_command cmdset_gwinstek_906x[] = {
	{ DMM_CMD_SETUP_REMOTE, "SYST:REM", },
	{ DMM_CMD_SETUP_LOCAL, "SYST:LOC", },
	{ DMM_CMD_SETUP_FUNC, "CONF:%s", },
	{ DMM_CMD_QUERY_FUNC, "CONF?", },
	{ DMM_CMD_START_ACQ, "INIT", },
	{ DMM_CMD_STOP_ACQ, "ABORT", },
	{ DMM_CMD_QUERY_VALUE, "VAL1?", },
	{ DMM_CMD_QUERY_PREC, "SENS:DET:RATE?", },
	ALL_ZERO,
};

static const struct scpi_command cmdset_owon[] = {
	{ DMM_CMD_SETUP_REMOTE, "SYST:REM", },
	{ DMM_CMD_SETUP_LOCAL, "SYST:LOC", },
	{ DMM_CMD_SETUP_FUNC, "CONF:%s", },
	{ DMM_CMD_QUERY_FUNC, "FUNC?", },
	{ DMM_CMD_QUERY_VALUE, "MEAS1?", },
	ALL_ZERO,
};

static const struct mqopt_item mqopts_agilent_34405a[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "VOLT ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "VOLT:AC ", NO_DFLT_PREC, FLAG_CONF_DELAY | FLAG_MEAS_DELAY, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "CURR ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "CURR:AC ", NO_DFLT_PREC, FLAG_CONF_DELAY | FLAG_MEAS_DELAY, },
	{ SR_MQ_RESISTANCE, 0, "RES", "RES ", NO_DFLT_PREC, FLAG_MEAS_DELAY, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "FRES ", NO_DFLT_PREC, FLAG_MEAS_DELAY, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "CONT", -1, FLAG_NO_RANGE, },
	{ SR_MQ_CAPACITANCE, 0, "CAP", "CAP ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "DIOD", -4, FLAG_NO_RANGE, },
	{ SR_MQ_TEMPERATURE, 0, "TEMP", "TEMP ", NO_DFLT_PREC, FLAG_NO_RANGE | FLAG_MEAS_DELAY, },
	{ SR_MQ_FREQUENCY, 0, "FREQ", "FREQ ", NO_DFLT_PREC, FLAG_NO_RANGE | FLAG_MEAS_DELAY, },
};

static const struct mqopt_item mqopts_agilent_34401a[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "VOLT ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "VOLT:AC ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "CURR ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "CURR:AC ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, 0, "RES", "RES ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "FRES ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "CONT", -1, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "DIOD", -4, FLAGS_NONE, },
	{ SR_MQ_FREQUENCY, 0, "FREQ", "FREQ ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_TIME, 0, "PER", "PER ", NO_DFLT_PREC, FLAGS_NONE, },
};

static const struct mqopt_item mqopts_gwinstek_gdm8200a[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "01", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "02", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "03", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "04", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "05", NO_DFLT_PREC, FLAGS_NONE, }, /* mA */
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "06", NO_DFLT_PREC, FLAGS_NONE, }, /* mA */
	{ SR_MQ_RESISTANCE, 0, "RES", "07", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "16", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "13", -1, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "17", -4, FLAGS_NONE, },
	{ SR_MQ_TEMPERATURE, 0, "TEMP", "09", NO_DFLT_PREC, FLAGS_NONE, }, /* Celsius */
	{ SR_MQ_TEMPERATURE, 0, "TEMP", "15", NO_DFLT_PREC, FLAGS_NONE, }, /* Fahrenheit */
	{ SR_MQ_FREQUENCY, 0, "FREQ", "08", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_TIME, 0, "PER", "14", NO_DFLT_PREC, FLAGS_NONE, },
};

static const struct mqopt_item mqopts_gwinstek_gdm906x[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "VOLT ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "VOLT:AC", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "CURR ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "CURR:AC", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, 0, "RES", "RES", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "FRES", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "CONT", -1, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "DIOD", -4, FLAGS_NONE, },
	{ SR_MQ_TEMPERATURE, 0, "TEMP", "TEMP", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_FREQUENCY, 0, "FREQ", "FREQ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_TIME, 0, "PER", "PER", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CAPACITANCE, 0, "CAP", "CAP", NO_DFLT_PREC, FLAGS_NONE, },
};

static const struct mqopt_item mqopts_owon_xdm2041[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "VOLT AC", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "VOLT", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "CURR AC", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "CURR", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, 0, "RES", "RES", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "FRES", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "CONT", -1, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "DIOD", -4, FLAGS_NONE, },
	{ SR_MQ_TEMPERATURE, 0, "TEMP", "TEMP", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_FREQUENCY, 0, "FREQ", "FREQ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CAPACITANCE, 0, "CAP", "CAP", NO_DFLT_PREC, FLAGS_NONE, },
};

static const struct mqopt_item mqopts_siglent_sdm3055[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VOLT:DC", "VOLT ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VOLT:AC", "VOLT:AC ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC, "CURR:DC", "CURR ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC, "CURR:AC", "CURR:AC ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, 0, "RES", "RES ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE, "FRES", "FRES ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CONTINUITY, 0, "CONT", "CONT", -1, FLAGS_NONE, },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC | SR_MQFLAG_DIODE, "DIOD", "DIOD", -4, FLAGS_NONE, },
	{ SR_MQ_FREQUENCY, 0, "FREQ", "FREQ ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_TIME, 0, "PER", "PER ", NO_DFLT_PREC, FLAGS_NONE, },
	{ SR_MQ_CAPACITANCE, 0, "CAP", "CAP", NO_DFLT_PREC, FLAGS_NONE, },
};

SR_PRIV const struct scpi_dmm_model models[] = {
	{
		"Agilent", "34405A",
		1, 5, cmdset_agilent, ARRAY_AND_SIZE(mqopts_agilent_34405a),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic_range),
		0, 200 * 1000, 2500 * 1000, 0, FALSE,
		scpi_dmm_get_range_text, scpi_dmm_set_range_from_text, NULL,
	},
	{
		"Agilent", "34410A",
		1, 6, cmdset_hp, ARRAY_AND_SIZE(mqopts_agilent_34405a),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic),
		0, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
	},
	{
		"Agilent", "34460A",
		1, 6, cmdset_agilent, ARRAY_AND_SIZE(mqopts_agilent_34405a),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic_range),
		0, 0, 10 * 1000, 0, FALSE,
		scpi_dmm_get_range_text, scpi_dmm_set_range_from_text, NULL,
	},
	{
		"GW", "GDM8251A",
		1, 6, cmdset_gwinstek, ARRAY_AND_SIZE(mqopts_gwinstek_gdm8200a),
		scpi_dmm_get_meas_gwinstek,
		ARRAY_AND_SIZE(devopts_generic),
		2500 * 1000, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
	},
	{
		"GW", "GDM8255A",
		1, 6, cmdset_gwinstek, ARRAY_AND_SIZE(mqopts_gwinstek_gdm8200a),
		scpi_dmm_get_meas_gwinstek,
		ARRAY_AND_SIZE(devopts_generic),
		2500 * 1000, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
	},
	{
		"GWInstek", "GDM9060",
		1, 6, cmdset_gwinstek_906x, ARRAY_AND_SIZE(mqopts_gwinstek_gdm906x),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic),
		0, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
	},
	{
		"GWInstek", "GDM9061",
		1, 6, cmdset_gwinstek_906x, ARRAY_AND_SIZE(mqopts_gwinstek_gdm906x),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic),
		0, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
	},
	{
		"HP", "34401A",
		1, 6, cmdset_hp, ARRAY_AND_SIZE(mqopts_agilent_34401a),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic),
		/* 34401A: typ. 1020ms for AC readings (default is 1000ms). */
		1500 * 1000, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
	},
	{
		"Keysight", "34465A",
		1, 6, cmdset_agilent, ARRAY_AND_SIZE(mqopts_agilent_34405a),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic_range),
		0, 0, 10 * 1000, 0, FALSE,
		scpi_dmm_get_range_text, scpi_dmm_set_range_from_text, NULL,
	},
	{
		"OWON", "XDM2041",
		1, 5, cmdset_owon, ARRAY_AND_SIZE(mqopts_owon_xdm2041),
		scpi_dmm_get_meas_gwinstek,
		ARRAY_AND_SIZE(devopts_generic),
		0, 0, 0, 1e9, TRUE,
		NULL, NULL, NULL,
	},
	{
		"Siglent", "SDM3055",
		1, 5, cmdset_hp, ARRAY_AND_SIZE(mqopts_siglent_sdm3055),
		scpi_dmm_get_meas_agilent,
		ARRAY_AND_SIZE(devopts_generic),
		0, 0, 0, 0, FALSE,
		NULL, NULL, NULL,
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

/*
 * Some devices (such as Owon XDM2041) do not support the standard
 * OPeration Complete? command. This function tests the command with
 * a short timeout, and returns TRUE if any reply (busy or not) is received.
 */
static gboolean probe_opc_support(struct sr_scpi_dev_inst *scpi)
{
	gboolean result;
	GString *response;

	response = g_string_sized_new(128);
	result = TRUE;
	if (sr_scpi_get_data(scpi, SCPI_CMD_OPC, &response) != SR_OK)
		result = FALSE;
	g_string_free(response, TRUE);

	return result;
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
	const char *command;

	ret = sr_scpi_get_hw_id(scpi, &hw_info);
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

	if (model->check_opc && !probe_opc_support(scpi))
		scpi->no_opc_command = TRUE;

	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->conn = scpi;
	sdi->driver = &scpi_dmm_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	ret = sr_scpi_connection_id(scpi, &sdi->connection_id);
	if (ret != SR_OK) {
		g_free(sdi->connection_id);
		sdi->connection_id = NULL;
	}
	sr_scpi_hw_info_free(hw_info);
	if (model->read_timeout_us)  /* non-default read timeout */
		scpi->read_timeout_us = model->read_timeout_us;
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	devc->num_channels = model->num_channels;
	devc->cmdset = model->cmdset;
	devc->model = model;

	for (i = 0; i < devc->num_channels; i++) {
		channel_name = g_strdup_printf("P%zu", i + 1);
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, channel_name);
	}

	/*
	 * If device has DMM_CMD_SETUP_LOCAL command, send it now. To avoid
	 * leaving device in remote mode (if only a "scan" is run).
	 */
	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_SETUP_LOCAL);
	if (command && *command) {
		scpi_dmm_cmd_delay(scpi);
		sr_scpi_send(scpi, command);
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
	const char *command;

	devc = sdi->priv;
	scpi = sdi->conn;
	if (!scpi)
		return SR_ERR_BUG;

	sr_dbg("DIAG: sdi->status %d.", sdi->status - SR_ST_NOT_FOUND);
	if (sdi->status <= SR_ST_INACTIVE)
		return SR_OK;

	/*
	 * If device has DMM_CMD_SETUP_LOCAL command, send it now
	 * to avoid leaving device in remote mode.
	 */
	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_SETUP_LOCAL);
	if (command && *command) {
		scpi_dmm_cmd_delay(scpi);
		sr_scpi_send(scpi, command);
	}

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
	const char *range;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->connection_id)
			return SR_ERR_NA;
		*data = g_variant_new_string(sdi->connection_id);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_MEASURED_QUANTITY:
		ret = scpi_dmm_get_mq(sdi, &mq, &mqflag, NULL, NULL);
		if (ret != SR_OK)
			return ret;
		arr[0] = g_variant_new_uint32(mq);
		arr[1] = g_variant_new_uint64(mqflag);
		*data = g_variant_new_tuple(arr, ARRAY_SIZE(arr));
		return SR_OK;
	case SR_CONF_RANGE:
		if (!devc || !devc->model->get_range_text)
			return SR_ERR_NA;
		range = devc->model->get_range_text(sdi);
		if (!range || !*range)
			return SR_ERR_NA;
		*data = g_variant_new_string(range);
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
	const char *range;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_MEASURED_QUANTITY:
		tuple_child = g_variant_get_child_value(data, 0);
		mq = g_variant_get_uint32(tuple_child);
		g_variant_unref(tuple_child);
		tuple_child = g_variant_get_child_value(data, 1);
		mqflag = g_variant_get_uint64(tuple_child);
		g_variant_unref(tuple_child);
		return scpi_dmm_set_mq(sdi, mq, mqflag);
	case SR_CONF_RANGE:
		if (!devc || !devc->model->set_range_from_text)
			return SR_ERR_NA;
		range = g_variant_get_string(data, NULL);
		return devc->model->set_range_from_text(sdi, range);
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
		if (!devc)
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts_generic);
		return std_opts_config_list(key, data, sdi, cg,
			ARRAY_AND_SIZE(scanopts), ARRAY_AND_SIZE(drvopts),
			devc->model->devopts, devc->model->devopts_size);
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
	case SR_CONF_RANGE:
		if (!devc || !devc->model->get_range_text_list)
			return SR_ERR_NA;
		*data = devc->model->get_range_text_list(sdi);
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
	const struct mqopt_item *item;
	const char *command;
	char *response;
	gboolean do_mq_meas_delay;

	scpi = sdi->conn;
	devc = sdi->priv;

	ret = scpi_dmm_get_mq(sdi, &devc->start_acq_mq.curr_mq,
		&devc->start_acq_mq.curr_mqflag, NULL, &item);
	if (ret != SR_OK)
		return ret;

	/*
	 * Query for current precision if DMM supports the command
	 */
	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_QUERY_PREC);
	if (command && *command) {
		scpi_dmm_cmd_delay(scpi);
		ret = sr_scpi_get_string(scpi, command, &response);
		if (ret == SR_OK) {
			g_strstrip(response);
			g_free(devc->precision);
			devc->precision = g_strdup(response);
			g_free(response);
			sr_dbg("%s: Precision: '%s'", __func__, devc->precision);
		} else {
			sr_info("Precision query ('%s') failed: %d",
				command, ret);
		}
	}

	command = sr_scpi_cmd_get(devc->cmdset, DMM_CMD_START_ACQ);
	if (command && *command) {
		scpi_dmm_cmd_delay(scpi);
		ret = sr_scpi_send(scpi, command);
		if (ret != SR_OK)
			return ret;
	}

	do_mq_meas_delay = item->drv_flags & FLAG_MEAS_DELAY;
	if (do_mq_meas_delay && devc->model->meas_delay_us)
		g_usleep(devc->model->meas_delay_us);

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
		scpi_dmm_cmd_delay(scpi);
		(void)sr_scpi_send(scpi, command);
	}
	sr_scpi_source_remove(sdi->session, scpi);

	std_session_send_df_end(sdi);

	g_free(devc->precision);
	devc->precision = NULL;

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
