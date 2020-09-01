/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Hannu Vuolasaho <vuokkosetae@gmail.com>
 * Copyright (C) 2018-2019 Frank Stettner <frank-stettner@gmx.net>
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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	SR_CONF_FORCE_DETECT,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const struct korad_kaxxxxp_model models[] = {
	/* Device enum, vendor, model, ID reply, channels, voltage, current */
	{VELLEMAN_PS3005D, "Velleman", "PS3005D",
		"VELLEMANPS3005DV2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{VELLEMAN_LABPS3005D, "Velleman", "LABPS3005D",
		"VELLEMANLABPS3005DV2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{KORAD_KA3005P, "Korad", "KA3005P",
		"KORADKA3005PV2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	/* Sometimes the KA3005P has an extra 0x01 after the ID. */
	{KORAD_KA3005P_0X01, "Korad", "KA3005P",
		"KORADKA3005PV2.0\x01", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	/* Sometimes the KA3005P has an extra 0xBC after the ID. */
	{KORAD_KA3005P_0XBC, "Korad", "KA3005P",
		"KORADKA3005PV2.0\xBC", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{KORAD_KA3005P_V42, "Korad", "KA3005P",
		"KORAD KA3005P V4.2", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{KORAD_KA3005P_V55, "Korad", "KA3005P",
		"KORAD KA3005P V5.5", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{KORAD_KD3005P, "Korad", "KD3005P",
		"KORAD KD3005P V2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{KORAD_KD3005P_V20_NOSP, "Korad", "KD3005P",
		"KORADKD3005PV2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{RND_320_KD3005P, "RND", "KD3005P",
		"RND 320-KD3005P V4.2", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{RND_320_KA3005P, "RND", "KA3005P",
		"RND 320-KA3005P V5.5", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{RND_320K30PV, "RND", "KA3005P",
		"RND 320-KA3005P V2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{TENMA_72_2550_V2, "Tenma", "72-2550",
		"TENMA72-2550V2.0", 1, {0, 61, 0.01}, {0, 3.1, 0.001}},
	{TENMA_72_2540_V20, "Tenma", "72-2540",
		"TENMA72-2540V2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{TENMA_72_2540_V21, "Tenma", "72-2540",
		"TENMA 72-2540 V2.1", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{TENMA_72_2540_V52, "Tenma", "72-2540",
		"TENMA 72-2540 V5.2", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{TENMA_72_2535_V21, "Tenma", "72-2535",
		"TENMA 72-2535 V2.1", 1, {0, 31, 0.01}, {0, 3.1, 0.001}},
	{STAMOS_SLS31_V20, "Stamos Soldering", "S-LS-31",
		"S-LS-31 V2.0", 1, {0, 31, 0.01}, {0, 5.1, 0.001}},
	{KORAD_KD6005P, "Korad", "KD6005P",
		"KORAD KD6005P V2.2", 1, {0, 61, 0.01}, {0, 5.1, 0.001}},
	ALL_ZERO
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	static const char *serno_prefix = " SN:";

	struct dev_context *devc;
	GSList *l;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	const char *conn, *serialcomm;
	const char *force_detect;
	struct sr_serial_dev_inst *serial;
	char reply[50];
	int ret, i, model_id;
	size_t len;
	char *serno;

	conn = NULL;
	serialcomm = NULL;
	force_detect = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_FORCE_DETECT:
			force_detect = g_variant_get_string(src->data, NULL);
			break;
		default:
			sr_err("Unknown option %d, skipping.", src->key);
			break;
		}
	}

	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = "9600/8n1";
	if (force_detect && !*force_detect)
		force_detect = NULL;

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	/*
	 * Prepare a receive buffer for the identification response that
	 * is large enough to hold the longest known model name, and an
	 * optional serial number. Communicate the identification request.
	 */
	len = 0;
	for (i = 0; models[i].id; i++) {
		if (len < strlen(models[i].id))
			len = strlen(models[i].id);
	}
	len += strlen(serno_prefix) + 12;
	if (len > sizeof(reply) - 1)
		len = sizeof(reply) - 1;
	sr_dbg("Want max %zu bytes.", len);

	ret = korad_kaxxxxp_send_cmd(serial, "*IDN?");
	if (ret < 0)
		return NULL;

	ret = korad_kaxxxxp_read_chars(serial, len, reply);
	if (ret < 0)
		return NULL;
	sr_dbg("Received: %d, %s", ret, reply);

	/*
	 * Isolate the optional serial number at the response's end.
	 * Lookup the response's model ID in the list of known models.
	 */
	serno = g_strrstr(reply, serno_prefix);
	if (serno) {
		*serno = '\0';
		serno += strlen(serno_prefix);
	}

	model_id = -1;
	for (i = 0; models[i].id; i++) {
		if (g_strcmp0(models[i].id, reply) != 0)
			continue;
		model_id = i;
		break;
	}
	if (model_id < 0 && force_detect) {
		sr_warn("Found model ID '%s' is unknown, trying '%s' spec.",
			reply, force_detect);
		for (i = 0; models[i].id; i++) {
			if (strcmp(models[i].id, force_detect) != 0)
				continue;
			sr_info("Found replacement, using it instead.");
			model_id = i;
			break;
		}
	}
	if (model_id < 0) {
		sr_err("Unknown model ID '%s' detected, aborting.", reply);
		return NULL;
	}
	sr_dbg("Found: %s %s (idx %d, ID '%s').", models[model_id].vendor,
		models[model_id].name, model_id, models[model_id].id);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(models[model_id].vendor);
	sdi->model = g_strdup(models[model_id].name);
	if (serno)
		sdi->serial_num = g_strdup(serno);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->connection_id = g_strdup(conn);

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	g_mutex_init(&devc->rw_mutex);
	devc->model = &models[model_id];
	devc->req_sent_at = 0;
	devc->cc_mode_1_changed = FALSE;
	devc->cc_mode_2_changed = FALSE;
	devc->output_enabled_changed = FALSE;
	devc->ocp_enabled_changed = FALSE;
	devc->ovp_enabled_changed = FALSE;
	sdi->priv = devc;

	/* Get current status of device. */
	if (korad_kaxxxxp_get_all_values(serial, devc) < 0)
		goto exit_err;

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));

exit_err:
	sr_dev_inst_free(sdi);
	g_free(devc);
	sr_dbg("Scan failed.");

	return NULL;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_VOLTAGE:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_VOLTAGE, devc);
		*data = g_variant_new_double(devc->voltage);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_VOLTAGE_TARGET, devc);
		*data = g_variant_new_double(devc->voltage_target);
		break;
	case SR_CONF_CURRENT:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_CURRENT, devc);
		*data = g_variant_new_double(devc->current);
		break;
	case SR_CONF_CURRENT_LIMIT:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_CURRENT_LIMIT, devc);
		*data = g_variant_new_double(devc->current_limit);
		break;
	case SR_CONF_ENABLED:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_OUTPUT, devc);
		*data = g_variant_new_boolean(devc->output_enabled);
		break;
	case SR_CONF_REGULATION:
		/* Dual channel not supported. */
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_STATUS, devc);
		*data = g_variant_new_string((devc->cc_mode[0]) ? "CC" : "CV");
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_OCP, devc);
		*data = g_variant_new_boolean(devc->ocp_enabled);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		korad_kaxxxxp_get_value(sdi->conn, KAXXXXP_OVP, devc);
		*data = g_variant_new_boolean(devc->ovp_enabled);
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
	double dval;
	gboolean bval;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_VOLTAGE_TARGET:
		dval = g_variant_get_double(data);
		if (dval < devc->model->voltage[0] || dval > devc->model->voltage[1])
			return SR_ERR_ARG;
		devc->set_voltage_target = dval;
		if (korad_kaxxxxp_set_value(sdi->conn, KAXXXXP_VOLTAGE_TARGET, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_CURRENT_LIMIT:
		dval = g_variant_get_double(data);
		if (dval < devc->model->current[0] || dval > devc->model->current[1])
			return SR_ERR_ARG;
		devc->set_current_limit = dval;
		if (korad_kaxxxxp_set_value(sdi->conn, KAXXXXP_CURRENT_LIMIT, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_ENABLED:
		bval = g_variant_get_boolean(data);
		/* Set always so it is possible turn off with sigrok-cli. */
		devc->set_output_enabled = bval;
		if (korad_kaxxxxp_set_value(sdi->conn, KAXXXXP_OUTPUT, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		bval = g_variant_get_boolean(data);
		devc->set_ocp_enabled = bval;
		if (korad_kaxxxxp_set_value(sdi->conn, KAXXXXP_OCP, devc) < 0)
			return SR_ERR;
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		bval = g_variant_get_boolean(data);
		devc->set_ovp_enabled = bval;
		if (korad_kaxxxxp_set_value(sdi->conn, KAXXXXP_OVP, devc) < 0)
			return SR_ERR;
		break;
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
		if (!devc || !devc->model)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step_array(devc->model->voltage);
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (!devc || !devc->model)
			return SR_ERR_ARG;
		*data = std_gvar_min_max_step_array(devc->model->current);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;
	if (devc)
		g_mutex_clear(&devc->rw_mutex);

	return std_serial_dev_close(sdi);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	devc->req_sent_at = 0;
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN,
			KAXXXXP_POLL_INTERVAL_MS,
			korad_kaxxxxp_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver korad_kaxxxxp_driver_info = {
	.name = "korad-kaxxxxp",
	.longname = "Korad KAxxxxP",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(korad_kaxxxxp_driver_info);
