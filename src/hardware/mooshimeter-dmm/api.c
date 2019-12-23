/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Derek Hageman <hageman@inthat.cloud>
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

static struct sr_dev_driver mooshimeter_dmm_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AVG_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CHANNEL_CONFIG | SR_CONF_SET,
};

static void init_dev(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *chan;

	devc = g_new0(struct dev_context, 1);
	sdi->priv = devc;
	sdi->status = SR_ST_INITIALIZING;
	sdi->vendor = g_strdup("Mooshim Engineering");
	sdi->model = g_strdup("Mooshimeter");

	sr_sw_limits_init(&devc->limits);

	chan = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");
	devc->channel_meaning[0].mq = SR_MQ_CURRENT;
	devc->channel_meaning[0].unit = SR_UNIT_AMPERE;
	devc->channel_meaning[0].mqflags = SR_MQFLAG_DC;
	devc->channel_meaning[0].channels = g_slist_prepend(NULL, chan);

	chan = sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "CH2");
	devc->channel_meaning[1].mq = SR_MQ_VOLTAGE;
	devc->channel_meaning[1].unit = SR_UNIT_VOLT;
	devc->channel_meaning[1].mqflags = SR_MQFLAG_DC;
	devc->channel_meaning[1].channels = g_slist_prepend(NULL, chan);

	chan = sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, FALSE, "P");
	devc->channel_meaning[2].mq = SR_MQ_POWER;
	devc->channel_meaning[2].unit = SR_UNIT_WATT;
	devc->channel_meaning[2].mqflags = SR_MQFLAG_RMS;
	devc->channel_meaning[2].channels = g_slist_prepend(NULL, chan);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_bt_desc *desc;
	const char *conn;
	struct sr_config *src;
	GSList *l;
	int ret;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (!conn)
		return NULL;

	desc = sr_bt_desc_new();
	if (!desc)
		return NULL;

	ret = sr_bt_config_addr_remote(desc, conn);
	if (ret < 0)
		goto err;

	/*
	 * These handles where queried with btgatt-client, since the
	 * documentation specifies them in terms of UUIDs.
	 *
	 * service - start: 0x0010, end: 0xffff, type: primary, uuid: 1bc5ffa0-0200-62ab-e411-f254e005dbd4
         * charac - start: 0x0011, value: 0x0012, props: 0x08, ext_props: 0x0000, uuid: 1bc5ffa1-0200-62ab-e411-f254e005dbd4
         *         descr - handle: 0x0013, uuid: 00002901-0000-1000-8000-00805f9b34fb
         * charac - start: 0x0014, value: 0x0015, props: 0x10, ext_props: 0x0000, uuid: 1bc5ffa2-0200-62ab-e411-f254e005dbd4
         *         descr - handle: 0x0016, uuid: 00002902-0000-1000-8000-00805f9b34fb
         *         descr - handle: 0x0017, uuid: 00002901-0000-1000-8000-00805f9b34fb
	 */
	ret = sr_bt_config_notify(desc, 0x0015, 0x0012, 0x0016, 0x0001);
	if (ret < 0)
		goto err;

	ret = sr_bt_connect_ble(desc);
	if (ret < 0)
		goto err;
	sr_bt_disconnect(desc);

	struct sr_dev_inst *sdi = g_malloc0(sizeof(struct sr_dev_inst));
	struct dev_context *devc = g_malloc0(sizeof(struct dev_context));

	sdi->priv = devc;
	sdi->inst_type = SR_INST_USER;
	sdi->connection_id = g_strdup(conn);
	sdi->conn = desc;

	init_dev(sdi);

	return std_scan_complete(di, g_slist_prepend(NULL, sdi));

err:
	sr_bt_desc_free(desc);
	return NULL;
}

static int dev_clear(const struct sr_dev_driver *di)
{
	struct drv_context *drvc = di->context;
	struct sr_dev_inst *sdi;
	GSList *l;

	if (drvc) {
		for (l = drvc->instances; l; l = l->next) {
			sdi = l->data;
			struct sr_bt_desc *desc = sdi->conn;
			if (desc)
				sr_bt_desc_free(desc);
			sdi->conn = NULL;
		}
	}

	return std_dev_clear(di);
}

static int set_channel1_mean(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_RMS;
	devc->channel_meaning[0].mqflags |= SR_MQFLAG_DC;

	return mooshimeter_dmm_set_chooser(sdi, "CH1:ANALYSIS",
		"CH1:ANALYSIS:MEAN");
}

static int set_channel1_rms(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_DC;
	devc->channel_meaning[0].mqflags |= SR_MQFLAG_RMS;

	return mooshimeter_dmm_set_chooser(sdi, "CH1:ANALYSIS",
		"CH1:ANALYSIS:RMS");
}

static int set_channel1_buffer(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	devc->channel_meaning[0].mqflags &= ~(SR_MQFLAG_DC | SR_MQFLAG_RMS);

	return mooshimeter_dmm_set_chooser(sdi, "CH1:ANALYSIS",
		"CH1:ANALYSIS:BUFFER");
}

static int set_channel2_mean(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_RMS;
	devc->channel_meaning[1].mqflags |= SR_MQFLAG_DC;

	return mooshimeter_dmm_set_chooser(sdi, "CH2:ANALYSIS",
		"CH2:ANALYSIS:MEAN");
}

static int set_channel2_rms(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_DC;
	devc->channel_meaning[1].mqflags |= SR_MQFLAG_RMS;

	return mooshimeter_dmm_set_chooser(sdi, "CH2:ANALYSIS",
		"CH2:ANALYSIS:RMS");
}

static int set_channel2_buffer(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->channel_meaning[1].mqflags &= ~(SR_MQFLAG_DC | SR_MQFLAG_RMS);

	return mooshimeter_dmm_set_chooser(sdi, "CH2:ANALYSIS",
		"CH2:ANALYSIS:BUFFER");
}

static void autorange_channel1_current(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH1:RANGE_I",
		"CH1:MAPPING:CURRENT", value);
}

static int configure_channel1_current(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH1:MAPPING",
		"CH1:MAPPING:CURRENT");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH1:RANGE_I",
		"CH1:MAPPING:CURRENT", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[0] = autorange_channel1_current;
		devc->channel_meaning[0].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[0] = NULL;
		devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[0].mq = SR_MQ_CURRENT;
	devc->channel_meaning[0].unit = SR_UNIT_AMPERE;

	return SR_OK;
}

static void autorange_channel1_temperature(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH1:RANGE_I",
		"CH1:MAPPING:TEMP", value);
}

static int configure_channel1_temperature(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH1:MAPPING",
		"CH1:MAPPING:TEMP");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH1:RANGE_I",
		"CH1:MAPPING:TEMP", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[0] = autorange_channel1_temperature;
		devc->channel_meaning[0].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[0] = NULL;
		devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[0].mq = SR_MQ_TEMPERATURE;
	devc->channel_meaning[0].unit = SR_UNIT_KELVIN;

	return SR_OK;
}

static void autorange_channel1_auxv(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH1:RANGE_I",
		"SHARED:AUX_V", value);
}

static int configure_channel1_auxv(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SHARED", "SHARED:AUX_V");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH1:MAPPING",
		"CH1:MAPPING:SHARED");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH1:RANGE_I",
		"SHARED:AUX_V", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[0] = autorange_channel1_auxv;
		devc->channel_meaning[0].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[0] = NULL;
		devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[0].mq = SR_MQ_VOLTAGE;
	devc->channel_meaning[0].unit = SR_UNIT_VOLT;

	return SR_OK;
}

static void autorange_channel1_resistance(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH1:RANGE_I",
		"SHARED:RESISTANCE", value);
}

static int configure_channel1_resistance(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SHARED", "SHARED:RESISTANCE");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH1:MAPPING",
		"CH1:MAPPING:SHARED");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH1:RANGE_I",
		"SHARED:RESISTANCE", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[0] = autorange_channel1_resistance;
		devc->channel_meaning[0].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[0] = NULL;
		devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[0].mq = SR_MQ_RESISTANCE;
	devc->channel_meaning[0].unit = SR_UNIT_OHM;

	return SR_OK;
}

static void autorange_channel1_diode(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH1:RANGE_I",
		"SHARED:DIODE", value);
}

static int configure_channel1_diode(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SHARED", "SHARED:DIODE");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH1:MAPPING",
		"CH1:MAPPING:SHARED");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH1:RANGE_I",
		"SHARED:DIODE", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[0] = autorange_channel1_diode;
		devc->channel_meaning[0].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[0] = NULL;
		devc->channel_meaning[0].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[0].mqflags |= SR_MQFLAG_DIODE;
	devc->channel_meaning[0].mq = SR_MQ_VOLTAGE;
	devc->channel_meaning[0].unit = SR_UNIT_VOLT;

	return SR_OK;
}

static void autorange_channel2_voltage(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH2:RANGE_I",
		"CH2:MAPPING:VOLTAGE", value);
}

static int configure_channel2_voltage(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH2:MAPPING",
		"CH2:MAPPING:VOLTAGE");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH2:RANGE_I",
		"CH2:MAPPING:VOLTAGE", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[1] = autorange_channel2_voltage;
		devc->channel_meaning[1].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[1] = NULL;
		devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[1].mq = SR_MQ_VOLTAGE;
	devc->channel_meaning[1].unit = SR_UNIT_VOLT;

	return SR_OK;
}

static void autorange_channel2_temperature(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH2:RANGE_I",
		"CH2:MAPPING:TEMP", value);
}

static int configure_channel2_temperature(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH2:MAPPING",
		"CH2:MAPPING:TEMP");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH2:RANGE_I",
		"CH2:MAPPING:TEMP", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[1] = autorange_channel2_temperature;
		devc->channel_meaning[1].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[1] = NULL;
		devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[1].mq = SR_MQ_TEMPERATURE;
	devc->channel_meaning[1].unit = SR_UNIT_CELSIUS;

	return SR_OK;
}

static void autorange_channel2_auxv(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH2:RANGE_I",
		"SHARED:AUX_V", value);
}

static int configure_channel2_auxv(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SHARED", "SHARED:AUX_V");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH2:MAPPING",
		"CH2:MAPPING:SHARED");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH2:RANGE_I",
		"SHARED:AUX_V", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[1] = autorange_channel2_auxv;
		devc->channel_meaning[1].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[1] = NULL;
		devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[1].mq = SR_MQ_VOLTAGE;
	devc->channel_meaning[1].unit = SR_UNIT_VOLT;

	return SR_OK;
}

static void autorange_channel2_resistance(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH2:RANGE_I",
		"SHARED:RESISTANCE", value);
}

static int configure_channel2_resistance(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SHARED", "SHARED:RESISTANCE");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH2:MAPPING",
		"CH2:MAPPING:SHARED");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH2:RANGE_I",
		"SHARED:RESISTANCE", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[1] = autorange_channel2_resistance;
		devc->channel_meaning[1].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[1] = NULL;
		devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_DIODE;
	devc->channel_meaning[1].mq = SR_MQ_RESISTANCE;
	devc->channel_meaning[1].unit = SR_UNIT_OHM;

	return SR_OK;
}

static void autorange_channel2_diode(const struct sr_dev_inst *sdi,
	float value)
{
	mooshimeter_dmm_set_autorange(sdi, "CH2:RANGE_I",
		"SHARED:DIODE", value);
}

static int configure_channel2_diode(const struct sr_dev_inst *sdi,
	float range)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SHARED", "SHARED:DIODE");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "CH2:MAPPING",
		"CH2:MAPPING:SHARED");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "CH2:RANGE_I",
		"SHARED:DIODE", range);
	if (ret != SR_OK)
		return ret;

	if (range <= 0) {
		devc->channel_autorange[1] = autorange_channel2_diode;
		devc->channel_meaning[1].mqflags |= SR_MQFLAG_AUTORANGE;
	} else {
		devc->channel_autorange[1] = NULL;
		devc->channel_meaning[1].mqflags &= ~SR_MQFLAG_AUTORANGE;
	}

	devc->channel_meaning[1].mqflags |= SR_MQFLAG_DIODE;
	devc->channel_meaning[1].mq = SR_MQ_VOLTAGE;
	devc->channel_meaning[1].unit = SR_UNIT_VOLT;

	return SR_OK;
}

/*
 * Full string: CH1,CH2
 * Each channel: MODE[:RANGE[:ANALYSIS]]
 * Channel 1 mode:
 * 	Current, A
 * 	Temperature, T, K
 * 	Resistance, Ohm, W
 * 	Diode, D
 * 	Aux, LV
 * Channel 2 mode:
 * 	Voltage, V
 * 	Temperature, T, K
 * 	Resistance, Ohm, W
 * 	Diode, D
 * 	Aux, LV
 * Range is the upper bound of the range (e.g. 60 for 0-60 V or 600 for 0-600),
 * 	zero or absent for autoranging
 * Analysis:
 * 	Mean, DC
 * 	RMS, AC
 * 	Buffer, Samples
 */
static int apply_channel_config(const struct sr_dev_inst *sdi,
	const char *config)
{
	gchar **channel_config;
	gchar **parameters;
	const gchar *param;
	int ret = SR_ERR;
	float range;
	gboolean shared_in_use = FALSE;

	channel_config = g_strsplit_set(config, ",/", -1);
	if (!channel_config[0])
		goto err_free_channel_config;

	parameters = g_strsplit_set(channel_config[0], ":;", -1);
	if (parameters[0] && parameters[0][0]) {
		range = 0;
		if (parameters[1])
			range = g_ascii_strtod(parameters[1], NULL);

		param = parameters[0];
		if (!g_ascii_strncasecmp(param, "Resistance", 10) ||
			!g_ascii_strncasecmp(param, "Ohm", 3) ||
			!g_ascii_strncasecmp(param, "W", 1) ||
			!g_ascii_strncasecmp(param, "R", 1)) {
			ret = configure_channel1_resistance(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
			shared_in_use = TRUE;
		} else if (!g_ascii_strncasecmp(param, "Diode", 5) ||
			!g_ascii_strncasecmp(param, "D", 1)) {
			ret = configure_channel1_diode(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
			shared_in_use = TRUE;
		} else if (!g_ascii_strncasecmp(param, "Aux", 3) ||
			!g_ascii_strncasecmp(param, "LV", 2)) {
			ret = configure_channel1_auxv(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
			shared_in_use = TRUE;
		} else if (!g_ascii_strncasecmp(param, "T", 1) ||
			!g_ascii_strncasecmp(param, "K", 1)) {
			ret = configure_channel1_temperature(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else if (!g_ascii_strncasecmp(param, "Current", 7) ||
			!g_ascii_strncasecmp(param, "A", 1) ||
			*parameters[0]) {
			ret = configure_channel1_current(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else {
			sr_info("Unrecognized mode for CH1: %s.", param);
			ret = configure_channel1_current(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		}

		if (parameters[1] && parameters[2]) {
			param = parameters[2];
			if (!g_ascii_strcasecmp(param, "RMS") ||
				!g_ascii_strcasecmp(param, "AC")) {
				ret = set_channel1_rms(sdi);
				if (ret != SR_OK)
					goto err_free_parameters;
			} else if (!g_ascii_strcasecmp(param, "Buffer") ||
				!g_ascii_strcasecmp(param, "Samples")) {
				ret = set_channel1_buffer(sdi);
				if (ret != SR_OK)
					goto err_free_parameters;
			} else {
				ret = set_channel1_mean(sdi);
				if (ret != SR_OK)
					goto err_free_parameters;
			}
		}
	}
	g_strfreev(parameters);

	if (!channel_config[1]) {
		g_strfreev(channel_config);
		return SR_OK;
	}

	parameters = g_strsplit_set(channel_config[1], ":;", -1);
	if (parameters[0] && parameters[0][0]) {
		range = 0;
		if (parameters[1])
			range = g_ascii_strtod(parameters[1], NULL);

		param = parameters[0];
		if (!g_ascii_strncasecmp(param, "Resistance", 10) ||
			!g_ascii_strncasecmp(param, "Ohm", 3) ||
			!g_ascii_strncasecmp(param, "W", 1) ||
			!g_ascii_strncasecmp(param, "R", 1)) {
			if (shared_in_use) {
				ret = SR_ERR;
				goto err_free_parameters;
			}
			ret = configure_channel2_resistance(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else if (!g_ascii_strncasecmp(param, "Diode", 5) ||
			!g_ascii_strncasecmp(param, "D", 1)) {
			if (shared_in_use) {
				ret = SR_ERR;
				goto err_free_parameters;
			}
			ret = configure_channel2_diode(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else if (!g_ascii_strncasecmp(param, "Aux", 3) ||
			!g_ascii_strncasecmp(param, "LV", 2)) {
			if (shared_in_use) {
				ret = SR_ERR;
				goto err_free_parameters;
			}
			ret = configure_channel2_auxv(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else if (!g_ascii_strncasecmp(param, "T", 1) ||
			!g_ascii_strncasecmp(param, "K", 1)) {
			ret = configure_channel2_temperature(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else if (!g_ascii_strncasecmp(param, "V", 1) ||
			!param[0]) {
			ret = configure_channel2_voltage(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		} else {
			sr_info("Unrecognized mode for CH2: %s.", param);
			ret = configure_channel2_voltage(sdi, range);
			if (ret != SR_OK)
				goto err_free_parameters;
		}

		if (parameters[1] && parameters[2]) {
			param = parameters[2];
			if (!g_ascii_strcasecmp(param, "RMS") ||
				!g_ascii_strcasecmp(param, "AC")) {
				ret = set_channel2_rms(sdi);
				if (ret != SR_OK)
					goto err_free_parameters;
			} else if (!g_ascii_strcasecmp(param, "Buffer") ||
				!g_ascii_strcasecmp(param, "Samples")) {
				ret = set_channel2_buffer(sdi);
				if (ret != SR_OK)
					goto err_free_parameters;
			} else {
				ret = set_channel2_mean(sdi);
				if (ret != SR_OK)
					goto err_free_parameters;
			}
		}
	}
	g_strfreev(parameters);

	g_strfreev(channel_config);
	return SR_OK;

err_free_parameters:
	g_strfreev(parameters);
err_free_channel_config:
	g_strfreev(channel_config);
	return ret;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;

	ret = mooshimeter_dmm_open(sdi);
	if (ret != SR_OK)
		return ret;

	sdi->status = SR_ST_INACTIVE;

	ret = mooshimeter_dmm_set_chooser(sdi, "SAMPLING:TRIGGER",
		"SAMPLING:TRIGGER:OFF");
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "SAMPLING:RATE",
		"SAMPLING:RATE", 125);
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_larger_number(sdi, "SAMPLING:DEPTH",
		"SAMPLING:DEPTH", 64);
	if (ret != SR_OK)
		return ret;

	/* Looks like these sometimes get set to 8, somehow? */
	ret = mooshimeter_dmm_set_integer(sdi, "CH1:BUF_BPS", 24);
	if (ret != SR_OK)
		return ret;

	ret = mooshimeter_dmm_set_integer(sdi, "CH2:BUF_BPS", 24);
	if (ret != SR_OK)
		return ret;

	ret = configure_channel1_current(sdi, 0);
	if (ret != SR_OK)
		return ret;

	ret = set_channel1_mean(sdi);
	if (ret != SR_OK)
		return ret;

	ret = configure_channel2_voltage(sdi, 0);
	if (ret != SR_OK)
		return ret;

	ret = set_channel2_mean(sdi);
	if (ret != SR_OK)
		return ret;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	sdi->status = SR_ST_INACTIVE;

	g_slist_free(devc->channel_meaning[0].channels);
	devc->channel_meaning[0].channels = NULL;

	g_slist_free(devc->channel_meaning[1].channels);
	devc->channel_meaning[1].channels = NULL;

	g_slist_free(devc->channel_meaning[2].channels);
	devc->channel_meaning[2].channels = NULL;

	return mooshimeter_dmm_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	int ret;
	float value;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		ret = mooshimeter_dmm_get_chosen_number(sdi, "SAMPLING:RATE",
			"SAMPLING:RATE", &value);
		if (ret != SR_OK)
			return ret;
		*data = g_variant_new_uint64((guint64)value);
		return SR_OK;
	case SR_CONF_AVG_SAMPLES:
		ret = mooshimeter_dmm_get_chosen_number(sdi, "SAMPLING:DEPTH",
			"SAMPLING:DEPTH", &value);
		if (ret != SR_OK)
			return ret;
		*data = g_variant_new_uint64((guint64)value);
		return SR_OK;
	case SR_CONF_CHANNEL_CONFIG:
		return SR_ERR_NA;
	default:
		break;
	}

	return sr_sw_limits_config_get(&devc->limits, key, data);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		return mooshimeter_dmm_set_larger_number(sdi, "SAMPLING:RATE",
			"SAMPLING:RATE", g_variant_get_uint64(data));
	case SR_CONF_AVG_SAMPLES:
		return mooshimeter_dmm_set_larger_number(sdi, "SAMPLING:DEPTH",
			"SAMPLING:DEPTH", g_variant_get_uint64(data));
	case SR_CONF_CHANNEL_CONFIG:
		return apply_channel_config(sdi, g_variant_get_string(data, NULL));
	default:
		break;
	}

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg)
{
	int ret;
	float *values;
	size_t count;
	uint64_t *integers;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		ret = mooshimeter_dmm_get_available_number_choices(sdi,
			"SAMPLING:RATE", &values, &count);
		if (ret != SR_OK)
			return ret;
		integers = g_malloc(sizeof(uint64_t) * count);
		for (size_t i = 0; i < count; i++)
			integers[i] = (uint64_t)values[i];
		g_free(values);
		*data = std_gvar_samplerates(integers, count);
		g_free(integers);
		return SR_OK;
	case SR_CONF_AVG_SAMPLES:
		ret = mooshimeter_dmm_get_available_number_choices(sdi,
			"SAMPLING:DEPTH", &values, &count);
		if (ret != SR_OK)
			return ret;
		integers = g_malloc(sizeof(uint64_t) * count);
		for (size_t i = 0; i < count; i++)
			integers[i] = (uint64_t)values[i];
		g_free(values);
		*data = std_gvar_array_u64(integers, count);
		g_free(integers);
		return SR_OK;
	case SR_CONF_CHANNEL_CONFIG:
		return SR_ERR_NA;
	default:
		break;
	}

	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = mooshimeter_dmm_set_chooser(sdi, "SAMPLING:TRIGGER",
		"SAMPLING:TRIGGER:CONTINUOUS");
	if (ret)
		return ret;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	sr_session_source_add(sdi->session, -1, 0, 10000,
		mooshimeter_dmm_heartbeat, (void *)sdi);

	/* The Bluetooth socket isn't exposed, so just poll for data. */
	sr_session_source_add(sdi->session, -2, 0, 50,
		mooshimeter_dmm_poll, (void *)sdi);

	devc->enable_value_stream = TRUE;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	sr_session_source_remove(sdi->session, -1);
	sr_session_source_remove(sdi->session, -2);
	devc->enable_value_stream = FALSE;

	mooshimeter_dmm_set_chooser(sdi, "SAMPLING:TRIGGER",
		"SAMPLING:TRIGGER:OFF");

	return SR_OK;
}

static struct sr_dev_driver mooshimeter_dmm_driver_info = {
	.name = "mooshimeter-dmm",
	.longname = "Mooshimeter DMM",
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
SR_REGISTER_DEV_DRIVER(mooshimeter_dmm_driver_info);
