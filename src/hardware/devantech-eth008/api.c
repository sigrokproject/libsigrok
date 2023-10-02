/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include "config.h"

#include <string.h>

#include "protocol.h"

#define VENDOR_TEXT	"Devantech"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIPLEXER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_SET, /* Enable/disable all relays at once. */
};

static const uint32_t devopts_cg_do[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_di[] = {
	SR_CONF_ENABLED | SR_CONF_GET,
};

static const uint32_t devopts_cg_ai[] = {
	SR_CONF_VOLTAGE | SR_CONF_GET,
};

/* List of supported devices. Sorted by model ID. */
static const struct devantech_eth008_model models[] = {
	{ 18, "ETH002",    2,  0,  0, 0, 1, 0, 0, },
	{ 19, "ETH008",    8,  0,  0, 0, 1, 0, 0, },
	{ 20, "ETH484",   16,  8,  4, 0, 2, 2, 0x00f0, },
	{ 21, "ETH8020",  20,  8,  8, 0, 3, 4, 0, },
	{ 22, "WIFI484",  16,  8,  4, 0, 2, 2, 0x00f0, },
	{ 24, "WIFI8020", 20,  8,  8, 0, 3, 4, 0, },
	{ 26, "WIFI002",   2,  0,  0, 0, 1, 0, 0, },
	{ 28, "WIFI008",   8,  0,  0, 0, 1, 0, 0, },
	{ 52, "ETH1610",  10, 16, 16, 0, 2, 2, 0, },
};

static const struct devantech_eth008_model *find_model(uint8_t code)
{
	size_t idx;
	const struct devantech_eth008_model *check;

	for (idx = 0; idx < ARRAY_SIZE(models); idx++) {
		check = &models[idx];
		if (check->code != code)
			continue;
		return check;
	}

	return NULL;
}

static struct sr_dev_driver devantech_eth008_driver_info;

static struct sr_dev_inst *probe_device_conn(const char *conn)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *ser;
	uint8_t code, hwver, fwver;
	const struct devantech_eth008_model *model;
	gboolean has_serno_cmd;
	char snr_txt[16];
	struct channel_group_context *cgc;
	size_t ch_idx, nr, do_idx, di_idx, ai_idx;
	struct sr_channel_group *cg;
	char cg_name[24];
	int ret;

	sdi = g_malloc0(sizeof(*sdi));
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	ser = sr_serial_dev_inst_new(conn, NULL);
	sdi->conn = ser;
	if (!ser)
		goto probe_fail;
	ret = serial_open(ser, 0);
	if (ret != SR_OK)
		goto probe_fail;

	ret = devantech_eth008_get_model(ser, &code, &hwver, &fwver);
	if (ret != SR_OK)
		goto probe_fail;
	model = find_model(code);
	if (!model) {
		sr_err("Unknown model ID 0x%02x (HW %u, FW %u).",
			code, hwver, fwver);
		goto probe_fail;
	}
	devc->model_code = code;
	devc->hardware_version = hwver;
	devc->firmware_version = fwver;
	devc->model = model;
	sdi->vendor = g_strdup(VENDOR_TEXT);
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup_printf("HW%u FW%u", hwver, fwver);
	sdi->connection_id = g_strdup(conn);
	sdi->driver = &devantech_eth008_driver_info;
	sdi->inst_type = SR_INST_SERIAL;

	has_serno_cmd = TRUE;
	if (model->min_serno_fw && fwver < model->min_serno_fw)
		has_serno_cmd = FALSE;
	if (has_serno_cmd) {
		snr_txt[0] = '\0';
		ret = devantech_eth008_get_serno(ser,
			snr_txt, sizeof(snr_txt));
		if (ret != SR_OK)
			goto probe_fail;
		sdi->serial_num = g_strdup(snr_txt);
	}

	ch_idx = 0;
	devc->mask_do = (1UL << devc->model->ch_count_do) - 1;
	devc->mask_do &= ~devc->model->mask_do_missing;
	for (do_idx = 0; do_idx < devc->model->ch_count_do; do_idx++) {
		nr = do_idx + 1;
		if (devc->model->mask_do_missing & (1UL << do_idx))
			continue;
		snprintf(cg_name, sizeof(cg_name), "DO%zu", nr);
		cgc = g_malloc0(sizeof(*cgc));
		cg = sr_channel_group_new(sdi, cg_name, cgc);
		cgc->index = do_idx;
		cgc->number = nr;
		cgc->ch_type = DV_CH_DIGITAL_OUTPUT;
		(void)cg;
		ch_idx++;
	}
	for (di_idx = 0; di_idx < devc->model->ch_count_di; di_idx++) {
		nr = di_idx + 1;
		snprintf(cg_name, sizeof(cg_name), "DI%zu", nr);
		cgc = g_malloc0(sizeof(*cgc));
		cg = sr_channel_group_new(sdi, cg_name, cgc);
		cgc->index = di_idx;
		cgc->number = nr;
		cgc->ch_type = DV_CH_DIGITAL_INPUT;
		(void)cg;
		ch_idx++;
	}
	for (ai_idx = 0; ai_idx < devc->model->ch_count_ai; ai_idx++) {
		nr = ai_idx + 1;
		snprintf(cg_name, sizeof(cg_name), "AI%zu", nr);
		cgc = g_malloc0(sizeof(*cgc));
		cg = sr_channel_group_new(sdi, cg_name, cgc);
		cgc->index = ai_idx;
		cgc->number = nr;
		cgc->ch_type = DV_CH_ANALOG_INPUT;
		(void)cg;
		ch_idx++;
	}
	if (1) {
		/* Create an analog channel for the supply voltage. */
		snprintf(cg_name, sizeof(cg_name), "Vsupply");
		cgc = g_malloc0(sizeof(*cgc));
		cg = sr_channel_group_new(sdi, cg_name, cgc);
		cgc->index = 0;
		cgc->number = 0;
		cgc->ch_type = DV_CH_SUPPLY_VOLTAGE;
		(void)cg;
		ch_idx++;
	}

	return sdi;

probe_fail:
	if (ser) {
		serial_close(ser);
		sr_serial_dev_inst_free(ser);
	}
	if (devc) {
		g_free(devc);
	}
	if (sdi) {
		sdi->priv = NULL;
		sr_dev_inst_free(sdi);
		sdi = NULL;
	}
	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	const char *conn;
	GSList *devices;
	struct sr_dev_inst *sdi;

	drvc = di->context;
	drvc->instances = NULL;

	/* A conn= spec is required for the TCP attached device. */
	conn = NULL;
	(void)sr_serial_extract_options(options, &conn, NULL);
	if (!conn || !*conn)
		return NULL;

	devices = NULL;
	sdi = probe_device_conn(conn);
	if (sdi)
		devices = g_slist_append(devices, sdi);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct channel_group_context *cgc;
	gboolean on;
	uint16_t vin;
	double vsupply;
	int ret;

	if (!cg) {
		switch (key) {
		case SR_CONF_CONN:
			if (!sdi->connection_id)
				return SR_ERR_NA;
			*data = g_variant_new_string(sdi->connection_id);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	cgc = cg->priv;
	if (!cgc)
		return SR_ERR_NA;
	switch (key) {
	case SR_CONF_ENABLED:
		if (cgc->ch_type == DV_CH_DIGITAL_OUTPUT) {
			ret = devantech_eth008_query_do(sdi, cg, &on);
			if (ret != SR_OK)
				return ret;
			*data = g_variant_new_boolean(on);
			return SR_OK;
		}
		if (cgc->ch_type == DV_CH_DIGITAL_INPUT) {
			ret = devantech_eth008_query_di(sdi, cg, &on);
			if (ret != SR_OK)
				return ret;
			*data = g_variant_new_boolean(on);
			return SR_OK;
		}
		return SR_ERR_NA;
	case SR_CONF_VOLTAGE:
		if (cgc->ch_type == DV_CH_ANALOG_INPUT) {
			ret = devantech_eth008_query_ai(sdi, cg, &vin);
			if (ret != SR_OK)
				return ret;
			*data = g_variant_new_uint32(vin);
			return SR_OK;
		}
		if (cgc->ch_type == DV_CH_SUPPLY_VOLTAGE) {
			ret = devantech_eth008_query_supply(sdi, cg, &vin);
			if (ret != SR_OK)
				return ret;
			vsupply = vin;
			vsupply /= 1000.;
			*data = g_variant_new_double(vsupply);
			return SR_OK;
		}
		return SR_ERR_NA;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct channel_group_context *cgc;
	gboolean on;

	if (!cg) {
		switch (key) {
		case SR_CONF_ENABLED:
			/* Enable/disable all channels at the same time. */
			on = g_variant_get_boolean(data);
			return devantech_eth008_setup_do(sdi, cg, on);
		default:
			return SR_ERR_NA;
		}
	}

	cgc = cg->priv;
	if (!cgc)
		return SR_ERR_NA;
	switch (key) {
	case SR_CONF_ENABLED:
		if (cgc->ch_type != DV_CH_DIGITAL_OUTPUT)
			return SR_ERR_NA;
		on = g_variant_get_boolean(data);
		return devantech_eth008_setup_do(sdi, cg, on);
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct channel_group_context *cgc;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts);
		default:
			return SR_ERR_NA;
		}
	}

	cgc = cg->priv;
	if (!cgc)
		return SR_ERR_NA;
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if (cgc->ch_type == DV_CH_DIGITAL_OUTPUT) {
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_do));
			return SR_OK;
		}
		if (cgc->ch_type == DV_CH_DIGITAL_INPUT) {
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_di));
			return SR_OK;
		}
		if (cgc->ch_type == DV_CH_ANALOG_INPUT) {
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_ai));
			return SR_OK;
		}
		if (cgc->ch_type == DV_CH_SUPPLY_VOLTAGE) {
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_ai));
			return SR_OK;
		}
		return SR_ERR_NA;
	default:
		return SR_ERR_NA;
	}
}

static struct sr_dev_driver devantech_eth008_driver_info = {
	.name = "devantech-eth008",
	.longname = "Devantech ETH008",
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
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = std_dummy_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(devantech_eth008_driver_info);
