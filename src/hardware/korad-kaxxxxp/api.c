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

#include <ctype.h>

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

/* Voltage and current ranges. Values are: Min, max, step. */
static const double volts_30[] = { 0, 31, 0.01, };
static const double volts_60[] = { 0, 61, 0.01, };
static const double amps_3[] = { 0, 3.1, 0.001, };
static const double amps_5[] = { 0, 5.1, 0.001, };

static const struct korad_kaxxxxp_model models[] = {
	/* Vendor, model name, ID reply, channels, voltage, current, quirks. */
	{"Korad", "KA3005P", "", 1, volts_30, amps_5,
		KORAD_QUIRK_ID_TRAILING},
	{"Korad", "KD3005P", "", 1, volts_30, amps_5, 0},
	{"Korad", "KD6005P", "", 1, volts_60, amps_5, 0},
	{"RND", "KA3005P", "RND 320-KA3005P", 1, volts_30, amps_5,
		KORAD_QUIRK_ID_OPT_VERSION},
	{"RND", "KD3005P", "RND 320-KD3005P", 1, volts_30, amps_5,
		KORAD_QUIRK_ID_OPT_VERSION},
	{"Stamos Soldering", "S-LS-31", "", 1, volts_30, amps_5,
		KORAD_QUIRK_ID_NO_VENDOR},
	{"Tenma", "72-2535", "", 1, volts_30, amps_3, 0},
	{"Tenma", "72-2540", "", 1, volts_30, amps_5, 0},
	{"Tenma", "72-2550", "", 1, volts_60, amps_3, 0},
	{"Tenma", "72-2705", "", 1, volts_30, amps_3, 0},
	{"Tenma", "72-2710", "", 1, volts_30, amps_5, 0},
	{"Velleman", "LABPS3005D", "", 1, volts_30, amps_5,
		KORAD_QUIRK_LABPS_OVP_EN},
	{"Velleman", "PS3005D V1.3", "VELLEMANPS3005DV1.3" , 1, volts_30, amps_5,
		KORAD_QUIRK_ID_TRAILING | KORAD_QUIRK_SLOW_PROCESSING},
	{"Velleman", "PS3005D", "", 1, volts_30, amps_5, 0},
	ALL_ZERO
};

/*
 * Bump this when adding new models[] above. Make sure the text buffer
 * for the ID response can hold the longest sequence that we expect in
 * the field which consists of: vendor + model [ + version ][ + serno ].
 * Don't be too generous here, the maximum receive buffer size affects
 * the timeout within which the first response character is expected.
 */
static const size_t id_text_buffer_size = 48;

/*
 * Check whether the device's "*IDN?" response matches a supported model.
 * The caller already stripped off the optional serial number.
 */
static gboolean model_matches(const struct korad_kaxxxxp_model *model,
	const char *id_text)
{
	gboolean matches;
	gboolean opt_version, skip_vendor, accept_trail;
	const char *want;

	if (!model)
		return FALSE;

	/*
	 * When the models[] entry contains a specific response text,
	 * then expect to see this very text in literal form. This
	 * lets the driver map weird and untypical responses to a
	 * specific set of display texts for vendor and model names.
	 * Accept an optionally trailing version if models[] says so.
	 */
	if (model->id && model->id[0]) {
		opt_version = model->quirks & KORAD_QUIRK_ID_OPT_VERSION;
		if (!opt_version) {
			matches = g_strcmp0(id_text, model->id) == 0;
			if (!matches)
				return FALSE;
			sr_dbg("Matches expected ID text: '%s'.", model->id);
			return TRUE;
		}
		matches = g_str_has_prefix(id_text, model->id);
		if (!matches)
			return FALSE;
		id_text += strlen(model->id);
		while (isspace((int)*id_text))
			id_text++;
		if (*id_text == 'V') {
			id_text++;
			while (*id_text == '.' || isdigit((int)*id_text))
				id_text++;
			while (isspace((int)*id_text))
				id_text++;
		}
		if (*id_text)
			return FALSE;
		sr_dbg("Matches expected ID text [vers]: '%s'.", model->id);
		return TRUE;
	}

	/*
	 * A more generic approach which covers most devices: Check
	 * for the very vendor and model names which also are shown
	 * to users (the display texts). Weakened to match responses
	 * more widely: Case insensitive checks, optional whitespace
	 * in responses, optional version details. Optional trailing
	 * garbage. Optional omission of the vendor name. Shall match
	 * all the devices which were individually listed in earlier
	 * implementations of the driver, and shall also match firmware
	 * versions that were not listed before.
	 */
	skip_vendor = model->quirks & KORAD_QUIRK_ID_NO_VENDOR;
	accept_trail = model->quirks & KORAD_QUIRK_ID_TRAILING;
	if (!skip_vendor) {
		want = model->vendor;
		matches = g_ascii_strncasecmp(id_text, want, strlen(want)) == 0;
		if (!matches)
			return FALSE;
		id_text += strlen(want);
		while (isspace((int)*id_text))
			id_text++;
	}
	want = model->name;
	matches = g_ascii_strncasecmp(id_text, want, strlen(want)) == 0;
	if (!matches)
		return FALSE;
	id_text += strlen(want);
	while (isspace((int)*id_text))
		id_text++;
	if (*id_text == 'V') {
		/* TODO Isolate and (also) return version details? */
		id_text++;
		while (*id_text == '.' || isdigit((int)*id_text))
			id_text++;
		while (isspace((int)*id_text))
			id_text++;
	}
	if (accept_trail) {
		/*
		 * TODO Determine how many non-printables to accept here
		 * and how strict to check for "known" garbage variants.
		 */
		switch (*id_text) {
		case '\x01':
		case '\xbc':
			id_text++;
			break;
		case '\x00':
			/* EMPTY */
			break;
		default:
			return FALSE;
		}
	}
	if (*id_text)
		return FALSE;
	sr_dbg("Matches generic '[vendor] model [vers] [trail]' pattern.");
	return TRUE;
}

/* Lookup a model from an ID response text. */
static const struct korad_kaxxxxp_model *model_lookup(const char *id_text)
{
	size_t idx;
	const struct korad_kaxxxxp_model *check;

	if (!id_text || !*id_text)
		return NULL;
	sr_dbg("Looking up: [%s].", id_text);

	for (idx = 0; idx < ARRAY_SIZE(models); idx++) {
		check = &models[idx];
		if (!check->name || !check->name[0])
			continue;
		if (!model_matches(check, id_text))
			continue;
		sr_dbg("Found: [%s] [%s]", check->vendor, check->name);
		return check;
	}
	sr_dbg("Not found");

	return NULL;
}

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
	int ret;
	const struct korad_kaxxxxp_model *model;
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

	/* Communicate the identification request. */
	len = id_text_buffer_size;
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

	model = model_lookup(reply);
	if (!model && force_detect) {
		sr_warn("Could not find model ID '%s', trying '%s'.",
			reply, force_detect);
		model = model_lookup(force_detect);
		if (model)
			sr_info("Found replacement, using it instead.");
	}
	if (!model) {
		sr_err("Unsupported model ID '%s', aborting.", reply);
		return NULL;
	}
	sr_dbg("Found: %s %s (idx %zu).", model->vendor, model->name,
		model - &models[0]);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(model->vendor);
	sdi->model = g_strdup(model->name);
	if (serno)
		sdi->serial_num = g_strdup(serno);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->connection_id = g_strdup(conn);

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "I");

	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);
	g_mutex_init(&devc->rw_mutex);
	devc->model = model;
	devc->next_req_time = 0;
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

	devc->next_req_time = 0;
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
