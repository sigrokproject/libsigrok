/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Aurelien Jacobs <aurel@gnuage.org>
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
	SR_CONF_MODBUSADDR
};

static const uint32_t drvopts[] = {
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_REGULATION | SR_CONF_GET,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET,
};

/* The IDs in this list are only guessed and needs to be verified
   against some real hardware. If at least a few of them matches,
   it will probably be safe to enable the others. */
static const struct maynuo_m97_model supported_models[] = {
//	{  53, "M9711"     ,   30, 150,    150 },
//	{  54, "M9712"     ,   30, 150,    300 },
//	{  55, "M9712C"    ,   60, 150,    300 },
//	{  56, "M9713"     ,  120, 150,    600 },
//	{  57, "M9712B"    ,   15, 500,    300 },
//	{  58, "M9713B"    ,   30, 500,    600 },
//	{  59, "M9714"     ,  240, 150,   1200 },
//	{  60, "M9714B"    ,   60, 500,   1200 },
//	{  61, "M9715"     ,  240, 150,   1800 },
//	{  62, "M9715B"    ,  120, 500,   1800 },
//	{  63, "M9716"     ,  240, 150,   2400 },
//	{  64, "M9716B"    ,  120, 500,   2400 },
//	{  65, "M9717C"    ,  480, 150,   3600 },
//	{  66, "M9717"     ,  240, 150,   3600 },
//	{  67, "M9717B"    ,  120, 500,   3600 },
//	{  68, "M9718"     ,  240, 150,   6000 },
//	{  69, "M9718B"    ,  120, 500,   6000 },
//	{  70, "M9718D"    ,  240, 500,   6000 },
//	{  71, "M9836"     ,  500, 150,  20000 },
//	{  72, "M9836B"    ,  240, 500,  20000 },
//	{  73, "M9838B"    ,  240, 500,  50000 },
//	{  74, "M9839B"    ,  240, 500, 100000 },
//	{  75, "M9840B"    ,  500, 500, 200000 },
//	{  76, "M9840"     , 1500, 150, 200000 },
//	{  77, "M9712B30"  ,   30, 500,    300 },
//	{  78, "M9718E"    ,  120, 600,   6000 },
//	{  79, "M9718F"    ,  480, 150,   6000 },
//	{  80, "M9716E"    ,  480, 150,   3000 },
//	{  81, "M9710"     ,   30, 150,    150 },
//	{  82, "M9834"     ,  500, 150,  10000 },
//	{  83, "M9835"     ,  500, 150,  15000 },
//	{  84, "M9835B"    ,  240, 500,  15000 },
//	{  85, "M9837"     ,  500, 150,  35000 },
//	{  86, "M9837B"    ,  240, 500,  35000 },
//	{  87, "M9838"     ,  500, 150,  50000 },
//	{  88, "M9839"     ,  500, 150, 100000 },
//	{  89, "M9835C"    , 1000, 150,  15000 }, /* ?? */
//	{  90, "M9836C"    , 1000, 150,  20000 }, /* ?? */
//	{  91, "M9718F-300",  480, 300,   6000 }, /* ?? */
//	{  92, "M9836F"    , 1000, 150,  20000 }, /* ?? */
//	{  93, "M9836E"    ,  240, 600,  20000 }, /* ?? */
//	{  94, "M9717D"    ,  240, 500,   3600 }, /* ?? */
//	{  95, "M9836B-720",  240, 720,  20000 }, /* ?? */
//	{  96, "M9834H"    ,  500, 150,  10000 }, /* ?? */
//	{  97, "M9836H"    ,  500, 150,  20000 }, /* ?? */
//	{  98, "M9718F-500",  480, 500,   6000 }, /* ?? */
//	{  99, "M9834B"    ,  240, 500,  10000 }, /* ?? */
//	{ 100, "M9811"     ,   30, 150,    200 },
	{ 101, "M9812"     ,   30, 150,    300 },
//	{ 102, "M9812B"    ,   15, 500,    300 },
};

SR_PRIV struct sr_dev_driver maynuo_m97_driver_info;

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct sr_dev_inst *probe_device(struct sr_modbus_dev_inst *modbus)
{
	const struct maynuo_m97_model *model = NULL;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	uint16_t id, version;
	unsigned int i;

	int ret = maynuo_m97_get_model_version(modbus, &id, &version);
	if (ret != SR_OK)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(supported_models); i++)
		if (id == supported_models[i].id) {
			model = &supported_models[i];
			break;
		}
	if (model == NULL) {
		sr_err("Unknown model: %d.", id);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_ACTIVE;
	sdi->vendor = g_strdup("Maynuo");
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup_printf("v%d.%d", version/10, version%10);
	sdi->conn = modbus;
	sdi->driver = &maynuo_m97_driver_info;
	sdi->inst_type = SR_INST_MODBUS;

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("1");
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V1");
	cg->channels = g_slist_append(cg->channels, ch);

	ch = sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "I1");
	cg->channels = g_slist_append(cg->channels, ch);

	devc = g_malloc0(sizeof(struct dev_context));
	devc->model = model;

	sdi->priv = devc;

	return sdi;
}

static int config_compare(gconstpointer a, gconstpointer b)
{
	const struct sr_config *ac = a, *bc = b;
	return ac->key != bc->key;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config default_serialcomm = {
	    .key = SR_CONF_SERIALCOMM,
	    .data = g_variant_new_string("9600/8n1"),
	};
	struct sr_config default_modbusaddr = {
	    .key = SR_CONF_MODBUSADDR,
	    .data = g_variant_new_uint64(1),
	};
	GSList *opts = options, *devices;

	if (!g_slist_find_custom(options, &default_serialcomm, config_compare))
		opts = g_slist_prepend(opts, &default_serialcomm);
	if (!g_slist_find_custom(options, &default_modbusaddr, config_compare))
		opts = g_slist_prepend(opts, &default_modbusaddr);

	devices = sr_modbus_scan(di->context, opts, probe_device);

	while (opts != options)
		opts = g_slist_delete_link(opts, opts);
	g_variant_unref(default_serialcomm.data);
	g_variant_unref(default_modbusaddr.data);

	return devices;
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus = sdi->conn;

	if (sr_modbus_open(modbus) < 0)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	maynuo_m97_set_bit(modbus, PC1, 1);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	modbus = sdi->conn;

	if (modbus) {
		devc = sdi->priv;
		if (devc->expecting_registers) {
			/* Wait for the last data that was requested from the device. */
			uint16_t registers[devc->expecting_registers];
			sr_modbus_read_holding_registers(modbus, -1,
			                                 devc->expecting_registers,
			                                 registers);
		}

		maynuo_m97_set_bit(modbus, PC1, 0);

		if (sr_modbus_close(modbus) < 0)
			return SR_ERR;
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	enum maynuo_m97_mode mode;
	int ret, ivalue;
	float fvalue;

	(void)cg;

	modbus = sdi->conn;
	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_ENABLED:
		if ((ret = maynuo_m97_get_bit(modbus, ISTATE, &ivalue)) == SR_OK)
			*data = g_variant_new_boolean(ivalue);
		break;
	case SR_CONF_REGULATION:
		if ((ret = maynuo_m97_get_bit(modbus, UNREG, &ivalue)) != SR_OK)
			break;
		if (ivalue)
			*data = g_variant_new_string("UR");
		else if ((ret = maynuo_m97_get_mode(modbus, &mode)) == SR_OK)
			*data = g_variant_new_string(maynuo_m97_mode_to_str(mode));
		break;
	case SR_CONF_VOLTAGE:
		if ((ret = maynuo_m97_get_float(modbus, U, &fvalue)) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		if ((ret = maynuo_m97_get_float(modbus, UFIX, &fvalue)) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_CURRENT:
		if ((ret = maynuo_m97_get_float(modbus, I, &fvalue)) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_CURRENT_LIMIT:
		if ((ret = maynuo_m97_get_float(modbus, IFIX, &fvalue)) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		if ((ret = maynuo_m97_get_bit(modbus, UOVER, &ivalue)) == SR_OK)
			*data = g_variant_new_boolean(ivalue);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		if ((ret = maynuo_m97_get_float(modbus, UMAX, &fvalue)) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		if ((ret = maynuo_m97_get_bit(modbus, IOVER, &ivalue)) == SR_OK)
			*data = g_variant_new_boolean(ivalue);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		if ((ret = maynuo_m97_get_float(modbus, IMAX, &fvalue)) == SR_OK)
			*data = g_variant_new_double(fvalue);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION:
		*data = g_variant_new_boolean(TRUE);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		if ((ret = maynuo_m97_get_bit(modbus, HEAT, &ivalue)) == SR_OK)
			*data = g_variant_new_boolean(ivalue);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	modbus = sdi->conn;
	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	case SR_CONF_ENABLED:
		ret = maynuo_m97_set_input(modbus, g_variant_get_boolean(data));
		break;
	case SR_CONF_VOLTAGE_TARGET:
		ret = maynuo_m97_set_float(modbus, UFIX, g_variant_get_double(data));
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = maynuo_m97_set_float(modbus, IFIX, g_variant_get_double(data));
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		ret = maynuo_m97_set_float(modbus, UMAX, g_variant_get_double(data));
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ret = maynuo_m97_set_float(modbus, IMAX, g_variant_get_double(data));
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariantBuilder gvb;
	int ret;

	/* Always available, even without sdi. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	} else if (key == SR_CONF_DEVICE_OPTIONS && !sdi) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, ARRAY_SIZE(drvopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	ret = SR_OK;
	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts_cg, ARRAY_SIZE(devopts_cg), sizeof(uint32_t));
			break;
		case SR_CONF_VOLTAGE_TARGET:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, write resolution. */
			g_variant_builder_add_value(&gvb, g_variant_new_double(0.0));
			g_variant_builder_add_value(&gvb, g_variant_new_double(devc->model->max_voltage));
			g_variant_builder_add_value(&gvb, g_variant_new_double(0.001));
			*data = g_variant_builder_end(&gvb);
			break;
		case SR_CONF_CURRENT_LIMIT:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, step. */
			g_variant_builder_add_value(&gvb, g_variant_new_double(0.0));
			g_variant_builder_add_value(&gvb, g_variant_new_double(devc->model->max_current));
			g_variant_builder_add_value(&gvb, g_variant_new_double(0.0001));
			*data = g_variant_builder_end(&gvb);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	modbus = sdi->conn;
	devc = sdi->priv;

	if ((ret = sr_modbus_source_add(sdi->session, modbus, G_IO_IN, 10,
			maynuo_m97_receive_data, (void *)sdi)) != SR_OK)
		return ret;

	std_session_send_df_header(sdi, LOG_PREFIX);

	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	return maynuo_m97_capture_start(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	std_session_send_df_end(sdi, LOG_PREFIX);

	modbus = sdi->conn;
	sr_modbus_source_remove(sdi->session, modbus);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver maynuo_m97_driver_info = {
	.name = "maynuo-m97",
	.longname = "maynuo M97/M98 series",
	.api_version = 1,
	.init = init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
