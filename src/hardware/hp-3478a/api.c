/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Frank Stettner <frank-stettner@gmx.net>
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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DIGITS | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const struct {
	enum sr_mq mq;
	enum sr_mqflag mqflag;
} mqopts[] = {
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC},
	{SR_MQ_VOLTAGE, SR_MQFLAG_AC},
	{SR_MQ_CURRENT, SR_MQFLAG_DC},
	{SR_MQ_CURRENT, SR_MQFLAG_AC},
	{SR_MQ_RESISTANCE, 0},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE},
};

static const struct {
	enum sr_mq mq;
	enum sr_mqflag mqflag;
	int range_exp;
	const char *range_str;
} rangeopts[] = {
	/* -99 is a dummy exponent for auto ranging. */
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC,             -99,   "Auto"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC,              -2,   "30mV"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC,              -1,   "300mV"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC,               0,   "3V"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC,               1,   "30V"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_DC,               2,   "300V"},
	/* -99 is a dummy exponent for auto ranging. */
	{SR_MQ_VOLTAGE, SR_MQFLAG_AC,             -99,   "Auto"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_AC,              -1,   "300mV"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_AC,               0,   "3V"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_AC,               1,   "30V"},
	{SR_MQ_VOLTAGE, SR_MQFLAG_AC,               2,   "300V"},
	/* -99 is a dummy exponent for auto ranging. */
	{SR_MQ_CURRENT, SR_MQFLAG_DC,             -99,   "Auto"},
	{SR_MQ_CURRENT, SR_MQFLAG_DC,              -1,   "300mV"},
	{SR_MQ_CURRENT, SR_MQFLAG_DC,               0,   "3V"},
	/* -99 is a dummy exponent for auto ranging. */
	{SR_MQ_CURRENT, SR_MQFLAG_AC,             -99,   "Auto"},
	{SR_MQ_CURRENT, SR_MQFLAG_AC,              -1,   "300mV"},
	{SR_MQ_CURRENT, SR_MQFLAG_AC,               0,   "3V"},
	/* -99 is a dummy exponent for auto ranging. */
	{SR_MQ_RESISTANCE, 0,                     -99,   "Auto"},
	{SR_MQ_RESISTANCE, 0,                       1,   "30"},
	{SR_MQ_RESISTANCE, 0,                       2,   "300"},
	{SR_MQ_RESISTANCE, 0,                       3,   "3k"},
	{SR_MQ_RESISTANCE, 0,                       4,   "30k"},
	{SR_MQ_RESISTANCE, 0,                       5,   "300k"},
	{SR_MQ_RESISTANCE, 0,                       6,   "3M"},
	{SR_MQ_RESISTANCE, 0,                       7,   "30M"},
	/* -99 is a dummy exponent for auto ranging. */
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,   -99,   "Auto"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     1,   "30R"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     2,   "300R"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     3,   "3kR"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     4,   "30kR"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     5,   "300kR"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     6,   "3MR"},
	{SR_MQ_RESISTANCE, SR_MQFLAG_FOUR_WIRE,     7,   "30MR"},
};

/** Available digits as strings. */
static const char *digits[] = {
	"3.5", "4.5", "5.5",
};

/** Mapping between devc->spec_digits and digits string. */
static const char *digits_map[] = {
	"", "", "", "", "3.5", "4.5", "5.5",
};

static struct sr_dev_driver hp_3478a_driver_info;

static int create_front_channel(struct sr_dev_inst *sdi, int chan_idx)
{
	struct sr_channel *channel;
	struct channel_context *chanc;

	chanc = g_malloc(sizeof(*chanc));
	chanc->location = TERMINAL_FRONT;

	channel = sr_channel_new(sdi, chan_idx++, SR_CHANNEL_ANALOG, TRUE, "P1");
	channel->priv = chanc;

	return chan_idx;
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	int ret;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup("Hewlett-Packard");
	sdi->model = g_strdup("3478A");
	sdi->conn = scpi;
	sdi->driver = &hp_3478a_driver_info;
	sdi->inst_type = SR_INST_SCPI;

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	sdi->priv = devc;

	/* Get actual status (function, digits, ...). */
	ret = hp_3478a_get_status_bytes(sdi);
	if (ret != SR_OK)
		return NULL;

	create_front_channel(sdi, 0);

	return sdi;
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
	struct dev_context *devc;
	int ret;
	GVariant *arr[2];
	unsigned int i;
	const char *range_str;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_MEASURED_QUANTITY:
		ret = hp_3478a_get_status_bytes(sdi);
		if (ret != SR_OK)
			return ret;
		arr[0] = g_variant_new_uint32(devc->measurement_mq);
		arr[1] = g_variant_new_uint64(devc->measurement_mq_flags);
		*data = g_variant_new_tuple(arr, 2);
		break;
	case SR_CONF_RANGE:
		ret = hp_3478a_get_status_bytes(sdi);
		if (ret != SR_OK)
			return ret;
		range_str = "Auto";
		for (i = 0; i < ARRAY_SIZE(rangeopts); i++) {
			if (rangeopts[i].mq == devc->measurement_mq &&
					rangeopts[i].mqflag == devc->measurement_mq_flags &&
					rangeopts[i].range_exp == devc->range_exp) {
				range_str = rangeopts[i].range_str;
				break;
			}
		}
		*data = g_variant_new_string(range_str);
		break;
	case SR_CONF_DIGITS:
		ret = hp_3478a_get_status_bytes(sdi);
		if (ret != SR_OK)
			return ret;
		*data = g_variant_new_string(digits_map[devc->spec_digits]);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	enum sr_mq mq;
	enum sr_mqflag mq_flags;
	GVariant *tuple_child;
	unsigned int i;
	const char *range_str;
	const char *digits_str;

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
		mq_flags = g_variant_get_uint64(tuple_child);
		g_variant_unref(tuple_child);
		return hp_3478a_set_mq(sdi, mq, mq_flags);
	case SR_CONF_RANGE:
		range_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(rangeopts); i++) {
			if (rangeopts[i].mq == devc->measurement_mq &&
					rangeopts[i].mqflag == devc->measurement_mq_flags &&
					g_strcmp0(rangeopts[i].range_str, range_str) == 0) {
				return hp_3478a_set_range(sdi, rangeopts[i].range_exp);
			}
		}
		return SR_ERR_NA;
	case SR_CONF_DIGITS:
		digits_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(rangeopts); i++) {
			if (g_strcmp0(digits_map[i], digits_str) == 0)
				return hp_3478a_set_digits(sdi, i);
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
	int ret;
	unsigned int i;
	GVariant *gvar, *arr[2];
	GVariantBuilder gvb;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_MEASURED_QUANTITY:
		/*
		 * TODO: move to std.c as
		 *       SR_PRIV GVariant *std_gvar_measured_quantities()
		 */
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(mqopts); i++) {
			arr[0] = g_variant_new_uint32(mqopts[i].mq);
			arr[1] = g_variant_new_uint64(mqopts[i].mqflag);
			gvar = g_variant_new_tuple(arr, 2);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_RANGE:
		ret = hp_3478a_get_status_bytes(sdi);
		if (ret != SR_OK)
			return ret;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(rangeopts); i++) {
			if (rangeopts[i].mq == devc->measurement_mq &&
					rangeopts[i].mqflag == devc->measurement_mq_flags) {
				g_variant_builder_add(&gvb, "s", rangeopts[i].range_str);
			}
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_DIGITS:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(digits));
		break;
	default:
		return SR_ERR_NA;
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
	 * NOTE: For faster readings, there are some things one can do:
	 *     - Turn off the display: sr_scpi_send(scpi, "D3SIGROK").
	 *     - Set the line frequency to 60Hz via switch (back of the unit).
	 *     - Set to 3.5 digits measurement.
	 */

	/* Set to internal trigger. */
	sr_scpi_send(scpi, "T1");
	/* Get device status. */
	hp_3478a_get_status_bytes(sdi);

	return sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 100,
			hp_3478a_receive_data, (void *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;

	sr_scpi_source_remove(sdi->session, scpi);
	std_session_send_df_end(sdi);

	/* Set to internal trigger. */
	sr_scpi_send(scpi, "T1");
	/* Turn on display. */
	sr_scpi_send(scpi, "D1");

	return SR_OK;
}

static struct sr_dev_driver hp_3478a_driver_info = {
	.name = "hp-3478a",
	.longname = "HP 3478A",
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
SR_REGISTER_DEV_DRIVER(hp_3478a_driver_info);
